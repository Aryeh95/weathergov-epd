/* Hebrew (Israel) locale data for esp32-weather-epd.
 * UTF-8. Right-to-left: the renderer transcodes these strings to the Hebrew
 * font family's ISO-8859-8 page and lays each line out right to left
 * (renderer.cpp, shapeText). Needs FONT_INCLUDE_Heebo (config.h).
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

#include "_locale.h"
#include <vector>
#include <Arduino.h>

// LC_TIME
// locale-based information,
// see https://man7.org/linux/man-pages/man3/nl_langinfo.3.html for more info.
// Note to Translators:
//   The LC_TIME definitions are included in the localedata/locales directory of
//   the glibc source tree, in files named after the locale codes and encoding
//   schemes. For example, the en_US locale data is stored in the
//   localedata/locales/en_US file, which contains the definitions for date and
//   time formats, month and day names, and other time-related settings for the
//   LC_TIME category.
//   https://www.gnu.org/software/libc/sources.html
// D_T_FMT                 string for formatting date and time
const char *LC_D_T_FMT     = "%a %d %b %Y %H:%M:%S %Z";
// LC_D_FMT                date format string
const char *LC_D_FMT       = "%d/%m/%y";
// T_FMT                   time format string
const char *LC_T_FMT       = "%H:%M:%S";
// T_FMT_AMPM              a.m. or p.m. time format string
const char *LC_T_FMT_AMPM  = "%I:%M:%S %p";
// AM_STR                  Ante Meridian affix
const char *LC_AM_STR      = "לפנה\"צ";
// PM_STR                  Post Meridian affix
const char *LC_PM_STR      = "אחה\"צ";
// DAY_{1-7}               name of the n-th day of the week
const char *LC_DAY[7]      = {"ראשון", "שני", "שלישי", "רביעי", "חמישי", "שישי", "שבת"};
// ABDAY_{1-7}             abbreviated name of the n-th day of the week
const char *LC_ABDAY[7]    = {"א'", "ב'", "ג'", "ד'", "ה'", "ו'", "ש'"};
// MON_{1-12}              name of the n-th month of the year
const char *LC_MON[12]     = {"ינואר", "פברואר", "מרץ", "אפריל", "מאי", "יוני",
                              "יולי", "אוגוסט", "ספטמבר", "אוקטובר", "נובמבר", "דצמבר"};
// ABMON_{1-12}            abbreviated name of the n-th month of the year
const char *LC_ABMON[12]   = {"ינו'", "פבר'", "מרץ", "אפר'", "מאי", "יוני",
                              "יולי", "אוג'", "ספט'", "אוק'", "נוב'", "דצמ'"};
// ERA                     era description segments
const char *LC_ERA         = "";
// ERA_D_FMT               era date format string
const char *LC_ERA_D_FMT   = "";
// ERA_D_T_FMT             era date and time format string
const char *LC_ERA_D_T_FMT = "";
// ERA_T_FMT               era time format string
const char *LC_ERA_T_FMT   = "";

// OWM LANGUAGE
// For full list of languages, see
// https://openweathermap.org/api/one-call-api#multi
// Note: "[only] The contents of the 'description' field will be translated."
const String OWM_LANG = "he";
const bool LC_RTL = true;

// CURRENT CONDITIONS
const char *TXT_FEELS_LIKE         = "מרגיש כמו";
const char *TXT_SUNRISE            = "זריחה";
const char *TXT_SUNSET             = "שקיעה";
const char *TXT_MOONRISE           = "זריחת ירח";
const char *TXT_MOONSET            = "שקיעת ירח";
const char *TXT_WIND               = "רוח";
const char *TXT_HUMIDITY           = "לחות";
const char *TXT_UV_INDEX           = "מדד UV";
const char *TXT_PRESSURE           = "לחץ אוויר";
const char *TXT_AIR_QUALITY        = "איכות אוויר";
const char *TXT_AIR_POLLUTION      = "זיהום אוויר";
const char *TXT_VISIBILITY         = "ראות";
const char *TXT_POLLEN             = "אבקנים";
// Pollen type driving the index, shown as a small tag beside the
// "Pollen" label. Keep these short -- they must fit the widget cell.
const char *TXT_POLLEN_TREE        = "עצים";
const char *TXT_POLLEN_GRASS       = "עשב";
const char *TXT_POLLEN_WEED        = "עשבייה";
const char *TXT_POLLEN_ALL         = "הכל";
const char *TXT_INDOOR_TEMPERATURE = "טמפרטורה";
const char *TXT_INDOOR_HUMIDITY    = "לחות";
const char *TXT_DEWPOINT           = "נקודת טל";
const char *TXT_TEMPERATURE        = "טמפרטורה";

// MOON PHASE
const char *TXT_MOONPHASE       = "מופע הירח";
const char *TXT_NEW_MOON        = "מולד";
const char *TXT_WAXING_CRESCENT = "סהר מתמלא";
const char *TXT_FIRST_QUARTER   = "רבע ראשון";
const char *TXT_WAXING_GIBBOUS  = "גיבן מתמלא";
const char *TXT_FULL_MOON       = "ירח מלא";
const char *TXT_WANING_GIBBOUS  = "גיבן מתמעט";
const char *TXT_THIRD_QUARTER   = "רבע אחרון";
const char *TXT_WANING_CRESCENT = "סהר מתמעט";

// UV INDEX
const char *TXT_UV_LOW       = "נמוך";
const char *TXT_UV_MODERATE  = "בינוני";
const char *TXT_UV_HIGH      = "גבוה";
const char *TXT_UV_VERY_HIGH = "גבוה מאוד";
const char *TXT_UV_EXTREME   = "קיצוני";

// HEAT STRESS (the IMS's own words for its six levels)
const char *TXT_HEAT_STRESS = "עומס חום";
const char *TXT_HEAT_STRESS_LEVEL[6] = {"ללא", "קל", "מתון", "בינוני", "כבד", "קיצוני"};

// WIFI
const char *TXT_WIFI_EXCELLENT     = "מצוין";
const char *TXT_WIFI_GOOD          = "טוב";
const char *TXT_WIFI_FAIR          = "סביר";
const char *TXT_WIFI_WEAK          = "חלש";
const char *TXT_WIFI_NO_CONNECTION = "אין חיבור";

// UNIT SYMBOLS - TEMPERATURE
const char *TXT_UNITS_TEMP_KELVIN     = "K";
const char *TXT_UNITS_TEMP_CELSIUS    = "°C";
const char *TXT_UNITS_TEMP_FAHRENHEIT = "°F";
// UNIT SYMBOLS - WIND SPEED
const char *TXT_UNITS_SPEED_METERSPERSECOND   = "m/s";
const char *TXT_UNITS_SPEED_FEETPERSECOND     = "ft/s";
const char *TXT_UNITS_SPEED_KILOMETERSPERHOUR = "km/h";
const char *TXT_UNITS_SPEED_MILESPERHOUR      = "mph";
const char *TXT_UNITS_SPEED_KNOTS             = "kt";
const char *TXT_UNITS_SPEED_BEAUFORT          = "";
// UNIT SYMBOLS - PRESSURE
const char *TXT_UNITS_PRES_HECTOPASCALS             = "hPa";
const char *TXT_UNITS_PRES_PASCALS                  = "Pa";
const char *TXT_UNITS_PRES_MILLIMETERSOFMERCURY     = "mmHg";
const char *TXT_UNITS_PRES_INCHESOFMERCURY          = "inHg";
const char *TXT_UNITS_PRES_MILLIBARS                = "mbar";
const char *TXT_UNITS_PRES_ATMOSPHERES              = "atm";
const char *TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER = "g/cm²";
const char *TXT_UNITS_PRES_POUNDSPERSQUAREINCH      = "lb/in²";
// UNITS SYMBOLS - VISIBILITY DISTANCE
const char *TXT_UNITS_DIST_KILOMETERS = "km";
const char *TXT_UNITS_DIST_MILES      = "mi";
// UNITS SYMBOLS - PRECIPITATION
const char *TXT_UNITS_PRECIP_MILLIMETERS = "mm";
const char *TXT_UNITS_PRECIP_CENTIMETERS = "cm";
const char *TXT_UNITS_PRECIP_INCHES      = "in";

// MISCELLANEOUS MESSAGES
// Title Case
const char *TXT_LOW_BATTERY = "סוללה חלשה";
const char *TXT_NETWORK_NOT_AVAILABLE = "הרשת אינה זמינה";
const char *TXT_TIME_SYNCHRONIZATION_FAILED = "סנכרון השעון נכשל";
const char *TXT_WIFI_CONNECTION_FAILED = "החיבור ל-WiFi נכשל";
// First Word Capitalized
const char *TXT_ATTEMPTING_HTTP_REQ = "Attempting HTTP request";
const char *TXT_AWAKE_FOR = "Awake for";
const char *TXT_BATTERY_VOLTAGE = "Battery voltage";
const char *TXT_CONNECTING_TO = "Connecting to";
const char *TXT_COULD_NOT_CONNECT_TO = "Could not connect to";
const char *TXT_ENTERING_DEEP_SLEEP_FOR = "Entering deep sleep for";
const char *TXT_READING_FROM = "Reading from";
const char *TXT_FAILED  = "נכשל";
const char *TXT_SUCCESS = "הצליח";
const char *TXT_UNKNOWN = "לא ידוע";
// All Lowercase
const char *TXT_NOT_FOUND = "לא נמצא";
const char *TXT_READ_FAILED = "הקריאה נכשלה";
// Complete Sentences
const char *TXT_FAILED_TO_GET_TIME = "קבלת השעה נכשלה!";
const char *TXT_HIBERNATING_INDEFINITELY_NOTICE = "נכנס לשינה ללא זמן יקיצה!";
const char *TXT_REFERENCING_OLDER_TIME_NOTICE = "סנכרון השעון לפני השינה נכשל, משתמש בשעה ישנה יותר.";
const char *TXT_WAITING_FOR_SNTP = "ממתין לסנכרון SNTP.";
const char *TXT_LOW_BATTERY_VOLTAGE = "מתח סוללה נמוך!";
const char *TXT_VERY_LOW_BATTERY_VOLTAGE = "מתח סוללה נמוך מאוד!";
const char *TXT_CRIT_LOW_BATTERY_VOLTAGE = "מתח סוללה נמוך באופן קריטי!";

// ALERTS
// The display can show up to 2 alerts, but alerts can be unpredictable in
// severity and number. If more than 2 alerts are active, the esp32 will attempt
// to interpret the urgency of each alert and prefer to display the most urgent
// and recently issued alerts of each event type. Depending on your region
// different keywords are used to convey the level of urgency.
//
// A vector array is used to store these keywords. Urgency is ranked from low to
// high where the first index of the vector is the least urgent keyword and the
// last index is the most urgent keyword. Expected as all lowercase.
//
// Note to Translators:
//   OpenWeatherMap returns alerts in English regardless of the OWM LANGUAGE
//   option or territory. For this reason it is preferred that you do not
//   translate text related to Alerts. Understandably, it may be undesirable to
//   see English alerts in territories where English is uncommon. To satisfy
//   this, users should set the DISPLAY_ALERTS macro to 0 in config.h.
//
// Here are a few examples, uncomment the array for your region (or create your
// own).
const std::vector<String> ALERT_URGENCY = {"ירוקה", "מוקדמת", "צהובה", "כתומה", "אדומה"}; // Israel Meteorological Service: early warning < yellow < orange < red
// const std::vector<String> ALERT_URGENCY = {"yellow", "amber", "red"};                 // United Kingdom's national weather service (MET Office)
// const std::vector<String> ALERT_URGENCY = {"minor", "moderate", "severe", "extreme"}; // METEO
// const std::vector<String> ALERT_URGENCY = {}; // Disable urgency interpretation (algorithm will fallback to only prefer the most recently issued alerts)

// ALERT TERMINOLOGY
// Weather terminology associated with each alert icon
const std::vector<String> TERM_SMOG =
    {"ערפיח"};
const std::vector<String> TERM_SMOKE =
    {"עשן"};
const std::vector<String> TERM_FOG =
    {"ערפל", "ערפילי", "ראות", "ענן נמוך"};
const std::vector<String> TERM_METEOR =
    {"מטאור"};
const std::vector<String> TERM_NUCLEAR =
    {"רדיואקטיבי", "קרינה"};
const std::vector<String> TERM_BIOHAZARD =
    {"כימי", "ביולוגי"};
const std::vector<String> TERM_EARTHQUAKE =
    {"רעידת אדמה"};
const std::vector<String> TERM_FIRE =
    {"שריפות", "שריפה"};
const std::vector<String> TERM_HEAT =
    {"חום", "טמפרטורות קיצוניות"};
const std::vector<String> TERM_WINTER =
    {"שלג", "קרה", "קרח", "התקרחות", "קור", "ברד"};
const std::vector<String> TERM_TSUNAMI =
    {"צונאמי", "ים גבוה", "רחצה", "גלים"};
const std::vector<String> TERM_LIGHTNING =
    {"רעמים", "ברקים", "סופות"};
const std::vector<String> TERM_SANDSTORM =
    {"אובך", "אבק", "חול"};
const std::vector<String> TERM_FLOOD =
    {"שטפונות", "הצפות", "גשם", "נחלים"};
const std::vector<String> TERM_VOLCANO =
    {"וולקני", "אפר"};
const std::vector<String> TERM_AIR_QUALITY =
    {"זיהום", "איכות אוויר"};
const std::vector<String> TERM_TORNADO =
    {"טורנדו"};
const std::vector<String> TERM_SMALL_CRAFT_ADVISORY =
    {"כלי שיט"};
const std::vector<String> TERM_GALE_WARNING =
    {"סערה בים"};
const std::vector<String> TERM_STORM_WARNING =
    {"סערה"};
const std::vector<String> TERM_HURRICANE_WARNING =
    {"רוחות קיצוניות"};
const std::vector<String> TERM_HURRICANE =
    {"הוריקן", "ציקלון"};
const std::vector<String> TERM_DUST =
    {"אבק", "חול"};
const std::vector<String> TERM_STRONG_WIND =
    {"רוח", "רוחות", "גזירת רוח"};

// AIR QUALITY INDEX
extern "C" {
// Seemingly every country uses a different scale for Air Quality Index (AQI).
// I have written a library to calculate many of the most popular AQI scales.
// Feel free to request the addition of a new AQI scale by opening an Issue.
// https://github.com/lmarzen/pollutant-concentration-to-aqi
// Please choose this region's preferred AQI scale.
//   AUSTRALIA_AQI        CANADA_AQHI          EUROPEAN_UNION_CAQI
//   HONG_KONG_AQHI       INDIA_AQI            CHINA_AQI
//   SINGAPORE_PSI        SOUTH_KOREA_CAI      UNITED_KINGDOM_DAQI
//   UNITED_STATES_AQI
const aqi_scale_t AQI_SCALE = UNITED_STATES_AQI;

const char *AUSTRALIA_AQI_TXT[6] =
{
  "Very Good",
  "Good",
  "Fair",
  "Poor",
  "Very Poor",
  "Hazardous",
};
const char *CANADA_AQHI_TXT[4] =
{
  "Low",
  "Moderate",
  "High",
  "Very High",
};
const char *EUROPEAN_UNION_CAQI_TXT[5] =
{
  "Very Low",
  "Low",
  "Medium",
  "High",
  "Very High",
};
const char *HONG_KONG_AQHI_TXT[5] =
{
  "Low",
  "Moderate",
  "High",
  "Very High",
  "Hazardous",
};
const char *INDIA_AQI_TXT[6] =
{
  "Good",
  "Satisfactory",
  "Moderate",
  "Poor",
  "Very Poor",
  "Severe",
};
const char *CHINA_AQI_TXT[6] =
{
  "Excellent",
  "Good",
  "Lightly Polluted",
  "Moderately Polluted",
  "Heavily Polluted",
  "Severely Polluted",
};
const char *SINGAPORE_PSI_TXT[5] =
{
  "Good",
  "Moderate",
  "Unhealthy",
  "Very Unhealthy",
  "Hazardous",
};
const char *SOUTH_KOREA_CAI_TXT[4] =
{
  "Good",
  "Medium",
  "Unhealthy",
  "Very Unhealthy",
};
const char *UNITED_KINGDOM_DAQI_TXT[4] =
{
  "Low",
  "Moderate",
  "High",
  "Very High",
};
const char *UNITED_STATES_AQI_TXT[6] =
{
  "טוב",
  "בינוני",
  "לא בריא לרגישים",
  "לא בריא",
  "לא בריא מאוד",
  "מסוכן",
};
} // end extern "C"
/* Short forms of the above, for the AQI widget's badge when the column is
 * too narrow for the full name (see _locale.h).
 */
const char *UNITED_STATES_AQI_SHORT_TXT[6] =
{
  "טוב",
  "בינוני",
  "לרגישים",
  "לא בריא",
  "לא בריא+",
  "מסוכן",
};


// COMPASS POINT
const char *COMPASS_POINT_NOTATION[32] = {
   "צ",   "צ/צמ",  "צצמ",  "צמ/צ",  "צמ",  "צמ/מ",  "מצמ",  "מ/צ",
   "מ",   "מ/ד",   "מדמ",  "דמ/מ",  "דמ",  "דמ/ד",  "דדמ",  "ד/מ",
   "ד",   "ד/מע",  "דדמע", "דמע/ד", "דמע", "דמע/מע","מעדמע","מע/ד",
   "מע",  "מע/צ",  "מעצמע","צמע/מע","צמע", "צמע/צ", "צצמע", "צ/מע",
};

// HTTP CLIENT ERRORS
const char *TXT_HTTPC_ERROR_CONNECTION_REFUSED   = "Connection Refused";
const char *TXT_HTTPC_ERROR_SEND_HEADER_FAILED   = "Send Header Failed";
const char *TXT_HTTPC_ERROR_SEND_PAYLOAD_FAILED  = "Send Payload Failed";
const char *TXT_HTTPC_ERROR_NOT_CONNECTED        = "Not Connected";
const char *TXT_HTTPC_ERROR_CONNECTION_LOST      = "Connection Lost";
const char *TXT_HTTPC_ERROR_NO_STREAM            = "No Stream";
const char *TXT_HTTPC_ERROR_NO_HTTP_SERVER       = "No HTTP Server";
const char *TXT_HTTPC_ERROR_TOO_LESS_RAM         = "Too Less Ram";
const char *TXT_HTTPC_ERROR_ENCODING             = "Transfer-Encoding Not Supported";
const char *TXT_HTTPC_ERROR_STREAM_WRITE         = "Stream Write Error";
const char *TXT_HTTPC_ERROR_READ_TIMEOUT         = "Read Timeout";

// HTTP RESPONSE STATUS CODES
// 1xx - Informational Responses
const char *TXT_HTTP_RESPONSE_100 = "Continue";
const char *TXT_HTTP_RESPONSE_101 = "Switching Protocols";
const char *TXT_HTTP_RESPONSE_102 = "Processing";
const char *TXT_HTTP_RESPONSE_103 = "Early Hints";
// 2xx - Successful Responses
const char *TXT_HTTP_RESPONSE_200 = "OK";
const char *TXT_HTTP_RESPONSE_201 = "Created";
const char *TXT_HTTP_RESPONSE_202 = "Accepted";
const char *TXT_HTTP_RESPONSE_203 = "Non-Authoritative Information";
const char *TXT_HTTP_RESPONSE_204 = "No Content";
const char *TXT_HTTP_RESPONSE_205 = "Reset Content";
const char *TXT_HTTP_RESPONSE_206 = "Partial Content";
const char *TXT_HTTP_RESPONSE_207 = "Multi-Status";
const char *TXT_HTTP_RESPONSE_208 = "Already Reported";
const char *TXT_HTTP_RESPONSE_226 = "IM Used";
// 3xx - Redirection Responses
const char *TXT_HTTP_RESPONSE_300 = "Multiple Choices";
const char *TXT_HTTP_RESPONSE_301 = "Moved Permanently";
const char *TXT_HTTP_RESPONSE_302 = "Found";
const char *TXT_HTTP_RESPONSE_303 = "See Other";
const char *TXT_HTTP_RESPONSE_304 = "Not Modified";
const char *TXT_HTTP_RESPONSE_305 = "Use Proxy";
const char *TXT_HTTP_RESPONSE_307 = "Temporary Redirect";
const char *TXT_HTTP_RESPONSE_308 = "Permanent Redirect";
// 4xx - Client Error Responses
const char *TXT_HTTP_RESPONSE_400 = "Bad Request";
const char *TXT_HTTP_RESPONSE_401 = "Unauthorized";
const char *TXT_HTTP_RESPONSE_402 = "Payment Required";
const char *TXT_HTTP_RESPONSE_403 = "Forbidden";
const char *TXT_HTTP_RESPONSE_404 = "Not Found";
const char *TXT_HTTP_RESPONSE_405 = "Method Not Allowed";
const char *TXT_HTTP_RESPONSE_406 = "Not Acceptable";
const char *TXT_HTTP_RESPONSE_407 = "Proxy Authentication Required";
const char *TXT_HTTP_RESPONSE_408 = "Request Timeout";
const char *TXT_HTTP_RESPONSE_409 = "Conflict";
const char *TXT_HTTP_RESPONSE_410 = "Gone";
const char *TXT_HTTP_RESPONSE_411 = "Length Required";
const char *TXT_HTTP_RESPONSE_412 = "Precondition Failed";
const char *TXT_HTTP_RESPONSE_413 = "Content Too Large";
const char *TXT_HTTP_RESPONSE_414 = "URI Too Long";
const char *TXT_HTTP_RESPONSE_415 = "Unsupported Media Type";
const char *TXT_HTTP_RESPONSE_416 = "Range Not Satisfiable";
const char *TXT_HTTP_RESPONSE_417 = "Expectation Failed";
const char *TXT_HTTP_RESPONSE_418 = "I'm a teapot";
const char *TXT_HTTP_RESPONSE_421 = "Misdirected Request";
const char *TXT_HTTP_RESPONSE_422 = "Unprocessable Content";
const char *TXT_HTTP_RESPONSE_423 = "Locked";
const char *TXT_HTTP_RESPONSE_424 = "Failed Dependency";
const char *TXT_HTTP_RESPONSE_425 = "Too Early";
const char *TXT_HTTP_RESPONSE_426 = "Upgrade Required";
const char *TXT_HTTP_RESPONSE_428 = "Precondition Required";
const char *TXT_HTTP_RESPONSE_429 = "Too Many Requests";
const char *TXT_HTTP_RESPONSE_431 = "Request Header Fields Too Large";
const char *TXT_HTTP_RESPONSE_451 = "Unavailable For Legal Reasons";
// 5xx - Server Error Responses
const char *TXT_HTTP_RESPONSE_500 = "Internal Server Error";
const char *TXT_HTTP_RESPONSE_501 = "Not Implemented";
const char *TXT_HTTP_RESPONSE_502 = "Bad Gateway";
const char *TXT_HTTP_RESPONSE_503 = "Service Unavailable";
const char *TXT_HTTP_RESPONSE_504 = "Gateway Timeout";
const char *TXT_HTTP_RESPONSE_505 = "HTTP Version Not Supported";
const char *TXT_HTTP_RESPONSE_506 = "Variant Also Negotiates";
const char *TXT_HTTP_RESPONSE_507 = "Insufficient Storage";
const char *TXT_HTTP_RESPONSE_508 = "Loop Detected";
const char *TXT_HTTP_RESPONSE_510 = "Not Extended";
const char *TXT_HTTP_RESPONSE_511 = "Network Authentication Required";

// ARDUINOJSON DESERIALIZATION ERROR CODES
const char *TXT_DESERIALIZATION_ERROR_OK               = "Deserialization OK";
const char *TXT_DESERIALIZATION_ERROR_EMPTY_INPUT      = "Deserialization Empty Input";
const char *TXT_DESERIALIZATION_ERROR_INCOMPLETE_INPUT = "Deserialization Incomplete Input";
const char *TXT_DESERIALIZATION_ERROR_INVALID_INPUT    = "Deserialization Invalid Input";
const char *TXT_DESERIALIZATION_ERROR_NO_MEMORY        = "Deserialization No Memory";
const char *TXT_DESERIALIZATION_ERROR_TOO_DEEP         = "Deserialization Too Deep";

// WIFI STATUS
const char *TXT_WL_NO_SHIELD       = "ללא מודול";
const char *TXT_WL_IDLE_STATUS     = "המתנה";
const char *TXT_WL_NO_SSID_AVAIL   = "אין רשת זמינה";
const char *TXT_WL_SCAN_COMPLETED  = "הסריקה הושלמה";
const char *TXT_WL_CONNECTED       = "מחובר";
const char *TXT_WL_CONNECT_FAILED  = "החיבור נכשל";
const char *TXT_WL_CONNECTION_LOST = "החיבור אבד";
const char *TXT_WL_DISCONNECTED    = "מנותק";

// WIFI FAILURE DETAILS
// Shown on the 2nd line of the connection error screen.
const char *TXT_WIFI_REASON_NO_AP_FOUND    = "הרשת לא נמצאה";
const char *TXT_WIFI_REASON_AUTH_FAIL      = "ייתכן שהסיסמה שגויה";
const char *TXT_WIFI_REASON_ASSOC_FAIL     = "הנתב דחה את החיבור";
const char *TXT_WIFI_REASON_BEACON_TIMEOUT = "אות חלש";
const char *TXT_WIFI_REASON_TIMEOUT        = "אין תגובה מהנתב";
const char *TXT_WIFI_REASON_UNKNOWN        = "Unknown Reason";
