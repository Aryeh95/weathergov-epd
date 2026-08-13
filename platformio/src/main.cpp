/* Main program for esp32-weather-epd.
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

#include "config.h"
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <time.h>
#include <WiFi.h>
#include <Wire.h>

#include "_locale.h"
#include "api_response.h"
#include "client_utils.h"
#include "config.h"
#include "display_utils.h"
#include "icons/icons_196x196.h"
#include "portal.h"
#include "renderer.h"
#include "settings.h"
#include "sun.h"

#if defined(SENSOR_BME280)
  #include <Adafruit_BME280.h>
#endif
#if defined(SENSOR_BME680)
  #include <Adafruit_BME680.h>
#endif
#if defined(USE_HTTPS_WITH_CERT_VERIF) || defined(USE_HTTPS_WITH_CERT_VERIF)
  #include <WiFiClientSecure.h>
#endif
#ifdef USE_HTTPS_WITH_CERT_VERIF
  #include "cert.h"
#endif

// too large to allocate locally on stack
static owm_current_t            current;
static owm_hourly_t             hourly[OWM_NUM_HOURLY];
static owm_daily_t              daily[OWM_NUM_DAILY];
static std::vector<owm_alerts_t> alerts;
static owm_resp_air_pollution_t air_quality;

Preferences prefs;

// Double-reset detector for entering the configuration web portal.
// The marker must live in NVS flash: on the ESP32 the RST button performs a
// power-on-class reset (rst:0x1 POWERON_RESET) that wipes RTC memory, so an
// RTC_DATA_ATTR flag cannot survive a button press. Each wake arms the "drd"
// marker at boot and disarms it just before entering deep sleep, so the
// marker can only still be armed at the next boot if the previous wake was
// cut short -- ie. the user pressed RST twice while the device was awake
// (anytime within the ~40s wake window).
static void disarmDoubleReset()
{
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("drd", false);
  prefs.end();
}

/* Put esp32 into ultra low-power deep sleep (<11μA).
 * Aligns wake time to the minute. Sleep times defined in config.cpp.
 */
void beginDeepSleep(unsigned long startTime, tm *timeInfo)
{
  disarmDoubleReset();
  if (!getLocalTime(timeInfo))
  {
    Serial.println(TXT_REFERENCING_OLDER_TIME_NOTICE);
  }

  // To simplify sleep time calculations, the current time stored by timeInfo
  // will be converted to time relative to the WAKE_TIME. This way if a
  // SLEEP_DURATION is not a multiple of 60 minutes it can be more trivially,
  // aligned and it can easily be deterimined whether we must sleep for
  // additional time due to bedtime.
  // i.e. when curHour == 0, then timeInfo->tm_hour == WAKE_TIME
  int bedtimeHour = INT_MAX;
  if (BED_TIME != WAKE_TIME)
  {
    bedtimeHour = (BED_TIME - WAKE_TIME + 24) % 24;
  }

  // time is relative to wake time
  int curHour = (timeInfo->tm_hour - WAKE_TIME + 24) % 24;
  const int curMinute = curHour * 60 + timeInfo->tm_min;
  const int curSecond = curHour * 3600
                      + timeInfo->tm_min * 60
                      + timeInfo->tm_sec;
  const int desiredSleepSeconds = SLEEP_DURATION * 60;
  const int offsetMinutes = curMinute % SLEEP_DURATION;
  const int offsetSeconds = curSecond % desiredSleepSeconds;

  // align wake time to nearest multiple of SLEEP_DURATION
  int sleepMinutes = SLEEP_DURATION - offsetMinutes;
  if (desiredSleepSeconds - offsetSeconds < 120
   || offsetSeconds / (float)desiredSleepSeconds > 0.95f)
  { // if we have a sleep time less than 2 minutes OR less 5% SLEEP_DURATION,
    // skip to next alignment
    sleepMinutes += SLEEP_DURATION;
  }

  // estimated wake time, if this falls in a sleep period then sleepDuration
  // must be adjusted
  const int predictedWakeHour = ((curMinute + sleepMinutes) / 60) % 24;

  uint64_t sleepDuration;
  if (predictedWakeHour < bedtimeHour)
  {
    sleepDuration = sleepMinutes * 60 - timeInfo->tm_sec;
  }
  else
  {
    const int hoursUntilWake = 24 - curHour;
    sleepDuration = hoursUntilWake * 3600ULL
                    - (timeInfo->tm_min * 60ULL + timeInfo->tm_sec);
  }

  // add extra delay to compensate for esp32's with fast RTCs.
  sleepDuration += 3ULL;
  sleepDuration *= 1.0015f;

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  Serial.print(TXT_AWAKE_FOR);
  Serial.println(" "  + String((millis() - startTime) / 1000.0, 3) + "s");
  Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
  Serial.println(" " + String(sleepDuration) + "s");
  esp_deep_sleep_start();
} // end beginDeepSleep

/* Put esp32 into deep sleep for a fixed number of minutes.
 *
 * Used instead of beginDeepSleep() when WiFi or time sync fails: those paths
 * cannot trust the clock (a fresh boot starts at the 1970 epoch until NTP
 * syncs), and beginDeepSleep()'s bedtime-aligned math computed from a bogus
 * clock can schedule multi-hour sleeps. A plain fixed interval means the
 * device just keeps retrying and recovers on its own once its network is
 * reachable again.
 */
void beginFixedSleep(unsigned long startTime, unsigned long minutes)
{
  disarmDoubleReset();
  esp_sleep_enable_timer_wakeup(minutes * 60ULL * 1000000ULL);
  Serial.print(TXT_AWAKE_FOR);
  Serial.println(" " + String((millis() - startTime) / 1000.0, 3) + "s");
  Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
  Serial.println(" " + String(minutes) + "min");
  esp_deep_sleep_start();
} // end beginFixedSleep

/* Program entry point.
 */
void setup()
{
  unsigned long startTime = millis();
  Serial.begin(115200);

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  disableBuiltinLED();

  // Load WiFi/location/time/battery/widget-layout settings from
  // data/config.json (LittleFS). Falls back to the compiled-in defaults in
  // config.cpp if the file is missing or invalid.
  loadSettings();

  // Open namespace for read/write to non-volatile storage
  prefs.begin(NVS_NAMESPACE, false);

#if BATTERY_MONITORING
  uint32_t batteryVoltage = readBatteryVoltage();
  Serial.print(TXT_BATTERY_VOLTAGE);
  Serial.println(": " + String(batteryVoltage) + "mv");

  // When the battery is low, the display should be updated to reflect that, but
  // only the first time we detect low voltage. The next time the display will
  // refresh is when voltage is no longer low. To keep track of that we will
  // make use of non-volatile storage.
  bool lowBat = prefs.getBool("lowBat", false);

  // low battery, deep sleep now
  if (batteryVoltage <= LOW_BATTERY_VOLTAGE)
  {
    if (lowBat == false)
    { // battery is now low for the first time
      prefs.putBool("lowBat", true);
      prefs.end();
      initDisplay();
      do
      {
        drawError(battery_alert_0deg_196x196, TXT_LOW_BATTERY);
      } while (display.nextPage());
      powerOffDisplay();
    }

    if (batteryVoltage <= CRIT_LOW_BATTERY_VOLTAGE)
    { // critically low battery
      // don't set esp_sleep_enable_timer_wakeup();
      // We won't wake up again until someone manually presses the RST button.
      Serial.println(TXT_CRIT_LOW_BATTERY_VOLTAGE);
      Serial.println(TXT_HIBERNATING_INDEFINITELY_NOTICE);
    }
    else if (batteryVoltage <= VERY_LOW_BATTERY_VOLTAGE)
    { // very low battery
      esp_sleep_enable_timer_wakeup(VERY_LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_VERY_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(VERY_LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    else
    { // low battery
      esp_sleep_enable_timer_wakeup(LOW_BATTERY_SLEEP_INTERVAL
                                    * 60ULL * 1000000ULL);
      Serial.println(TXT_LOW_BATTERY_VOLTAGE);
      Serial.print(TXT_ENTERING_DEEP_SLEEP_FOR);
      Serial.println(" " + String(LOW_BATTERY_SLEEP_INTERVAL) + "min");
    }
    esp_deep_sleep_start();
  }
  // battery is no longer low, reset variable in non-volatile storage
  if (lowBat == true)
  {
    prefs.putBool("lowBat", false);
  }
#else
  uint32_t batteryVoltage = UINT32_MAX;
#endif

  // CONFIGURATION WEB PORTAL
  // Entered when the RST button was pressed twice a few seconds apart (the
  // NVS "drd" marker from the interrupted boot is still armed), or
  // automatically when the device has no WiFi configured (fresh flash).
  // Arming happens after the low-battery check above so a battery-protection
  // wake can neither trigger nor be interrupted into the portal.
  bool portalRequested = prefs.getBool("drd", false);
  size_t drdWritten = prefs.putBool("drd", true); // armed; disarmed once WiFi connect concludes
#if DEBUG_LEVEL >= 0
  Serial.printf("[drd] marker was %d, armed (%u bytes written)\n",
                portalRequested, drdWritten);
#endif
  bool unconfigured = (strcmp(WIFI_SSID, "ssid") == 0);
  if (portalRequested || unconfigured)
  {
    prefs.putBool("drd", false);
    prefs.end();
    if (unconfigured)
    {
      Serial.println("No WiFi configured, starting setup hotspot.");
    }
    else
    {
      Serial.println("Double reset detected, starting configuration portal.");
    }
    runConfigPortal(unconfigured); // never returns
  }

  // All data should have been loaded from NVS. Close filesystem.
  prefs.end();

  String statusStr = {};
  String tmpStr = {};
  tm timeInfo = {};

  // START WIFI
  int wifiRSSI = 0; // “Received Signal Strength Indicator"
  wl_status_t wifiStatus = startWiFi(wifiRSSI);
  if (wifiStatus != WL_CONNECTED)
  { // WiFi Connection Failed
    // Ask for the failure detail (possibly incorrect password, network not
    // found, no response...) before killWiFi(), which could clear the
    // stored disconnect reason.
    String wifiDetail = getWifiFailureDetail(wifiStatus);
    killWiFi();

    const char *errTitle;
    String errLine2;
    if (wifiStatus == WL_NO_SSID_AVAIL)
    { // the scan found no network with this name; show the SSID being sought
      // so a typo in config.json is immediately visible. (Also reported for
      // 5GHz-only networks -- the ESP32 only supports 2.4GHz.)
      errTitle = TXT_NETWORK_NOT_AVAILABLE;
      errLine2 = "'" + String(WIFI_SSID) + "'";
    }
    else
    {
      errTitle = TXT_WIFI_CONNECTION_FAILED;
      errLine2 = wifiDetail;
    }
    Serial.println(String(errTitle) + " - " + errLine2);

    // The device retries every WIFI_RETRY_INTERVAL minutes until it
    // reconnects, which may mean many failed attempts in a row while out of
    // range. The e-paper refresh is by far the most expensive part of a
    // wake, so only redraw the error screen when it isn't already showing
    // this exact error (tracked in non-volatile storage, same pattern as the
    // low-battery screen).
    String errDescriptor = String(errTitle) + "|" + errLine2;
    prefs.begin(NVS_NAMESPACE, false);
    // isKey check avoids Preferences logging a spurious NOT_FOUND error on
    // the first failure, before the marker exists
    bool alreadyShown = prefs.isKey("wifiErr")
                     && (prefs.getString("wifiErr", "") == errDescriptor);
    if (!alreadyShown)
    {
      prefs.putString("wifiErr", errDescriptor);
    }
    prefs.end();
    if (!alreadyShown)
    {
      initDisplay();
      do
      {
        drawError(wifi_x_196x196, errTitle, errLine2);
      } while (display.nextPage());
      powerOffDisplay();
    }

    beginFixedSleep(startTime, WIFI_RETRY_INTERVAL);
  }

  // WiFi is connected: clear the WiFi error marker -- whatever is drawn
  // from here on (weather data or a different error screen) replaces the
  // WiFi error screen, so a future WiFi failure must be drawn again.
  prefs.begin(NVS_NAMESPACE, false);
  if (prefs.isKey("wifiErr"))
  {
    prefs.remove("wifiErr");
  }
  prefs.end();

  // TIME SYNCHRONIZATION
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
  bool timeConfigured = waitForSNTPSync(&timeInfo);
  if (!timeConfigured)
  {
    Serial.println(TXT_TIME_SYNCHRONIZATION_FAILED);
    killWiFi();
    initDisplay();
    do
    {
      drawError(wi_time_4_196x196, TXT_TIME_SYNCHRONIZATION_FAILED);
    } while (display.nextPage());
    powerOffDisplay();
    // the clock is unreliable if sync failed, so use a fixed-interval sleep
    // rather than beginDeepSleep()'s clock-based calculation
    beginFixedSleep(startTime, WIFI_RETRY_INTERVAL);
  }

  // MAKE API REQUESTS
#ifdef USE_HTTP
  WiFiClient client;
#elif defined(USE_HTTPS_NO_CERT_VERIF)
  WiFiClientSecure client;
  client.setInsecure();
#elif defined(USE_HTTPS_WITH_CERT_VERIF)
  WiFiClientSecure client;
  client.setCACert(cert_root_ca_bundle);
#endif

  // weather.gov forecast + current conditions. This is the primary data
  // source; if it fails there is nothing worth displaying.
  String failedStep;
  int rxStatus = getNWSWeather(client, current, hourly, daily, failedStep);
  if (rxStatus != HTTP_CODE_OK)
  {
    killWiFi();
    statusStr = "weather.gov API (" + failedStep + ")";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
    initDisplay();
    do
    {
      drawError(wi_cloud_down_196x196, statusStr, tmpStr);
    } while (display.nextPage());
    powerOffDisplay();
    beginDeepSleep(startTime, &timeInfo);
  }

#if DISPLAY_ALERTS
  // Alerts are non-fatal: if this fails, the display simply shows no
  // alerts.
  rxStatus = getNWSAlerts(client, alerts);
  if (rxStatus != HTTP_CODE_OK)
  {
    statusStr = "weather.gov Alerts API";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
  }
#endif

  // UV index and air quality (weather.gov does not provide either). Also
  // non-fatal: if this fails, the UVI/Air Quality widgets show "0".
  float uvi = 0.f;
  rxStatus = getAirQuality(client, air_quality, uvi);
  if (rxStatus != HTTP_CODE_OK)
  {
    statusStr = "Open-Meteo Air Quality API";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
  }
  current.uvi = uvi;

  // Official US EPA AQI from AirNow, when an API key is configured.
  // Non-fatal: on failure (or with no key, or no monitor within range)
  // us_aqi stays -1 and the Air Quality widget falls back to the AQI
  // computed from Open-Meteo's pollutant concentrations above. Because that
  // fallback is seamless, an AirNow failure is logged to serial only -- the
  // status-bar warning is reserved for conditions that actually degrade
  // what's displayed (an Open-Meteo failure, which also loses the UV
  // index, still warns above).
  air_quality.us_aqi = -1;
  if (!AIRNOW_APIKEY.isEmpty())
  {
    rxStatus = getAirNowAQI(client, air_quality.us_aqi);
    if (rxStatus != HTTP_CODE_OK)
    {
      Serial.println("AirNow API " + String(rxStatus, DEC) + ": "
                     + getHttpResponsePhrase(rxStatus)
                     + " - using Open-Meteo AQI instead");
    }
  }

  // SUNRISE/SUNSET
  // weather.gov does not report these, so they are computed locally from the
  // location and today's local date. Must happen after the API calls, which
  // zero-initialize `current`.
  if (!calcSunriseSunset(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1,
                         timeInfo.tm_mday, LAT.toDouble(), LON.toDouble(),
                         current.sunrise, current.sunset))
  { // polar day or polar night, the sun does not cross the horizon today
    current.sunrise = 0;
    current.sunset = 0;
    Serial.println("Sun does not rise/set today at this latitude.");
  }

  killWiFi(); // WiFi no longer needed

  // GET INDOOR TEMPERATURE AND HUMIDITY, start BMEx80...
  pinMode(PIN_BME_PWR, OUTPUT);
  digitalWrite(PIN_BME_PWR, HIGH);
#if defined(SENSOR_INIT_DELAY_MS) && SENSOR_INIT_DELAY_MS > 0
  delay(SENSOR_INIT_DELAY_MS);
#endif
  TwoWire I2C_bme = TwoWire(0);
  I2C_bme.begin(PIN_BME_SDA, PIN_BME_SCL, 100000); // 100kHz
  float inTemp     = NAN;
  float inHumidity = NAN;
#if defined(SENSOR_BME280)
  Serial.print(String(TXT_READING_FROM) + " BME280... ");
  Adafruit_BME280 bme;

  if(bme.begin(BME_ADDRESS, &I2C_bme))
  {
#endif
#if defined(SENSOR_BME680)
  Serial.print(String(TXT_READING_FROM) + " BME680... ");
  Adafruit_BME680 bme(&I2C_bme);

  if(bme.begin(BME_ADDRESS))
  {
#endif
    inTemp     = bme.readTemperature(); // Celsius
    inHumidity = bme.readHumidity();    // %

    // check if BME readings are valid
    // note: readings are checked again before drawing to screen. If a reading
    //       is not a number (NAN) then an error occurred, a dash '-' will be
    //       displayed.
    if (std::isnan(inTemp) || std::isnan(inHumidity))
    {
      statusStr = "BME " + String(TXT_READ_FAILED);
      Serial.println(statusStr);
    }
    else
    {
      Serial.println(TXT_SUCCESS);
    }
  }
  else
  {
    statusStr = "BME " + String(TXT_NOT_FOUND); // check wiring
    Serial.println(statusStr);
  }
  digitalWrite(PIN_BME_PWR, LOW);

  String refreshTimeStr;
  getRefreshTimeStr(refreshTimeStr, timeConfigured, &timeInfo);
  String dateStr;
  getDateStr(dateStr, &timeInfo);

  // RENDER FULL REFRESH
  initDisplay();
  do
  {
    drawCurrentConditions(current, daily[0], air_quality, inTemp, inHumidity);
    drawOutlookGraph(hourly, daily, timeInfo);
    drawForecast(daily, timeInfo);
    drawLocationDate(CITY_STRING, dateStr);
#if DISPLAY_ALERTS
    drawAlerts(alerts, CITY_STRING, dateStr);
#endif
    drawStatusBar(statusStr, refreshTimeStr, wifiRSSI, batteryVoltage);
  } while (display.nextPage());
  powerOffDisplay();

  // DEEP SLEEP
  beginDeepSleep(startTime, &timeInfo);
} // end setup

/* This will never run
 */
void loop()
{
} // end loop

