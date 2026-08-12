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

// arduino/esp32 libraries
#include <Arduino.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
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
#include "display_utils.h"
#include "renderer.h"
#ifndef USE_HTTP
  #include <WiFiClientSecure.h>
#endif

static const uint16_t HTTPS_PORT = 443;
static const char *NWS_ENDPOINT = "api.weather.gov";
static const char *AIR_QUALITY_ENDPOINT = "air-quality-api.open-meteo.com";

// Reason code from the most recent WiFi disconnect event, captured so a
// failed connection can be explained on the error screen. 0 means no
// disconnect event was ever received (ie. the access point never responded).
static uint8_t wifiDisconnectReason = 0;

static void onWiFiStaDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  wifiDisconnectReason = info.wifi_sta_disconnected.reason;
} // end onWiFiStaDisconnected

/* Power-on and connect WiFi.
 * Takes int parameter to store WiFi RSSI, or “Received Signal Strength
 * Indicator"
 *
 * Returns WiFi status.
 */
wl_status_t startWiFi(int &wifiRSSI)
{
  WiFi.mode(WIFI_STA);
  wifiDisconnectReason = 0;
  WiFi.onEvent(onWiFiStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  Serial.printf("%s '%s'", TXT_CONNECTING_TO, WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // timeout if WiFi does not connect in WIFI_TIMEOUT ms from now
  unsigned long timeout = millis() + WIFI_TIMEOUT;
  wl_status_t connection_status = WiFi.status();

  // WIFI_TIMEOUT is a worst-case allowance for networks that are present but
  // slow to associate. When the failure is definitive (SSID not found in a
  // scan, or the AP rejected us), waiting out the rest of the timeout just
  // burns battery -- especially now that failed attempts repeat every
  // WIFI_RETRY_INTERVAL minutes. Give up once a definitive failure state has
  // persisted for 10s, which still allows a few scan/auto-reconnect cycles
  // in case the network was only momentarily invisible.
  unsigned long failedSince = 0;
  while ((connection_status != WL_CONNECTED) && (millis() < timeout))
  {
    if (connection_status == WL_NO_SSID_AVAIL
     || connection_status == WL_CONNECT_FAILED)
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
    connection_status = WiFi.status();
  }
  Serial.println();

  if (connection_status == WL_CONNECTED)
  {
    wifiRSSI = WiFi.RSSI(); // get WiFi signal strength now, because the WiFi
                            // will be turned off to save power!
    Serial.println("IP: " + WiFi.localIP().toString());
  }
  else
  {
    Serial.printf("%s '%s' (%s)\n", TXT_COULD_NOT_CONNECT_TO, WIFI_SSID,
                  getWifiFailureDetail(connection_status).c_str());
  }
  return connection_status;
} // startWiFi

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
  WiFi.disconnect();
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

/* Performs an HTTP GET request, retrying up to 3 times, and hands the
 * response stream to parseFn to be deserialized on success.
 *
 * Returns the HTTP Status Code (or a negative offset error code, see
 * getHttpResponsePhrase for the meaning of negative codes).
 */
static int httpGetAndParse(WiFiClient &client, const String &host,
                           uint16_t port, const String &uri,
                           const String &userAgent,
                           std::function<DeserializationError(WiFiClient&)> parseFn)
{
  int attempts = 0;
  bool rxSuccess = false;
  DeserializationError jsonErr = {};

  String sanitizedUri = "https://" + host + uri;
  Serial.print(TXT_ATTEMPTING_HTTP_REQ);
  Serial.println(": " + sanitizedUri);

  int httpResponse = 0;
  while (!rxSuccess && attempts < 3)
  {
    wl_status_t connection_status = WiFi.status();
    if (connection_status != WL_CONNECTED)
    {
      // -512 offset distinguishes these errors from httpClient errors
      return -512 - static_cast<int>(connection_status);
    }

    HTTPClient http;
    http.setConnectTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    http.setTimeout(HTTP_CLIENT_TCP_TIMEOUT); // default 5000ms
    // Force HTTP/1.0. Both weather.gov and Open-Meteo reply with
    // "Transfer-Encoding: chunked" over HTTP/1.1 for any non-trivial
    // response, but HTTPClient::getStream() hands back the raw socket
    // WITHOUT stripping the chunk framing. ArduinoJson then sees the leading
    // hex chunk length instead of '{' -- either failing outright
    // (InvalidInput) or, when the chunk length happens to be all decimal
    // digits, silently parsing it as a bare number and yielding an empty
    // document. HTTP/1.0 has no chunked encoding, so the body arrives clean.
    http.useHTTP10(true);
    if (!userAgent.isEmpty())
    {
      http.setUserAgent(userAgent);
    }
    http.begin(client, host, port, uri);
    httpResponse = http.GET();
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
    client.stop();
    http.end();
    Serial.println("  " + String(httpResponse, DEC) + " "
                   + getHttpResponsePhrase(httpResponse));
    ++attempts;
  }

  return httpResponse;
} // end httpGetAndParse

/* Fetches weather.gov's gridpoint forecast (12-hour periods and hourly), and
 * current conditions from the nearest observation station.
 *
 * Returns the HTTP Status Code of the request that failed, or HTTP_CODE_OK.
 */
int getNWSWeather(WiFiClient &client, owm_current_t &current,
                  owm_hourly_t *hourly, owm_daily_t *daily,
                  String &failedStep)
{
  String forecastUrl, forecastHourlyUrl, stationsUrl;
  int httpResponse = httpGetAndParse(client, NWS_ENDPOINT, HTTPS_PORT,
      "/points/" + LAT + "," + LON, NWS_USER_AGENT,
      [&](WiFiClient &s) {
        return deserializeNWSPoints(s, forecastUrl, forecastHourlyUrl,
                                    stationsUrl);
      });
  if (httpResponse != HTTP_CODE_OK)
  {
    failedStep = "points";
    return httpResponse;
  }

  // fetch the hourly forecast first; current conditions fall back to
  // hourly[0] for any field an automated observation station doesn't report.
  String host, uri;
  splitUrl(forecastHourlyUrl, host, uri);
  httpResponse = httpGetAndParse(client, host, HTTPS_PORT, uri, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSForecastHourly(s, hourly); });
  if (httpResponse != HTTP_CODE_OK)
  {
    failedStep = "hourly forecast";
    return httpResponse;
  }

  splitUrl(forecastUrl, host, uri);
  httpResponse = httpGetAndParse(client, host, HTTPS_PORT, uri, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSForecastDaily(s, daily); });
  if (httpResponse != HTTP_CODE_OK)
  {
    failedStep = "daily forecast";
    return httpResponse;
  }

  // current conditions: look up the nearest observation station, then fetch
  // its latest observation. Neither failure is fatal -- current conditions
  // simply fall back to the hourly forecast.
  String stationId;
  splitUrl(stationsUrl, host, uri);
  int stationsResponse = httpGetAndParse(client, host, HTTPS_PORT, uri,
      NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSStations(s, stationId); });

  if (stationsResponse == HTTP_CODE_OK && !stationId.isEmpty())
  {
    httpGetAndParse(client, NWS_ENDPOINT, HTTPS_PORT,
        "/stations/" + stationId + "/observations/latest", NWS_USER_AGENT,
        [&](WiFiClient &s) {
          return deserializeNWSObservation(s, hourly[0], current);
        });
  }
  else
  {
    fillCurrentFromFallback(hourly[0], current);
  }

  return HTTP_CODE_OK;
} // end getNWSWeather

/* Fetches active weather alerts for LAT/LON from weather.gov.
 *
 * Returns the HTTP Status Code.
 */
int getNWSAlerts(WiFiClient &client, std::vector<owm_alerts_t> &alerts)
{
  return httpGetAndParse(client, NWS_ENDPOINT, HTTPS_PORT,
      "/alerts/active?point=" + LAT + "," + LON, NWS_USER_AGENT,
      [&](WiFiClient &s) { return deserializeNWSAlerts(s, alerts); });
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
