/* Configuration option declarations for esp32-weather-epd.
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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <cstdint>
#include <Arduino.h>

// E-PAPER PANEL
// This project supports the following E-Paper panels:
//   DISP_BW_V2 - 7.5in e-Paper (v2)           800x480px  Black/White
//   DISP_3C_B  - 7.5in e-Paper (B)            800x480px  Red/Black/White
//   DISP_7C_F  - 7.3in ACeP e-Paper (F)       800x480px  7-Color
//   DISP_7C_E6 - 7.3in spectra 6 e-Paper (E6) 800x480px  7-Color
//   DISP_BW_V1 - 7.5in e-Paper (v1)           640x384px  Black/White
//   DISP_7C_709 - 7.09in spectra 6 e-Paper    1600x1200px 6-Color
//                 (Good Display GDEB0709E01, 282 ppi; has a layout of its
//                 own, see renderer709.cpp)
// Uncomment the macro that identifies your physical panel.
// (Building with -e seeed_reterminal_e1002 selects the panel automatically;
//  the reTerminal E1002's built-in 7.3in Spectra 6 panel is a GDEP073E01,
//  i.e. DISP_7C_E6.)
// (The reTerminal E1001 build defines both board flags: the E1002 flag for
//  the shared E-series mainboard quirks, plus E1001 to swap the panel for
//  its 7.5in monochrome one -- a GDEY075T7-class panel, i.e. DISP_BW_V2.)
// (Building with -e seeed_xiao_ee02_709 selects the GDEB0709E01 on Seeed's
//  XIAO ePaper Display Board EE02.)
#if defined(BOARD_XIAO_EE02)
  #define DISP_7C_709
#elif defined(BOARD_RETERMINAL_E1001)
  #define DISP_BW_V2
#elif defined(BOARD_RETERMINAL_E1002)
  #define DISP_7C_E6
#else
#define DISP_BW_V2
// #define DISP_3C_B
// #define DISP_7C_F
// #define DISP_7C_E6
// #define DISP_BW_V1
#endif

// E-PAPER DRIVER BOARD
// The DESPI-C02 is the only officially supported driver board.
// Support for the Waveshare rev2.2 and rev2.3 is deprecated.
// The Waveshare rev2.2 is no longer in production.
// Users of the Waveshare rev2.3 have reported experiencing low contrast issues.
// Uncomment the macro that identifies your driver board hardware.
#define DRIVER_DESPI_C02
// #define DRIVER_WAVESHARE

// INDOOR ENVIRONMENT SENSOR
// Uncomment the macro that identifies your sensor.
// (The reTerminal E1002 has an SHT4x temperature/humidity sensor onboard,
//  selected automatically when building with -e seeed_reterminal_e1002.)
// (The EE02 has no sensor of its own. An SHT4x soldered to the pads of the
//  font chip the board was never given is found by itself; without one the
//  Indoor cell shows visibility. See the README for the four wires.)
// SENSOR_OPTIONAL: a sensor that does not answer is taken to be absent
// rather than broken, and is not reported in the status bar.
#if defined(BOARD_XIAO_EE02)
  #define SENSOR_SHT4X
  #define SENSOR_OPTIONAL
  // its supply is switched on just before it is read
  #define SENSOR_INIT_DELAY_MS 10
#elif defined(BOARD_RETERMINAL_E1002)
  #define SENSOR_SHT4X
#else
#define SENSOR_BME280
// #define SENSOR_BME680
#endif

// If you encounter issues with the BME280 sensor showing no data, uncomment and
// add a small delay before reading it's value. 300ms seems to work for most people
// #define SENSOR_INIT_DELAY_MS 300

// 3 COLOR E-INK ACCENT COLOR
// Defines the 3rd color to be used when a 3+ color display is selected.
#if defined(DISP_3C_B) || defined(DISP_7C_F) || defined(DISP_7C_E6) \
 || defined(DISP_7C_709)
  // #define ACCENT_COLOR GxEPD_BLACK
  #define ACCENT_COLOR GxEPD_RED
  // #define ACCENT_COLOR GxEPD_GREEN
  // #define ACCENT_COLOR GxEPD_BLUE
  // #define ACCENT_COLOR GxEPD_YELLOW
  // #define ACCENT_COLOR GxEPD_ORANGE
#endif

// EXPANDED COLOR PALETTE (multicolor panels only)
// The 7-color panels (ACeP and Spectra 6) can render red, yellow, green, and
// blue natively, so instead of a single accent color the display uses a small
// semantic palette: each define below names a *meaning*, and everything with
// that meaning is drawn in that color. Tune to taste -- each is a one-line
// change. On black/white and 3-color panels these all collapse to black
// (the single ACCENT_COLOR above is still used where it always was), so the
// rendering code can use them unconditionally.
//   COLOR_SUN     : sun-dominant weather icons, sunrise/sunset widget icons
//   COLOR_PRECIP  : rain/snow weather icons, precipitation bars + axis labels
//   COLOR_TEMP_HI : daily forecast high temperature
//   COLOR_TEMP_LO : daily forecast low temperature
//   COLOR_DEWPOINT: hourly dew point curve on the outlook graph (optional,
//                   config.json "graph_dewpoint")
//   COLOR_GOOD    : AQI/UVI icon when conditions are good/low
//   COLOR_BAD     : AQI/UVI icon when conditions are unhealthy/very high,
//                   tornado icon
// Note: yellow line art on white e-paper has the least contrast of the
// palette; if COLOR_SUN is hard to read on your panel, GxEPD_ORANGE (dithered
// red/yellow) or GxEPD_RED are the usual substitutes.
#if defined(DISP_7C_F) || defined(DISP_7C_E6) || defined(DISP_7C_709)
  #define MULTICOLOR_DISPLAY
  #define COLOR_SUN     GxEPD_YELLOW
  #define COLOR_PRECIP  GxEPD_BLUE
  #define COLOR_TEMP_HI GxEPD_RED
  #define COLOR_TEMP_LO GxEPD_BLUE
  #define COLOR_DEWPOINT GxEPD_BLUE
  #define COLOR_GOOD    GxEPD_GREEN
  #define COLOR_BAD     GxEPD_RED
#else
  #define COLOR_SUN     GxEPD_BLACK
  #define COLOR_PRECIP  GxEPD_BLACK
  #define COLOR_TEMP_HI GxEPD_BLACK
  #define COLOR_TEMP_LO GxEPD_BLACK
  #define COLOR_DEWPOINT GxEPD_BLACK
  #define COLOR_GOOD    GxEPD_BLACK
  #define COLOR_BAD     GxEPD_BLACK
#endif

// LOCALE
// If your locale is not here, you can add it by copying and modifying one of
// the files in src/locales. Please feel free to create a pull request to add
// official support for your locale.
//   Language (Territory)            code
//   German (Germany)                de_DE
//   English (United Kingdom)        en_GB
//   English (United States)         en_US
//   Estonian (Estonia)              et_EE
//   Finnish (Finland)               fi_FI
//   French (France)                 fr_FR
//   Italiano (Italia)               it_IT
//   Dutch (Belgium)                 nl_BE
//   Portuguese (Brazil)             pt_BR
//   Spanish (Spain)                 es_ES
#define LOCALE en_US

// UNITS
// Define exactly one macro for each measurement type below.

// UNITS - TEMPERATURE
//   Metric   : Celsius
//   Imperial : Fahrenheit
// #define UNITS_TEMP_KELVIN
// #define UNITS_TEMP_CELSIUS
#define UNITS_TEMP_FAHRENHEIT

// UNITS - WIND SPEED
//   Metric   : Kilometers per Hour
//   Imperial : Miles per Hour
// #define UNITS_SPEED_METERSPERSECOND
// #define UNITS_SPEED_FEETPERSECOND
// #define UNITS_SPEED_KILOMETERSPERHOUR
#define UNITS_SPEED_MILESPERHOUR
// #define UNITS_SPEED_KNOTS
// #define UNITS_SPEED_BEAUFORT

// UNITS - PRESSURE
//   Metric   : Millibars
//   Imperial : Inches of Mercury
// #define UNITS_PRES_HECTOPASCALS
// #define UNITS_PRES_PASCALS
// #define UNITS_PRES_MILLIMETERSOFMERCURY
// #define UNITS_PRES_INCHESOFMERCURY
#define UNITS_PRES_MILLIBARS
// #define UNITS_PRES_ATMOSPHERES
// #define UNITS_PRES_GRAMSPERSQUARECENTIMETER
// #define UNITS_PRES_POUNDSPERSQUAREINCH

// UNITS - VISIBILITY DISTANCE
//   Metric   : Kilometers
//   Imperial : Miles
// #define UNITS_DIST_KILOMETERS
#define UNITS_DIST_MILES

// UNITS - PRECIPITATION (HOURLY)
// The hourly outlook graph's bars. weather.gov's hourly forecast carries only
// Probability of Precipitation; its amounts exist solely in the raw gridpoint
// as 6-hour buckets, which would draw as four flat steps a day -- so PoP is
// the only hourly option.
#define UNITS_HOURLY_PRECIP_POP
// #define UNITS_HOURLY_PRECIP_MILLIMETERS  // unsupported, see above
// #define UNITS_HOURLY_PRECIP_CENTIMETERS  // unsupported, see above
// #define UNITS_HOURLY_PRECIP_INCHES       // unsupported, see above

// UNITS - PRECIPITATION (DAILY)
// The number under each day in the forecast row: probability, or an amount.
// Amounts are liquid-equivalent totals from two sources -- weather.gov's
// gridpoint QPF for the ~3 days it reaches (today counts only what is still
// ahead), then Open-Meteo's daily sum for the days beyond -- see precip.h.
// Selecting an amount adds one ~230 KB weather.gov request per refresh.
// #define UNITS_DAILY_PRECIP_POP
// #define UNITS_DAILY_PRECIP_MILLIMETERS
// #define UNITS_DAILY_PRECIP_CENTIMETERS
#define UNITS_DAILY_PRECIP_INCHES

// Derived: the daily row shows amounts, so fetch them.
#if !defined(UNITS_DAILY_PRECIP_POP)
  #define DAILY_PRECIP_AMOUNTS 1
#else
  #define DAILY_PRECIP_AMOUNTS 0
#endif

// Hypertext Transfer Protocol (HTTP)
// HTTP
//   HTTP does not provide encryption or any security measures, making it highly
//   vulnerable to eavesdropping and data tampering. Has the advantage of using
//   less power.
// HTTPS_NO_CERT_VERIF
//   HTTPS without X.509 certificate verification provides encryption but lacks
//   authentication and is susceptible to man-in-the-middle attacks.
// HTTPS_WITH_CERT_VERIF
//   HTTPS with X.509 certificate verification offers the highest level of
//   security by providing encryption and verifying the identity of the server.
//
//   HTTPS with X.509 certificate verification comes with the draw back that
//   eventually the certificates on the esp32 will expire, requiring you to
//   update the certificates in cert.h and reflash this software.
//   cert.h holds the root CAs of every host the firmware talks to (ISRG
//   Root X1, Amazon Root CA 1, GTS Root R1); the first to expire does so
//   on 2035-06-04. A server that cannot be verified is not talked to: the
//   request fails like any other failed request.
//
//   weather.gov and Open-Meteo are HTTPS-only, so USE_HTTP is not a valid
//   option for this project.
//
// Verification is the default. To build without it, do not edit this file:
// create include/config_local.h (git ignores it, so the choice stays on
// your machine and survives updates) containing the one line
//   #define USE_HTTPS_NO_CERT_VERIF
#if __has_include("config_local.h")
  #include "config_local.h"
#endif
#if !defined(USE_HTTP) && !defined(USE_HTTPS_NO_CERT_VERIF) \
 && !defined(USE_HTTPS_WITH_CERT_VERIF)
  #define USE_HTTPS_WITH_CERT_VERIF
#endif

// WIND DIRECTION INDICATOR
// Choose whether the wind direction indicator should be an arrow, number, or
// expressed in Compass Point Notation (CPN).
// The arrow indicator can be combined with NUMBER or CPN.
//
//   PRECISION                  #     ERROR   EXAMPLE
//   Cardinal                   4  ±45.000°   E
//   Intercardinal (Ordinal)    8  ±22.500°   NE
//   Secondary Intercardinal   16  ±11.250°   NNE
//   Tertiary Intercardinal    32   ±5.625°   NbE
#define WIND_INDICATOR_ARROW
// #define WIND_INDICATOR_NUMBER
// #define WIND_INDICATOR_CPN_CARDINAL
// #define WIND_INDICATOR_CPN_INTERCARDINAL
// #define WIND_INDICATOR_CPN_SECONDARY_INTERCARDINAL
// #define WIND_INDICATOR_CPN_TERTIARY_INTERCARDINAL
// #define WIND_INDICATOR_NONE

// WIND DIRECTION ICON PRECISION
// The wind direction icon shown to the left of the wind speed can indicate wind
// direction with a minimum error of ±0.5°. This uses more flash storage because
// 360 24x24 wind direction icons must be stored, totaling ~25kB. For either
// preference or in case flash space becomes a concern there are a handful of
// selectable options listed below. 360 points seems excessive, but the option
// is there.
//
//   PRECISION                  #     ERROR  STORAGE
//   Cardinal                   4  ±45.000°     288B  E
//   Intercardinal (Ordinal)    8  ±22.500°     576B  NE
//   Secondary Intercardinal   16  ±11.250°   1,152B  NNE
//   Tertiary Intercardinal    32   ±5.625°   2,304B  NbE
//   (360)                    360   ±0.500°  25,920B  1°
// Uncomment your preferred wind level direction precision.
// #define WIND_ICONS_CARDINAL
// #define WIND_ICONS_INTERCARDINAL
#define WIND_ICONS_SECONDARY_INTERCARDINAL
// #define WIND_ICONS_TERTIARY_INTERCARDINAL
// #define WIND_ICONS_360

// WIDGET POSITIONS
// The order of current condition widgets you want to display is configured
// at runtime in data/config.json ("widget_positions"), not here, so the
// layout can be changed without recompiling. See config.cpp for the
// compiled-in fallback values used when config.json is missing that key.
// Grid layout:
//  0   1
//  2   3
//  4   5
//  6   7
//  8   9
// if DISP_BW_V1 is used, 6,7,8,9 are not available
// Widgets: sunrise, sunset, wind, humidity, dewpoint, uvi, pressure,
// air_quality (alternative: visibility), intemp, inhumidity. A widget
// assigned position -1 is disabled.
// weather.gov does not report sunrise/sunset, so they are computed locally
// from your latitude/longitude and the date (see sun.h) -- no API needed.
// Moon data (moonrise/moonset/phase) is not available and those widgets have
// been removed.


// FONTS
// A handful of popular Open Source typefaces are included with this project.
// Every family switched on here is compiled into the firmware and can then
// be chosen at runtime: config.json "font" (and "font_small" for the
// labels, axis text and tags under 11 pt), or the Font dropdowns in the
// portal (with a live preview). Each family costs roughly 150-250 KB of
// flash, so boards with a 4 MB flash (FireBeetle) have room for one or two;
// the reTerminal E1002 (8 MB) can carry several.
//
//   FONT           FONT_INCLUDE_ NAME       FAMILY          LICENSE
//   Bitter         Bitter                   Bitter          SIL OFL v1.1
//   FreeMono       FreeMono                 GNU FreeFont    GNU GPL v3.0
//   FreeSans       FreeSans                 GNU FreeFont    GNU GPL v3.0
//   FreeSerif      FreeSerif                GNU FreeFont    GNU GPL v3.0
//   Lato           Lato_Regular             Lato            SIL OFL v1.1
//   Montserrat     Montserrat_Regular       Montserrat      SIL OFL v1.1
//   Open Sans      OpenSans_Regular         Open Sans       SIL OFL v1.1
//   Poppins        Poppins_Regular          Poppins         SIL OFL v1.1
//   Quicksand      Quicksand_Regular        Quicksand       SIL OFL v1.1
//   Raleway        Raleway_Regular          Raleway         SIL OFL v1.1
//   Roboto         Roboto_Regular           Roboto          Apache v2.0
//   Roboto Mono    RobotoMono_Regular       Roboto Mono     Apache v2.0
//   Roboto Slab    RobotoSlab_Regular       Roboto Slab     Apache v2.0
//   Ubuntu         Ubuntu_R                 Ubuntu font     UFL v1.0
//   Ubuntu Mono    UbuntuMono_R             Ubuntu font     UFL v1.0
//
// Adding a font: python tools/fontconvert.py <Name> <font.ttf>, then
// python tools/gen_font_table.py, then switch it on below.
//
// Note:
//   The layout of the display was designed around spacing and size of the GNU
//   FreeSans font, but this project supports the ability to modularly swap
//   fonts. Using a font other than FreeSans may result in undesired spacing or
//   other artifacts. An unknown or missing "font" setting falls back to the
//   first family switched on here.
#define FONT_INCLUDE_FreeSans 1
#if defined(BOARD_RETERMINAL_E1002)
  #define FONT_INCLUDE_Bitter 1
#endif

// FORECAST TEMPERATURE ORDER
// The order of temperture Hi|Lo can optionally be configured using
// the following options.
//   HL   : High | Low
//   LH   : Low | High
//
#define TEMP_ORDER_HL
// #define TEMP_ORDER_LH

// DAILY PRECIPITATION
// Daily precipitation indicated under Hi|Lo can optionally be configured using
// the following options.
//   0 : Disable (hide always)
//   1 : Enable (show always)
//   2 : Smart (show only when precipitation is forecasted)
#define DISPLAY_DAILY_PRECIP 2

// HOURLY WEATHER ICONS
// Weather icons to be displayed on the temperature and precipitation chart.
// They are drawn at the the x-axis tick marks just above the temperature line
//   0 : Disable
//   1 : Enable
#define DISPLAY_HOURLY_ICONS 1

// ALERTS
//   Alerts are sourced from weather.gov's /alerts/active endpoint (US only).
//   Disable alerts by changing the DISPLAY_ALERTS macro to 0.
#define DISPLAY_ALERTS 1

// STATUS BAR EXTRAS
//   Extra information that can be displayed on the status bar. Set to 1 to
//   enable.
#define STATUS_BAR_EXTRAS_BAT_PERCENTAGE 1
#define STATUS_BAR_EXTRAS_BAT_VOLTAGE    0
#define STATUS_BAR_EXTRAS_WIFI_STRENGTH  1
#define STATUS_BAR_EXTRAS_WIFI_RSSI      0

// BATTERY MONITORING
//   You may choose to power your weather display with or without a battery.
//   Low power behavior can be controlled in config.cpp.
//   If you wish to disable battery monitoring set this macro to 0.
#define BATTERY_MONITORING 1

// NON-VOLATILE STORAGE (NVS) NAMESPACE
#define NVS_NAMESPACE "weather_epd"

// GDEB0709E01 ORIENTATION (DISP_7C_709 only)
//   The panel counts its pixels in portrait, flex cable at the bottom; the
//   layout is landscape. 1 = the flex cable is on the viewer's right, which
//   is how the enclosure in enclosure/gdeb0709e01 holds it. 3 = on the left.
//   If the picture comes up upside down, this is the number to change.
#define EPD709_ROTATION 1

// WHAT THIS FIRMWARE WAS BUILT FOR
//   The build target's name and the panel, compiled into the image as one
//   line of text. The portal reads it out of a firmware file before
//   installing it, and refuses one built for a different board or panel.
#if defined(DISP_BW_V2)
  #define PANEL_NAME "DISP_BW_V2"
#elif defined(DISP_3C_B)
  #define PANEL_NAME "DISP_3C_B"
#elif defined(DISP_7C_F)
  #define PANEL_NAME "DISP_7C_F"
#elif defined(DISP_7C_E6)
  #define PANEL_NAME "DISP_7C_E6"
#elif defined(DISP_BW_V1)
  #define PANEL_NAME "DISP_BW_V1"
#elif defined(DISP_7C_709)
  #define PANEL_NAME "DISP_7C_709"
#else
  #define PANEL_NAME "unknown"
#endif
#ifndef BUILD_ENV
  #define BUILD_ENV "unknown" // set by platformio.ini
#endif
#define FIRMWARE_TARGET_PREFIX_LEN 13
#define FIRMWARE_TARGET "WGEPD-TARGET:" BUILD_ENV "/" PANEL_NAME ";"

// DEBUG
//   If defined, enables increase verbosity over the serial port.
//   level 0: basic status information, assists troubleshooting (default)
//   level 1: increased verbosity for debugging
//   level 2: print api responses to serial monitor
#define DEBUG_LEVEL 0

// Pins are fixed by your wiring, set them in "config.cpp".
// A pin set to PIN_UNUSED (0xFF) is skipped entirely (for boards where that
// signal is hardwired, e.g. the reTerminal E1002's always-on panel supply).
#define PIN_UNUSED 0xFF
extern const uint8_t PIN_BAT_ADC;
extern const uint8_t PIN_BAT_EN;
// Wake buttons (PIN_UNUSED on boards without them). Both are active-low
// deep-sleep wake sources: PORTAL wakes into the configuration portal,
// REFRESH wakes into an immediate weather refresh.
extern const uint8_t PIN_BTN_PORTAL;
extern const uint8_t PIN_BTN_REFRESH;
extern const uint8_t PIN_EPD_BUSY;
extern const uint8_t PIN_EPD_CS;
// Second chip select, for panels with two controllers (GDEB0709E01: the
// right half of the glass). PIN_UNUSED everywhere else.
extern const uint8_t PIN_EPD_CS2;
extern const uint8_t PIN_EPD_RST;
extern const uint8_t PIN_EPD_DC;
extern const uint8_t PIN_EPD_SCK;
extern const uint8_t PIN_EPD_MISO;
extern const uint8_t PIN_EPD_MOSI;
extern const uint8_t PIN_EPD_PWR;
// microSD slot control pins, PIN_UNUSED on boards whose SD slot (if any)
// does not share the ePaper SPI bus. See idleSDCard().
extern const uint8_t PIN_SD_EN;
extern const uint8_t PIN_SD_CS;
extern const uint8_t PIN_BME_SDA;
extern const uint8_t PIN_BME_SCL;
extern const uint8_t PIN_BME_PWR;
extern const uint8_t BME_ADDRESS;

// Everything below is a per-deployment setting (WiFi, location, time, battery
// thresholds, and widget layout). Fallback values live in "config.cpp", but
// at boot loadSettings() (settings.cpp) overwrites them with whatever is
// found in data/config.json on the device's LittleFS filesystem, so the
// project can be reconfigured by editing config.json and running
// `pio run --target uploadfs` -- no firmware recompile required.
extern char   WIFI_SSID[33];
extern char   WIFI_PASSWORD[65];
extern unsigned long WIFI_TIMEOUT;
// Optional static IP (skips DHCP, saving 2-4s per wake). All four must be
// set for it to take effect; leave STATIC_IP empty to use DHCP.
extern char STATIC_IP[16];
extern char STATIC_GATEWAY[16];
extern char STATIC_SUBNET[16];
extern char STATIC_DNS[16];
extern unsigned HTTP_CLIENT_TCP_TIMEOUT;
extern String NWS_USER_AGENT;
extern String AIRNOW_APIKEY;
extern String POLLEN_APIKEY;
extern String CURRENT_SOURCE;
extern String PORTAL_AP_PASSWORD;
extern int    PORTAL_TIMEOUT;
extern String LAT;
extern String LON;
extern String CITY_STRING;
extern char   TIMEZONE[64];
extern char   TIME_FORMAT[16];
extern char   HOUR_FORMAT[16];
extern char   DATE_FORMAT[32];
extern char   REFRESH_TIME_FORMAT[32];
extern char   NTP_SERVER_1[64];
extern char   NTP_SERVER_2[64];
extern unsigned long NTP_TIMEOUT;
extern int    SLEEP_DURATION;
extern int    WIFI_RETRY_INTERVAL;
extern int    OUTAGE_GRACE;
extern int    BED_TIME;
extern int    WAKE_TIME;
extern int    HOURLY_GRAPH_MAX;
extern bool   GRAPH_DEWPOINT;
extern int    FORECAST_DAYS;
extern int    WIDGET_ROWS;
extern bool   DARK_MODE;
extern int    FONT_FAMILY_INDEX;
extern int    FONT_SMALL_FAMILY_INDEX;
extern uint32_t WARN_BATTERY_VOLTAGE;
extern uint32_t LOW_BATTERY_VOLTAGE;
extern uint32_t VERY_LOW_BATTERY_VOLTAGE;
extern uint32_t CRIT_LOW_BATTERY_VOLTAGE;
extern unsigned long LOW_BATTERY_SLEEP_INTERVAL;
extern unsigned long VERY_LOW_BATTERY_SLEEP_INTERVAL;
extern uint32_t MAX_BATTERY_VOLTAGE;
extern uint32_t MIN_BATTERY_VOLTAGE;

// Widget grid positions (see WIDGET POSITIONS comment above). -1 disables a
// widget.
extern int POS_SUNRISE;
extern int POS_SUNSET;
extern int POS_WIND;
extern int POS_HUMIDITY;
extern int POS_DEWPOINT;
extern int POS_UVI;
extern int POS_PRESSURE;
extern int POS_AIR_QUALITY;
extern int POS_VISIBILITY;
extern int POS_MOON_PHASE;
extern int POS_POLLEN;
extern int POS_INTEMP;
extern int POS_INHUMIDITY;

// CONFIG VALIDATION - DO NOT MODIFY
#if !(  defined(DISP_BW_V2)  \
      ^ defined(DISP_3C_B)   \
      ^ defined(DISP_7C_F)   \
      ^ defined(DISP_7C_E6)  \
      ^ defined(DISP_BW_V1)  \
      ^ defined(DISP_BW_X3)  \
      ^ defined(DISP_7C_709))
  #error Invalid configuration. Exactly one display panel must be selected.
#endif
#if !(  defined(DRIVER_WAVESHARE) \
      ^ defined(DRIVER_DESPI_C02))
  #error Invalid configuration. Exactly one driver board must be selected.
#endif
#if !(  defined(SENSOR_BME280) \
      ^ defined(SENSOR_BME680) \
      ^ defined(SENSOR_SHT4X) \
      ^ defined(SENSOR_NONE))
  #error Invalid configuration. Exactly one sensor must be selected.
#endif
#if !(defined(LOCALE))
  #error Invalid configuration. Locale not selected.
#endif
#if !(  defined(UNITS_TEMP_KELVIN)      \
      ^ defined(UNITS_TEMP_CELSIUS)     \
      ^ defined(UNITS_TEMP_FAHRENHEIT))
  #error Invalid configuration. Exactly one temperature unit must be selected.
#endif
#if !(  defined(UNITS_SPEED_METERSPERSECOND)   \
      ^ defined(UNITS_SPEED_FEETPERSECOND)     \
      ^ defined(UNITS_SPEED_KILOMETERSPERHOUR) \
      ^ defined(UNITS_SPEED_MILESPERHOUR)      \
      ^ defined(UNITS_SPEED_KNOTS)             \
      ^ defined(UNITS_SPEED_BEAUFORT))
  #error Invalid configuration. Exactly one wind speed unit must be selected.
#endif
#if !(  defined(UNITS_PRES_HECTOPASCALS)             \
      ^ defined(UNITS_PRES_PASCALS)                  \
      ^ defined(UNITS_PRES_MILLIMETERSOFMERCURY)     \
      ^ defined(UNITS_PRES_INCHESOFMERCURY)          \
      ^ defined(UNITS_PRES_MILLIBARS)                \
      ^ defined(UNITS_PRES_ATMOSPHERES)              \
      ^ defined(UNITS_PRES_GRAMSPERSQUARECENTIMETER) \
      ^ defined(UNITS_PRES_POUNDSPERSQUAREINCH))
  #error Invalid configuration. Exactly one pressure unit must be selected.
#endif
#if !(  defined(UNITS_DIST_KILOMETERS) \
      ^ defined(UNITS_DIST_MILES))
  #error Invalid configuration. Exactly one distance unit must be selected.
#endif
#if !defined(UNITS_HOURLY_PRECIP_POP)
  #error Invalid configuration. weather.gov only provides probability of precipitation (PoP); UNITS_HOURLY_PRECIP_POP must be selected.
#endif
#if !(  defined(TEMP_ORDER_HL)      \
      ^ defined(TEMP_ORDER_LH))
  #error Invalid configuration. Exactly one temperature order must be selected.
#endif
#if !(  defined(UNITS_DAILY_PRECIP_POP)          \
      ^ defined(UNITS_DAILY_PRECIP_MILLIMETERS)  \
      ^ defined(UNITS_DAILY_PRECIP_CENTIMETERS)  \
      ^ defined(UNITS_DAILY_PRECIP_INCHES))
  #error Invalid configuration. Exactly one daily precipitation unit must be selected.
#endif
#if !(  defined(USE_HTTP)                   \
      ^ defined(USE_HTTPS_NO_CERT_VERIF)    \
      ^ defined(USE_HTTPS_WITH_CERT_VERIF))
  #error Invalid configuration. Exactly one HTTP mode must be selected.
#endif
#if defined(USE_HTTP)
  #error Invalid configuration. weather.gov and Open-Meteo require HTTPS; USE_HTTP is not supported.
#endif
#if !(  defined(WIND_INDICATOR_ARROW)                         \
      || (                                                    \
          defined(WIND_INDICATOR_NUMBER)                      \
        ^ defined(WIND_INDICATOR_CPN_CARDINAL)                \
        ^ defined(WIND_INDICATOR_CPN_INTERCARDINAL)           \
        ^ defined(WIND_INDICATOR_CPN_SECONDARY_INTERCARDINAL) \
        ^ defined(WIND_INDICATOR_CPN_TERTIARY_INTERCARDINAL)  \
      )                                                       \
      ^ defined(WIND_INDICATOR_NONE))
  #error Invalid configuration. Illegal selction of wind indicator(s).
#endif
#if defined(WIND_INDICATOR_ARROW)                   \
 && !(  defined(WIND_ICONS_CARDINAL)                \
      ^ defined(WIND_ICONS_INTERCARDINAL)           \
      ^ defined(WIND_ICONS_SECONDARY_INTERCARDINAL) \
      ^ defined(WIND_ICONS_TERTIARY_INTERCARDINAL)  \
      ^ defined(WIND_ICONS_360))
  #error Invalid configuration. Exactly one wind direction icon precision level must be selected.
#endif
#if !(defined(DISPLAY_DAILY_PRECIP))
  #error Invalid configuration. DISPLAY_DAILY_PRECIP not defined.
#endif
#if !(defined(DISPLAY_HOURLY_ICONS))
  #error Invalid configuration. DISPLAY_HOURLY_ICONS not defined.
#endif
#if !(defined(DISPLAY_ALERTS))
  #error Invalid configuration. DISPLAY_ALERTS not defined.
#endif
#if !(defined(BATTERY_MONITORING))
  #error Invalid configuration. BATTERY_MONITORING not defined.
#endif
#if !(defined(DEBUG_LEVEL))
  #error Invalid configuration. DEBUG_LEVEL not defined.
#endif

#endif
