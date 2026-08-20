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
// Onboard SHT4x temperature/humidity sensor
const uint8_t PIN_BME_SDA = 19;
const uint8_t PIN_BME_SCL = 20;
const uint8_t PIN_BME_PWR = PIN_UNUSED;  // sensor supply is hardwired
const uint8_t BME_ADDRESS = 0x44;        // SHT4x fixed I2C address
#else
// ADC pin used to measure battery voltage
const uint8_t PIN_BAT_ADC  = A2; // A0 for micro-usb firebeetle
const uint8_t PIN_BAT_EN   = PIN_UNUSED; // FireBeetle divider is always connected
// Pins for E-Paper Driver Board
const uint8_t PIN_EPD_BUSY = 14; // 5 for micro-usb firebeetle
const uint8_t PIN_EPD_CS   = 13;
const uint8_t PIN_EPD_RST  = 21;
const uint8_t PIN_EPD_DC   = 22;
const uint8_t PIN_EPD_SCK  = 18;
const uint8_t PIN_EPD_MISO = 19; // 19 Master-In Slave-Out not used, as no data from display
const uint8_t PIN_EPD_MOSI = 23;
const uint8_t PIN_EPD_PWR  = 26; // Irrelevant if directly connected to 3.3V
// I2C Pins used for BME280
const uint8_t PIN_BME_SDA = 17;
const uint8_t PIN_BME_SCL = 16;
const uint8_t PIN_BME_PWR =  4;   // Irrelevant if directly connected to 3.3V
const uint8_t BME_ADDRESS = 0x76; // 0x76 if SDO -> GND; 0x77 if SDO -> VCC
#endif // BOARD_RETERMINAL_E1002

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
char TIMEZONE[64] = "EST5EDT,M3.2.0,M11.1.0";
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
int POS_WIND        = 4;
int POS_UVI         = 5;
int POS_PRESSURE    = 6;
int POS_AIR_QULITY  = 7;
int POS_VISIBILITY  = -1;
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

