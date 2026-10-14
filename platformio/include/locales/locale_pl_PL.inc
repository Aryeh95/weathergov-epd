/* Polish locale data for esp32-weather-epd.
 * UTF-8: the renderer transcodes these strings to the font family's
 * ISO-8859-2 page (rtl.cpp, shapeText), so the family must carry the
 * Polish letters: the pl_PL defaults file switches Lato_CE on.
 * Translation from upstream issue #159 (Windows-1250 escapes there),
 * extended for this fork's strings.
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
const char *LC_D_T_FMT     = "%a, %-d %b %Y, %T";
// LC_D_FMT                date format string
const char *LC_D_FMT       = "%d.%m.%Y";
// T_FMT                   time format string
const char *LC_T_FMT       = "%T";
// T_FMT_AMPM              a.m. or p.m. time format string
const char *LC_T_FMT_AMPM  = "";
// AM_STR                  Ante Meridian affix
const char *LC_AM_STR      = "";
// PM_STR                  Post Meridian affix
const char *LC_PM_STR      = "";
// DAY_{1-7}               name of the n-th day of the week
const char *LC_DAY[7]      = {"niedziela", "poniedziałek", "wtorek", "środa",
                              "czwartek", "piątek", "sobota"};
// ABDAY_{1-7}             abbreviated name of the n-th day of the week
const char *LC_ABDAY[7]    = {"nd", "pn", "wt", "śr", "cz", "pt", "sb"};
// MON_{1-12}              name of the n-th month of the year
const char *LC_MON[12]     = {"stycznia", "lutego", "marca", "kwietnia",
                               "maja", "czerwca", "lipca", "sierpnia",
                               "września", "października", "listopada", "grudnia"};
// ABMON_{1-12}            abbreviated name of the n-th month of the year
const char *LC_ABMON[12]   = {"sty", "lut", "mar", "kwi", "maj", "cze",
                              "lip", "sie", "wrz", "paź", "lis", "gru"};
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
const String OWM_LANG = "pl";
const bool LC_RTL = false;
const char *LC_HEB_MON[14] = {"Tiszri", "Cheszwan", "Kislew", "Tewet", "Szwat", "Adar", "Adar I",
                              "Adar II", "Nisan", "Ijar", "Siwan", "Tamuz", "Aw", "Elul"};
const bool  LC_HEBCAL_LETTERS = false;
const char *LC_HEBCAL_MONTH_PREFIX = "";
const bool  LC_PREFER_HEAT_STRESS = false;
const char *LC_HEB_HOLIDAYS[25] = {
  "Rosz ha-Szana", "Post Gedaliasza", "Jom Kipur", "Sukot", "Chol ha-Moed Sukot",
  "Hoszana Raba", "Simchat Tora", "Chanuka", "Asara be-Tewet", "Tu bi-Szwat",
  "Post Estery", "Purim", "Szuszan Purim", "Pesach", "Chol ha-Moed Pesach",
  "Siódmy dzień Pesach", "Jom ha-Szoa", "Jom ha-Zikaron", "Jom ha-Acmaut", "Lag ba-Omer",
  "Jom Jeruszalaim", "Szawuot", "Post 17 Tamuz", "Tisza be-Aw", "Tu be-Aw"};
const char *TXT_CANDLE_LIGHTING = "Zapalenie świec";
const char *TXT_SHABBAT_ENDS    = "Koniec szabatu";
const char *TXT_HOLIDAY_ENDS    = "Koniec święta";
const bool  LC_SHABBAT_TIMES = false;
const char *TXT_IL_AQI[4] = {"Dobra", "Średnia", "Niska", "Bardzo niska"};
const char *TXT_IL_AQI_TAG = "(MoEP)";
const char *TXT_AQI_TAG_EPA = "(EPA)";
const char *TXT_AQI_TAG_MODEL = "(model)";
const char *TXT_PORTAL_ON_WIFI = "Połączono z WiFi: ";
const char *TXT_PORTAL_OPEN = "Otwórz ";
const char *TXT_PORTAL_OR = "lub ";
const char *TXT_PORTAL_JOIN = "Dołącz do sieci WiFi: ";
const char *TXT_PORTAL_PASSWORD = "Hasło: ";
const char *TXT_PORTAL_THEN_OPEN = "Następnie otwórz ";
const char *TXT_PORTAL_PAUSED[3] = {"Konfiguracja wstrzymana, by oszczędzać baterię.", "Naciśnij przycisk reset (RST),", "aby ponownie uruchomić hotspot konfiguracji."};

// CURRENT CONDITIONS
const char *TXT_FEELS_LIKE         = "Odczuwalna";
const char *TXT_SUNRISE            = "Wschód słońca";
const char *TXT_SUNSET             = "Zachód słońca";
const char *TXT_MOONRISE           = "Wschód Księżyca";
const char *TXT_MOONSET            = "Zachód Księżyca";
const char *TXT_WIND               = "Wiatr";
const char *TXT_HUMIDITY           = "Wilgotność";
const char *TXT_UV_INDEX           = "Indeks UV";
const char *TXT_PRESSURE           = "Ciśnienie";
const char *TXT_AIR_QUALITY        = "Jakość powietrza";
const char *TXT_AIR_POLLUTION      = "Zanieczyszczenie";
const char *TXT_VISIBILITY         = "Widoczność";
const char *TXT_MORE_THAN          = "> ";
const char *TXT_POLLEN             = "Pyłki";
// Pollen type driving the index, shown as a small tag beside the
// "Pollen" label. Keep these short -- they must fit the widget cell.
const char *TXT_POLLEN_TREE        = "drzewa";
const char *TXT_POLLEN_GRASS       = "trawy";
const char *TXT_POLLEN_WEED        = "chwasty";
const char *TXT_POLLEN_ALL         = "wszystkie";
const char *TXT_INDOOR_TEMPERATURE = "Temperatura";
const char *TXT_INDOOR_HUMIDITY    = "Wilgotność";
const char *TXT_DEWPOINT           = "Punkt rosy";
const char *TXT_TEMPERATURE        = "Temperatura";

// MOON PHASE
const char *TXT_MOONPHASE       = "Faza Księżyca";
const char *TXT_NEW_MOON        = "Nów";
const char *TXT_WAXING_CRESCENT = "Przybywający sierp";
const char *TXT_FIRST_QUARTER   = "Pierwsza kwadra";
const char *TXT_WAXING_GIBBOUS  = "Przybywający garb";
const char *TXT_FULL_MOON       = "Pełnia";
const char *TXT_WANING_GIBBOUS  = "Ubywający garb";
const char *TXT_THIRD_QUARTER   = "Ostatnia kwadra";
const char *TXT_WANING_CRESCENT = "Ubywający sierp";

// UV INDEX
const char *TXT_UV_LOW       = "Niskie";
const char *TXT_UV_MODERATE  = "Umiarkowane";
const char *TXT_UV_HIGH      = "Wysokie";
const char *TXT_UV_VERY_HIGH = "Bardzo wysokie";
const char *TXT_UV_EXTREME   = "Ekstremalne";

// HEAT STRESS (Israel Meteorological Service discomfort-index levels)
const char *TXT_HEAT_STRESS = "Stres cieplny";
const char *TXT_HEAT_STRESS_LEVEL[6] = {"Brak", "Lekki", "Łagodny", "Umiarkowany", "Silny", "Skrajny"};

// WIFI
const char *TXT_WIFI_EXCELLENT     = "Doskonała";
const char *TXT_WIFI_GOOD          = "Bardzo dobra";
const char *TXT_WIFI_FAIR          = "Dobra";
const char *TXT_WIFI_WEAK          = "Słaba";
const char *TXT_WIFI_NO_CONNECTION = "Brak połączenia";

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
const char *TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER = "g/cm˛";
const char *TXT_UNITS_PRES_POUNDSPERSQUAREINCH      = "lb/in˛";
// UNITS SYMBOLS - VISIBILITY DISTANCE
const char *TXT_UNITS_DIST_KILOMETERS = "km";
const char *TXT_UNITS_DIST_MILES      = "mi";
// UNITS SYMBOLS - PRECIPITATION
const char *TXT_UNITS_PRECIP_MILLIMETERS = "mm";
const char *TXT_UNITS_PRECIP_CENTIMETERS = "cm";
const char *TXT_UNITS_PRECIP_INCHES      = "in";

// MISCELLANEOUS MESSAGES
// Title Case
const char *TXT_LOW_BATTERY = "Słaba bateria";
const char *TXT_NETWORK_NOT_AVAILABLE = "Sieć niedostępna";
const char *TXT_TIME_SYNCHRONIZATION_FAILED = "Niepowodzenie synchronizacji czasu";
const char *TXT_WIFI_CONNECTION_FAILED = "Nie udało się połączyć z Wi-Fi";
// First Word Capitalized
const char *TXT_ATTEMPTING_HTTP_REQ = "Próba żądania HTTP";
const char *TXT_AWAKE_FOR = "Aktywny od";
const char *TXT_BATTERY_VOLTAGE = "Napięcie akumulatora";
const char *TXT_CONNECTING_TO = "Łączenie z";
const char *TXT_COULD_NOT_CONNECT_TO = "Nie można połączyć się z";
const char *TXT_ENTERING_DEEP_SLEEP_FOR = "Wejście w głęboki sen na";
const char *TXT_READING_FROM = "Odczyt z";
const char *TXT_FAILED  = "Niepowodzenie";
const char *TXT_SUCCESS = "Sukces";
const char *TXT_UNKNOWN = "Nieznany";
// All Lowercase
const char *TXT_NOT_FOUND = "nie znaleziono";
const char *TXT_READ_FAILED = "odczyt nieudany";
// Complete Sentences
const char *TXT_FAILED_TO_GET_TIME = "Nie udało się zsynchronizować czasu!";
const char *TXT_HIBERNATING_INDEFINITELY_NOTICE = "Hibernacja bez wybudzania!";
const char *TXT_REFERENCING_OLDER_TIME_NOTICE = "Nie udało się zsynchronizować czasu przed hibernacją, odwołując się do starszego czasu.";
const char *TXT_WAITING_FOR_SNTP = "Oczekiwanie na synchronizację SNTP.";
const char *TXT_LOW_BATTERY_VOLTAGE = "Niskie napięcie akumulatora!";
const char *TXT_VERY_LOW_BATTERY_VOLTAGE = "Bardzo niskie napięcie akumulatora!";
const char *TXT_CRIT_LOW_BATTERY_VOLTAGE = "Krytycznie niskie napięcie akumulatora!";

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
const std::vector<String> ALERT_URGENCY = {"yellow", "amber", "red"}; // US National Weather Service
// const std::vector<String> ALERT_URGENCY = {"yellow", "amber", "red"};                 // United Kingdom's national weather service (MET Office)
// const std::vector<String> ALERT_URGENCY = {"minor", "moderate", "severe", "extreme"}; // METEO
// const std::vector<String> ALERT_URGENCY = {}; // Disable urgency interpretation (algorithm will fallback to only prefer the most recently issued alerts)

// ALERT TERMINOLOGY
// Weather terminology associated with each alert icon
const std::vector<String> TERM_SMOG =
    {"smog"};
const std::vector<String> TERM_SMOKE =
    {"smoke"};
const std::vector<String> TERM_FOG =
    {"fog", "haar"};
const std::vector<String> TERM_METEOR =
    {"meteor", "asteroid"};
const std::vector<String> TERM_NUCLEAR =
    {"nuclear", "ionizing radiation"};
const std::vector<String> TERM_BIOHAZARD =
    {"biohazard", "biological hazard"};
const std::vector<String> TERM_EARTHQUAKE =
    {"earthquake"};
const std::vector<String> TERM_FIRE =
    {"fire", "red flag"};
const std::vector<String> TERM_HEAT =
    {"heat"};
const std::vector<String> TERM_WINTER =
    {"blizzard", "winter", "ice", "icy", "snow", "sleet", "cold",
     "freezing rain", "wind chill", "freeze", "frost", "hail"};
const std::vector<String> TERM_TSUNAMI =
    {"tsunami", "surf"};
const std::vector<String> TERM_LIGHTNING =
    {"thunderstorm", "storm cell", "pulse storm", "squall line", "supercell",
     "lightning"};
const std::vector<String> TERM_SANDSTORM =
    {"sandstorm", "blowing dust", "dust storm"};
const std::vector<String> TERM_FLOOD =
    {"flood", "storm surge", "seiche", "swell", "high seas", "high tides",
     "tidal surge", "hydrologic"};
const std::vector<String> TERM_VOLCANO =
    {"volcanic", "ash", "volcano", "eruption"};
const std::vector<String> TERM_AIR_QUALITY =
    {"air", "stagnation", "pollution"};
const std::vector<String> TERM_TORNADO =
    {"tornado"};
const std::vector<String> TERM_SMALL_CRAFT_ADVISORY =
    {"small craft", "wind advisory"};
const std::vector<String> TERM_GALE_WARNING =
    {"gale"};
const std::vector<String> TERM_STORM_WARNING =
    {"storm warning"};
const std::vector<String> TERM_HURRICANE_WARNING =
    {"hurricane force wind", "extreme wind", "high wind"};
const std::vector<String> TERM_HURRICANE =
    {"hurricane", "tropical storm", "typhoon", "cyclone"};
const std::vector<String> TERM_DUST =
    {"dust", "sand"};
const std::vector<String> TERM_STRONG_WIND =
    {"wind"};

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
const aqi_scale_t AQI_SCALE = EUROPEAN_UNION_CAQI;

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
  "Bardzo niski",
  "Niski",
  "Średni",
  "Wysoki",
  "Bardzo wysoki",
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
  "Good",
  "Moderate",
  "Unhealthy for Sensitive Groups",
  "Unhealthy",
  "Very Unhealthy",
  "Hazardous",
};
} // end extern "C"
/* Short forms of the above, for the AQI widget's badge when the column is
 * too narrow for the full name (see _locale.h).
 */
const char *UNITED_STATES_AQI_SHORT_TXT[6] =

{
  "Dobra",
  "Umiark.",
  "Dla wrażl.",
  "Niezdrowa",
  "B. niezdrowa",
  "Niebezp.",
};


// COMPASS POINT
const char *COMPASS_POINT_NOTATION[32] = {
//   0°   11.25°   22.5°   33.75°   45°   56.25°   67.5°   78.75°
   "N",  "NbE",  "NNE",   "NEbN",  "NE", "NEbE",  "ENE",  "EbN",
//  90°  101.25°  112.5°  123.75°  135°  146.25°  157.5°  168.75°
   "E",  "EbS",   "ESE",  "SEbE",  "SE", "SEbS",  "SSE",  "SbE",
// 180°  191.25°  202.5°  213.75°  225°  236.25°  247.5°  258.75°
   "S",  "SbW",   "SSW",  "SWbS",  "SW", "SWbW",  "WSW",  "WbS",
// 270°  281.25°  292.5°  303.75°  315°  326.25°  337.5°  348.75°
   "W",  "WbN",   "WNW",  "NWbW",  "NW", "NWbN",  "NNW",  "NbW",
};

// HTTP CLIENT ERRORS
const char *TXT_HTTPC_ERROR_CONNECTION_REFUSED   = "Połączenie odrzucone";
const char *TXT_HTTPC_ERROR_SEND_HEADER_FAILED   = "Wysyłanie nagłówka nie powiodło się";
const char *TXT_HTTPC_ERROR_SEND_PAYLOAD_FAILED  = "Wysyłanie danych nie powiodło się";
const char *TXT_HTTPC_ERROR_NOT_CONNECTED        = "Nie połączono";
const char *TXT_HTTPC_ERROR_CONNECTION_LOST      = "Połączenie utracone";
const char *TXT_HTTPC_ERROR_NO_STREAM            = "Brak transmisji";
const char *TXT_HTTPC_ERROR_NO_HTTP_SERVER       = "Brak serwera HTTP";
const char *TXT_HTTPC_ERROR_TOO_LESS_RAM         = "Za mało pamięci RAM";
const char *TXT_HTTPC_ERROR_ENCODING             = "Kodowanie transferu nie jest obsługiwane";
const char *TXT_HTTPC_ERROR_STREAM_WRITE         = "Błąd zapisu transmisji";
const char *TXT_HTTPC_ERROR_READ_TIMEOUT         = "Przekroczono limit czasu odczytu";

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
const char *TXT_DESERIALIZATION_ERROR_OK               = "Deserializacja OK";
const char *TXT_DESERIALIZATION_ERROR_EMPTY_INPUT      = "Deserializacja Puste dane wejściowe";
const char *TXT_DESERIALIZATION_ERROR_INCOMPLETE_INPUT = "Deserializacja Niekompletne dane wejściowe";
const char *TXT_DESERIALIZATION_ERROR_INVALID_INPUT    = "Deserializacja Nieprawidłowe dane wejściowe";
const char *TXT_DESERIALIZATION_ERROR_NO_MEMORY        = "Deserializacja Brak pamięci";
const char *TXT_DESERIALIZATION_ERROR_TOO_DEEP         = "Deserializacja Za głęboko";

// WIFI STATUS
const char *TXT_WL_NO_SHIELD       = "Brak WiFi Shield";
const char *TXT_WL_IDLE_STATUS     = "WiFi Bezczynny";
const char *TXT_WL_NO_SSID_AVAIL   = "Brak dostępnego SSID";
const char *TXT_WL_SCAN_COMPLETED  = "Skanowanie WiFI ukończone";
const char *TXT_WL_CONNECTED       = "Połączony z WiFI";
const char *TXT_WL_CONNECT_FAILED  = "Połączenie z WiFi nieudane";
const char *TXT_WL_CONNECTION_LOST = "Połączenie z WiFi utracone";
const char *TXT_WL_DISCONNECTED    = "Rozłączony z WiFi";

// WIFI FAILURE DETAILS
// Shown on the 2nd line of the connection error screen.
const char *TXT_WIFI_REASON_NO_AP_FOUND    = "Nie znaleziono sieci";
const char *TXT_WIFI_REASON_AUTH_FAIL      = "Prawdopodobnie błędne hasło";
const char *TXT_WIFI_REASON_ASSOC_FAIL     = "Router odrzucił połączenie";
const char *TXT_WIFI_REASON_BEACON_TIMEOUT = "Słaby sygnał";
const char *TXT_WIFI_REASON_TIMEOUT        = "Brak odpowiedzi routera";
const char *TXT_WIFI_REASON_UNKNOWN        = "Nieznana przyczyna";
