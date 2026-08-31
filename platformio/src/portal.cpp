/* Configuration web portal for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
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

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <driver/rtc_io.h>
#include <WebServer.h>
#include <WiFi.h>

#include "client_utils.h"
#include "config.h"
#include "portal.h"
#include "renderer.h"

// icon header files
#include "icons/icons_196x196.h"

// Note: on-screen and web text in this file is English-only, matching the
// (English) web page itself, and is deliberately not routed through the
// locale system.

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
static unsigned long lastActivity = 0;
static unsigned long restartAt = 0; // 0 = no restart scheduled

static const char *AP_SSID = "WeatherEPD-Setup";

// Scan results cached right before the hotspot starts: scanning while the
// soft-AP is up (AP_STA) is unreliable on some chips (observed on the
// ESP32-S3: live scans return zero networks with a client connected), so
// the page falls back to this snapshot when a live scan comes up empty.
struct scan_entry_t { String ssid; int32_t rssi; bool open; };
static std::vector<scan_entry_t> scanCache;

static void cacheScanResults()
{
  int found = WiFi.scanNetworks();
  for (int i = 0; i < found; ++i)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty())
    {
      continue;
    }
    bool seen = false;
    for (const scan_entry_t &e : scanCache)
    {
      if (e.ssid == ssid)
      {
        seen = true;
        break;
      }
    }
    if (!seen)
    {
      scanCache.push_back({ssid, WiFi.RSSI(i),
                           WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    }
  }
  WiFi.scanDelete();
  Serial.printf("[portal] cached %u networks from pre-hotspot scan\n",
                scanCache.size());
}
static const IPAddress AP_IP(192, 168, 4, 1);

/* Serves the portal single-page UI from LittleFS.
 */
static void handleRoot()
{
  lastActivity = millis();
  File f = LittleFS.open("/portal.html", "r");
  if (!f)
  {
    server.send(500, "text/plain",
                "portal.html missing from LittleFS. Run "
                "`pio run --target uploadfs` to upload the data/ folder.");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
} // end handleRoot

/* Serves the current /config.json (raw, comments included).
 */
static void handleGetConfig()
{
  lastActivity = millis();
  File f = LittleFS.open("/config.json", "r");
  if (!f)
  {
    server.send(404, "text/plain", "config.json not found");
    return;
  }
  server.streamFile(f, "application/json");
  f.close();
} // end handleGetConfig

/* Validates and saves a new /config.json, then schedules a restart so the
 * device boots into its normal cycle with the new settings.
 */
static void handlePostConfig()
{
  lastActivity = millis();
  String body = server.arg("plain");

  // validate before touching flash. Comments are allowed (the firmware is
  // built with ARDUINOJSON_ENABLE_COMMENTS), trailing commas are not.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    server.send(400, "application/json",
                String("{\"ok\":false,\"error\":\"Invalid JSON: ")
                + err.c_str() + "\"}");
    return;
  }
  if (doc["wifi"]["ssid"].isNull()
   || String(doc["wifi"]["ssid"].as<const char *>()).isEmpty())
  {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"wifi.ssid is required\"}");
    return;
  }

  // keep one backup generation in case the new config turns out to be bad
  if (LittleFS.exists("/config.bak"))
  {
    LittleFS.remove("/config.bak");
  }
  LittleFS.rename("/config.json", "/config.bak");
  File f = LittleFS.open("/config.json", "w");
  if (!f)
  {
    LittleFS.rename("/config.bak", "/config.json");
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":\"Failed to write config.json\"}");
    return;
  }
  f.print(body);
  f.close();

  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Saved. Restarting...\"}");
  restartAt = millis() + 1500; // give the response time to flush
} // end handlePostConfig

/* Reports how the portal is currently reachable; the web page shows this in
 * a banner at the top.
 */
static void handleGetInfo()
{
  lastActivity = millis();
  JsonDocument doc;
  if (apMode)
  {
    doc["mode"]        = "hotspot";
    doc["ssid"]        = AP_SSID;
    doc["ap_password"] = PORTAL_AP_PASSWORD;
    doc["ip"]          = AP_IP.toString();
  }
  else
  {
    doc["mode"] = "wifi";
    doc["ssid"] = WIFI_SSID;
    doc["ip"]   = WiFi.localIP().toString();
    doc["mdns"] = "weatherepd.local";
  }
  doc["timeout_minutes"] = PORTAL_TIMEOUT;
  doc["build"] = __DATE__ " " __TIME__;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
} // end handleGetInfo

/* Reports nearby WiFi networks so the page can offer a picker instead of a
 * typed SSID. The scan runs asynchronously: the first call starts it and
 * returns {"status":"scanning"}; the page polls until the results arrive.
 * Networks are already sorted strongest-first by the scanner; duplicate
 * SSIDs (multiple access points of one network) are collapsed.
 */
static void handleScan()
{
  lastActivity = millis();
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED)
  {
    WiFi.scanNetworks(true /* async */);
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING)
  {
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  JsonDocument doc;
  doc["status"] = "done";
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; ++i)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty())
    {
      continue;
    }
    bool seen = false;
    for (JsonObject o : arr)
    {
      if (ssid == o["ssid"].as<const char *>())
      {
        seen = true;
        break;
      }
    }
    if (seen)
    {
      continue;
    }
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
  if (arr.size() == 0)
  { // live scan found nothing (unreliable in AP mode); serve the snapshot
    for (const scan_entry_t &e : scanCache)
    {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = e.ssid;
      o["rssi"] = e.rssi;
      o["open"] = e.open;
    }
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
} // end handleScan

// FIRMWARE UPDATE (OTA)
// The page uploads a firmware.bin built for this board; Update writes it to
// the inactive app slot and only switches the boot partition when the image
// completes and verifies, so a failed or interrupted upload leaves the
// running firmware untouched.
static String otaError;

static void handleUpdateUpload()
{
  lastActivity = millis();
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START)
  {
    otaError = "";
    Serial.println("[portal] OTA upload started: " + up.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
    {
      otaError = Update.errorString();
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE && otaError.isEmpty())
  {
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
    {
      otaError = Update.errorString();
    }
  }
  else if (up.status == UPLOAD_FILE_END && otaError.isEmpty())
  {
    if (Update.end(true))
    {
      Serial.printf("[portal] OTA complete: %u bytes\n", up.totalSize);
    }
    else
    {
      otaError = Update.errorString();
    }
  }
  else if (up.status == UPLOAD_FILE_ABORTED)
  {
    Update.abort();
    otaError = "upload aborted";
  }
} // end handleUpdateUpload

static void handleUpdateFinish()
{
  lastActivity = millis();
  if (otaError.length())
  {
    Serial.println("[portal] OTA failed: " + otaError);
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":\"" + otaError + "\"}");
    return;
  }
  server.send(200, "application/json",
              "{\"ok\":true,\"message\":\"Firmware updated. Restarting...\"}");
  restartAt = millis() + 1500;
} // end handleUpdateFinish

/* In hotspot mode every unknown URL redirects to the portal, which is what
 * makes phone/laptop captive-portal detection pop the page up automatically.
 */
static void handleNotFound()
{
  if (apMode)
  {
    server.sendHeader("Location",
                      "http://" + AP_IP.toString() + "/", true);
    server.send(302, "text/plain", "");
  }
  else
  {
    server.send(404, "text/plain", "Not found");
  }
} // end handleNotFound

/* Draws how to reach the portal on the e-paper display.
 */
static void drawPortalScreen(const String &line1, const String &line2,
                             const String &line3)
{
  initDisplay();
  do
  {
    fillDisplayBackground();
    drawConfigPortalScreen(line1, line2, line3);
  } while (display.nextPage());
  powerOffDisplay();
} // end drawPortalScreen

void runConfigPortal(bool forceAp)
{
  Serial.println("[portal] starting configuration portal");

  bool staConnected = false;
  if (!forceAp)
  {
    int wifiRSSI = 0;
    staConnected = (startWiFi(wifiRSSI) == WL_CONNECTED);
  }

  String urlStr;
  if (staConnected)
  {
    urlStr = "http://" + WiFi.localIP().toString() + "/";
    if (MDNS.begin("weatherepd"))
    {
      MDNS.addService("http", "tcp", 80);
    }
    Serial.println("[portal] on WiFi '" + String(WIFI_SSID) + "' at " + urlStr
                   + " (or http://weatherepd.local/)");
    drawPortalScreen("Connected to WiFi: " + String(WIFI_SSID),
                     "Open " + urlStr,
                     "or http://weatherepd.local/");
  }
  else
  {
    apMode = true;
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    cacheScanResults(); // AP-mode scans are unreliable; snapshot first
    // AP+STA so the STA half can run WiFi scans for the network picker while
    // the hotspot serves the page.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, PORTAL_AP_PASSWORD.c_str());
    dnsServer.start(53, "*", AP_IP);
    urlStr = "http://" + AP_IP.toString() + "/";
    Serial.println("[portal] hotspot '" + String(AP_SSID) + "' (password: "
                   + PORTAL_AP_PASSWORD + ") at " + urlStr);
    drawPortalScreen("Join WiFi network: " + String(AP_SSID),
                     "Password: " + PORTAL_AP_PASSWORD,
                     "Then open " + urlStr);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/config", HTTP_POST, handlePostConfig);
  server.on("/info", HTTP_GET, handleGetInfo);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  lastActivity = millis();

  const unsigned long timeoutMs = PORTAL_TIMEOUT * 60UL * 1000UL;
  while (true)
  {
    if (apMode)
    {
      dnsServer.processNextRequest();
    }
    server.handleClient();

    if (restartAt != 0 && millis() >= restartAt)
    {
      Serial.println("[portal] configuration saved, restarting");
      esp_restart();
    }
    if (millis() - lastActivity >= timeoutMs)
    {
      if (forceAp)
      {
        // Unconfigured device: restarting would just start another hotspot
        // cycle forever (draining a battery in about a day). Show how to
        // resume setup, then hibernate until the reset button (or, on
        // boards that have one, the portal button) wakes it.
        Serial.println("[portal] setup inactive for " + String(PORTAL_TIMEOUT)
                       + "min, hibernating until reset");
        drawPortalScreen("Setup paused to save power.",
                         "Press the reset (RST) button",
                         "to start the setup hotspot again.");
        WiFi.mode(WIFI_OFF);
#if SOC_PM_SUPPORT_EXT_WAKEUP
        if (PIN_BTN_PORTAL != PIN_UNUSED)
        {
          rtc_gpio_pullup_en(static_cast<gpio_num_t>(PIN_BTN_PORTAL));
          rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BTN_PORTAL));
          esp_sleep_enable_ext0_wakeup(
              static_cast<gpio_num_t>(PIN_BTN_PORTAL), 0);
        }
#endif
        esp_deep_sleep_start(); // no timer: sleeps until RST/button
      }
      Serial.println("[portal] inactive for " + String(PORTAL_TIMEOUT)
                     + "min, restarting into normal cycle");
      esp_restart();
    }
    delay(2);
  }
} // end runConfigPortal
