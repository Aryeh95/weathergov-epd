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
#include <esp_sntp.h>
#include <WiFi.h>
#include <Wire.h>

#include "_locale.h"
#include "api_response.h"
#include "client_utils.h"
#include "config.h"
#include "diag.h"
#include "display_utils.h"
#include "icons/icons_196x196.h"
#include "portal.h"
#include "renderer.h"
#include "build_rev.h"
#include "history.h"
#include "settings.h"
#include "snapshot.h"
#include "sun.h"

#if defined(SENSOR_BME280)
  #include <Adafruit_BME280.h>
#endif
#if defined(SENSOR_BME680)
  #include <Adafruit_BME680.h>
#endif
#if defined(SENSOR_SHT4X)
  #include <Adafruit_SHT4x.h>
#endif
#if defined(USE_HTTPS_NO_CERT_VERIF) || defined(USE_HTTPS_WITH_CERT_VERIF)
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

// WAKE BUTTONS (boards that have them, e.g. reTerminal E1002)
// Armed before every deep sleep: the PORTAL button (EXT0) wakes straight
// into the configuration portal, the REFRESH button (EXT1) wakes into an
// immediate weather refresh. Both are active low, so internal RTC pullups
// are enabled for the sleep period.
#if defined(PIN_UNUSED) && SOC_PM_SUPPORT_EXT_WAKEUP
#include <driver/rtc_io.h>
static void enableButtonWake()
{
  if (PIN_BTN_PORTAL != PIN_UNUSED)
  {
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(PIN_BTN_PORTAL));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BTN_PORTAL));
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_BTN_PORTAL), 0);
  }
  if (PIN_BTN_REFRESH != PIN_UNUSED)
  {
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(PIN_BTN_REFRESH));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BTN_REFRESH));
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN_REFRESH,
                                 ESP_EXT1_WAKEUP_ALL_LOW);
  }
}
#else
// No EXT0/EXT1 wake on this SoC (ESP32-C3) or no wake buttons wired.
static void enableButtonWake() {}
#endif

/* A wake that could not fetch the weather. True if the screen should be
 * left as it is: the weather is on it, and it has been there for less than
 * OUTAGE_GRACE minutes of failed attempts.
 *
 * Only a wake by the timer holds the screen. Someone who pressed a button
 * or reset the device is standing in front of it waiting for something to
 * happen, and is owed the reason that nothing did.
 */
static bool holdLastScreen()
{
#if defined(SIMULATE_OUTAGE)
  const bool byTimer = true;
#else
  const bool byTimer =
    (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);
#endif
  Preferences store;
  store.begin(NVS_NAMESPACE, false);
  const bool good = store.getBool("goodScreen", false);
  const int age = store.getInt("outageMin", 0);
  const bool hold = good && byTimer && age < OUTAGE_GRACE;
  if (good && !hold)
  { // an error screen is about to replace the weather
    store.putBool("goodScreen", false);
  }
  store.end();
  if (hold)
  {
    Serial.println("[outage] " + String(age) + " min without an update; "
                   "leaving the last weather on the screen (up to "
                   + String(OUTAGE_GRACE) + " min)");
  }
  return hold;
} // end holdLastScreen

/* The weather page. `stale` marks the time in the status bar as old. */
static void drawWeatherPage(const tm &timeInfo, const String &dateStr,
                            const String &refreshTimeStr,
                            const String &statusStr, int rssi,
                            uint32_t batteryVoltage,
                            const pollen_info_t &pollen, float inTemp,
                            float inHumidity, bool stale)
{
#ifdef DISP_7C_709
  layout709Begin(alerts, hourly);
#endif
  initDisplay();
  do
  {
    fillDisplayBackground();
    drawCurrentConditions(current, daily[0], air_quality, pollen,
                          inTemp, inHumidity);
    drawOutlookGraph(hourly, daily, timeInfo);
    drawForecast(daily, timeInfo);
    drawLocationDate(CITY_STRING, dateStr);
#if DISPLAY_ALERTS
    drawAlerts(alerts, CITY_STRING, dateStr);
#endif
    drawStatusBar(statusStr, refreshTimeStr, rssi, batteryVoltage,
                  historyBatteryDaysLeft(), stale);
  } while (display.nextPage());
  powerOffDisplay();
} // end drawWeatherPage

/* The first failed wake of an outage: the last weather is drawn once more
 * with its time marked as old -- in red, or with a warning sign where there
 * is no red -- so that the screen itself says the forecast is not current.
 * The later wakes of the same outage leave the screen alone. `reason` goes
 * in the status bar. True if the page was drawn.
 */
static bool markStale(const String &reason, int rssi, uint32_t batteryVoltage)
{
  Preferences store;
  store.begin(NVS_NAMESPACE, false);
  const bool marked = store.getBool("staleShown", false);
  if (!marked)
  { // before drawing: a wake cut short must not try again every time
    store.putBool("staleShown", true);
  }
  store.end();
  if (marked)
  {
    return false;
  }

  snapshot_meta_t meta;
  if (!snapshotLoad(current, hourly, daily, alerts, air_quality, meta))
  {
    Serial.println("[outage] no saved weather to draw again");
    return false;
  }
  // the page as it was: its own time, its own graph length
  setenv("TZ", TIMEZONE, 1);
  tzset();
  const time_t when = static_cast<time_t>(meta.when);
  tm timeInfo = {};
  localtime_r(&when, &timeInfo);
  setSunTimes(current.sunrise, current.sunset);
  setPageTime(when);
  HOURLY_GRAPH_MAX = std::min(HOURLY_GRAPH_MAX,
                              std::max(8, static_cast<int>(meta.graphHours)));
  // The trends and the battery estimate come from the history. Nothing
  // new is known about the weather, so only the battery gets a sample --
  // and nothing at all if the clock is not set, as a record dated 1970
  // would throw the others out.
  if (time(nullptr) > 1600000000L)
  {
    prefs.begin(NVS_NAMESPACE, false);
#if BATTERY_MONITORING
    historyUpdate(time(nullptr), NAN, NAN, batteryVoltage, prefs);
#else
    historyUpdate(time(nullptr), NAN, NAN, 0, prefs);
#endif
    prefs.end();
  }

  Serial.println("[outage] drawing the weather of " + meta.refreshTime
                 + " again, marked as old");
  const unsigned long drawStart = millis();
  drawWeatherPage(timeInfo, meta.date, meta.refreshTime, reason, rssi,
                  batteryVoltage, meta.pollen, meta.inTemp, meta.inHumidity,
                  true);
  wakeDiag.drawMs = millis() - drawStart;
  return true;
} // end markStale

/* On the way to sleep: if this wake did not get the weather, the outage is
 * older by the wake and by the sleep that follows it. Counted this way
 * rather than read off the clock because one of the failures is not having
 * a clock.
 */
static void countOutage(unsigned long startTime, uint64_t sleepSeconds)
{
  if (wakeDiag.outcome != DIAG_HELD && wakeDiag.outcome != DIAG_STALE
   && wakeDiag.outcome != DIAG_WIFI
   && wakeDiag.outcome != DIAG_CLOCK && wakeDiag.outcome != DIAG_WEATHER)
  {
    return;
  }
  Preferences store;
  store.begin(NVS_NAMESPACE, false);
  const int age = store.getInt("outageMin", 0);
  if (age < 100000)
  {
    const uint64_t seconds = sleepSeconds + (millis() - startTime) / 1000;
    store.putInt("outageMin", age + static_cast<int>((seconds + 30) / 60));
  }
  store.end();
} // end countOutage

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

  countOutage(startTime, sleepDuration);
  diagSave(startTime, static_cast<uint32_t>(sleepDuration));
  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  enableButtonWake();
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
  countOutage(startTime, minutes * 60ULL);
  diagSave(startTime, static_cast<uint32_t>(minutes * 60UL));
  esp_sleep_enable_timer_wakeup(minutes * 60ULL * 1000000ULL);
  enableButtonWake();
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
#if defined(BOARD_XIAO_EE02)
  // Serial is the USB port here (UART0's pins drive the panel). Without a
  // computer listening, every print would wait out its timeout; on battery
  // that is most of the time, so do not wait at all.
  Serial.setTxTimeoutMs(0);
#endif
  Serial.println("[build] " GIT_REV " (" __DATE__ " " __TIME__ ")");
#if defined(USE_HTTPS_NO_CERT_VERIF)
  Serial.println("[tls] certificates are NOT verified (config_local.h)");
#endif

#if DEBUG_LEVEL >= 1
  printHeapUsage();
#endif

  wakeDiag.outcome = DIAG_OTHER;
  wakeDiag.cause = static_cast<uint8_t>(esp_sleep_get_wakeup_cause());

#if defined(DISP_7C_709) && defined(EPD709_TEST_CARD)
  // Bring-up build: no WiFi, no settings, just the test card, then sleep
  // until the reset button is pressed.
  initDisplay();
  do
  {
    drawTestCard709();
  } while (display.nextPage());
  powerOffDisplay();
  Serial.println("[epd709] test card drawn, sleeping");
  esp_deep_sleep_start();
#endif

  disableBuiltinLED();
  // Before any SPI activity: an unparked microSD slot on the panel's bus
  // stops the display refreshing whenever a card is inserted.
  idleSDCard();

  // Load WiFi/location/time/battery/widget-layout settings from
  // data/config.json (LittleFS). Falls back to the compiled-in defaults in
  // config.cpp if the file is missing or invalid.
  loadSettings();

  // Open namespace for read/write to non-volatile storage
  prefs.begin(NVS_NAMESPACE, false);

  // Everything cached between wakes was fetched for one place, from one
  // source, in one time zone. When any of those changes -- in the portal
  // or by uploading a new config.json -- the caches go, or the next refresh
  // would show the new city's forecast with the old city's rain, pollen
  // and air quality.
  {
    const String place = LAT + "," + LON + "|" + CURRENT_SOURCE + "|"
                       + TIMEZONE;
    if (!prefs.isKey("cfgKey"))
    { // the first wake of a firmware that keeps this record: what is
      // cached was fetched under the settings in force, so it stays
      prefs.putString("cfgKey", place);
    }
    else if (prefs.getString("cfgKey", "") != place)
    {
      static const char *const CACHES[] = {
        "qpfStamp", "qpfBlob", "qpfTime", "pollenStamp", "pollenTree",
        "pollenGrass", "pollenWeed", "pollenMax", "airnowStamp", "airnowAqi",
        "omStamp", "omHum", "omDew", "omPres", "omVis", "dayStamp", "dayHi",
        "dayLo", "nwsLat", "nwsLon", "nwsFc", "nwsFcH", "wxhist"};
      for (const char *key : CACHES)
      {
        if (prefs.isKey(key))
        {
          prefs.remove(key);
        }
      }
      snapshotForget();
      prefs.putString("cfgKey", place);
      Serial.println("[cache] location, source or time zone changed; "
                     "cached weather dropped");
    }
  }

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
      prefs.putBool("goodScreen", false);
      prefs.end();
      initDisplay();
      do
      {
        fillDisplayBackground();
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
    wakeDiag.batteryMv = static_cast<uint16_t>(batteryVoltage);
    diagOutcome(DIAG_BATTERY, 0, "");
    diagSave(startTime,
             (batteryVoltage <= CRIT_LOW_BATTERY_VOLTAGE) ? 0
             : (batteryVoltage <= VERY_LOW_BATTERY_VOLTAGE)
               ? VERY_LOW_BATTERY_SLEEP_INTERVAL * 60UL
               : LOW_BATTERY_SLEEP_INTERVAL * 60UL);
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
#if defined(BOARD_RETERMINAL_E1002) || defined(BOARD_XIAO_EE02)
  // This board has a dedicated portal button, so the double-reset detector
  // is unnecessary -- and skipping it means an interrupted wake (a firmware
  // flash, most commonly) no longer drops the next boot into the portal.
  bool portalRequested = false;
  prefs.putBool("drd", false);
#else
  bool portalRequested = prefs.getBool("drd", false);
  size_t drdWritten = prefs.putBool("drd", true); // armed; disarmed once WiFi connect concludes
#if DEBUG_LEVEL >= 0
  Serial.printf("[drd] marker was %d, armed (%u bytes written)\n",
                portalRequested, drdWritten);
#endif
#endif // BOARD_RETERMINAL_E1002
  // Wake buttons (see enableButtonWake): the PORTAL button wakes via EXT0,
  // the REFRESH button via EXT1. A refresh-button wake needs no special
  // handling -- proceeding with a normal update IS the response.
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const bool buttonPortal = (wakeCause == ESP_SLEEP_WAKEUP_EXT0);
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT1)
  {
    Serial.println("Woken by refresh button.");
  }
  bool unconfigured = (strcmp(WIFI_SSID, "ssid") == 0);
  if (portalRequested || buttonPortal || unconfigured)
  {
    prefs.putBool("drd", false);
    prefs.putBool("goodScreen", false); // the setup screen replaces it
    prefs.end();
    if (unconfigured)
    {
      Serial.println("No WiFi configured, starting setup hotspot.");
    }
    else if (buttonPortal)
    {
      Serial.println("Woken by portal button, starting configuration portal.");
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
#if BATTERY_MONITORING
  wakeDiag.batteryMv = static_cast<uint16_t>(batteryVoltage);
#endif
  unsigned long phaseStart = millis();
  wl_status_t wifiStatus = startWiFi(wifiRSSI);
#if defined(SIMULATE_OUTAGE)
  Serial.println("[test] SIMULATE_OUTAGE: behaving as if WiFi had failed");
  wifiStatus = WL_NO_SSID_AVAIL;
  wifiRSSI = 0;
#endif
  wakeDiag.wifiMs = millis() - phaseStart;
  wakeDiag.rssi = static_cast<int8_t>(constrain(wifiRSSI, -127, 0));
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
    if (holdLastScreen())
    {
      diagOutcome(markStale(errTitle, 0, batteryVoltage) ? DIAG_STALE
                                                         : DIAG_HELD,
                  0, String(errTitle) + ": " + errLine2);
      beginFixedSleep(startTime, WIFI_RETRY_INTERVAL);
    }
    diagOutcome(DIAG_WIFI, 0,
                String(errTitle) + ": " + errLine2);
    String errDescriptor = String(errTitle) + "|" + errLine2;
    prefs.begin(NVS_NAMESPACE, false);
    // isKey check avoids Preferences logging a spurious NOT_FOUND error on
    // the first failure, before the marker exists
    bool alreadyShown = prefs.isKey("wifiErr")
                     && (prefs.getString("wifiErr", "") == errDescriptor);
    if (!alreadyShown)
    {
      prefs.putString("wifiErr", errDescriptor);
      // this screen replaces any other error screen
      if (prefs.isKey("timeErr")) { prefs.remove("timeErr"); }
      if (prefs.isKey("apiErr"))  { prefs.remove("apiErr"); }
    }
    prefs.end();
    if (!alreadyShown)
    {
      initDisplay();
      do
      {
        fillDisplayBackground();
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
  // The ESP32's RTC keeps time through deep sleep, but its slow clock is an
  // RC oscillator: it wanders by up to a minute over a few hours, and since
  // each wake is scheduled by that same clock the device always believes it
  // woke exactly on the minute while the real time slips. So the wake never
  // blocks on SNTP while the last sync (NVS "lastNtpSync") is recent -- the
  // RTC is good enough to start the API calls -- but SNTP is still kicked
  // off in the background and, once it has answered (well within the ~10 s
  // of network traffic), the corrected clock is what the status bar shows
  // and the next wake is aligned to. A blocking sync happens only when the
  // last one is stale, or after a power loss reset the RTC to the epoch.
  const long NTP_RESYNC_INTERVAL_SEC = 6 * 3600L;
  prefs.begin(NVS_NAMESPACE, false);
  time_t lastNtpSync = static_cast<time_t>(prefs.getLong64("lastNtpSync", 0));
  prefs.end();
  time_t rtcNow = time(nullptr);
  const unsigned long wakeMillis = millis();
  bool timeConfigured = false;
  bool ntpInBackground = false;
  if (lastNtpSync > 1600000000L && rtcNow >= lastNtpSync
      && rtcNow - lastNtpSync < NTP_RESYNC_INTERVAL_SEC)
  {
    // TZ before anything reads the local time: the variable does not
    // survive deep sleep, and without it localtime_r() answers in UTC --
    // after 8 pm Eastern that is already tomorrow, which rolled the day's
    // remembered high and low over four hours early. (Starts SNTP too; it
    // does not wait.)
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    localtime_r(&rtcNow, &timeInfo);
    timeConfigured = true;
    Serial.println("[time] RTC synced " + String(rtcNow - lastNtpSync)
                   + "s ago, SNTP in the background");
    ntpInBackground = true;
  }
  else
  {
    Serial.println("[time] full sync: rtcNow=" + String((long)rtcNow)
                   + " lastNtpSync=" + String((long)lastNtpSync)
                   + " delta=" + String((long)(rtcNow - lastNtpSync)));
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    phaseStart = millis();
    timeConfigured = waitForSNTPSync(&timeInfo);
    wakeDiag.clockMs = millis() - phaseStart;
    if (timeConfigured)
    {
      prefs.begin(NVS_NAMESPACE, false);
      prefs.putLong64("lastNtpSync", static_cast<int64_t>(time(nullptr)));
      prefs.end();
    }
  }
  if (!timeConfigured)
  {
    Serial.println(TXT_TIME_SYNCHRONIZATION_FAILED);
    killWiFi();
    if (holdLastScreen())
    {
      diagOutcome(markStale(TXT_TIME_SYNCHRONIZATION_FAILED, wifiRSSI,
                            batteryVoltage) ? DIAG_STALE : DIAG_HELD,
                  0, TXT_TIME_SYNCHRONIZATION_FAILED);
      beginFixedSleep(startTime, WIFI_RETRY_INTERVAL);
    }
    diagOutcome(DIAG_CLOCK, 0, TXT_TIME_SYNCHRONIZATION_FAILED);

    // The device retries every WIFI_RETRY_INTERVAL minutes. Avoid redrawing
    // the expensive e-paper screen if the time error is already showing.
    prefs.begin(NVS_NAMESPACE, false);
    bool alreadyShown = prefs.isKey("timeErr") && prefs.getBool("timeErr", false);
    if (!alreadyShown)
    {
      prefs.putBool("timeErr", true);
      if (prefs.isKey("apiErr")) { prefs.remove("apiErr"); }
    }
    prefs.end();

    if (!alreadyShown)
    {
      initDisplay();
      do
      {
        fillDisplayBackground();
        drawError(wi_time_4_196x196, TXT_TIME_SYNCHRONIZATION_FAILED);
      } while (display.nextPage());
      powerOffDisplay();
    }
    // the clock is unreliable if sync failed, so use a fixed-interval sleep
    // rather than beginDeepSleep()'s clock-based calculation
    beginFixedSleep(startTime, WIFI_RETRY_INTERVAL);
  }
  else
  {
    prefs.begin(NVS_NAMESPACE, false);
    if (prefs.isKey("timeErr"))
    {
      prefs.remove("timeErr");
    }
    prefs.end();
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

  phaseStart = millis();
  int rxStatus = HTTP_CODE_OK;
#if DISPLAY_ALERTS
  // Alerts first: the reply is small and arrives over HTTP/1.1 keep-alive,
  // so the forecast requests that follow reuse its TLS session instead of
  // paying their own ~1.2 s handshake. Non-fatal: if this fails, the display
  // simply shows no alerts.
  rxStatus = getNWSAlerts(client, alerts);
  if (rxStatus != HTTP_CODE_OK)
  {
    statusStr = "weather.gov Alerts API";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
  }
#endif

  // weather.gov forecast + current conditions. This is the primary data
  // source; if it fails there is nothing worth displaying.
  String failedStep;
  rxStatus = getNWSWeather(client, current, hourly, daily, failedStep);
  if (rxStatus != HTTP_CODE_OK)
  {
    killWiFi();
    statusStr = "weather.gov API (" + failedStep + ")";
    tmpStr = String(rxStatus, DEC) + ": " + getHttpResponsePhrase(rxStatus);
    Serial.println(statusStr + " - " + tmpStr);
    wakeDiag.fetchMs = millis() - phaseStart;
    if (holdLastScreen())
    {
      // (the saved weather replaces what little was fetched)
      const String what = statusStr + ": " + tmpStr;
      diagOutcome(markStale(statusStr, wifiRSSI, batteryVoltage)
                    ? DIAG_STALE : DIAG_HELD, rxStatus, what);
      beginDeepSleep(startTime, &timeInfo);
    }
    diagOutcome(DIAG_WEATHER, rxStatus, statusStr + ": " + tmpStr);

    // Avoid repeatedly redrawing the e-paper screen during prolonged API outages
    String errDescriptor = statusStr + "|" + tmpStr;
    prefs.begin(NVS_NAMESPACE, false);
    bool alreadyShown = prefs.isKey("apiErr")
                     && (prefs.getString("apiErr", "") == errDescriptor);
    if (!alreadyShown)
    {
      prefs.putString("apiErr", errDescriptor);
    }
    prefs.end();

    if (!alreadyShown)
    {
      initDisplay();
      do
      {
        fillDisplayBackground();
        drawError(wi_cloud_down_196x196, statusStr, tmpStr);
      } while (display.nextPage());
      powerOffDisplay();
    }
    beginDeepSleep(startTime, &timeInfo);
  }

  // Clear api error marker on successful fetch
  prefs.begin(NVS_NAMESPACE, false);
  if (prefs.isKey("apiErr"))
  {
    prefs.remove("apiErr");
  }
  prefs.end();

  // UV index and air quality (weather.gov does not provide either). Also
  // non-fatal: if this fails, the UVI/Air Quality widgets show "--" (they
  // used to show 0 and "Good", which is a reading, and a reassuring one).
  float uvi = NAN;
  air_quality.valid = false;
  rxStatus = getAirQuality(client, air_quality, uvi);
  if (rxStatus != HTTP_CODE_OK)
  {
    uvi = NAN;
    air_quality.valid = false;
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
  // Pollen forecast from the Google Pollen API, when a key is configured
  // and the widget occupies a slot. Non-fatal: on failure the widget shows
  // "--".
  pollen_info_t pollen;
  pollen.tree = pollen.grass = pollen.weed = 0;
  pollen.max_upi = -1;
#ifdef DISP_7C_709
  // This layout always has a pollen cell; it has no widget slots to give
  // or withhold one.
  const bool wantPollen = true;
#else
  const bool wantPollen = (POS_POLLEN >= 0);
#endif
  if (!POLLEN_APIKEY.isEmpty() && wantPollen)
  {
    // Pollen is a daily forecast, so the result is cached in NVS and the
    // API is only called once per 3-hour bucket -- ~8 calls/day instead of
    // one per wake, keeping usage far inside Google's free monthly tier.
    time_t pNow = time(nullptr);
    tm pTm;
    localtime_r(&pNow, &pTm);
    const int32_t pollenStamp = ((pTm.tm_year + 1900) * 10000
                                 + (pTm.tm_mon + 1) * 100 + pTm.tm_mday) * 10
                                + pTm.tm_hour / 3;
    prefs.begin(NVS_NAMESPACE, false);
    if (prefs.getInt("pollenStamp", 0) == pollenStamp)
    {
      pollen.tree    = prefs.getInt("pollenTree", 0);
      pollen.grass   = prefs.getInt("pollenGrass", 0);
      pollen.weed    = prefs.getInt("pollenWeed", 0);
      pollen.max_upi = prefs.getInt("pollenMax", -1);
    }
    else
    {
      rxStatus = getGooglePollen(client, pollen);
      if (rxStatus != HTTP_CODE_OK)
      {
        pollen.max_upi = -1;
        Serial.println("Google Pollen API " + String(rxStatus, DEC) + ": "
                       + getHttpResponsePhrase(rxStatus));
      }
      else
      {
        prefs.putInt("pollenStamp", pollenStamp);
        prefs.putInt("pollenTree", pollen.tree);
        prefs.putInt("pollenGrass", pollen.grass);
        prefs.putInt("pollenWeed", pollen.weed);
        prefs.putInt("pollenMax", pollen.max_upi);
      }
    }
    prefs.end();
  }

  air_quality.us_aqi = -1;
  if (!AIRNOW_APIKEY.isEmpty())
  {
    // AirNow's gateway is routinely the slowest call of the wake (8-25s),
    // and its stations only report hourly -- so the result is cached in
    // NVS per clock hour, like the pollen forecast. A failed fetch is not
    // cached, so the next wake retries.
    time_t aNow = time(nullptr);
    tm aTm;
    localtime_r(&aNow, &aTm);
    const int32_t airnowStamp = ((aTm.tm_year + 1900) * 10000
                                 + (aTm.tm_mon + 1) * 100
                                 + aTm.tm_mday) * 100 + aTm.tm_hour;
    prefs.begin(NVS_NAMESPACE, false);
    if (prefs.getInt("airnowStamp", 0) == airnowStamp)
    {
      air_quality.us_aqi = prefs.getInt("airnowAqi", -1);
      Serial.println("[airnow] cached AQI " + String(air_quality.us_aqi)
                     + " for this hour, skipping fetch");
    }
    else
    {
      rxStatus = getAirNowAQI(client, air_quality.us_aqi);
      if (rxStatus != HTTP_CODE_OK)
      {
        Serial.println("AirNow API " + String(rxStatus, DEC) + ": "
                       + getHttpResponsePhrase(rxStatus)
                       + " - using Open-Meteo AQI instead");
      }
      else
      {
        prefs.putInt("airnowStamp", airnowStamp);
        prefs.putInt("airnowAqi", air_quality.us_aqi);
      }
    }
    prefs.end();
  }

  // NWS serves the forecast as day/night periods and drops each period once
  // it ends, so after the daytime period passes, "today" consists only of
  // "Tonight" and its high collapses to the overnight low (70|70 while it
  // was 85 all afternoon). Remember today's widest forecast Hi|Lo across
  // wakes (NVS) instead: the 85 seen in the morning forecast still shows at
  // 9pm. The column stays purely forecast-derived -- deliberately no blend
  // with the current observation, whose station may be unrepresentative
  // (urban heat island). Rolls over automatically when the local date
  // changes. (Reaching this code implies the clock is synced, so the date
  // stamp is trustworthy.)
  prefs.begin(NVS_NAMESPACE, false);
  const int32_t todayStamp = (timeInfo.tm_year + 1900) * 10000
                             + (timeInfo.tm_mon + 1) * 100
                             + timeInfo.tm_mday;
  if (prefs.getInt("dayStamp", 0) == todayStamp)
  {
    float hi = prefs.getFloat("dayHi", daily[0].temp.max);
    float lo = prefs.getFloat("dayLo", daily[0].temp.min);
    if (hi > daily[0].temp.max && hi < 333.0f)
    {
      daily[0].temp.max = hi;
    }
    if (lo < daily[0].temp.min && lo > 200.0f)
    {
      daily[0].temp.min = lo;
    }
  }
  prefs.putInt("dayStamp", todayStamp);
  prefs.putFloat("dayHi", daily[0].temp.max);
  prefs.putFloat("dayLo", daily[0].temp.min);
  prefs.end();

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
  // Icon day/night selection uses the computed sun times (see isDaytimeAt in
  // display_utils.cpp); NWS's own flag flips at fixed 6am/6pm instead.
  setSunTimes(current.sunrise, current.sunset);

  killWiFi(); // WiFi no longer needed
  wakeDiag.fetchMs = millis() - phaseStart;

  // GET INDOOR TEMPERATURE AND HUMIDITY, start indoor sensor...
  float inTemp     = NAN;
  float inHumidity = NAN;
#ifdef SENSOR_NONE
  // No environment sensor on this board; the indoor widgets show "--".
#else
  if (PIN_BME_PWR != PIN_UNUSED)
  {
    pinMode(PIN_BME_PWR, OUTPUT);
    digitalWrite(PIN_BME_PWR, HIGH);
  }
#if defined(SENSOR_INIT_DELAY_MS) && SENSOR_INIT_DELAY_MS > 0
  delay(SENSOR_INIT_DELAY_MS);
#endif
#if defined(SENSOR_SHT4X)
  #define SENSOR_NAME "SHT4x"
#else
  #define SENSOR_NAME "BME"
#endif
  TwoWire I2C_bme = TwoWire(0);
  I2C_bme.begin(PIN_BME_SDA, PIN_BME_SCL, 100000); // 100kHz
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
#if defined(SENSOR_SHT4X)
  // Onboard SHT4x (reTerminal E1002). Fixed I2C address, event-based API.
  Serial.print(String(TXT_READING_FROM) + " SHT4x... ");
  Adafruit_SHT4x sht4;

  if(sht4.begin(&I2C_bme))
  {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    sensors_event_t humEvent, tempEvent;
    if (sht4.getEvent(&humEvent, &tempEvent))
    {
      inTemp     = tempEvent.temperature;       // Celsius
      inHumidity = humEvent.relative_humidity;  // %
    }
#else
    inTemp     = bme.readTemperature(); // Celsius
    inHumidity = bme.readHumidity();    // %
#endif

    // check if BME readings are valid
    // note: readings are checked again before drawing to screen. If a reading
    //       is not a number (NAN) then an error occurred, a dash '-' will be
    //       displayed.
    if (std::isnan(inTemp) || std::isnan(inHumidity))
    {
      statusStr = SENSOR_NAME " " + String(TXT_READ_FAILED);
      Serial.println(statusStr);
    }
    else
    {
      Serial.println(TXT_SUCCESS);
    }
  }
  else
  {
#if defined(SENSOR_OPTIONAL)
    // none fitted, which is how the board comes
    Serial.println(TXT_NOT_FOUND);
#else
    statusStr = SENSOR_NAME " " + String(TXT_NOT_FOUND); // check wiring
    Serial.println(statusStr);
#endif
  }
  if (PIN_BME_PWR != PIN_UNUSED)
  {
    // let go of the lines before the sensor loses its supply, or it
    // would draw through them
    I2C_bme.end();
    if (PIN_BME_PWR != PIN_EPD_PWR)
    { // a supply shared with the panel stays up: the panel is next
      digitalWrite(PIN_BME_PWR, LOW);
    }
  }
#endif // SENSOR_NONE

  // Re-read the clock now that the network work is done: if the background
  // SNTP answered, this is the corrected time (and the drift it removed is
  // logged); otherwise it is the RTC, a few seconds on from the wake.
  if (timeConfigured)
  {
    time_t now = time(nullptr);
    if (ntpInBackground
        && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
    {
      const long expected = rtcNow + (millis() - wakeMillis) / 1000;
      Serial.println("[time] SNTP corrected the RTC by "
                     + String((long)(now - expected)) + " s ("
                     + String((long)(rtcNow - lastNtpSync)) + " s since the last sync)");
      prefs.begin(NVS_NAMESPACE, false);
      prefs.putLong64("lastNtpSync", static_cast<int64_t>(now));
      prefs.end();
    }
    localtime_r(&now, &timeInfo);
  }
  String refreshTimeStr;
  getRefreshTimeStr(refreshTimeStr, timeConfigured, &timeInfo);
  String dateStr;
  getDateStr(dateStr, &timeInfo);

  // RENDER FULL REFRESH
  // Record this wake's readings into the NVS history and derive the
  // pressure/indoor trends and the battery-runtime estimate shown on the
  // display (see history.h).
  prefs.begin(NVS_NAMESPACE, false);
  // 0 = pressure not available (the "nws" source, or a failed fetch with
  // nothing cached). NaN makes the history ring skip the sample; logging a
  // 0 hPa would read as a 1000 hPa crash and pin the trend arrow for hours.
  const float pressureSample = (current.pressure > 0)
                             ? static_cast<float>(current.pressure) : NAN;
#if BATTERY_MONITORING
  historyUpdate(time(nullptr), pressureSample, inTemp, batteryVoltage,
                prefs);
#else
  historyUpdate(time(nullptr), pressureSample, inTemp, 0, prefs);
#endif
  prefs.end();
  Serial.println("[history] pressure trend " + String(historyPressureTrend())
                 + ", indoor trend " + String(historyIndoorTrend())
                 + ", battery days left " + String(historyBatteryDaysLeft()));

  phaseStart = millis();
  drawWeatherPage(timeInfo, dateStr, refreshTimeStr, statusStr, wifiRSSI,
                  batteryVoltage, pollen, inTemp, inHumidity, false);
  wakeDiag.drawMs = millis() - phaseStart;

  // Kept for the day the network is not there: see markStale().
  {
    snapshot_meta_t meta;
    meta.when = static_cast<int64_t>(mktime(&timeInfo));
    meta.graphHours = HOURLY_GRAPH_MAX;
    meta.inTemp = inTemp;
    meta.inHumidity = inHumidity;
    meta.pollen = pollen;
    meta.refreshTime = refreshTimeStr;
    meta.date = dateStr;
    const unsigned long saveStart = millis();
    const bool saved = snapshotSave(current, hourly, daily, alerts,
                                    air_quality, meta);
    Serial.println(String("[snapshot] ") + (saved ? "saved" : "NOT saved")
                   + " in " + String(millis() - saveStart) + " ms");
  }

  // The screen now shows the weather: an outage, if there was one, is over.
  prefs.begin(NVS_NAMESPACE, false);
  if (!prefs.getBool("goodScreen", false) || prefs.getInt("outageMin", 0) != 0
      || prefs.getBool("staleShown", false))
  {
    prefs.putBool("goodScreen", true);
    prefs.putInt("outageMin", 0);
    prefs.putBool("staleShown", false);
  }
  prefs.end();
  // anything that was missing (a reading that could not be fetched) rides
  // along as the note of a wake that otherwise went well
  diagOutcome(DIAG_OK, 0, statusStr.isEmpty() ? statusStr
                                             : statusStr + ": " + tmpStr);

  // DEEP SLEEP
  beginDeepSleep(startTime, &timeInfo);
} // end setup

/* This will never run
 */
void loop()
{
} // end loop

