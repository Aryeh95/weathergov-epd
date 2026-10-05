/* Locale data declarations for esp32-weather-epd.
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

#ifndef ___LOCALE_H__
#define ___LOCALE_H__

#include <vector>
#include <Arduino.h>
#include <aqi.h>

// LC_TIME
extern const char *LC_D_T_FMT;
extern const char *LC_D_FMT;
extern const char *LC_T_FMT;
extern const char *LC_T_FMT_AMPM;
extern const char *LC_AM_STR;
extern const char *LC_PM_STR;
extern const char *LC_DAY[7];
extern const char *LC_ABDAY[7];
extern const char *LC_MON[12];
extern const char *LC_ABMON[12];
extern const char *LC_ERA;
extern const char *LC_ERA_D_FMT;
extern const char *LC_ERA_D_T_FMT;
extern const char *LC_ERA_T_FMT;

// OWM LANGUAGE (also the language code sent to IMS: "en" or "he")
extern const String OWM_LANG;

// SCRIPT DIRECTION
// true for a right-to-left language (Hebrew). The renderer then transcodes
// UTF-8 text to the Hebrew font family's code page and reorders each line
// visually (renderer.cpp, shapeText); layout positions do not change.
extern const bool LC_RTL;

// HEBREW CALENDAR (_strftime's %K %J %L %N, hebcal.cpp)
// Month names, Tishrei first, with Adar, Adar I and Adar II separately;
// whether to write the day and year in Hebrew letters (gematria) rather
// than digits; and the preposition %L and %N put before the month ("ב" in
// Hebrew: כ"ד בתשרי, the form of running text; %K and %J leave it off, as
// calendars and datelines do: כ"ד תשרי).
extern const char *LC_HEB_MON[14];
extern const bool  LC_HEBCAL_LETTERS;
extern const char *LC_HEBCAL_MONTH_PREFIX;

// ISRAELI AIR QUALITY INDEX (Ministry of Environmental Protection)
// Its four bands, best first, as the Ministry's legend names and colours
// them: good (51-100, green), medium (0-50, yellow), low (-1..-200, red),
// very low (below -200, brown); and the widget's source tag.
extern const char *TXT_IL_AQI[4];
extern const char *TXT_IL_AQI_TAG;

// JEWISH HOLIDAYS (hebcal.cpp, _strftime's %Q, the sunset widget)
// 25 names in the order hebcal.cpp lists them: Rosh Hashanah, Tzom
// Gedaliah, Yom Kippur, Sukkot, Chol HaMoed Sukkot, Hoshana Rabbah, Simchat
// Torah, Hanukkah, Asara BeTevet, Tu BiShvat, Taanit Esther, Purim, Shushan
// Purim, Pesach, Chol HaMoed Pesach, Shvii shel Pesach, Yom HaShoah, Yom
// HaZikaron, Yom HaAtzmaut, Lag BaOmer, Yom Yerushalayim, Shavuot, Shiva
// Asar BeTammuz, Tisha BeAv, Tu BeAv.
extern const char *LC_HEB_HOLIDAYS[25];
// The sunset widget's labels on the eve and the close of Shabbat and holidays.
extern const char *TXT_CANDLE_LIGHTING;
extern const char *TXT_SHABBAT_ENDS;
extern const char *TXT_HOLIDAY_ENDS;
// Whether the sunset widget shows them by default (config.json "shabbat").
extern const bool LC_SHABBAT_TIMES;

// WIDGET DEFAULTS
// true where heat stress, not the dew point, is the everyday humidity
// measure (Israel): a config.json that does not place the heat stress
// widget gets it in the dew point's slot (settings.cpp).
extern const bool LC_PREFER_HEAT_STRESS;

// CURRENT CONDITIONS
extern const char *TXT_FEELS_LIKE;
extern const char *TXT_SUNRISE;
extern const char *TXT_SUNSET;
extern const char *TXT_MOONRISE;
extern const char *TXT_MOONSET;
extern const char *TXT_WIND;
extern const char *TXT_HUMIDITY;
extern const char *TXT_UV_INDEX;
extern const char *TXT_PRESSURE;
extern const char *TXT_AIR_QUALITY;
extern const char *TXT_AIR_POLLUTION;
extern const char *TXT_VISIBILITY;
extern const char *TXT_MORE_THAN;   // prefix of a value above the scale ("> 10 km")
extern const char *TXT_POLLEN;
extern const char *TXT_POLLEN_TREE;
extern const char *TXT_POLLEN_GRASS;
extern const char *TXT_POLLEN_WEED;
extern const char *TXT_POLLEN_ALL;
extern const char *TXT_INDOOR_TEMPERATURE;
extern const char *TXT_INDOOR_HUMIDITY;
extern const char *TXT_DEWPOINT;
extern const char *TXT_TEMPERATURE;

// MOON PHASE
extern const char *TXT_MOONPHASE;
extern const char *TXT_NEW_MOON;
extern const char *TXT_WAXING_CRESCENT;
extern const char *TXT_FIRST_QUARTER;
extern const char *TXT_WAXING_GIBBOUS;
extern const char *TXT_FULL_MOON;
extern const char *TXT_WANING_GIBBOUS;
extern const char *TXT_THIRD_QUARTER;
extern const char *TXT_WANING_CRESCENT;

// UV INDEX
extern const char *TXT_UV_LOW;
extern const char *TXT_UV_MODERATE;
extern const char *TXT_UV_HIGH;
extern const char *TXT_UV_VERY_HIGH;
extern const char *TXT_UV_EXTREME;

// HEAT STRESS (Israel Meteorological Service's discomfort index bands:
// none < 22, light 22-24, mild 24-26, moderate 26-28, heavy 28-30, extreme)
extern const char *TXT_HEAT_STRESS;
extern const char *TXT_HEAT_STRESS_LEVEL[6];

// WIFI
extern const char *TXT_WIFI_EXCELLENT;
extern const char *TXT_WIFI_GOOD;
extern const char *TXT_WIFI_FAIR;
extern const char *TXT_WIFI_WEAK;
extern const char *TXT_WIFI_NO_CONNECTION;

// UNIT SYMBOLS - TEMPERATURE
extern const char *TXT_UNITS_TEMP_KELVIN;
extern const char *TXT_UNITS_TEMP_CELSIUS;
extern const char *TXT_UNITS_TEMP_FAHRENHEIT;
// UNIT SYMBOLS - WIND SPEED
extern const char *TXT_UNITS_SPEED_METERSPERSECOND;
extern const char *TXT_UNITS_SPEED_FEETPERSECOND;
extern const char *TXT_UNITS_SPEED_KILOMETERSPERHOUR;
extern const char *TXT_UNITS_SPEED_MILESPERHOUR;
extern const char *TXT_UNITS_SPEED_KNOTS;
extern const char *TXT_UNITS_SPEED_BEAUFORT;
// UNIT SYMBOLS - PRESSURE
extern const char *TXT_UNITS_PRES_HECTOPASCALS;
extern const char *TXT_UNITS_PRES_PASCALS;
extern const char *TXT_UNITS_PRES_MILLIMETERSOFMERCURY;
extern const char *TXT_UNITS_PRES_INCHESOFMERCURY;
extern const char *TXT_UNITS_PRES_MILLIBARS;
extern const char *TXT_UNITS_PRES_ATMOSPHERES;
extern const char *TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER;
extern const char *TXT_UNITS_PRES_POUNDSPERSQUAREINCH;
// UNIT SYMBOLS - VISIBILITY DISTANCE
extern const char *TXT_UNITS_DIST_KILOMETERS;
extern const char *TXT_UNITS_DIST_MILES;
// UNIT SYMBOLS - PRECIPITATION
extern const char *TXT_UNITS_PRECIP_MILLIMETERS;
extern const char *TXT_UNITS_PRECIP_CENTIMETERS;
extern const char *TXT_UNITS_PRECIP_INCHES;

// MISCELLANEOUS MESSAGES
// Title Case
extern const char *TXT_LOW_BATTERY;
extern const char *TXT_NETWORK_NOT_AVAILABLE;
extern const char *TXT_TIME_SYNCHRONIZATION_FAILED;
extern const char *TXT_WIFI_CONNECTION_FAILED;
// WiFi failure details, shown on the 2nd line of the connection error screen
extern const char *TXT_WIFI_REASON_NO_AP_FOUND;
extern const char *TXT_WIFI_REASON_AUTH_FAIL;
extern const char *TXT_WIFI_REASON_ASSOC_FAIL;
extern const char *TXT_WIFI_REASON_BEACON_TIMEOUT;
extern const char *TXT_WIFI_REASON_TIMEOUT;
extern const char *TXT_WIFI_REASON_UNKNOWN;
// First Word Capitalized
extern const char *TXT_ATTEMPTING_HTTP_REQ;
extern const char *TXT_AWAKE_FOR;
extern const char *TXT_BATTERY_VOLTAGE;
extern const char *TXT_CONNECTING_TO;
extern const char *TXT_COULD_NOT_CONNECT_TO;
extern const char *TXT_ENTERING_DEEP_SLEEP_FOR;
extern const char *TXT_READING_FROM;
extern const char *TXT_FAILED;
extern const char *TXT_SUCCESS;
extern const char *TXT_UNKNOWN;
// All Lowercase
extern const char *TXT_NOT_FOUND;
extern const char *TXT_READ_FAILED;
// Complete 
extern const char *TXT_FAILED_TO_GET_TIME;
extern const char *TXT_HIBERNATING_INDEFINITELY_NOTICE;
extern const char *TXT_REFERENCING_OLDER_TIME_NOTICE;
extern const char *TXT_WAITING_FOR_SNTP;
extern const char *TXT_LOW_BATTERY_VOLTAGE;
extern const char *TXT_VERY_LOW_BATTERY_VOLTAGE;
extern const char *TXT_CRIT_LOW_BATTERY_VOLTAGE;

// ALERTS
extern const std::vector<String> ALERT_URGENCY;
// ALERT TERMINOLOGY
extern const std::vector<String> TERM_SMOG;
extern const std::vector<String> TERM_SMOKE;
extern const std::vector<String> TERM_FOG;
extern const std::vector<String> TERM_METEOR;
extern const std::vector<String> TERM_NUCLEAR;
extern const std::vector<String> TERM_BIOHAZARD;
extern const std::vector<String> TERM_EARTHQUAKE;
extern const std::vector<String> TERM_FIRE;
extern const std::vector<String> TERM_HEAT;
extern const std::vector<String> TERM_WINTER;
extern const std::vector<String> TERM_TSUNAMI;
extern const std::vector<String> TERM_LIGHTNING;
extern const std::vector<String> TERM_SANDSTORM;
extern const std::vector<String> TERM_FLOOD;
extern const std::vector<String> TERM_VOLCANO;
extern const std::vector<String> TERM_AIR_QUALITY;
extern const std::vector<String> TERM_TORNADO;
extern const std::vector<String> TERM_SMALL_CRAFT_ADVISORY;
extern const std::vector<String> TERM_GALE_WARNING;
extern const std::vector<String> TERM_STORM_WARNING;
extern const std::vector<String> TERM_HURRICANE_WARNING;
extern const std::vector<String> TERM_HURRICANE;
extern const std::vector<String> TERM_DUST;
extern const std::vector<String> TERM_STRONG_WIND;

// AIR QUALITY INDEX
extern "C" {
extern const aqi_scale_t AQI_SCALE;
extern const char *AUSTRALIA_AQI_TXT[6];
extern const char *CANADA_AQHI_TXT[4];
extern const char *EUROPEAN_UNION_CAQI_TXT[5];
extern const char *HONG_KONG_AQHI_TXT[5];
extern const char *INDIA_AQI_TXT[6];
extern const char *CHINA_AQI_TXT[6];
extern const char *SINGAPORE_PSI_TXT[5];
extern const char *SOUTH_KOREA_CAI_TXT[4];
extern const char *UNITED_KINGDOM_DAQI_TXT[4];
extern const char *UNITED_STATES_AQI_TXT[6];
}
/* Short forms of the United States AQI band names, same order.
 *
 * The left-panel widget draws the band name as a colored badge, and several
 * of the full names are wider than the column: a three-digit AQI leaves 59px
 * and "> 500" leaves 39, against 89px for "Very Unhealthy" at 7pt. The
 * renderer falls back to these when the full name will not fit at either
 * font size. Best-effort: where a short form still does not fit, the full
 * name wraps as plain text exactly as it did before these existed.
 */
extern const char *UNITED_STATES_AQI_SHORT_TXT[6];

// COMPASS POINT
extern const char *COMPASS_POINT_NOTATION[32];

// HTTP CLIENT ERRORS
extern const char *TXT_HTTPC_ERROR_CONNECTION_REFUSED;
extern const char *TXT_HTTPC_ERROR_SEND_HEADER_FAILED;
extern const char *TXT_HTTPC_ERROR_SEND_PAYLOAD_FAILED;
extern const char *TXT_HTTPC_ERROR_NOT_CONNECTED;
extern const char *TXT_HTTPC_ERROR_CONNECTION_LOST;
extern const char *TXT_HTTPC_ERROR_NO_STREAM;
extern const char *TXT_HTTPC_ERROR_NO_HTTP_SERVER;
extern const char *TXT_HTTPC_ERROR_TOO_LESS_RAM;
extern const char *TXT_HTTPC_ERROR_ENCODING;
extern const char *TXT_HTTPC_ERROR_STREAM_WRITE;
extern const char *TXT_HTTPC_ERROR_READ_TIMEOUT;

// HTTP RESPONSE STATUS CODES
// 1xx - Informational Responses
extern const char *TXT_HTTP_RESPONSE_100;
extern const char *TXT_HTTP_RESPONSE_101;
extern const char *TXT_HTTP_RESPONSE_102;
extern const char *TXT_HTTP_RESPONSE_103;
// 2xx - Successful Responses
extern const char *TXT_HTTP_RESPONSE_200;
extern const char *TXT_HTTP_RESPONSE_201;
extern const char *TXT_HTTP_RESPONSE_202;
extern const char *TXT_HTTP_RESPONSE_203;
extern const char *TXT_HTTP_RESPONSE_204;
extern const char *TXT_HTTP_RESPONSE_205;
extern const char *TXT_HTTP_RESPONSE_206;
extern const char *TXT_HTTP_RESPONSE_207;
extern const char *TXT_HTTP_RESPONSE_208;
extern const char *TXT_HTTP_RESPONSE_226;
// 3xx - Redirection Responses
extern const char *TXT_HTTP_RESPONSE_300;
extern const char *TXT_HTTP_RESPONSE_301;
extern const char *TXT_HTTP_RESPONSE_302;
extern const char *TXT_HTTP_RESPONSE_303;
extern const char *TXT_HTTP_RESPONSE_304;
extern const char *TXT_HTTP_RESPONSE_305;
extern const char *TXT_HTTP_RESPONSE_307;
extern const char *TXT_HTTP_RESPONSE_308;
// 4xx - Client Error Responses
extern const char *TXT_HTTP_RESPONSE_400;
extern const char *TXT_HTTP_RESPONSE_401;
extern const char *TXT_HTTP_RESPONSE_402;
extern const char *TXT_HTTP_RESPONSE_403;
extern const char *TXT_HTTP_RESPONSE_404;
extern const char *TXT_HTTP_RESPONSE_405;
extern const char *TXT_HTTP_RESPONSE_406;
extern const char *TXT_HTTP_RESPONSE_407;
extern const char *TXT_HTTP_RESPONSE_408;
extern const char *TXT_HTTP_RESPONSE_409;
extern const char *TXT_HTTP_RESPONSE_410;
extern const char *TXT_HTTP_RESPONSE_411;
extern const char *TXT_HTTP_RESPONSE_412;
extern const char *TXT_HTTP_RESPONSE_413;
extern const char *TXT_HTTP_RESPONSE_414;
extern const char *TXT_HTTP_RESPONSE_415;
extern const char *TXT_HTTP_RESPONSE_416;
extern const char *TXT_HTTP_RESPONSE_417;
extern const char *TXT_HTTP_RESPONSE_418;
extern const char *TXT_HTTP_RESPONSE_421;
extern const char *TXT_HTTP_RESPONSE_422;
extern const char *TXT_HTTP_RESPONSE_423;
extern const char *TXT_HTTP_RESPONSE_424;
extern const char *TXT_HTTP_RESPONSE_425;
extern const char *TXT_HTTP_RESPONSE_426;
extern const char *TXT_HTTP_RESPONSE_428;
extern const char *TXT_HTTP_RESPONSE_429;
extern const char *TXT_HTTP_RESPONSE_431;
extern const char *TXT_HTTP_RESPONSE_451;
// 5xx - Server Error Responses
extern const char *TXT_HTTP_RESPONSE_500;
extern const char *TXT_HTTP_RESPONSE_501;
extern const char *TXT_HTTP_RESPONSE_502;
extern const char *TXT_HTTP_RESPONSE_503;
extern const char *TXT_HTTP_RESPONSE_504;
extern const char *TXT_HTTP_RESPONSE_505;
extern const char *TXT_HTTP_RESPONSE_506;
extern const char *TXT_HTTP_RESPONSE_507;
extern const char *TXT_HTTP_RESPONSE_508;
extern const char *TXT_HTTP_RESPONSE_510;
extern const char *TXT_HTTP_RESPONSE_511;

// ARDUINOJSON DESERIALIZATION ERROR CODES
extern const char *TXT_DESERIALIZATION_ERROR_OK;
extern const char *TXT_DESERIALIZATION_ERROR_EMPTY_INPUT;
extern const char *TXT_DESERIALIZATION_ERROR_INCOMPLETE_INPUT;
extern const char *TXT_DESERIALIZATION_ERROR_INVALID_INPUT;
extern const char *TXT_DESERIALIZATION_ERROR_NO_MEMORY;
extern const char *TXT_DESERIALIZATION_ERROR_TOO_DEEP;

// WIFI STATUS
extern const char *TXT_WL_NO_SHIELD;
extern const char *TXT_WL_IDLE_STATUS;
extern const char *TXT_WL_NO_SSID_AVAIL;
extern const char *TXT_WL_SCAN_COMPLETED;
extern const char *TXT_WL_CONNECTED;
extern const char *TXT_WL_CONNECT_FAILED;
extern const char *TXT_WL_CONNECTION_LOST;
extern const char *TXT_WL_DISCONNECTED;

#endif

