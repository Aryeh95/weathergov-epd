/* Client side utilities for esp32-weather-epd.
 * Copyright (C) 2022-2025  Luke Marzen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// built-in C++ libraries
#include <cstring>
#include <functional>
#include <vector>
#include <utility>

// arduino/esp32 libraries
#include <Arduino.h>
#include <esp_sntp.h>
#include <esp_system.h> // esp_random
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>

// additional libraries
#include <Adafruit_BusIO_Register.h>
#include <ArduinoJson.h>

// header files
#include "_locale.h"
#include "api_response.h"
#include "aqi.h"
#include "client_utils.h"
#include "config.h"
#include "conversions.h"
#include "display_utils.h"
#include "renderer.h"
#ifndef USE_HTTP
  #include <WiFiClientSecure.h>
#endif

static const uint16_t HTTPS_PORT = 443;
static const char *NWS_ENDPOINT = "api.weather.gov";
static const char *AIR_QUALITY_ENDPOINT = "air-quality-api.open-meteo.com";
static const char *OPEN_METEO_ENDPOINT  = "api.open-meteo.com";
static const char *AIRNOW_ENDPOINT = "www.airnowapi.org";
static const char *POLLEN_ENDPOINT = "pollen.googleapis.com";

// weather.gov gridpoint QPF cache (see getNWSWeather). Refetched when the
// local clock crosses a QPF_CACHE_HOURS boundary; a copy up to
// QPF_STALE_OK_S old still stands in for a failed fetch.
static const int     QPF_CACHE_HOURS = 2;
static const size_t  QPF_CACHE_MAX   = 64;          // buckets kept in NVS
static const long    QPF_STALE_OK_S  = 12L * 3600L;

// One HTTPClient for the whole wake. Its destructor stops the socket, so a
// per-request instance could never hand a live TLS session to the next
// request. s_openHost/Port record which host that socket is open to: a
// request to the same host reuses it (HTTP/1.1 keep-alive), any other host
// closes it first -- HTTPClient itself would happily send the next request
// down whatever socket is open.
static HTTPClient s_http;
static String     s_openHost;
static uint16_t   s_openPort = 0;
static const char *GOOGLE_WEATHER_ENDPOINT = "weather.googleapis.com";
static const char *IMS_ENDPOINT = "ims.gov.il";
static const char *SVIVA_SITE_ENDPOINT = "air.sviva.gov.il";      // Ministry of Environmental Protection
static const char *SVIVA_API_ENDPOINT  = "air-papi.sviva.gov.il"; // ... its data API
static const char *IMS_USER_AGENT = "esp32-weather-epd";

// Reason code from the most recent WiFi disconnect event, captured so a
// failed connection can be explained on the error screen. 0 means no
// disconnect event was ever received (ie. the access point never responded).
static uint8_t wifiDisconnectReason = 0;

static void onWiFiStaDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  // ASSOC_LEAVE is what our own WiFi.disconnect() between two attempts
  // produces. It arrives after the attempt it ends, and used to replace
  // whatever the router had said with "Unknown Reason (8)".
  if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE)
  {
    wifiDisconnectReason = info.wifi_sta_disconnected.reason;
  }
} // end onWiFiStaDisconnected

/* Forgets the access point startWiFi() connects straight to on the next
 * wake (its BSSID and channel), so that wake scans for the strongest one
 * again.
 */
static void forgetCachedAp()
{
  Preferences bp;
  bp.begin(NVS_NAMESPACE, false);
  bp.remove("apSsid");
  bp.remove("apBssid");
  bp.remove("apChan");
  bp.end();
} // end forgetCachedAp

/* Power-on and connect WiFi.
 * Takes int parameter to store WiFi RSSI, or “Received Signal Strength
 * Indicator"
 *
 * Returns WiFi status.
 */
wl_status_t startWiFi(int &wifiRSSI, unsigned long patienceMs)
{
  // how long one attempt may wait for a router that does not answer
  const unsigned long patience = (patienceMs != 0) ? patienceMs : WIFI_TIMEOUT;
  WiFi.mode(WIFI_STA);
  // Static IP skips the DHCP exchange (2-4s per wake). Falls back to DHCP
  // when unset or unparsable, and on any network but the one it was set for
  // (a router elsewhere would not route for it: every wake would fail).
  if (STATIC_IP[0] != '\0' && strcmp(STATIC_SSID, WIFI_SSID) != 0)
  {
    Serial.println("[wifi] the static IP was set for another network, "
                   "using DHCP");
  }
  else if (STATIC_IP[0] != '\0')
  {
    IPAddress ip, gw, sn, dns;
    if (ip.fromString(STATIC_IP) && gw.fromString(STATIC_GATEWAY)
        && sn.fromString(STATIC_SUBNET) && dns.fromString(STATIC_DNS))
    {
      WiFi.config(ip, gw, sn, dns);
    }
    else
    {
      Serial.println("[wifi] invalid static IP settings, using DHCP");
    }
  }
  wifiDisconnectReason = 0;
  WiFi.onEvent(onWiFiStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // WIFI_TIMEOUT is a worst-case allowance for networks that are present but
  // slow to associate. When the failure is definitive (SSID not found in a
  // scan, or the AP rejected us), waiting out the rest of the timeout just
  // burns battery -- especially now that failed attempts repeat every
  // WIFI_RETRY_INTERVAL minutes. Give up once a definitive failure state has
  // persisted for 10s, which still allows a few scan/auto-reconnect cycles
  // in case the network was only momentarily invisible.
  auto waitForConnect = [](unsigned long timeoutMs) -> wl_status_t
  {
    unsigned long timeout = millis() + timeoutMs;
    wl_status_t st = WiFi.status();
    unsigned long failedSince = 0;
    while ((st != WL_CONNECTED) && (millis() < timeout))
    {
      if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED)
      {
        if (failedSince == 0)
        {
          failedSince = millis();
        }
        else if (millis() - failedSince >= 10000)
        {
          break;
        }
      }
      else
      {
        failedSince = 0;
      }
      Serial.print(".");
      delay(50);
      st = WiFi.status();
    }
    Serial.println();
    return st;
  };

  // FAST RECONNECT: associate directly with the AP (BSSID + channel) that
  // won the scan last wake, skipping the ~2s scan entirely -- stationary
  // devices nearly always reconnect to the same AP. A directed connect to a
  // stale cache (router rebooted onto a new channel, mesh node gone) fails
  // fast; the cache is then cleared and the normal scan-and-pick runs.
  wl_status_t connection_status = WL_IDLE_STATUS;
  uint8_t cachedBssid[6];
  int32_t cachedChannel = 0;
  bool haveCachedAp = false;
  {
    Preferences bp;
    bp.begin(NVS_NAMESPACE, true);
    if (bp.getString("apSsid", "") == WIFI_SSID
        && bp.getBytesLength("apBssid") == sizeof(cachedBssid))
    {
      bp.getBytes("apBssid", cachedBssid, sizeof(cachedBssid));
      cachedChannel = bp.getInt("apChan", 0);
      haveCachedAp = (cachedChannel > 0);
    }
    bp.end();
  }
  if (haveCachedAp)
  {
    Serial.printf("%s '%s' [cached AP %02X:%02X:%02X:%02X:%02X:%02X, ch%ld]",
                  TXT_CONNECTING_TO, WIFI_SSID,
                  cachedBssid[0], cachedBssid[1], cachedBssid[2],
                  cachedBssid[3], cachedBssid[4], cachedBssid[5],
                  cachedChannel);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, cachedChannel, cachedBssid);
    connection_status = waitForConnect(6000);
    if (connection_status != WL_CONNECTED)
    {
      Serial.println("[wifi] cached AP connect failed, falling back to scan");
      forgetCachedAp();
      wifiDisconnectReason = 0;
      WiFi.disconnect();
      delay(100);
    }
  }

  if (connection_status != WL_CONNECTED)
  {
  // On networks with several access points sharing one SSID (mesh systems),
  // the ESP32 associates with whichever AP answers first -- which can be a
  // distant, weak one, producing a link that "connects" but drops UDP (NTP)
  // and stalls TCP. Scan first and lock onto the strongest AP; the ~2s scan
  // is cheap insurance. If the scan finds nothing (hidden SSID, or the AP
  // was momentarily quiet), fall back to the plain connect.
  int32_t bestChannel = 0;
  uint8_t bestBssid[6];
  int bestRSSI = -1000;
  bool haveBssid = false;
  int found = WiFi.scanNetworks();
  for (int i = 0; i < found; ++i)
  {
    if (WiFi.SSID(i) == WIFI_SSID && WiFi.RSSI(i) > bestRSSI)
    {
      bestRSSI = WiFi.RSSI(i);
      bestChannel = WiFi.channel(i);
      memcpy(bestBssid, WiFi.BSSID(i), 6);
      haveBssid = true;
    }
  }
  WiFi.scanDelete();

  Serial.printf("%s '%s'", TXT_CONNECTING_TO, WIFI_SSID);
  if (haveBssid)
  {
    Serial.printf(" [strongest AP %02X:%02X:%02X:%02X:%02X:%02X, %ddBm, ch%ld]",
                  bestBssid[0], bestBssid[1], bestBssid[2], bestBssid[3],
                  bestBssid[4], bestBssid[5], bestRSSI, bestChannel);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, bestChannel, bestBssid);
  }
  else
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

    connection_status = waitForConnect(patience);

  if (connection_status != WL_CONNECTED && haveBssid)
  {
    // Some mesh APs reject or mishandle authentication when the client pins
    // a specific BSSID/channel (observed on hardware: auth failure 202
    // against a healthy -39dBm AP whose password had not changed). Retry
    // once without the lock, letting the AP group steer the association.
    Serial.println("[wifi] locked connect failed, retrying without AP lock");
    wifiDisconnectReason = 0;
    WiFi.disconnect();
    delay(100);
    Serial.printf("%s '%s'", TXT_CONNECTING_TO, WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    connection_status = waitForConnect(patience);
  }
  } // end scan-and-connect fallback

  if (connection_status == WL_CONNECTED)
  {
    wifiRSSI = WiFi.RSSI(); // get WiFi signal strength now, because the WiFi
                            // will be turned off to save power!
    Serial.println("IP: " + WiFi.localIP().toString()
                 + "  gw: " + WiFi.gatewayIP().toString()
                 + "  mask: " + WiFi.subnetMask().toString()
                 + "  dns: " + WiFi.dnsIP().toString());

    // Refresh the fast-reconnect cache with the AP actually associated
    // with. A weak link (< -75dBm) is not cached: pinning a marginal AP
    // hurts more than next wake's scan, which may find a better one.
    Preferences bp;
    bp.begin(NVS_NAMESPACE, false);
    if (wifiRSSI >= -75 && WiFi.BSSID() != nullptr)
    {
      uint8_t cur[6];
      memcpy(cur, WiFi.BSSID(), sizeof(cur));
      uint8_t prev[6] = {0};
      bool same = bp.getBytesLength("apBssid") == sizeof(prev)
               && bp.getBytes("apBssid", prev, sizeof(prev)) == sizeof(prev)
               && memcmp(prev, cur, sizeof(prev)) == 0
               && bp.getInt("apChan", 0) == WiFi.channel()
               && bp.getString("apSsid", "") == WIFI_SSID;
      if (!same)
      { // write only on change to limit NVS wear
        bp.putBytes("apBssid", cur, sizeof(cur));
        bp.putInt("apChan", WiFi.channel());
        bp.putString("apSsid", WIFI_SSID);
        Serial.println("[wifi] cached AP for fast reconnect");
      }
    }
    else
    {
      bp.remove("apSsid");
      bp.remove("apBssid");
      bp.remove("apChan");
    }
    bp.end();
  }
  else
  {
    Serial.printf("%s '%s' (%s)\n", TXT_COULD_NOT_CONNECT_TO, WIFI_SSID,
                  getWifiFailureDetail(connection_status).c_str());
  }
  return connection_status;
} // startWiFi

/* Brings back a WiFi link that dropped in the middle of a wake: a router
 * restarting, a mesh node handing the display on, or a battery sagging
 * under the radio's load. Without this the wake gave up at once ("Connection
 * Lost", -517) and, since the next wake connected straight back to the same
 * cached access point, a link that had gone bad could fail every wake until
 * the settings were erased (reported on a reTerminal E1002 after about a
 * week). So the cached access point is forgotten first -- the next wake
 * scans for the strongest one -- and the network is joined once more, with
 * no access point pinned. Only once per wake: a link that keeps dropping
 * is better left until the next one than chased on the battery.
 *
 * Returns the WiFi status.
 */
static wl_status_t rejoinWiFi()
{
  static bool tried = false;
  if (tried)
  {
    return WiFi.status();
  }
  tried = true;
  Serial.printf("[wifi] link lost (status %d, reason %u), rejoining\n",
                static_cast<int>(WiFi.status()), wifiDisconnectReason);
  forgetCachedAp();
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
  {
    delay(50);
  }
  Serial.printf("[wifi] %s after %lu ms\n",
                WiFi.status() == WL_CONNECTED ? "rejoined" : "still down",
                millis() - start);
  return WiFi.status();
} // end rejoinWiFi

/* Explains why the WiFi connection attempt failed, based on the reason code
 * of the last disconnect event received from the WiFi stack.
 *
 * Note on interpreting the reason codes: routers deliberately report a wrong
 * passphrase in several different ways (an explicit auth failure, an expired
 * or timed-out 4-way handshake, or by just dropping the handshake), so all
 * of those map to "possibly incorrect password". A missing/mistyped network
 * name or a 5GHz-only network surfaces as either NO_AP_FOUND or a scan that
 * found nothing (WL_NO_SSID_AVAIL, reported before any disconnect event).
 * If no disconnect event arrived at all, the access point never responded.
 */
String getWifiFailureDetail(wl_status_t status)
{
  if (status == WL_NO_SSID_AVAIL)
  {
    return TXT_WIFI_REASON_NO_AP_FOUND;
  }

  String detail;
  switch (wifiDisconnectReason)
  {
  case 0: // never got far enough to be disconnected
    return TXT_WIFI_REASON_TIMEOUT;
  case WIFI_REASON_NO_AP_FOUND:
    detail = TXT_WIFI_REASON_NO_AP_FOUND;
    break;
  case WIFI_REASON_AUTH_EXPIRE:
  case WIFI_REASON_AUTH_FAIL:
  case WIFI_REASON_NOT_AUTHED:
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
  case WIFI_REASON_802_1X_AUTH_FAILED:
    detail = TXT_WIFI_REASON_AUTH_FAIL;
    break;
  case WIFI_REASON_ASSOC_FAIL:
  case WIFI_REASON_ASSOC_EXPIRE:
  case WIFI_REASON_ASSOC_TOOMANY:
  case WIFI_REASON_NOT_ASSOCED:
  case WIFI_REASON_CONNECTION_FAIL:
    detail = TXT_WIFI_REASON_ASSOC_FAIL;
    break;
  case WIFI_REASON_BEACON_TIMEOUT:
    detail = TXT_WIFI_REASON_BEACON_TIMEOUT;
    break;
  default:
    detail = TXT_WIFI_REASON_UNKNOWN;
    break;
  }
  // append the raw reason code, ex. "Possibly Incorrect Password (202)", so
  // unusual failures can be looked up in esp_wifi_types.h
  detail += " (" + String(wifiDisconnectReason) + ")";
  return detail;
} // end getWifiFailureDetail

/* Disconnect and power-off WiFi.
 */
void killWiFi()
{
  // Say goodbye and give the goodbye time to be sent: switching the radio
  // off in the same breath can cut it short, and a router that never heard
  // the display leave may not let it back in straight away.
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.disconnect();
    const unsigned long start = millis();
    while (WiFi.status() == WL_CONNECTED && millis() - start < 300)
    {
      delay(10);
    }
    delay(30);
  }
  WiFi.mode(WIFI_OFF);
} // killWiFi

/* Prints the local time to serial monitor.
 *
 * Returns true if getting local time was a success, otherwise false.
 */
bool printLocalTime(tm *timeInfo)
{
  int attempts = 0;
  while (!getLocalTime(timeInfo) && attempts++ < 3)
  {
    Serial.println(TXT_FAILED_TO_GET_TIME);
    return false;
  }
  Serial.println(timeInfo, "%A, %B %d, %Y %H:%M:%S");
  return true;
} // printLocalTime

/* Waits for NTP server time sync, adjusted for the time zone specified in
 * config.cpp.
 *
 * Returns true if time was set successfully, otherwise false.
 *
 * Note: Must be connected to WiFi to get time from NTP server.
 */
bool waitForSNTPSync(tm *timeInfo)
{
  // Wait for SNTP synchronization to complete
  unsigned long timeout = millis() + NTP_TIMEOUT;
  if ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
      && (millis() < timeout))
  {
    Serial.print(TXT_WAITING_FOR_SNTP);
    delay(100); // ms
    while ((sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
        && (millis() < timeout))
    {
      Serial.print(".");
      delay(100); // ms
    }
    Serial.println();
  }
  return printLocalTime(timeInfo);
} // waitForSNTPSync

/* Splits a full URL, ex. "https://api.weather.gov/gridpoints/LWX/106,95",
 * into its host and path+query components.
 */
static void splitUrl(const String &url, String &host, String &uri)
{
  int start = url.indexOf("://");
  start = (start == -1) ? 0 : start + 3;
  int slash = url.indexOf('/', start);
  if (slash == -1)
  {
    host = url.substring(start);
    uri = "/";
  }
  else
  {
    host = url.substring(start, slash);
    uri = url.substring(slash);
  }
} // end splitUrl

/* Performs an HTTP GET request, retrying up to maxAttempts times, and hands
 * the response stream to parseFn to be deserialized on success.
 *
 * If logUri is non-empty it is printed to serial instead of uri; used to
 * censor API keys from the logs. timeoutMs overrides
 * HTTP_CLIENT_TCP_TIMEOUT when non-zero; used for hosts that are known to
 * respond slowly (AirNow's gateway can take 15-25s under load, where every
 * other API answers in ~1s).
 *
 * Returns the HTTP Status Code (or a negative offset error code, see
 * getHttpResponsePhrase for the meaning of negative codes).
 */
static int httpGetAndParse(WiFiClient &client, const String &host,
                           uint16_t port, const String &uri,
                           const String &userAgent,
                           std::function<DeserializationError(WiFiClient&)> parseFn,
                           const String &logUri = "",
                           unsigned timeoutMs = 0,
                           int maxAttempts = 3,
                           bool http11 = false,
                           const std::vector<std::pair<String, String>> *extraHeaders = nullptr)
{
  int attempts = 0;
  // HTTP/1.1 only for replies known to carry a Content-Length: it keeps the
  // TLS session open for the next request to the same host (~1.2 s saved
  // per reuse). A chunked reply drops back to HTTP/1.0 below.
  bool useHttp11 = http11;
  bool rxSuccess = false;
  DeserializationError jsonErr = {};
  if (timeoutMs == 0)
  {
    timeoutMs = HTTP_CLIENT_TCP_TIMEOUT;
  }

  String sanitizedUri = "https://" + host + (logUri.isEmpty() ? uri : logUri);
  Serial.print(TXT_ATTEMPTING_HTTP_REQ);
  Serial.println(": " + sanitizedUri);

  int httpResponse = 0;
  while (!rxSuccess && attempts < maxAttempts)
  {
    wl_status_t connection_status = WiFi.status();
    if (connection_status != WL_CONNECTED)
    {
      connection_status = rejoinWiFi();
    }
    if (connection_status != WL_CONNECTED)
    {
      // -512 offset distinguishes these errors from httpClient errors
      return -512 - static_cast<int>(connection_status);
    }

    HTTPClient &http = s_http;
    if (client.connected() && (s_openHost != host || s_openPort != port))
    {
      client.stop();
      s_openHost = "";
    }
    http.setConnectTimeout(timeoutMs); // default 5000ms
    http.setTimeout(timeoutMs); // default 5000ms
    // HTTP/1.0 unless the caller asked for 1.1. Both weather.gov and
    // Open-Meteo reply with "Transfer-Encoding: chunked" over HTTP/1.1 for
    // most responses, but HTTPClient::getStream() hands back the raw socket
    // WITHOUT stripping the chunk framing. ArduinoJson then sees the leading
    // hex chunk length instead of '{' -- either failing outright
    // (InvalidInput) or, when the chunk length happens to be all decimal
    // digits, silently parsing it as a bare number and yielding an empty
    // document. HTTP/1.0 has no chunked encoding, so the body arrives clean;
    // the price is that the server closes the connection after each reply.
    // (useHTTP10 also sets the keep-alive flag to match.)
    http.useHTTP10(!useHttp11);
    if (!userAgent.isEmpty())
    {
      http.setUserAgent(userAgent);
    }
    const unsigned long tReq = millis();
    http.begin(client, host, port, uri);
    if (extraHeaders)
    {
      for (const auto &h : *extraHeaders)
      {
        http.addHeader(h.first, h.second);
      }
    }
    httpResponse = http.GET();
    const unsigned long tHdr = millis(); // connect + TLS + request + server
    if (useHttp11 && httpResponse == HTTP_CODE_OK && http.getSize() < 0)
    { // chunked: getStream() would hand the chunk framing to the parser
      Serial.println("  chunked HTTP/1.1 reply, retrying as HTTP/1.0");
      http.end();
      client.stop();
      s_openHost = "";
      useHttp11 = false;
      continue;
    }
    if (httpResponse == HTTP_CODE_OK)
    {
      jsonErr = parseFn(http.getStream());
      if (jsonErr)
      {
        // -256 offset distinguishes these errors from httpClient errors
        httpResponse = -256 - static_cast<int>(jsonErr.code());
      }
      rxSuccess = !jsonErr;
    }
    // end() leaves the socket open only after a reusable HTTP/1.1 reply;
    // after anything else (HTTP/1.0, errors, a half-read body) it is closed
    // so the next request starts clean.
    http.end();
    if (rxSuccess && client.connected())
    {
      s_openHost = host;
      s_openPort = port;
    }
    else
    {
      client.stop();
      s_openHost = "";
    }
    Serial.println("  " + String(httpResponse, DEC) + " "
                   + getHttpResponsePhrase(httpResponse)
                   + "  (" + String(tHdr - tReq) + " ms to headers, "
                   + String(millis() - tHdr) + " ms body)");
    ++attempts;
  }

  return httpResponse;
} // end httpGetAndParse

/* The part of a weather fetch every forecast provider shares: the current
 * conditions from CURRENT_SOURCE (Open-Meteo, Google, or the provider's own
 * -- the hourly forecast's first period, or providerCurrent when the
 * provider has a current-conditions feed), the NVS cache that bridges a
 * failed fetch, and, unless the provider supplied daily rain totals itself
 * (providerDaily), the daily precipitation amounts from weather.gov's QPF
 * buckets and Open-Meteo.
 */
static int finishWeather(WiFiClient &client, owm_current_t &current,
                         owm_hourly_t *hourly, owm_daily_t *daily,
                         const std::vector<qpf_bucket_t> &qpf,
                         bool providerDaily,
                         std::function<int()> providerCurrent)
{
  // current conditions, from the source selected by CURRENT_SOURCE
  // (config.json "api.current_source"):
  //   "open-meteo" (default): Open-Meteo's model interpolated to LAT/LON,
  //     refreshed every ~15 minutes. Preferred over the nearest NWS station
  //     because "nearest" can still be 10+ miles away in a different
  //     microclimate (a suburban location served by an urban-waterfront
  //     airport station read 7-10F hot).
  //   "google": Google Weather API current conditions -- nearby stations
  //     blended with a 15-minute nowcast. Needs the (Google Maps Platform)
  //     POLLEN_APIKEY with the Weather API enabled.
  //   "nws": weather.gov's hourly forecast period for the current hour,
  //     which the hourly fetch above already delivered. No extra request.
  // Failure of a model source is not fatal -- current conditions fall back
  // to the hourly forecast.
  bool useGoogle = (CURRENT_SOURCE == "google");
  bool useNWS    = (CURRENT_SOURCE == "nws");
  if (useGoogle && POLLEN_APIKEY.isEmpty())
  {
    Serial.println("[current] source \"google\" needs api.pollen_api_key, "
                   "using Open-Meteo instead");
    useGoogle = false;
  }
  bool useOM = !useGoogle && !useNWS;

  // Open-Meteo request. When it is the current-conditions source this
  // carries the `current` block; with DAILY_PRECIP_AMOUNTS it also carries
  // (or, for another source, only carries) the daily precipitation totals
  // for the forecast-row days weather.gov's QPF does not reach (precip.h).
  om_daily_precip_t omDaily = {};
  om_gaps_t omGaps = {0, -1};
  // A forecast provider's own current-conditions feed (IMS now_analysis)
  // has no pressure or visibility; Open-Meteo fills those two in.
  const bool fillGaps = useNWS && providerCurrent;
  int omResponse = HTTP_CODE_OK;
#if DAILY_PRECIP_AMOUNTS
  const bool needOM = useOM || fillGaps || !providerDaily;
#else
  const bool needOM = useOM || fillGaps;
#endif
  if (needOM)
  {
    String omUri = "/v1/forecast?latitude=" + LAT + "&longitude=" + LON;
    if (useOM)
    {
      omUri += "&current=temperature_2m,apparent_temperature,"
               "relative_humidity_2m,dew_point_2m,pressure_msl,"
               "cloud_cover,visibility,wind_speed_10m,wind_gusts_10m,"
               "wind_direction_10m&wind_speed_unit=ms";
    }
    else if (fillGaps)
    {
      omUri += "&current=pressure_msl,visibility";
    }
#if DAILY_PRECIP_AMOUNTS
    // Local midnights for the coordinate's own zone, as unix time.
    omUri += "&daily=precipitation_sum&forecast_days=7";
#else
    omUri += "&forecast_days=1";
#endif
    omUri += "&timezone=auto&timeformat=unixtime";
    omResponse = httpGetAndParse(client, OPEN_METEO_ENDPOINT, HTTPS_PORT,
        omUri, "",
        [&](WiFiClient &s) {
          return deserializeOpenMeteoCurrent(s, hourly[0], current, omDaily,
                                             useOM,
                                             fillGaps ? &omGaps : nullptr);
        });
  }

  int curResponse = HTTP_CODE_OK;
  const char *curName = "open-meteo";
  if (useGoogle)
  {
    curName = "google";
    curResponse = getGoogleCurrent(client, hourly[0], current);
  }
  else if (useOM)
  {
    curResponse = omResponse;
  }
  else if (providerCurrent)
  {
    // the forecast provider's own current conditions (IMS now_analysis)
    curName = "provider";
    curResponse = providerCurrent();
    if (curResponse == HTTP_CODE_OK && omResponse == HTTP_CODE_OK)
    {
      if (current.pressure <= 0)  current.pressure   = omGaps.pressure;
      if (current.visibility < 0) current.visibility = omGaps.visibility;
    }
  }
  else
  {
    fillCurrentFromFallback(hourly[0], current);
  }

  if (!useNWS || providerCurrent)
  {
    // Both model sources intermittently fail (api.open-meteo.com serves
    // bursts of 503s -- measured ~1 in 6 requests during a rough patch, and
    // bursts can eat all 3 attempts of a wake). The hourly-forecast fallback
    // has no pressure/visibility (and humidity/dew point only when NWS
    // reports them), which blanked those widgets for a full refresh cycle.
    // Cache the last good values in NVS and reuse them for up to 2 hours --
    // these fields drift slowly, so slightly stale numbers beat empty
    // widgets.
    Preferences omCache;
    omCache.begin(NVS_NAMESPACE, false);
    if (curResponse == HTTP_CODE_OK)
    {
      omCache.putULong("omStamp", static_cast<unsigned long>(time(nullptr)));
      omCache.putInt("omHum", current.humidity);
      omCache.putFloat("omDew", current.dew_point);
      omCache.putInt("omPres", current.pressure);
      omCache.putInt("omVis", current.visibility);
    }
    else
    {
      fillCurrentFromFallback(hourly[0], current);
      unsigned long stamp = omCache.getULong("omStamp", 0);
      unsigned long now = static_cast<unsigned long>(time(nullptr));
      if (stamp != 0 && now >= stamp && now - stamp < 2UL * 3600UL)
      {
        current.humidity   = omCache.getInt("omHum", current.humidity);
        current.dew_point  = omCache.getFloat("omDew", current.dew_point);
        current.pressure   = omCache.getInt("omPres", current.pressure);
        current.visibility = omCache.getInt("omVis", current.visibility);
        Serial.println("[" + String(curName)
                       + "] fetch failed, reusing cached conditions ("
                       + String((now - stamp) / 60) + "min old)");
      }
    }
    omCache.end();
  }

#if DAILY_PRECIP_AMOUNTS
  if (!providerDaily)
  {
    const int64_t now = static_cast<int64_t>(time(nullptr));
    for (int i = 0; i < OWM_NUM_DAILY; ++i)
    {
      daily_precip_pick_t pick = pickDailyPrecip(daily[i].dt, now, qpf, omDaily);
      daily[i].rain       = pick.mm;
      daily[i].snow       = 0.f;
      daily[i].precip_src = pick.src;
    }
  }
#else
  (void)qpf;
  (void)providerDaily;
#endif

  return HTTP_CODE_OK;
} // end finishWeather

/* Fetches weather.gov's gridpoint forecast (12-hour periods and hourly), and
 * current conditions from the source selected by CURRENT_SOURCE.
 *
 * Returns the HTTP Status Code of the request that failed, or HTTP_CODE_OK.
 */
int getNWSWeather(WiFiClient &client, owm_current_t &current,
                  owm_hourly_t *hourly, owm_daily_t *daily,
                  String &failedStep)
{
  String forecastUrl, forecastHourlyUrl;

  // Check NVS for cached gridpoint URLs for the current LAT/LON
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  String cachedLat = prefs.getString("nwsLat", "");
  String cachedLon = prefs.getString("nwsLon", "");
  forecastUrl       = prefs.getString("nwsFc", "");
  forecastHourlyUrl = prefs.getString("nwsFcH", "");
  prefs.end();

  bool haveCachedUrls = (cachedLat == LAT && cachedLon == LON
                         && !forecastUrl.isEmpty()
                         && !forecastHourlyUrl.isEmpty());

  auto fetchPoints = [&]() -> int {
    int res = httpGetAndParse(client, NWS_ENDPOINT, HTTPS_PORT,
        "/points/" + LAT + "," + LON, NWS_USER_AGENT,
        [&](WiFiClient &s) {
          return deserializeNWSPoints(s, forecastUrl, forecastHourlyUrl);
        });
    if (res == HTTP_CODE_OK && !forecastUrl.isEmpty() && !forecastHourlyUrl.isEmpty())
    {
      prefs.begin(NVS_NAMESPACE, false);
      prefs.putString("nwsLat", LAT);
      prefs.putString("nwsLon", LON);
      prefs.putString("nwsFc", forecastUrl);
      prefs.putString("nwsFcH", forecastHourlyUrl);
      prefs.end();
      Serial.println("[nws] cached gridpoint forecast URLs for " + LAT + "," + LON);
    }
    return res;
  };

  if (haveCachedUrls)
  {
    Serial.println("[nws] using cached gridpoint forecast URLs");
  }
  else
  {
    int httpResponse = fetchPoints();
    if (httpResponse != HTTP_CODE_OK)
    {
      failedStep = "points";
      return httpResponse;
    }
  }

  // The daily forecast goes first: its ~13 KB reply carries a Content-Length,
  // so it can travel over HTTP/1.1 keep-alive and the hourly request that
  // follows reuses the TLS session. (The hourly reply is chunked, so it has
  // to be HTTP/1.0 and closes the connection afterwards.) Current
  // conditions later fall back to hourly[0] for any field the chosen source
  // doesn't report.
  String host, uri;
  splitUrl(forecastUrl, host, uri);
  int httpResponse = httpGetAndParse(client, host, HTTPS_PORT, uri, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSForecastDaily(s, daily); },
      "", 0, 3, true);

  // If 404 and we were using cached URLs, re-fetch points and retry once
  if (httpResponse == HTTP_CODE_NOT_FOUND && haveCachedUrls)
  {
    Serial.println("[nws] cached forecast URL returned 404, re-querying /points");
    haveCachedUrls = false;
    int ptsRes = fetchPoints();
    if (ptsRes == HTTP_CODE_OK)
    {
      splitUrl(forecastUrl, host, uri);
      httpResponse = httpGetAndParse(client, host, HTTPS_PORT, uri, NWS_USER_AGENT,
          [&](WiFiClient &s) { return deserializeNWSForecastDaily(s, daily); },
          "", 0, 3, true);
    }
    else
    {
      failedStep = "points";
      return ptsRes;
    }
  }

  if (httpResponse != HTTP_CODE_OK)
  {
    failedStep = "daily forecast";
    return httpResponse;
  }

  splitUrl(forecastHourlyUrl, host, uri);
  httpResponse = httpGetAndParse(client, host, HTTPS_PORT, uri, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSForecastHourly(s, hourly); });
  if (httpResponse != HTTP_CODE_OK)
  {
    failedStep = "hourly forecast";
    return httpResponse;
  }
  // The graph draws HOURLY_GRAPH_MAX hours. NWS sends far more than that,
  // but should it ever send fewer, draw what there is, not empty hours.
  {
    int got = 0;
    while (got < OWM_NUM_HOURLY && hourly[got].dt > 0)
    {
      ++got;
    }
    if (got < HOURLY_GRAPH_MAX)
    {
      Serial.println("[nws] hourly forecast holds only " + String(got)
                     + " hours");
      if (got < 8)
      {
        failedStep = "hourly forecast";
        return -256 - static_cast<int>(DeserializationError::IncompleteInput);
      }
      HOURLY_GRAPH_MAX = got;
    }
  }

  std::vector<qpf_bucket_t> qpf;
  // The 7.09in layout's stacked graphs want the hours' amounts, gusts, sky
  // cover and feels-like: the hourly forecast has only the chance of rain
  // and the wind, and the rest is in the same raw gridpoint as the QPF.
  grid_series_t series;
  const bool wantSeries = (GRAPH_LAYOUT != 0);
  if (DAILY_PRECIP_AMOUNTS || wantSeries)
  {
    // Daily precipitation AMOUNTS for the forecast row. weather.gov keeps
    // these only in the raw gridpoint, which is the same URL as the hourly
    // forecast minus "/forecast/hourly". It is a ~230 KB response for 13
    // numbers, so it is only fetched when a daily amount unit is compiled in
    // (or the stacked graphs are on), and never fatal: without it every day
    // falls back to Open-Meteo (below).
    //
    // It is also the most expensive request of a wake (~3.6 s measured on
    // the E1002, most of it streaming the body), and three-day rain totals
    // do not change every 20 minutes, so the buckets are cached in NVS and
    // refetched only every QPF_CACHE_HOURS. A copy up to QPF_STALE_OK_S old
    // also stands in for a failed fetch: hours-old NWS totals beat losing
    // the days it covers to the Open-Meteo fallback.
    Preferences qp;
    qp.begin(NVS_NAMESPACE, false);
    const time_t qNow = time(nullptr);
    tm qTm;
    localtime_r(&qNow, &qTm);
    const int32_t qpfStamp = ((qTm.tm_year + 1900) * 10000
                              + (qTm.tm_mon + 1) * 100 + qTm.tm_mday) * 100
                             + qTm.tm_hour / QPF_CACHE_HOURS;
    auto loadBlob = [&](const char *key, std::vector<qpf_bucket_t> &v) -> bool
    {
      const size_t len = qp.getBytesLength(key);
      if (len == 0 || len % sizeof(qpf_bucket_t) != 0
          || len > sizeof(qpf_bucket_t) * QPF_CACHE_MAX)
      {
        return false;
      }
      v.resize(len / sizeof(qpf_bucket_t));
      return qp.getBytes(key, v.data(), len) == len;
    };
    auto saveBlob = [&](const char *key, const std::vector<qpf_bucket_t> &v)
    {
      const size_t n = (v.size() > QPF_CACHE_MAX) ? QPF_CACHE_MAX : v.size();
      if (n)
      {
        qp.putBytes(key, v.data(), n * sizeof(qpf_bucket_t));
      }
      else
      {
        qp.remove(key);
      }
    };
    // the series are kept only while the stacked graphs are on; a copy
    // without them does not do for a wake that wants them
    auto loadCached = [&]() -> bool
    {
      if (!loadBlob("qpfBlob", qpf))
      {
        return false;
      }
      if (!wantSeries)
      {
        return true;
      }
      return loadBlob("gustBlob", series.gust)
          && loadBlob("skyBlob", series.sky)
          && loadBlob("feelBlob", series.feel);
    };

    if (qp.getInt("qpfStamp", 0) == qpfStamp && loadCached())
    {
      Serial.println("[nws] cached gridpoint QPF (" + String(qpf.size())
                     + " buckets), skipping fetch");
    }
    else
    {
      String qpfHost, qpfUri;
      splitUrl(forecastHourlyUrl, qpfHost, qpfUri);
      int cut = qpfUri.indexOf("/forecast");
      if (cut > 0)
      {
        qpfUri = qpfUri.substring(0, cut);
      }
      qpf.clear();
      series = grid_series_t();
      int qpfResponse = httpGetAndParse(client, qpfHost, HTTPS_PORT, qpfUri,
          NWS_USER_AGENT,
          [&](WiFiClient &s) {
            return deserializeNWSGridpointQPF(s, qpf,
                                              wantSeries ? &series : nullptr);
          });
      if (qpfResponse == HTTP_CODE_OK && !qpf.empty())
      {
        saveBlob("qpfBlob", qpf);
        saveBlob("gustBlob", series.gust);
        saveBlob("skyBlob", series.sky);
        saveBlob("feelBlob", series.feel);
        qp.putInt("qpfStamp", qpfStamp);
        qp.putLong("qpfTime", static_cast<long>(qNow));
      }
      else
      {
        const long fetched = qp.getLong("qpfTime", 0);
        if (fetched != 0 && qNow - fetched < QPF_STALE_OK_S && loadCached())
        {
          Serial.println("[nws] gridpoint QPF unavailable ("
                         + String(qpfResponse, DEC) + "), using the copy from "
                         + String((qNow - fetched) / 60) + " min ago");
        }
        else
        {
          qpf.clear();
          series = grid_series_t();
          Serial.println("[nws] gridpoint QPF unavailable ("
                         + String(qpfResponse, DEC)
                         + "), daily amounts fall back to Open-Meteo");
        }
      }
    }
    qp.end();
  }

  if (wantSeries)
  {
    // the hours' amounts, shared out of the 6-hour buckets by the hours'
    // chance of rain; gusts, sky cover and feels-like where the gridpoint
    // has them
    int64_t start[OWM_NUM_HOURLY];
    float pop[OWM_NUM_HOURLY], mm[OWM_NUM_HOURLY];
    int n = 0;
    while (n < OWM_NUM_HOURLY && hourly[n].dt > 0)
    {
      start[n] = hourly[n].dt;
      pop[n] = hourly[n].pop;
      ++n;
    }
    spreadQpf(qpf, start, pop, n, mm);
    for (int i = 0; i < n; ++i)
    {
      const int64_t mid = hourly[i].dt + 1800;
      hourly[i].precip = mm[i];
      const float gust = bucketAt(series.gust, mid);
      if (!std::isnan(gust))
      {
        hourly[i].wind_gust = std::max(hourly[i].wind_speed, gust / 3.6f);
      }
      const float sky = bucketAt(series.sky, mid);
      if (!std::isnan(sky))
      {
        hourly[i].clouds = static_cast<int>(std::round(sky));
      }
      const float feel = bucketAt(series.feel, mid);
      if (!std::isnan(feel))
      {
        hourly[i].feels_like = celsius_to_kelvin(feel);
      }
    }
  }

  return finishWeather(client, current, hourly, daily, qpf, false, nullptr);
} // end getNWSWeather

/* Fetches active weather alerts for LAT/LON from weather.gov.
 *
 * Returns the HTTP Status Code.
 */
int getNWSAlerts(WiFiClient &client, std::vector<owm_alerts_t> &alerts)
{
  return httpGetAndParse(client, NWS_ENDPOINT, HTTPS_PORT,
      "/alerts/active?point=" + LAT + "," + LON, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSAlerts(s, alerts); },
      "", 0, 3, true); // small, Content-Length: keep-alive for the forecast
} // end getNWSAlerts

/* Fetches UV index and air pollutant concentrations for LAT/LON from
 * Open-Meteo (weather.gov does not provide either).
 *
 * Returns the HTTP Status Code.
 */
int getAirQuality(WiFiClient &client, owm_resp_air_pollution_t &air,
                  float &uvi)
{
  // past_hours includes the current (partial) hour, so this yields exactly
  // OWM_NUM_AIR_POLLUTION hourly values ending at "now" -- no future forecast
  // hours are needed.
  String uri = "/v1/air-quality?latitude=" + LAT + "&longitude=" + LON
             + "&hourly=pm10,pm2_5,carbon_monoxide,nitrogen_dioxide,"
               "sulphur_dioxide,ozone,uv_index"
               "&timezone=UTC&past_hours=" + String(OWM_NUM_AIR_POLLUTION)
             + "&forecast_hours=0";
  return httpGetAndParse(client, AIR_QUALITY_ENDPOINT, HTTPS_PORT, uri, "",
      [&](WiFiClient &s) { return deserializeAirQuality(s, air, uvi); });
} // end getAirQuality

/* Fetches the official US EPA Air Quality Index for LAT/LON from AirNow
 * (airnow.gov). Requires a free API key (AIRNOW_APIKEY); callers should
 * skip this entirely when no key is configured.
 *
 * aqi is set to the maximum AQI across reported pollutants, or left at -1
 * if no monitoring station reported within the search radius.
 *
 * Returns the HTTP Status Code.
 */
int getAirNowAQI(WiFiClient &client, int &aqi)
{
  // Site-level NowCast data inside a bounding box around LAT/LON. The
  // simpler /aq/observation/latLong/current/ endpoint aggregates by
  // "reporting area" and routinely drops pollutants whose hourly ingest is
  // lagging (a missing PM2.5 row once made the display show ozone's AQI 6
  // while airnow.gov showed PM2.5 NowCast 52); the raw site feed carries
  // every station's per-pollutant NowCast AQI directly, matching the
  // airnow.gov website.
  float lat = LAT.toFloat(), lon = LON.toFloat();
  String baseUri = String("/aq/data/?format=application/json") +
                   "&parameters=OZONE,PM25,PM10" +
                   "&BBOX=" + String(lon - 0.3f, 4) + "," +
                              String(lat - 0.3f, 4) + "," +
                              String(lon + 0.3f, 4) + "," +
                              String(lat + 0.3f, 4) +
                   "&dataType=B&monitorType=2&verbose=0" +
                   "&includerawconcentrations=0";
  String uri = baseUri + "&API_KEY=" + AIRNOW_APIKEY;
  // API key is censored from the serial log to reduce the risk of users
  // exposing their key.
  String logUri = baseUri + "&API_KEY={API key}";
  // AirNow's gateway intermittently takes 15-25s to answer (where every
  // other API used by this project answers in ~1s), so give it a 30s
  // timeout instead of HTTP_CLIENT_TCP_TIMEOUT -- a slow success beats
  // repeated fast failures. 2 attempts caps the worst case at 60s on this
  // non-fatal request.
  return httpGetAndParse(client, AIRNOW_ENDPOINT, HTTPS_PORT, uri, "",
      [&](WiFiClient &s) { return deserializeAirNow(s, aqi); }, logUri,
      30000, 2);
} // end getAirNowAQI

/* Fetches today's pollen forecast from the Google Pollen API
 * (https://developers.google.com/maps/documentation/pollen). Requires a
 * Google Maps Platform API key with the Pollen API enabled
 * (config.json api.pollen_api_key). Non-fatal on failure.
 */
/* Fetches current conditions from the Google Maps Platform Weather API.
 * Uses POLLEN_APIKEY (a Maps Platform key; the Weather API must be enabled
 * on its project). On failure `current` is filled from the hourly forecast.
 *
 * Returns the HTTP Status Code.
 */
int getGoogleCurrent(WiFiClient &client, const owm_hourly_t &fallback,
                     owm_current_t &current)
{
  String baseUri = "/v1/currentConditions:lookup"
                   "?location.latitude=" + LAT
                 + "&location.longitude=" + LON;
  String uri = baseUri + "&key=" + POLLEN_APIKEY;
  // API key is censored from the serial log to reduce the risk of users
  // exposing their key.
  String logUri = baseUri + "&key={API key}";
  int res = httpGetAndParse(client, GOOGLE_WEATHER_ENDPOINT, HTTPS_PORT, uri,
      "", [&](WiFiClient &s) {
        return deserializeGoogleCurrent(s, fallback, current);
      }, logUri);
  if (res != HTTP_CODE_OK)
  {
    fillCurrentFromFallback(fallback, current);
  }
  return res;
} // end getGoogleCurrent

int getGooglePollen(WiFiClient &client, pollen_info_t &pollen)
{
  String baseUri = "/v1/forecast:lookup"
                   "?location.latitude=" + LAT
                 + "&location.longitude=" + LON
                 + "&days=1";
  String uri = baseUri + "&key=" + POLLEN_APIKEY;
  // API key is censored from the serial log to reduce the risk of users
  // exposing their key.
  String logUri = baseUri + "&key={API key}";
  return httpGetAndParse(client, POLLEN_ENDPOINT, HTTPS_PORT, uri, "",
      [&](WiFiClient &s) { return deserializePollen(s, pollen); }, logUri);
} // end getGooglePollen


/* ---- Israel Meteorological Service ------------------------------------ */

// IMS answers in English or Hebrew; the locale decides which.
static String imsLang()
{
  return (OWM_LANG == "he") ? "he" : "en";
}

/* The IMS location for LAT/LON (or IMS_LOCATION_ID when set): its id, its
 * warning region and its name. ims.gov.il's location list hardly changes,
 * so the answer is cached in NVS against the coordinates and setting that
 * produced it; the list (~30 KB) is fetched again only when they change.
 *
 * Returns the HTTP Status Code.
 */
int getIMSLocation(WiFiClient &client, int &lid, int &rid, String &name)
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  const bool cached = prefs.getString("imsLat", "") == LAT
                   && prefs.getString("imsLon", "") == LON
                   && prefs.getInt("imsCfg", -1) == IMS_LOCATION_ID
                   && prefs.getInt("imsLid", 0) > 0;
  if (cached)
  {
    lid  = prefs.getInt("imsLid", 0);
    rid  = prefs.getInt("imsRid", 0);
    name = prefs.getString("imsName", "");
    prefs.end();
    Serial.println("[ims] location " + String(lid) + " (" + name
                   + "), region " + String(rid) + " (cached)");
    return HTTP_CODE_OK;
  }
  prefs.end();

  const double lat = LAT.toDouble();
  const double lon = LON.toDouble();
  int res = httpGetAndParse(client, IMS_ENDPOINT, HTTPS_PORT,
      "/" + imsLang() + "/locations_info", IMS_USER_AGENT,
      [&](WiFiClient &s) {
        return deserializeIMSLocations(s, lat, lon, IMS_LOCATION_ID, lid, rid,
                                       name);
      });
  if (res == HTTP_CODE_OK && lid > 0)
  {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("imsLat", LAT);
    prefs.putString("imsLon", LON);
    prefs.putInt("imsCfg", IMS_LOCATION_ID);
    prefs.putInt("imsLid", lid);
    prefs.putInt("imsRid", rid);
    prefs.putString("imsName", name);
    prefs.end();
    Serial.println("[ims] location " + String(lid) + " (" + name
                   + "), region " + String(rid));
  }
  return res;
} // end getIMSLocation

/* Fetches the Israel Meteorological Service's 7-day hourly forecast for
 * the configured location, then the current conditions from
 * CURRENT_SOURCE ("nws" meaning IMS's own now_analysis feed). Populates
 * current/hourly/daily. Daily rain totals are IMS's own, so the Open-Meteo
 * daily request is skipped.
 *
 * Returns the HTTP Status Code of the request that failed, or HTTP_CODE_OK.
 */
int getIMSWeather(WiFiClient &client, owm_current_t &current,
                  owm_hourly_t *hourly, owm_daily_t *daily,
                  String &failedStep)
{
  int lid = 0, rid = 0;
  String name;
  int res = getIMSLocation(client, lid, rid, name);
  if (res != HTTP_CODE_OK)
  {
    failedStep = "locations";
    return res;
  }

  res = httpGetAndParse(client, IMS_ENDPOINT, HTTPS_PORT,
      "/" + imsLang() + "/forecast_data/" + String(lid), IMS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeIMSForecast(s, hourly, daily); });
  if (res != HTTP_CODE_OK)
  {
    failedStep = "forecast";
    return res;
  }

  // the hourly series ends where the feed does (the 7th day's last hour);
  // the graph cannot show hours that are not there
  int got = 0;
  while (got < OWM_NUM_HOURLY && hourly[got].dt > 0)
  {
    ++got;
  }
  if (got < HOURLY_GRAPH_MAX)
  {
    Serial.println("[ims] hourly forecast holds only " + String(got) + " hours");
    if (got < 8)
    {
      failedStep = "forecast";
      return -256 - static_cast<int>(DeserializationError::IncompleteInput);
    }
    HOURLY_GRAPH_MAX = got;
  }

  std::vector<qpf_bucket_t> noQpf;
  return finishWeather(client, current, hourly, daily, noQpf, true,
      [&]() -> int {
        return httpGetAndParse(client, IMS_ENDPOINT, HTTPS_PORT,
            "/" + imsLang() + "/now_analysis?lid=" + String(lid),
            IMS_USER_AGENT,
            [&](WiFiClient &s) {
              return deserializeIMSCurrent(s, lid, hourly[0], current);
            });
      });
} // end getIMSWeather

/* Fetches the Israel Meteorological Service's current warnings for the
 * configured location's region.
 *
 * Returns the HTTP Status Code.
 */
int getIMSAlerts(WiFiClient &client, std::vector<owm_alerts_t> &alerts)
{
  int lid = 0, rid = 0;
  String name;
  int res = getIMSLocation(client, lid, rid, name);
  if (res != HTTP_CODE_OK)
  {
    return res;
  }
  res = httpGetAndParse(client, IMS_ENDPOINT, HTTPS_PORT,
      "/" + imsLang() + "/warnings", IMS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeIMSAlerts(s, rid, alerts); });
  if (res == HTTP_CODE_OK)
  {
    Serial.println("[ims] " + String(alerts.size()) + " warning(s) for region "
                   + String(rid));
  }
  return res;
} // end getIMSAlerts


/* ---- Open-Meteo forecast ----------------------------------------------- */

/* Fetches Open-Meteo's forecast for LAT/LON (one request: 48 hours from the
 * current hour, seven days, the coordinates' own time zone), then the
 * current conditions and the rest through finishWeather. Open-Meteo's
 * daily precipitation_sum is the forecast row's amount, so the weather.gov
 * QPF path is skipped.
 *
 * Returns the HTTP Status Code of the request that failed, or HTTP_CODE_OK.
 */
int getOpenMeteoWeather(WiFiClient &client, owm_current_t &current,
                        owm_hourly_t *hourly, owm_daily_t *daily,
                        String &failedStep)
{
  const String uri = "/v1/forecast?latitude=" + LAT + "&longitude=" + LON
      + "&hourly=temperature_2m,apparent_temperature,relative_humidity_2m,"
        "dew_point_2m,precipitation_probability,precipitation,weather_code,"
        "cloud_cover,wind_speed_10m,wind_gusts_10m,wind_direction_10m,is_day"
      + "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        "precipitation_sum,precipitation_probability_max,wind_speed_10m_max,"
        "wind_gusts_10m_max,wind_direction_10m_dominant,uv_index_max,"
        "cloud_cover_mean"
      + "&forecast_hours=" + String(OWM_NUM_HOURLY) + "&forecast_days="
      + String(OWM_NUM_DAILY)
      + "&timezone=auto&timeformat=unixtime&wind_speed_unit=ms";
  int res = httpGetAndParse(client, OPEN_METEO_ENDPOINT, HTTPS_PORT, uri, "",
      [&](WiFiClient &s) { return deserializeOpenMeteoForecast(s, hourly, daily); });
  if (res != HTTP_CODE_OK)
  {
    failedStep = "forecast";
    return res;
  }

  int got = 0;
  while (got < OWM_NUM_HOURLY && hourly[got].dt > 0)
  {
    ++got;
  }
  if (got < HOURLY_GRAPH_MAX)
  {
    Serial.println("[open-meteo] hourly forecast holds only " + String(got)
                   + " hours");
    if (got < 8)
    {
      failedStep = "forecast";
      return -256 - static_cast<int>(DeserializationError::IncompleteInput);
    }
    HOURLY_GRAPH_MAX = got;
  }

  std::vector<qpf_bucket_t> noQpf;
  return finishWeather(client, current, hourly, daily, noQpf, true, nullptr);
} // end getOpenMeteoWeather


/* ---- Israel Ministry of Environmental Protection ----------------------- */

/* A small POST: the Ministry's guest-token handshake is two of them. Returns
 * the HTTP status; body and the Set-Cookie header (if any) come back in
 * respBody / setCookie.
 */
static int httpPostText(WiFiClient &client, const String &host,
                        const String &uri,
                        const std::vector<std::pair<String, String>> &headers,
                        const String &body, String &respBody, String &setCookie)
{
  if (WiFi.status() != WL_CONNECTED && rejoinWiFi() != WL_CONNECTED)
  {
    return -512 - static_cast<int>(WiFi.status());
  }
  Serial.println(String(TXT_ATTEMPTING_HTTP_REQ) + ": https://" + host + uri
                 + " (POST)");
  HTTPClient &http = s_http;
  if (client.connected())
  {
    client.stop();
    s_openHost = "";
  }
  http.setConnectTimeout(HTTP_CLIENT_TCP_TIMEOUT);
  http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT);
  http.useHTTP10(true);
  http.begin(client, host, HTTPS_PORT, uri);
  for (const auto &h : headers)
  {
    http.addHeader(h.first, h.second);
  }
  static const char *wanted[] = {"Set-Cookie"};
  http.collectHeaders(wanted, 1);
  const int code = http.POST(body);
  respBody  = (code > 0) ? http.getString() : String();
  setCookie = http.header("Set-Cookie");
  http.end();
  client.stop();
  s_openHost = "";
  Serial.println("  " + String(code, DEC) + " " + getHttpResponsePhrase(code));
  return code;
} // end httpPostText

/* The Ministry's site logs its visitors in as "Guest": a POST for an API
 * token, then a POST that trades it for a JWT, which the data API wants
 * both as an Authorization header and as the X-Access-Token cookie. The
 * JWT is good for an hour; it is kept in NVS and reused for 50 minutes so
 * that most wakes skip the handshake.
 *
 * Returns the HTTP status of the step that failed, or HTTP_CODE_OK.
 */
static int svivaToken(WiFiClient &client, String &jwt)
{
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  const unsigned long now = static_cast<unsigned long>(time(nullptr));
  const unsigned long at = prefs.getULong("svJwtAt", 0);
  jwt = prefs.getString("svJwt", "");
  prefs.end();
  if (!jwt.isEmpty() && at != 0 && now >= at && now - at < 50UL * 60UL)
  {
    return HTTP_CODE_OK;
  }

  // a fresh request-verification token (any UUID will do)
  char uuid[40];
  snprintf(uuid, sizeof(uuid), "%08lx-%04lx-4%03lx-%04lx-%012llx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random() & 0xFFFF),
           static_cast<unsigned long>(esp_random() & 0xFFF),
           static_cast<unsigned long>((esp_random() & 0x3FFF) | 0x8000),
           static_cast<unsigned long long>(esp_random()) << 16
             | (esp_random() & 0xFFFF));
  std::vector<std::pair<String, String>> hdr = {
    {"Accept", "application/json, text/plain, */*"},
    {"Origin", "https://air.sviva.gov.il"},
    {"Referer", "https://air.sviva.gov.il/"},
    {"x-requestverificationtoken", uuid},
    {"Content-Type", "application/json; charset=UTF-8"},
  };
  String body, cookie;
  int code = httpPostText(client, SVIVA_SITE_ENDPOINT, "/Account/GetApiToken",
                          hdr, "{\"userName\":\"Guest\"}", body, cookie);
  if (code != HTTP_CODE_OK)
  {
    return code;
  }
  body.trim();
  body.replace("\"", "");
  if (body.isEmpty())
  {
    return -256 - static_cast<int>(DeserializationError::EmptyInput);
  }
  hdr.push_back({"Authorization", "ApiToken " + body});
  code = httpPostText(client, SVIVA_API_ENDPOINT, "/v1/GenerateToken", hdr,
                      "{}", body, cookie);
  if (code != HTTP_CODE_OK)
  {
    return code;
  }
  // Set-Cookie: X-Access-Token=<jwt>; path=/; ...
  int at0 = cookie.indexOf("X-Access-Token=");
  if (at0 < 0)
  {
    return -256 - static_cast<int>(DeserializationError::InvalidInput);
  }
  at0 += 15;
  int end = cookie.indexOf(';', at0);
  jwt = (end < 0) ? cookie.substring(at0) : cookie.substring(at0, end);
  jwt.trim();
  if (jwt.isEmpty())
  {
    return -256 - static_cast<int>(DeserializationError::InvalidInput);
  }
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("svJwt", jwt);
  prefs.putULong("svJwtAt", now);
  prefs.end();
  Serial.println("[sviva] new guest token");
  return HTTP_CODE_OK;
} // end svivaToken

static std::vector<std::pair<String, String>> svivaHeaders(const String &jwt)
{
  return {
    {"Accept", "application/json, text/plain, */*"},
    {"Authorization", "JwtToken " + jwt},
    {"Cookie", "X-Access-Token=" + jwt},
  };
}

/* The Ministry stations the index is read from: IL_AQ_STATION_ID alone when
 * set, else the active stations within 15 km of LAT/LON (up to four,
 * always at least the nearest) out of the Ministry's station list -- a
 * ~900 KB reply, filtered to ids and coordinates while it streams, fetched
 * once and remembered in NVS against the coordinates.
 *
 * Returns the HTTP Status Code.
 */
static int svivaStations(WiFiClient &client, const String &jwt,
                         std::vector<int> &stationIds)
{
  stationIds.clear();
  if (IL_AQ_STATION_ID > 0)
  {
    stationIds.push_back(IL_AQ_STATION_ID);
    return HTTP_CODE_OK;
  }
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  String cached = (prefs.getString("svLat", "") == LAT
                   && prefs.getString("svLon", "") == LON)
                ? prefs.getString("svSids", "") : String();
  prefs.end();
  if (!cached.isEmpty())
  {
    int from = 0;
    while (from < static_cast<int>(cached.length()))
    {
      int comma = cached.indexOf(',', from);
      if (comma < 0) comma = cached.length();
      const int id = cached.substring(from, comma).toInt();
      if (id > 0) stationIds.push_back(id);
      from = comma + 1;
    }
    if (!stationIds.empty())
    {
      return HTTP_CODE_OK;
    }
  }
  const double lat = LAT.toDouble(), lon = LON.toDouble();
  const auto hdr = svivaHeaders(jwt);
  // the long reply gets a long timeout and a single attempt
  int res = httpGetAndParse(client, SVIVA_API_ENDPOINT, HTTPS_PORT,
      "/v1/envista/regions", "",
      [&](WiFiClient &s) { return deserializeSvivaStations(s, lat, lon, stationIds); },
      "", 60000, 1, false, &hdr);
  if (res == HTTP_CODE_OK && !stationIds.empty())
  {
    String list;
    for (int id : stationIds)
    {
      list += (list.isEmpty() ? "" : ",") + String(id);
    }
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("svLat", LAT);
    prefs.putString("svLon", LON);
    prefs.putString("svSids", list);
    prefs.end();
    Serial.println("[sviva] stations within 15 km: " + list);
  }
  return res;
} // end svivaStations

/* Fetches the Israel Ministry of Environmental Protection's air quality
 * index around LAT/LON: the lowest (worst) current index among the
 * stations svivaStations() chose. `found` is false when none of them has
 * an index in the last four hours.
 *
 * Returns the HTTP Status Code of the request that failed, or HTTP_CODE_OK.
 */
int getIsraelAQI(WiFiClient &client, bool &found, int &index)
{
  found = false;
  String jwt;
  int res = svivaToken(client, jwt);
  if (res != HTTP_CODE_OK)
  {
    return res;
  }
  std::vector<int> stationIds;
  res = svivaStations(client, jwt, stationIds);
  if (res != HTTP_CODE_OK)
  {
    return res;
  }
  const auto hdr = svivaHeaders(jwt);
  res = httpGetAndParse(client, SVIVA_API_ENDPOINT, HTTPS_PORT,
      "/v1/envista/stations/index/latest?hoursBack=4", "",
      [&](WiFiClient &s) { return deserializeSvivaIndex(s, stationIds, found, index); },
      "", 30000, 2, false, &hdr);
  if (res == HTTP_CODE_UNAUTHORIZED)
  {
    // the token was refused: forget it so the next wake logs in afresh
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove("svJwt");
    prefs.end();
  }
  if (res == HTTP_CODE_OK && !found)
  {
    Serial.println("[sviva] no station nearby has an index in the last 4 hours");
  }
  return res;
} // end getIsraelAQI

/* Prints debug information about heap usage.
 */
void printHeapUsage() {
  Serial.println("[debug] Heap Size       : "
                 + String(ESP.getHeapSize()) + " B");
  Serial.println("[debug] Available Heap  : "
                 + String(ESP.getFreeHeap()) + " B");
  Serial.println("[debug] Min Free Heap   : "
                 + String(ESP.getMinFreeHeap()) + " B");
  Serial.println("[debug] Max Allocatable : "
                 + String(ESP.getMaxAllocHeap()) + " B");
  return;
}
