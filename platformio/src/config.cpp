/* Configuration options for esp32-weather-epd.
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

#include <Arduino.h>
#include "config.h"

// PINS
// The configuration below is intended for use with the project's official 
// wiring diagrams using the FireBeetle 2 ESP32-E microcontroller board.
//
// Note: LED_BUILTIN pin will be disabled to reduce power draw.  Refer to your
//       board's pinout to ensure you avoid using a pin with this shared 
//       functionality.
//
#ifdef BOARD_RETERMINAL_E1002
// Seeed reTerminal E1002 -- fixed internal wiring, do not change.
// (Sources: Seeed wiki "Arduino Cookbook" pages for the reTerminal E Series.)
// ADC pin used to measure battery voltage (GPIO1 = ADC1_CH0, 1:2 divider).
const uint8_t PIN_BAT_ADC  = 1;
// Must be driven HIGH to connect the battery divider to the ADC.
const uint8_t PIN_BAT_EN   = 21;
// Built-in 7.3in Spectra 6 panel (GDEP073E01)
const uint8_t PIN_EPD_BUSY = 13;
const uint8_t PIN_EPD_CS   = 10;
const uint8_t PIN_EPD_RST  = 12;
const uint8_t PIN_EPD_DC   = 11;
const uint8_t PIN_EPD_SCK  = 7;  // SPI bus shared with the microSD slot
const uint8_t PIN_EPD_MISO = 8;  // (SD's MISO; the panel itself returns no data)
const uint8_t PIN_EPD_MOSI = 9;
const uint8_t PIN_EPD_PWR  = PIN_UNUSED; // panel supply is hardwired
// microSD slot. Unused by this firmware, but it sits on the panel's SPI bus
// and must be parked -- see idleSDCard() in display_utils.cpp.
// (The E1003 puts SD power enable on GPIO39 instead of 16.)
const uint8_t PIN_SD_EN    = 16; // active high, powers the slot
const uint8_t PIN_SD_CS    = 14;
// Front buttons (active low). Middle button wakes into the config portal,
// the right (green) button wakes into an immediate refresh. KEY2 (GPIO5,
// left) is currently unassigned.
const uint8_t PIN_BTN_PORTAL  = 4;  // KEY1, middle
const uint8_t PIN_BTN_REFRESH = 3;  // KEY0, right (green)
// Onboard SHT4x temperature/humidity sensor
const uint8_t PIN_BME_SDA = 19;
const uint8_t PIN_BME_SCL = 20;
const uint8_t PIN_BME_PWR = PIN_UNUSED;  // sensor supply is hardwired
const uint8_t BME_ADDRESS = 0x44;        // SHT4x fixed I2C address
#else
// ADC pin used to measure battery voltage
const uint8_t PIN_BAT_ADC  = A2; // A0 for micro-usb firebeetle
const uint8_t PIN_BAT_EN   = PIN_UNUSED; // FireBeetle divider is always connected
const uint8_t PIN_BTN_PORTAL  = PIN_UNUSED; // no wake buttons on this board
const uint8_t PIN_BTN_REFRESH = PIN_UNUSED;
// Pins for E-Paper Driver Board
const uint8_t PIN_EPD_BUSY = 14; // 5 for micro-usb firebeetle
const uint8_t PIN_EPD_CS   = 13;
const uint8_t PIN_EPD_RST  = 21;
const uint8_t PIN_EPD_DC   = 22;
const uint8_t PIN_EPD_SCK  = 18;
const uint8_t PIN_EPD_MISO = 19; // 19 Master-In Slave-Out not used, as no data from display
const uint8_t PIN_EPD_MOSI = 23;
const uint8_t PIN_EPD_PWR  = 26; // Irrelevant if directly connected to 3.3V
// No microSD slot sharing the panel's SPI bus on this wiring.
const uint8_t PIN_SD_EN    = PIN_UNUSED;
const uint8_t PIN_SD_CS    = PIN_UNUSED;
// I2C Pins used for BME280
const uint8_t PIN_BME_SDA = 17;
const uint8_t PIN_BME_SCL = 16;
const uint8_t PIN_BME_PWR =  4;   // Irrelevant if directly connected to 3.3V
const uint8_t BME_ADDRESS = 0x76; // 0x76 if SDO -> GND; 0x77 if SDO -> VCC
#endif // BOARD_RETERMINAL_E1002

// FORECAST DAYS
// Number of days shown in the daily forecast row, range [5-7]. At 5 days the
// icons are 64px; at 6 or 7 the columns are narrower, so 48px icons and a
// smaller temperature font are used.
int FORECAST_DAYS = 5;

// WIDGET ROWS
// Rows in the left-panel widget grid, range [5-6]. At 5 rows (10 slots) the
// widget icons are 48px; at 6 rows (12 slots, enough for every widget) the
// rows are tighter and 40px icons are used.
int WIDGET_ROWS = 5;

// Dark mode: black background, white text, dark-variant icons. Runtime
// setting (config.json "dark_mode"); full-color panels only -- ignored
// (forced off) on single-color panels.
bool DARK_MODE = false;

// Font family: index into FONT_FAMILIES (fonts/font_table.h), the families
// compiled in by config.h's FONT_INCLUDE_ switches. Runtime setting
// (config.json "font", by name); unknown names fall back to the first.
// FONT_SMALL_FAMILY_INDEX is the family for the small text (widget labels,
// graph axis, tags -- everything under FONT_SMALL_BELOW_PT); config.json
// "font_small", following "font" when empty or unknown.
int FONT_FAMILY_INDEX = 0;
int FONT_SMALL_FAMILY_INDEX = 0;

// PER-DEPLOYMENT SETTINGS (WiFi, location, time, battery, widget layout)
//
// The values below are only FALLBACK DEFAULTS, used if data/config.json is
// missing or does not specify a given key. In normal use these are all
// configured at runtime via data/config.json (loaded by loadSettings() in
// settings.cpp) so that the project can be reconfigured -- new WiFi network,
// new location, rearranged widgets, etc. -- by editing config.json and
// running `pio run --target uploadfs`, without touching this file or
// recompiling the firmware. See data/config.json for the actual live values.

// WIFI
char WIFI_SSID[33]     = "ssid";
char WIFI_PASSWORD[65] = "password";
unsigned long WIFI_TIMEOUT = 60000; // ms, WiFi connection timeout.
// Optional static IP configuration (config.json wifi.static_ip etc.).
// All four must be set to skip DHCP; empty = DHCP.
char STATIC_IP[16]      = "";
char STATIC_GATEWAY[16] = "";
char STATIC_SUBNET[16]  = "";
char STATIC_DNS[16]     = "";


// HTTP
// The following errors are likely the result of insuffient http client tcp
// timeout:
//   -1   Connection Refused
//   -11  Read Timeout
//   -258 Deserialization Incomplete Input
unsigned HTTP_CLIENT_TCP_TIMEOUT = 10000; // ms

// NATIONAL WEATHER SERVICE (NWS) API
// weather.gov's API is free and does not require an API key. NWS asks that
// requests include a descriptive User-Agent, ideally with a contact method,
// in case of problems: https://www.weather.gov/documentation/services-web-api
String NWS_USER_AGENT = "(esp32-weather-epd, your.email@example.com)";

// AIRNOW API (optional)
// AirNow (airnow.gov) is the EPA's official air quality service and provides
// the authoritative US AQI, pre-computed from certified monitoring stations.
// It requires a free API key: register at https://docs.airnowapi.org/
// When a key is set, the Air Quality widget shows AirNow's official AQI;
// when left empty, the AQI is computed from Open-Meteo's modeled pollutant
// concentrations instead (no key needed, slightly less authoritative).
String AIRNOW_APIKEY = "";

// GOOGLE POLLEN API (optional)
// Google's Pollen API (Google Maps Platform) provides a Universal Pollen
// Index forecast (tree/grass/weed). Requires an API key with the Pollen
// API enabled: https://developers.google.com/maps/documentation/pollen
// The device calls it at most once per 3 hours (cached in NVS), which
// stays far inside the free monthly quota. Consider setting a daily
// request cap in the Google Cloud Console as a billing safety net.
String POLLEN_APIKEY = "";

// CURRENT CONDITIONS SOURCE
// Where the "right now" temperature, feels-like, humidity, dew point,
// pressure, wind, cloud cover and visibility come from. Forecasts and
// alerts always come from weather.gov regardless of this setting.
//   "open-meteo" (default) - Open-Meteo's model interpolated to LAT/LON,
//                            refreshed every ~15 minutes. Free, no key.
//   "google"               - Google Maps Platform Weather API current
//                            conditions (station-blended nowcast, refreshed
//                            every 15 minutes). Uses POLLEN_APIKEY, which
//                            must have the Weather API enabled. Falls back
//                            to Open-Meteo when no key is configured.
//   "nws"                  - weather.gov's gridpoint hourly forecast period
//                            for the current hour. Free, no key, no extra
//                            request, but no pressure or visibility.
// If the chosen source fails, current conditions fall back to the hourly
// forecast (plus the last cached humidity/dew point/pressure/visibility).
String CURRENT_SOURCE = "open-meteo";

// FORECAST SOURCE
// Where the hourly and daily forecasts, the condition icons and the weather
// alerts come from.
//   "nws" (default) - weather.gov (US National Weather Service). United
//                     States only. Free, no key.
//   "ims"           - the Israel Meteorological Service (ims.gov.il), the
//                     public JSON feeds behind its site and app. Israel only.
//                     Free, no key. Hourly forecast for 7 days, with humidity,
//                     rain, wind and UV per hour; regional warnings. Replies
//                     come in the locale's language (LOCALE he_IL -> Hebrew).
//                     The CURRENT_SOURCE option "nws" then means IMS's own
//                     current conditions. IMS times are Israel local time, so
//                     TIMEZONE should be Israel's.
String FORECAST_SOURCE = "nws";
// IMS forecast location (lid from https://ims.gov.il/en/locations_info):
// 0 picks the one nearest LAT/LON automatically.
int IMS_LOCATION_ID = 0;

// AIR QUALITY INDEX SOURCE
//   "auto" (default) - AirNow when an AirNow key is set; the Israel
//                      Ministry of Environmental Protection when the
//                      forecast source is IMS; otherwise Open-Meteo's model.
//   "airnow"         - AirNow (US EPA), needs AIRNOW_APIKEY.
//   "israel"         - the Israel Ministry of Environmental Protection's
//                      national monitoring network (air.sviva.gov.il): the
//                      measured Israeli index of the nearest station, no
//                      key (the site's own guest token). Israel only.
//   "model"          - Open-Meteo's CAMS model, computed on the device.
String AQI_SOURCE = "auto";
// Ministry station id (air.sviva.gov.il) to pin one station; 0 uses the
// active stations within 15 km of LAT/LON (worst index), found once.
int IL_AQ_STATION_ID = 0;

// SHABBAT AND HOLIDAY TIMES
// On the eve of Shabbat or a holiday the sunset widget shows candle
// lighting instead (sunset less CANDLE_LIGHTING_MINUTES: 18 is the common
// custom, Jerusalem's is 40), and on the day itself their end (sunset plus
// HAVDALAH_MINUTES: 42 is common, some wait for 50 or 72). SHABBAT_TIMES
// -1 follows the locale (on for Hebrew), 0 and 1 force it.
int SHABBAT_TIMES = -1;
int CANDLE_LIGHTING_MINUTES = 18;
int HAVDALAH_MINUTES = 42;

// CONFIGURATION WEB PORTAL
// A browser UI for editing config.json without reflashing. Entered by
// pressing RST twice a few seconds apart (config mode on your WiFi), or
// automatically when the device is unconfigured (hotspot mode, network
// "WeatherEPD-Setup"). See portal.h for details.
// WPA2 password for the setup hotspot (8+ characters).
String PORTAL_AP_PASSWORD = "weatherepd";
// Minutes of inactivity before the portal gives up and the device resumes
// its normal wake/sleep cycle.
int PORTAL_TIMEOUT = 10;

// LOCATION
// Set your latitude and longitude.
// (used to look up your forecast gridpoint and nearest observation station)
String LAT = "40.7128";
String LON = "-74.0060";
// City name that will be shown in the top-right corner of the display.
String CITY_STRING = "New York";

// TIME
// For list of time zones see
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
char TIMEZONE[64] = DEFAULT_TIMEZONE; // the locale's (config.h)
// Time format used when displaying sunrise/set times. (Max 11 characters)
// For more information about formatting see
// https://man7.org/linux/man-pages/man3/strftime.3.html
char TIME_FORMAT[16] = "%l:%M%P"; // 12-hour ex: 1:23am  11:00pm
// char TIME_FORMAT[16] = "%H:%M";   // 24-hour ex: 01:23   23:00
// Time format used when displaying axis labels. (Max 11 characters)
// For more information about formatting see
// https://man7.org/linux/man-pages/man3/strftime.3.html
char HOUR_FORMAT[16] = "%l%P"; // 12-hour ex: 1am  11pm
// char HOUR_FORMAT[16] = "%H";      // 24-hour ex: 01   23
// Date format used when displaying date in top-right corner.
// For more information about formatting see
// https://man7.org/linux/man-pages/man3/strftime.3.html
char DATE_FORMAT[32] = "%a, %B %e"; // ex: Sat, January 1
// Date/Time format used when displaying the last refresh time along the bottom
// of the screen.
// For more information about formatting see
// https://man7.org/linux/man-pages/man3/strftime.3.html
char REFRESH_TIME_FORMAT[32] = "%x %H:%M";
// NTP_SERVER_1 is the primary time server, while NTP_SERVER_2 is a fallback.
// pool.ntp.org will find the closest available NTP server to you.
char NTP_SERVER_1[64] = "pool.ntp.org";
char NTP_SERVER_2[64] = "time.nist.gov";
// If you encounter the 'Failed To Fetch The Time' error, try increasing
// NTP_TIMEOUT or select closer/lower latency time servers.
unsigned long NTP_TIMEOUT = 20000; // ms
// Sleep duration in minutes. (aka how often esp32 will wake for an update)
// Aligned to the nearest minute boundary.
// For example, if set to 30 (minutes) the display will update at 00 or 30
// minutes past the hour. (range: [2-1440])
// Note: weather.gov's forecast is typically updated a few times per hour, so
//       updating more frequently than every ~15 minutes is unnecessary.
int SLEEP_DURATION = 15; // minutes
// How long to sleep between retries after a failed WiFi connection or time
// sync, in minutes. This is a plain fixed interval, deliberately independent
// of the clock-aligned SLEEP_DURATION/bedtime logic: without WiFi the clock
// may never have been synced (a fresh boot starts at the 1970 epoch), which
// would make the bedtime calculation nonsense. The device keeps retrying at
// this interval indefinitely, so it recovers on its own once it comes into
// range of its network -- no reset button needed.
int WIFI_RETRY_INTERVAL = 15; // minutes

// OUTAGE GRACE
// When the weather cannot be fetched -- no WiFi, no answer from
// weather.gov -- the last good screen is left up for this many minutes
// before an error screen replaces it. Most outages are over by then, and
// a forecast an hour old (it says when it was updated) is worth more
// than a message that the router restarted. 0 shows errors at once.
// A wake by button or reset always shows the error.
int OUTAGE_GRACE = 180; // minutes
// Bed Time Power Savings.
// If BED_TIME == WAKE_TIME, then this battery saving feature will be disabled.
// (range: [0-23])
int BED_TIME  = 00; // Last update at 00:00 (midnight) until WAKE_TIME.
int WAKE_TIME = 05; // Hour of first update after BED_TIME, 06:00.
// Note that the minute alignment of SLEEP_DURATION begins at WAKE_TIME even if
// Bed Time Power Savings is disabled.
// For example, if WAKE_TIME = 00 (midnight) and SLEEP_DURATION = 120, then the
// display will update at 00:00, 02:00, 04:00... until BED_TIME.
// If you desire to have your display refresh exactly once a day, you should set
// SLEEP_DURATION = 1440, and you can set the time it should update each day by
// setting both BED_TIME and WAKE_TIME to the hour you want it to update.

// HOURLY OUTLOOK GRAPH
// Number of hours to display on the outlook graph. (range: [8-48])
int HOURLY_GRAPH_MAX = 24;
// Draw the hourly dew point as a second curve under the temperature. Runtime
// setting (config.json "graph_dewpoint"). Off by default: it is a second
// line on an already busy graph, and only earns its place for people who
// read dew point (mugginess, fog and frost risk) rather than humidity.
bool GRAPH_DEWPOINT = false;

// BATTERY
// To protect the battery upon LOW_BATTERY_VOLTAGE, the display will cease to
// update until battery is charged again. The ESP32 will deep-sleep (consuming
// < 11μA), waking briefly check the voltage at the corresponding interval (in
// minutes). Once the battery voltage has fallen to CRIT_LOW_BATTERY_VOLTAGE,
// the esp32 will hibernate and a manual press of the reset (RST) button to
// begin operating again.
uint32_t WARN_BATTERY_VOLTAGE     = 3535; // (millivolts) ~20%
uint32_t LOW_BATTERY_VOLTAGE      = 3462; // (millivolts) ~10%
uint32_t VERY_LOW_BATTERY_VOLTAGE = 3442; // (millivolts)  ~8%
uint32_t CRIT_LOW_BATTERY_VOLTAGE = 3404; // (millivolts)  ~5%
unsigned long LOW_BATTERY_SLEEP_INTERVAL      = 30;  // (minutes)
unsigned long VERY_LOW_BATTERY_SLEEP_INTERVAL = 120; // (minutes)
// Battery voltage calculations are based on a typical 3.7v LiPo.
uint32_t MAX_BATTERY_VOLTAGE = 4200; // (millivolts)
uint32_t MIN_BATTERY_VOLTAGE = 3000; // (millivolts)

// WIDGET POSITIONS
// Set the order of current condition widgets you want to display, see the
// WIDGET POSITIONS comment in config.h for the grid layout. -1 disables a
// widget. air_quality and visibility default to sharing a slot (visibility
// disabled) since there usually isn't room to show both.
int POS_SUNRISE     = 0;
int POS_SUNSET      = 1;
int POS_HUMIDITY    = 2;
int POS_DEWPOINT    = 3;
int POS_HEAT_STRESS = -1; // heat stress (IMS discomfort index); the Israeli counterpart of the dew point
int POS_WIND        = 4;
int POS_UVI         = 5;
int POS_PRESSURE    = 6;
int POS_AIR_QUALITY  = 7;
int POS_VISIBILITY  = -1;
// Moon phase is computed on-device (see sun.cpp), no API needed. Hidden by
// default -- there are more widgets than the ten slots.
int POS_MOON_PHASE  = -1;
int POS_POLLEN      = -1;
int POS_INTEMP      = 8;
int POS_INHUMIDITY  = 9;

// See config.h for the below options
// E-PAPER PANEL
// LOCALE
// UNITS
// WIND ICON PRECISION
// FONTS
// ALERTS
// BATTERY MONITORING

