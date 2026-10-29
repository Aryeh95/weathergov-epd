/* API response deserialization for esp32-weather-epd.
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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <vector>
#include <ArduinoJson.h>
#include "api_response.h"
#include "config.h"
#include "_locale.h"
#include "conversions.h"

/* Converts a proleptic Gregorian civil date to the number of days since the
 * Unix epoch (1970-01-01). Implementation of Howard Hinnant's well known
 * "days_from_civil" algorithm, used here in place of timegm() (not part of
 * the standard library used by this toolchain).
 */
static int64_t daysFromCivil(int y, int m, int d)
{
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);          // [0, 399]
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
} // end daysFromCivil

/* Parses an ISO8601 timestamp as used throughout the weather.gov API, ex.
 * "2025-08-10T06:00:00-04:00", and returns Unix time, UTC.
 *
 * Returns 0 if the string could not be parsed.
 */
static int64_t parseISO8601(const String &s)
{
  if (s.length() < 19)
  {
    return 0;
  }

  int year, mon, day, hour, min, sec;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d",
            &year, &mon, &day, &hour, &min, &sec) != 6)
  {
    return 0;
  }

  int offSign = 1, offHour = 0, offMin = 0;
  if (s.length() >= 25
   && (s.charAt(19) == '+' || s.charAt(19) == '-'))
  {
    offSign = (s.charAt(19) == '-') ? -1 : 1;
    sscanf(s.c_str() + 20, "%d:%d", &offHour, &offMin);
  }

  int64_t utcSeconds = daysFromCivil(year, mon, day) * 86400LL
                      + hour * 3600LL + min * 60LL + sec;
  utcSeconds -= offSign * (offHour * 3600LL + offMin * 60LL);
  return utcSeconds;
} // end parseISO8601

/* Parses a weather.gov windSpeed string, ex. "10 mph" or "5 to 10 mph", and
 * returns the (higher, if a range) speed converted to meters/second.
 */
/* The temperature it feels like, as weather.gov works it out: the wind
 * chill at or below 50 F with a wind of 3 mph or more, the heat index at or
 * above 80 F, otherwise the temperature itself. For the hours of a source
 * that does not give it. Kelvin, % and m/s in, kelvin out.
 */
static float apparentTemperatureK(float tempK, int rh, float windMs)
{
  if (std::isnan(tempK))
  {
    return NAN;
  }
  const float t = kelvin_to_fahrenheit(tempK);
  const float v = meterspersecond_to_milesperhour(windMs);
  if (t <= 50.0f && v >= 3.0f)
  {
    const float p = std::pow(v, 0.16f);
    return fahrenheit_to_kelvin(35.74f + 0.6215f * t - 35.75f * p
                                + 0.4275f * t * p);
  }
  if (t >= 80.0f && rh > 0)
  {
    const float r = static_cast<float>(rh);
    float hi = 0.5f * (t + 61.0f + (t - 68.0f) * 1.2f + r * 0.094f);
    if ((hi + t) / 2.0f >= 80.0f)
    {
      hi = -42.379f + 2.04901523f * t + 10.14333127f * r
         - 0.22475541f * t * r - 0.00683783f * t * t
         - 0.05481717f * r * r + 0.00122874f * t * t * r
         + 0.00085282f * t * r * r - 0.00000199f * t * t * r * r;
      if (r < 13.0f && t <= 112.0f)
      {
        hi -= (13.0f - r) / 4.0f
              * std::sqrt((17.0f - std::fabs(t - 95.0f)) / 17.0f);
      }
      else if (r > 85.0f && t <= 87.0f)
      {
        hi += (r - 85.0f) / 10.0f * (87.0f - t) / 5.0f;
      }
    }
    return fahrenheit_to_kelvin(hi);
  }
  return tempK;
}

static float parseWindSpeedMph(const String &s)
{
  int firstNum = -1, secondNum = -1;
  int i = 0;
  int n = s.length();
  while (i < n)
  {
    if (isdigit(static_cast<unsigned char>(s.charAt(i))))
    {
      int start = i;
      while (i < n && isdigit(static_cast<unsigned char>(s.charAt(i))))
      {
        ++i;
      }
      int val = s.substring(start, i).toInt();
      if (firstNum == -1)
      {
        firstNum = val;
      }
      else
      {
        secondNum = val;
        break;
      }
    }
    else
    {
      ++i;
    }
  }

  if (firstNum == -1)
  {
    return 0.f;
  }
  int mph = (secondNum != -1) ? std::max(firstNum, secondNum) : firstNum;
  return milesperhour_to_meterspersecond(static_cast<float>(mph));
} // end parseWindSpeedMph

/* Converts a 16-point compass direction abbreviation (ex. "NW") as reported
 * by weather.gov into meteorological degrees.
 */
static int compassToDegrees(const String &dir)
{
  static const char *DIRS[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  for (int i = 0; i < 16; ++i)
  {
    if (dir == DIRS[i])
    {
      return static_cast<int>(std::round(i * 22.5f));
    }
  }
  return 0;
} // end compassToDegrees

/* Splits a weather.gov icon URL, ex.
 * "https://api.weather.gov/icons/land/day/few,20?size=medium", into its
 * day/night segment and the first (current) condition code.
 */
static void parseIconUrl(const String &iconUrl, String &dayNight, String &code)
{
  dayNight = "day";
  code = "";
  int landIdx = iconUrl.indexOf("/land/");
  if (landIdx == -1)
  {
    return;
  }
  int start = landIdx + 6;
  int slash = iconUrl.indexOf('/', start);
  if (slash == -1)
  {
    return;
  }
  dayNight = iconUrl.substring(start, slash);

  int codeStart = slash + 1;
  int end = codeStart;
  int n = iconUrl.length();
  while (end < n
      && iconUrl.charAt(end) != ','
      && iconUrl.charAt(end) != '/'
      && iconUrl.charAt(end) != '?')
  {
    ++end;
  }
  code = iconUrl.substring(codeStart, end);
} // end parseIconUrl

/* Lookup table mapping weather.gov icon condition codes
 * (https://api.weather.gov/icons) to an approximate OpenWeatherMap-style
 * condition id (reused so the existing icon-selection logic in
 * display_utils.cpp does not need to change) and an estimated cloud cover
 * percentage (weather.gov does not report cloud cover numerically in its
 * forecast endpoints).
 */
struct NwsIconMapEntry { const char *code; int id; int clouds; };
static const NwsIconMapEntry NWS_ICON_MAP[] = {
  {"skc",              800,  0},
  {"wind_skc",         800,  5},
  {"few",              801, 15},
  {"wind_few",         801, 20},
  {"sct",              802, 35},
  {"wind_sct",         802, 40},
  {"bkn",              803, 70},
  {"wind_bkn",         803, 75},
  {"ovc",              804, 95},
  {"wind_ovc",         804, 95},
  {"snow",             601, 90},
  {"rain_snow",        616, 90},
  {"rain_sleet",       611, 90},
  {"snow_sleet",       611, 90},
  {"fzra",             511, 90},
  {"rain_fzra",        511, 90},
  {"snow_fzra",        511, 90},
  {"sleet",            611, 90},
  {"rain",             500, 90},
  {"rain_showers",     520, 60},
  {"rain_showers_hi",  520, 40},
  {"tsra",             211, 90},
  {"tsra_sct",         211, 70},
  {"tsra_hi",          210, 50},
  {"tornado",          781, 90},
  {"hurricane",        771, 95},
  {"tropical_storm",   771, 90},
  {"dust",             731, 30},
  {"smoke",            711, 30},
  {"haze",             721, 20},
  {"hot",              800,  5},
  {"cold",             800,  5},
  {"blizzard",         602, 95},
  {"fog",              741, 85},
};

/* Applies the NWS_ICON_MAP lookup, filling in a synthetic owm_weather_t (id +
 * day/night marker) and an estimated cloud cover percentage.
 */
static void applyNwsIcon(const String &code, const String &dayNight,
                         owm_weather_t &weather, int &clouds)
{
  int id = 804;   // default: overcast clouds
  int cl = 50;
  for (const NwsIconMapEntry &entry : NWS_ICON_MAP)
  {
    if (code == entry.code)
    {
      id = entry.id;
      cl = entry.clouds;
      break;
    }
  }
  weather.id   = id;
  weather.icon = (dayNight == "night") ? "n" : "d";
  clouds = cl;
} // end applyNwsIcon

/* Fetches the gridpoint forecast URLs for a given lat/lon from weather.gov's
 * /points endpoint.
 */
DeserializationError deserializeNWSPoints(WiFiClient &json, String &forecastUrl,
                                          String &forecastHourlyUrl)
{
  JsonDocument filter;
  filter["properties"]["forecast"]       = true;
  filter["properties"]["forecastHourly"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  forecastUrl       = doc["properties"]["forecast"]      .as<const char *>();
  forecastHourlyUrl = doc["properties"]["forecastHourly"].as<const char *>();

  return error;
} // end deserializeNWSPoints

/* Parses weather.gov's 12-hour period /forecast endpoint into up to
 * OWM_NUM_DAILY owm_daily_t entries, merging each day/night period pair into
 * a single day.
 */
DeserializationError deserializeNWSForecastDaily(WiFiClient &json,
                                                 owm_daily_t *daily)
{
  JsonDocument filter;
  JsonObject period = filter["properties"]["periods"][0].to<JsonObject>();
  period["isDaytime"]                          = true;
  period["startTime"]                          = true;
  period["temperature"]                        = true;
  period["probabilityOfPrecipitation"]["value"] = true;
  period["windSpeed"]                          = true;
  period["windDirection"]                      = true;
  period["icon"]                               = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    daily[i] = {};
    daily[i].temp.min = 1e6f;
    daily[i].temp.max = -1e6f;
  }

  String curDate;
  int dayIdx = -1;
  for (JsonObject p : doc["properties"]["periods"].as<JsonArray>())
  {
    String startTime = p["startTime"].as<const char *>();
    if (startTime.length() < 10 || p["temperature"].isNull())
    { // a period without a date or a temperature cannot be placed
      continue;
    }
    String date = startTime.substring(0, 10);
    if (date != curDate)
    {
      ++dayIdx;
      if (dayIdx >= OWM_NUM_DAILY)
      {
        break;
      }
      curDate = date;
      daily[dayIdx].dt = parseISO8601(startTime);
    }

    float tempK = fahrenheit_to_kelvin(p["temperature"].as<float>());
    daily[dayIdx].temp.min = std::min(daily[dayIdx].temp.min, tempK);
    daily[dayIdx].temp.max = std::max(daily[dayIdx].temp.max, tempK);

    JsonVariant popVar = p["probabilityOfPrecipitation"]["value"];
    float pop = popVar.isNull() ? 0.f : (popVar.as<float>() / 100.f);
    daily[dayIdx].pop = std::max(daily[dayIdx].pop, pop);

    bool isDaytime = p["isDaytime"].as<bool>();
    // prefer the daytime period's conditions for the icon/wind/clouds shown;
    // fall back to whatever period we saw first for this day (ex. a lone
    // "Tonight" period when the forecast is fetched late at night).
    if (isDaytime || daily[dayIdx].weather.icon.isEmpty())
    {
      String dayNight, code;
      parseIconUrl(p["icon"].as<const char *>(), dayNight, code);
      applyNwsIcon(code, dayNight, daily[dayIdx].weather, daily[dayIdx].clouds);
      daily[dayIdx].wind_speed = parseWindSpeedMph(p["windSpeed"].as<const char *>());
      daily[dayIdx].wind_gust  = daily[dayIdx].wind_speed;
      daily[dayIdx].wind_deg   = compassToDegrees(p["windDirection"].as<const char *>());
    }
  }

  // A reply that parses but holds no periods is not a forecast.
  if (dayIdx < 0)
  {
    return DeserializationError::EmptyInput;
  }
  // Days the reply did not reach: no date, no temperatures. (They used to
  // keep the +/-1e6 the search for min and max starts from.)
  for (int i = dayIdx + 1; i < OWM_NUM_DAILY; ++i)
  {
    daily[i].dt = 0;
    daily[i].temp.min = NAN;
    daily[i].temp.max = NAN;
  }

  return error;
} // end deserializeNWSForecastDaily

/* Parses weather.gov's hourly /forecast/hourly endpoint into up to
 * OWM_NUM_HOURLY owm_hourly_t entries.
 */
DeserializationError deserializeNWSForecastHourly(WiFiClient &json,
                                                  owm_hourly_t *hourly)
{
  JsonDocument filter;
  JsonObject period = filter["properties"]["periods"][0].to<JsonObject>();
  period["startTime"]                          = true;
  period["temperature"]                        = true;
  period["probabilityOfPrecipitation"]["value"] = true;
  // Without this line the filter drops dewpoint entirely and every hour
  // parses as NaN -- which is exactly what the graph showed the first time:
  // legend present, no curve, axis unchanged.
  period["dewpoint"]["value"]                  = true;
  period["relativeHumidity"]["value"]          = true;
  period["windSpeed"]                          = true;
  period["windDirection"]                      = true;
  period["icon"]                               = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  // Hours the reply does not reach stay empty, with dt == 0.
  for (int k = 0; k < OWM_NUM_HOURLY; ++k)
  {
    hourly[k] = {};
  }
  int i = 0;
  for (JsonObject p : doc["properties"]["periods"].as<JsonArray>())
  {
    if (p["startTime"].isNull() || p["temperature"].isNull())
    {
      continue;
    }
    hourly[i] = {};
    hourly[i].dt   = parseISO8601(p["startTime"].as<const char *>());
    hourly[i].temp = fahrenheit_to_kelvin(p["temperature"].as<float>());

    JsonVariant popVar = p["probabilityOfPrecipitation"]["value"];
    hourly[i].pop = popVar.isNull() ? 0.f : (popVar.as<float>() / 100.f);
    // amounts are only in the gridpoint; getNWSWeather spreads them in
    hourly[i].precip = NAN;

    // Always degC on the hourly endpoint (unitCode wmoUnit:degC), whatever
    // temperatureUnit says about the integer temperature beside it. NaN when
    // NWS has no value for the hour, so the graph can skip that segment
    // rather than plot a zero.
    JsonVariant dewVar = p["dewpoint"]["value"];
    hourly[i].dew_point = dewVar.isNull() ? NAN
                                          : celsius_to_kelvin(dewVar.as<float>());

    JsonVariant rhVar = p["relativeHumidity"]["value"];
    hourly[i].humidity = rhVar.isNull()
                       ? 0 : static_cast<int>(std::round(rhVar.as<float>()));

    String dayNight, code;
    parseIconUrl(p["icon"].as<const char *>(), dayNight, code);
    applyNwsIcon(code, dayNight, hourly[i].weather, hourly[i].clouds);

    hourly[i].wind_speed = parseWindSpeedMph(p["windSpeed"].as<const char *>());
    hourly[i].wind_gust  = hourly[i].wind_speed;
    hourly[i].wind_deg   = compassToDegrees(p["windDirection"].as<const char *>());
    hourly[i].feels_like = apparentTemperatureK(hourly[i].temp,
                                                hourly[i].humidity,
                                                hourly[i].wind_speed);

    if (i == OWM_NUM_HOURLY - 1)
    {
      break;
    }
    ++i;
  }

  if (hourly[0].dt <= 0)
  { // parsed, but no hours in it
    return DeserializationError::EmptyInput;
  }

  return error;
} // end deserializeNWSForecastHourly

/* Populates owm_current_t entirely from the first hourly forecast period
 * (weather.gov's period for the current hour). This is the "nws" current
 * conditions source, and the fallback when the chosen source could not be
 * fetched/parsed. The hourly forecast carries no pressure or visibility.
 */
void fillCurrentFromFallback(const owm_hourly_t &fallback, owm_current_t &current)
{
  current = {};
  current.dt         = time(nullptr);
  current.weather     = fallback.weather;
  current.temp        = fallback.temp;
  current.feels_like  = fallback.temp;
  current.wind_speed  = fallback.wind_speed;
  current.wind_gust   = fallback.wind_gust;
  current.wind_deg    = fallback.wind_deg;
  current.clouds      = fallback.clouds;
  current.humidity    = fallback.humidity;
  current.dew_point   = fallback.dew_point;
  // The hourly forecast carries neither of these. Mark them unavailable so
  // the widgets show "--" and the pressure history skips the sample (see
  // main.cpp), instead of displaying a fabricated 1013 hPa / 10 km.
  current.pressure    = 0;
  current.visibility  = -1;
} // end fillCurrentFromFallback

/* Parses weather.gov's raw gridpoint (/gridpoints/WFO/x,y) for its
 * quantitativePrecipitation series. The response is large (~230 KB -- every
 * grid variable for 7 days) but the filter keeps only the QPF buckets, so
 * the document stays a few hundred bytes. Values are mm over each bucket;
 * the bucket is an ISO 8601 interval "start/duration", e.g.
 * "2026-09-04T18:00:00+00:00/PT6H".
 *
 * With `series` set, the windGust (km/h), skyCover (%) and
 * apparentTemperature (degC) series too, which the hourly forecast lacks.
 * Their buckets are an hour to a few hours long, so only the next two days
 * are kept.
 */
static void readBuckets(JsonArrayConst values, std::vector<qpf_bucket_t> &out,
                        int64_t until)
{
  out.clear();
  for (JsonObjectConst b : values)
  {
    const char *vt = b["validTime"].as<const char *>();
    if (!vt)
    {
      continue;
    }
    String interval(vt);
    int slash = interval.indexOf('/');
    if (slash < 0)
    {
      continue;
    }
    qpf_bucket_t q;
    q.start   = parseISO8601(interval.substring(0, slash));
    q.seconds = parseIsoDurationSeconds(interval.c_str() + slash + 1);
    JsonVariantConst val = b["value"];
    if (val.isNull())
    { // unknown is not dry: leave the interval uncovered
      continue;
    }
    q.mm = val.as<float>();
    if (q.start > 0 && q.seconds > 0 && (until == 0 || q.start < until))
    {
      out.push_back(q);
    }
  }
}

DeserializationError deserializeNWSGridpointQPF(WiFiClient &json,
                                                std::vector<qpf_bucket_t> &qpf,
                                                grid_series_t *series)
{
  JsonDocument filter;
  JsonObject v = filter["properties"]["quantitativePrecipitation"]["values"][0]
                   .to<JsonObject>();
  v["validTime"] = true;
  v["value"]     = true;
  if (series)
  {
    for (const char *name : {"windGust", "skyCover", "apparentTemperature"})
    {
      JsonObject g = filter["properties"][name]["values"][0].to<JsonObject>();
      g["validTime"] = true;
      g["value"]     = true;
    }
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
  if (error)
  {
    return error;
  }

  readBuckets(doc["properties"]["quantitativePrecipitation"]["values"]
                .as<JsonArrayConst>(), qpf, 0);
  if (series)
  {
    const int64_t until = static_cast<int64_t>(time(nullptr)) + 50L * 3600L;
    JsonObjectConst pr = doc["properties"];
    readBuckets(pr["windGust"]["values"], series->gust, until);
    readBuckets(pr["skyCover"]["values"], series->sky, until);
    readBuckets(pr["apparentTemperature"]["values"], series->feel, until);
  }
  return error;
} // end deserializeNWSGridpointQPF

/* WMO weather interpretation codes, as Open-Meteo reports them
 * (https://open-meteo.com/en/docs#weather_variable_documentation), to the
 * OpenWeatherMap-style condition id the icon selection runs on, an
 * estimated cloud cover, and a short description (English, Hebrew and
 * Polish for those builds; other locales leave it blank, as the
 * weather.gov path does, and let the icon speak).
 */
struct WmoCodeMapEntry { int code; int id; int clouds; const char *en; const char *he; const char *pl; };
static const WmoCodeMapEntry WMO_CODE_MAP[] = {
  { 0, 800,  0, "Clear",                   "בהיר", "Bezchmurnie"},
  { 1, 801, 20, "Mainly clear",            "בהיר לרוב", "Przeważnie bezchmurnie"},
  { 2, 802, 50, "Partly cloudy",           "מעונן חלקית", "Częściowe zachmurzenie"},
  { 3, 804, 95, "Overcast",                "מעונן", "Pochmurno"},
  {45, 741, 85, "Fog",                     "ערפל", "Mgła"},
  {48, 741, 85, "Freezing fog",            "ערפל קופא", "Mgła osadzająca szadź"},
  {51, 300, 80, "Light drizzle",           "טפטוף קל", "Lekka mżawka"},
  {53, 301, 85, "Drizzle",                 "טפטוף", "Mżawka"},
  {55, 302, 90, "Heavy drizzle",           "טפטוף מוגבר", "Gęsta mżawka"},
  {56, 511, 90, "Freezing drizzle",        "טפטוף קופא", "Marznąca mżawka"},
  {57, 511, 90, "Freezing drizzle",        "טפטוף קופא", "Marznąca mżawka"},
  {61, 500, 85, "Light rain",              "גשם קל", "Słaby deszcz"},
  {63, 501, 90, "Rain",                    "גשם", "Deszcz"},
  {65, 502, 95, "Heavy rain",              "גשם חזק", "Ulewny deszcz"},
  {66, 511, 90, "Freezing rain",           "גשם קופא", "Marznący deszcz"},
  {67, 511, 95, "Freezing rain",           "גשם קופא", "Marznący deszcz"},
  {71, 600, 90, "Light snow",              "שלג קל", "Słaby śnieg"},
  {73, 601, 90, "Snow",                    "שלג", "Śnieg"},
  {75, 602, 95, "Heavy snow",              "שלג כבד", "Intensywny śnieg"},
  {77, 600, 85, "Snow grains",             "גרגרי שלג", "Ziarna śniegu"},
  {80, 520, 50, "Light showers",           "ממטרים קלים", "Słabe przelotne opady"},
  {81, 521, 65, "Showers",                 "ממטרים", "Przelotne opady"},
  {82, 522, 80, "Heavy showers",           "ממטרים חזקים", "Ulewne przelotne opady"},
  {85, 620, 70, "Snow showers",            "מטחי שלג", "Przelotny śnieg"},
  {86, 621, 80, "Heavy snow showers",      "מטחי שלג כבדים", "Intensywny przelotny śnieg"},
  {95, 211, 85, "Thunderstorm",            "סופת רעמים", "Burza"},
  {96, 211, 90, "Thunderstorm with hail",  "סופת רעמים וברד", "Burza z gradem"},
  {99, 212, 95, "Thunderstorm with hail",  "סופת רעמים וברד", "Burza z gradem"},
};

static void applyWmoCode(int code, bool isDay, owm_weather_t &weather,
                         int &clouds)
{
  int id = 804, cl = 50;
  const char *en = "", *he = "", *pl = "";
  for (const WmoCodeMapEntry &e : WMO_CODE_MAP)
  {
    if (e.code == code)
    {
      id = e.id;
      cl = e.clouds;
      en = e.en;
      he = e.he;
      pl = e.pl;
      break;
    }
  }
  weather.id = id;
  weather.icon = isDay ? "d" : "n";
  if (OWM_LANG == "he")
  {
    weather.description = he;
  }
  else if (OWM_LANG == "pl")
  {
    weather.description = pl;
  }
  else if (OWM_LANG == "en")
  {
    weather.description = en;
  }
  else
  {
    weather.description = "";
  }
  clouds = cl;
} // end applyWmoCode

/* Parses Open-Meteo's current-conditions block (/v1/forecast?current=...,
 * requested with wind_speed_unit=ms). Values are model output interpolated
 * to the configured coordinates, refreshed every ~15 minutes. Any missing
 * field falls back to the first hourly forecast period, mirroring
 * deserializeNWSObservation; weather_code maps to the condition id and
 * description via applyWmoCode.
 *
 * With wantCurrent == false only the daily block is parsed and `current`
 * is left untouched (used when CURRENT_SOURCE is another provider but the
 * forecast row still needs Open-Meteo's daily precipitation). With `gaps`
 * set, pressure_msl, visibility and current sky cover are also read into it,
 * whatever wantCurrent is -- the gap fill for providers whose own
 * current-conditions feed lacks them (IMS).
 */
DeserializationError deserializeOpenMeteoCurrent(WiFiClient &json,
                                                 const owm_hourly_t &fallback,
                                                 owm_current_t &current,
                                                 om_daily_precip_t &omDaily,
                                                 bool wantCurrent,
                                                 om_gaps_t *gaps)
{
  omDaily.n = 0;
  if (gaps)
  {
    gaps->pressure   = 0;
    gaps->visibility = -1;
    gaps->clouds     = -1;
    gaps->weather    = {};
  }
  // Filter like every NWS parser does: the response also carries the
  // request echo (latitude/longitude/elevation/timezone), generation time
  // and a `current_units` block that is never read. Only the `current`
  // block matters, and only the fields the request asked for.
  JsonDocument filter;
  JsonObject cur = filter["current"].to<JsonObject>();
  cur["temperature_2m"]       = true;
  cur["apparent_temperature"] = true;
  cur["relative_humidity_2m"] = true;
  cur["dew_point_2m"]         = true;
  cur["pressure_msl"]         = true;
  cur["cloud_cover"]          = true;
  cur["weather_code"]         = true;
  cur["is_day"]               = true;
  cur["visibility"]           = true;
  cur["wind_speed_10m"]       = true;
  cur["wind_gusts_10m"]       = true;
  cur["wind_direction_10m"]   = true;
  // Daily precipitation totals ride along on the same request (same host,
  // same endpoint): the days weather.gov's QPF does not reach fall back to
  // these -- see precip.h.
  filter["daily"]["time"]              = true;
  filter["daily"]["precipitation_sum"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif

  if (error)
  {
    if (wantCurrent)
    {
      fillCurrentFromFallback(fallback, current);
    }
    return error;
  }

  JsonArray dTime = doc["daily"]["time"].as<JsonArray>();
  JsonArray dSum  = doc["daily"]["precipitation_sum"].as<JsonArray>();
  for (size_t k = 0; k < dTime.size() && k < dSum.size()
                     && omDaily.n < OM_DAILY_MAX; ++k)
  {
    omDaily.time[omDaily.n] = dTime[k].as<int64_t>();
    JsonVariant v = dSum[k];
    omDaily.mm[omDaily.n] = v.isNull() ? NAN : v.as<float>();
    ++omDaily.n;
  }

  JsonObject c = doc["current"];
  const bool omIsDay = c["is_day"].isNull() ? (fallback.weather.icon != "n")
                                            : (c["is_day"].as<int>() != 0);
  if (gaps)
  {
    JsonVariant gp = c["pressure_msl"];
    JsonVariant gv = c["visibility"];
    JsonVariant gc = c["cloud_cover"];
    JsonVariant gw = c["weather_code"];
    if (!gp.isNull())
      gaps->pressure = static_cast<int>(std::round(gp.as<float>()));
    if (!gv.isNull())
      gaps->visibility = static_cast<int>(gv.as<float>());
    if (!gw.isNull())
    {
      int wmoClouds = 0;
      applyWmoCode(gw.as<int>(), omIsDay, gaps->weather, wmoClouds);
      gaps->clouds = wmoClouds;
    }
    if (!gc.isNull())
      gaps->clouds = static_cast<int>(std::round(gc.as<float>()));
  }

  if (!wantCurrent)
  {
    // Another source supplies the current conditions; this request only
    // carried the daily precipitation totals and/or the gap fill
    // (see finishWeather).
    return error;
  }

  current = {};
  current.dt      = time(nullptr);
  current.weather = fallback.weather;
  current.clouds  = fallback.clouds;

  JsonVariant codeVar = c["weather_code"];
  if (!codeVar.isNull())
  {
    applyWmoCode(codeVar.as<int>(), omIsDay, current.weather, current.clouds);
  }

  JsonVariant tempVar = c["temperature_2m"];
  current.temp = !tempVar.isNull() ? celsius_to_kelvin(tempVar.as<float>())
                                    : fallback.temp;

  JsonVariant feelVar = c["apparent_temperature"];
  current.feels_like = !feelVar.isNull()
                      ? celsius_to_kelvin(feelVar.as<float>())
                      : current.temp;

  JsonVariant humVar = c["relative_humidity_2m"];
  current.humidity = !humVar.isNull()
                    ? static_cast<int>(std::round(humVar.as<float>()))
                    : 0;

  JsonVariant dewVar = c["dew_point_2m"];
  current.dew_point = !dewVar.isNull() ? celsius_to_kelvin(dewVar.as<float>())
                                        : NAN;

  JsonVariant pressVar = c["pressure_msl"];
  current.pressure = !pressVar.isNull()
                    ? static_cast<int>(std::round(pressVar.as<float>()))
                    : 0;  // not available

  JsonVariant visVar = c["visibility"];
  current.visibility = !visVar.isNull()
                      ? static_cast<int>(visVar.as<float>())
                      : -1; // not available

  JsonVariant spdVar = c["wind_speed_10m"];
  current.wind_speed = !spdVar.isNull() ? spdVar.as<float>()
                                         : fallback.wind_speed;

  JsonVariant gustVar = c["wind_gusts_10m"];
  current.wind_gust = !gustVar.isNull() ? gustVar.as<float>()
                                         : current.wind_speed;

  JsonVariant dirVar = c["wind_direction_10m"];
  current.wind_deg = !dirVar.isNull() ? static_cast<int>(dirVar.as<float>())
                                       : fallback.wind_deg;

  JsonVariant cloudVar = c["cloud_cover"];
  if (!cloudVar.isNull())
  {
    current.clouds = static_cast<int>(std::round(cloudVar.as<float>()));
  }

  return error;
} // end deserializeOpenMeteoCurrent

/* ---- Open-Meteo forecast ----------------------------------------------- */

/* Parses Open-Meteo's forecast (/v1/forecast?hourly=...&daily=..., requested
 * with forecast_hours=48, forecast_days=7, timezone=auto,
 * timeformat=unixtime and wind_speed_unit=ms): 48 hours from the current
 * hour into hourly[], seven local calendar days into daily[]. Every value
 * is model output interpolated to LAT/LON; a null (Open-Meteo's way of
 * saying "not available") leaves the field at its "unknown" value.
 */
DeserializationError deserializeOpenMeteoForecast(WiFiClient &json,
                                                  owm_hourly_t *hourly,
                                                  owm_daily_t *daily)
{
  JsonDocument filter;
  JsonObject h = filter["hourly"].to<JsonObject>();
  h["time"]                      = true;
  h["temperature_2m"]            = true;
  h["apparent_temperature"]      = true;
  h["relative_humidity_2m"]      = true;
  h["dew_point_2m"]              = true;
  h["precipitation_probability"] = true;
  h["precipitation"]             = true;
  h["weather_code"]              = true;
  h["cloud_cover"]               = true;
  h["wind_speed_10m"]            = true;
  h["wind_gusts_10m"]            = true;
  h["wind_direction_10m"]        = true;
  h["is_day"]                    = true;
  JsonObject d = filter["daily"].to<JsonObject>();
  d["time"]                          = true;
  d["weather_code"]                  = true;
  d["temperature_2m_max"]            = true;
  d["temperature_2m_min"]            = true;
  d["precipitation_sum"]             = true;
  d["precipitation_probability_max"] = true;
  d["wind_speed_10m_max"]            = true;
  d["wind_gusts_10m_max"]            = true;
  d["wind_direction_10m_dominant"]   = true;
  d["uv_index_max"]                  = true;
  d["cloud_cover_mean"]              = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  for (int i = 0; i < OWM_NUM_HOURLY; ++i)
  {
    hourly[i] = {};
  }
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    daily[i] = {};
    daily[i].temp.min = NAN;
    daily[i].temp.max = NAN;
    daily[i].rain = NAN;
    daily[i].dew_point = NAN;
  }

  // a column of the response as an array, or an empty array when the
  // variable is missing
  auto col = [&](const char *block, const char *name) -> JsonArrayConst {
    return doc[block][name].as<JsonArrayConst>();
  };
  // element k of a column, NAN when the column is short or the value null
  auto num = [](JsonArrayConst a, size_t k) -> float {
    if (k >= a.size())
    {
      return NAN;
    }
    JsonVariantConst v = a[k];
    return v.isNull() ? NAN : v.as<float>();
  };

  // ---- hours
  JsonArrayConst hTime = col("hourly", "time");
  JsonArrayConst hTemp = col("hourly", "temperature_2m");
  JsonArrayConst hFeel = col("hourly", "apparent_temperature");
  JsonArrayConst hRh   = col("hourly", "relative_humidity_2m");
  JsonArrayConst hDew  = col("hourly", "dew_point_2m");
  JsonArrayConst hPop  = col("hourly", "precipitation_probability");
  JsonArrayConst hPrec = col("hourly", "precipitation");
  JsonArrayConst hCode = col("hourly", "weather_code");
  JsonArrayConst hCld  = col("hourly", "cloud_cover");
  JsonArrayConst hWs   = col("hourly", "wind_speed_10m");
  JsonArrayConst hWg   = col("hourly", "wind_gusts_10m");
  JsonArrayConst hWd   = col("hourly", "wind_direction_10m");
  JsonArrayConst hDay  = col("hourly", "is_day");
  const int64_t now = static_cast<int64_t>(time(nullptr));
  int hi = 0;
  for (size_t k = 0; k < hTime.size() && hi < OWM_NUM_HOURLY; ++k)
  {
    const int64_t t = hTime[k].as<int64_t>();
    if (t <= 0 || t < now - 3600)
    { // the series should start at the current hour; drop anything older
      continue;
    }
    owm_hourly_t &hh = hourly[hi];
    hh.dt = t;
    const float tempC = num(hTemp, k);
    const float feelC = num(hFeel, k);
    const float dewC  = num(hDew, k);
    const float rh    = num(hRh, k);
    const float pop   = num(hPop, k);
    const float cld   = num(hCld, k);
    const float ws    = num(hWs, k);
    const float wg    = num(hWg, k);
    const float wd    = num(hWd, k);
    hh.temp       = std::isnan(tempC) ? NAN : celsius_to_kelvin(tempC);
    hh.feels_like = std::isnan(feelC) ? hh.temp : celsius_to_kelvin(feelC);
    hh.humidity   = std::isnan(rh) ? 0 : static_cast<int>(std::round(rh));
    hh.dew_point  = std::isnan(dewC) ? NAN : celsius_to_kelvin(dewC);
    hh.pop        = std::isnan(pop) ? 0.f : pop / 100.f;
    hh.precip     = num(hPrec, k);
    hh.wind_speed = std::isnan(ws) ? 0.f : ws;
    hh.wind_gust  = std::isnan(wg) ? hh.wind_speed : wg;
    hh.wind_deg   = std::isnan(wd) ? 0 : static_cast<int>(wd);
    const float codeF = num(hCode, k);
    const int code = std::isnan(codeF) ? -1 : static_cast<int>(codeF);  // -1: unmapped -> overcast
    const bool isDay = k < hDay.size() ? (hDay[k].as<int>() != 0) : true;
    applyWmoCode(code, isDay, hh.weather, hh.clouds);
    if (!std::isnan(cld))
    { // the model's own cloud cover beats the code's estimate
      hh.clouds = static_cast<int>(std::round(cld));
    }
    ++hi;
  }

  // ---- days
  JsonArrayConst dTime = col("daily", "time");
  JsonArrayConst dCode = col("daily", "weather_code");
  JsonArrayConst dTmax = col("daily", "temperature_2m_max");
  JsonArrayConst dTmin = col("daily", "temperature_2m_min");
  JsonArrayConst dSum  = col("daily", "precipitation_sum");
  JsonArrayConst dPop  = col("daily", "precipitation_probability_max");
  JsonArrayConst dWs   = col("daily", "wind_speed_10m_max");
  JsonArrayConst dWg   = col("daily", "wind_gusts_10m_max");
  JsonArrayConst dWd   = col("daily", "wind_direction_10m_dominant");
  JsonArrayConst dUv   = col("daily", "uv_index_max");
  JsonArrayConst dCld  = col("daily", "cloud_cover_mean");
  int di = 0;
  for (size_t k = 0; k < dTime.size() && di < OWM_NUM_DAILY; ++k)
  {
    const int64_t t = dTime[k].as<int64_t>();   // local midnight (timezone=auto)
    if (t <= 0)
    {
      continue;
    }
    owm_daily_t &dd = daily[di];
    dd.dt = t;
    const float tmax = num(dTmax, k);
    const float tmin = num(dTmin, k);
    const float pop  = num(dPop, k);
    const float ws   = num(dWs, k);
    const float wg   = num(dWg, k);
    const float wd   = num(dWd, k);
    const float uv   = num(dUv, k);
    const float cld  = num(dCld, k);
    dd.temp.max   = std::isnan(tmax) ? NAN : celsius_to_kelvin(tmax);
    dd.temp.min   = std::isnan(tmin) ? NAN : celsius_to_kelvin(tmin);
    dd.pop        = std::isnan(pop) ? 0.f : pop / 100.f;
    dd.rain       = num(dSum, k);               // NaN when the model has none
    dd.snow       = 0.f;
    dd.precip_src = std::isnan(dd.rain) ? PRECIP_SRC_NONE : PRECIP_SRC_OPEN_METEO;
    dd.wind_speed = std::isnan(ws) ? 0.f : ws;
    dd.wind_gust  = std::isnan(wg) ? dd.wind_speed : wg;
    dd.wind_deg   = std::isnan(wd) ? 0 : static_cast<int>(wd);
    dd.uvi        = std::isnan(uv) ? 0.f : uv;
    const float dCodeF = num(dCode, k);
    applyWmoCode(std::isnan(dCodeF) ? -1 : static_cast<int>(dCodeF), true, dd.weather, dd.clouds);
    if (!std::isnan(cld))
    {
      dd.clouds = static_cast<int>(std::round(cld));
    }
    ++di;
  }

  if (hi == 0 || di == 0)
  {
    return DeserializationError::EmptyInput;
  }
  return error;
} // end deserializeOpenMeteoForecast

/* Google Maps Platform Weather API WeatherCondition.Type enum values
 * (https://developers.google.com/maps/documentation/weather/reference/rest/v1/WeatherCondition#Type)
 * to the OpenWeatherMap-style condition id the icon selection runs on and an
 * estimated cloud cover when cloudCover is absent.
 */
struct GoogleCondMapEntry { const char *type; int id; int clouds; const char *en; const char *he; };
static const GoogleCondMapEntry GOOGLE_COND_MAP[] = {
  {"CLEAR",                    800,   0, "Clear",                   "בהיר"},
  {"MOSTLY_CLEAR",             801,  15, "Mostly clear",            "בהיר לרוב"},
  {"PARTLY_CLOUDY",            802,  45, "Partly cloudy",           "מעונן חלקית"},
  {"MOSTLY_CLOUDY",            803,  75, "Mostly cloudy",           "מעונן לרוב"},
  {"CLOUDY",                   804,  95, "Cloudy",                  "מעונן"},
  {"WINDY",                    800,  10, "Windy",                   "רוחות חזקות"},
  {"WIND_AND_RAIN",            501,  90, "Wind and rain",           "גשם ורוחות"},
  {"LIGHT_RAIN_SHOWERS",       520,  50, "Light showers",           "ממטרים קלים"},
  {"CHANCE_OF_SHOWERS",        520,  50, "Chance of showers",       "ייתכנו ממטרים"},
  {"SCATTERED_SHOWERS",        520,  60, "Scattered showers",       "ממטרים פזורים"},
  {"RAIN_SHOWERS",             521,  70, "Rain showers",            "ממטרים"},
  {"HEAVY_RAIN_SHOWERS",       522,  85, "Heavy showers",           "ממטרים חזקים"},
  {"LIGHT_TO_MODERATE_RAIN",   500,  85, "Light rain",              "גשם קל"},
  {"MODERATE_TO_HEAVY_RAIN",   502,  90, "Heavy rain",              "גשם חזק"},
  {"RAIN",                     501,  90, "Rain",                    "גשם"},
  {"LIGHT_RAIN",               500,  80, "Light rain",              "גשם קל"},
  {"HEAVY_RAIN",               502,  95, "Heavy rain",              "גשם כבד"},
  {"RAIN_PERIODICALLY_HEAVY",  502,  90, "Heavy rain",              "גשם חזק לפרקים"},
  {"LIGHT_SNOW_SHOWERS",       620,  65, "Light snow showers",      "מטחי שלג קלים"},
  {"CHANCE_OF_SNOW_SHOWERS",   620,  60, "Chance of snow showers",  "ייתכנו מטחי שלג"},
  {"SCATTERED_SNOW_SHOWERS",   620,  65, "Scattered snow showers",  "מטחי שלג פזורים"},
  {"SNOW_SHOWERS",             621,  75, "Snow showers",            "מטחי שלג"},
  {"HEAVY_SNOW_SHOWERS",       622,  90, "Heavy snow showers",      "מטחי שלג כבדים"},
  {"LIGHT_TO_MODERATE_SNOW",   600,  85, "Light snow",              "שלג קל"},
  {"MODERATE_TO_HEAVY_SNOW",   602,  95, "Heavy snow",              "שלג כבד"},
  {"SNOW",                     601,  90, "Snow",                    "שלג"},
  {"LIGHT_SNOW",               600,  85, "Light snow",              "שלג קל"},
  {"HEAVY_SNOW",               602,  95, "Heavy snow",              "שלג כבד"},
  {"SNOWSTORM",                602,  95, "Snowstorm",               "סופת שלגים"},
  {"SNOW_PERIODICALLY_HEAVY",  602,  95, "Heavy snow",              "שלג כבד לפרקים"},
  {"HEAVY_SNOW_STORM",         602, 100, "Heavy snowstorm",         "סופת שלגים עזה"},
  {"BLOWING_SNOW",             601,  85, "Blowing snow",            "שלג נישא ברוח"},
  {"RAIN_AND_SNOW",            616,  90, "Rain and snow",           "גשם ושלג"},
  {"HAIL",                     611,  85, "Hail",                    "ברד"},
  {"HAIL_SHOWERS",             611,  75, "Hail showers",            "מטחי ברד"},
  {"SLEET",                    611,  90, "Sleet",                   "שלג מעורב בגשם"},
  {"FREEZING_RAIN",            511,  90, "Freezing rain",           "גשם קופא"},
  {"FREEZING_DRIZZLE",         511,  85, "Freezing drizzle",        "טפטוף קופא"},
  {"DRIZZLE",                  301,  85, "Drizzle",                 "טפטוף"},
  {"THUNDERSTORM",             211,  85, "Thunderstorm",            "סופת רעמים"},
  {"THUNDERSHOWER",            210,  75, "Thundershower",           "ממטרים וסופות רעמים"},
  {"LIGHT_THUNDERSTORM_RAIN",  200,  80, "Light thunderstorm",      "גשם קל וסופות רעמים"},
  {"SCATTERED_THUNDERSTORMS",  210,  70, "Scattered thunderstorms", "סופות רעמים פזורות"},
  {"HEAVY_THUNDERSTORM",       212,  95, "Heavy thunderstorm",      "סופת רעמים חזקה"},
  {"FOG",                      741,  85, "Fog",                     "ערפל"},
  {"LIGHT_FOG",                701,  60, "Mist",                    "ערפל קל"},
  {"HAZE",                     721,  30, "Haze",                    "אובך קל"},
  {"DUST",                     731,  30, "Dust",                    "אביך"},
  {"SANDSTORM",                761,  40, "Sandstorm",               "סופת חול"},
  {"VERY_HOT",                 800,   5, "Very hot",                "חם מאוד"},
  {"VERY_COLD",                800,   5, "Very cold",               "קר מאוד"},
};

static bool applyGoogleCondition(const char *type, const char *desc, bool isDay,
                                 owm_weather_t &weather, int &clouds)
{
  if (!type || !*type)
  {
    return false;
  }
  for (const GoogleCondMapEntry &e : GOOGLE_COND_MAP)
  {
    if (strcmp(e.type, type) == 0)
    {
      weather.id   = e.id;
      weather.icon = isDay ? "d" : "n";
      if (OWM_LANG == "he")
      {
        weather.description = e.he;
      }
      else if (desc && *desc)
      {
        weather.description = desc;
      }
      else if (OWM_LANG == "en")
      {
        weather.description = e.en;
      }
      else
      {
        weather.description = "";
      }
      clouds = e.clouds;
      return true;
    }
  }
  return false;
}

/* Parses the Google Maps Platform Weather API's current conditions
 * (/v1/currentConditions:lookup, default METRIC units): a nowcast blended
 * from nearby stations, refreshed every 15 minutes. Any missing field falls
 * back to the first hourly forecast period, like deserializeOpenMeteoCurrent.
 * weatherCondition.type and weatherCondition.description.text set the
 * condition icon and localized description when recognized.
 */
DeserializationError deserializeGoogleCurrent(WiFiClient &json,
                                              const owm_hourly_t &fallback,
                                              owm_current_t &current)
{
  // The response also carries a 24-hour history block, icon URIs and
  // precipitation probabilities that are never read.
  JsonDocument filter;
  filter["isDaytime"]                       = true;
  filter["weatherCondition"]["type"]        = true;
  filter["weatherCondition"]["description"]["text"] = true;
  filter["temperature"]["degrees"]          = true;
  filter["feelsLikeTemperature"]["degrees"] = true;
  filter["dewPoint"]["degrees"]             = true;
  filter["relativeHumidity"]                = true;
  filter["airPressure"]["meanSeaLevelMillibars"] = true;
  filter["wind"]["speed"]["value"]          = true;
  filter["wind"]["gust"]["value"]           = true;
  filter["wind"]["direction"]["degrees"]    = true;
  filter["visibility"]["distance"]          = true;
  filter["cloudCover"]                      = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif

  if (error)
  {
    fillCurrentFromFallback(fallback, current);
    return error;
  }

  current = {};
  current.dt      = time(nullptr);
  current.weather = fallback.weather;
  current.clouds  = fallback.clouds;

  const bool isDay = doc["isDaytime"].isNull() ? (fallback.weather.icon != "n")
                                               : doc["isDaytime"].as<bool>();
  applyGoogleCondition(doc["weatherCondition"]["type"].as<const char *>(),
                       doc["weatherCondition"]["description"]["text"].as<const char *>(),
                       isDay, current.weather, current.clouds);

  JsonVariant tempVar = doc["temperature"]["degrees"];
  current.temp = !tempVar.isNull() ? celsius_to_kelvin(tempVar.as<float>())
                                    : fallback.temp;

  JsonVariant feelVar = doc["feelsLikeTemperature"]["degrees"];
  current.feels_like = !feelVar.isNull()
                      ? celsius_to_kelvin(feelVar.as<float>())
                      : current.temp;

  JsonVariant humVar = doc["relativeHumidity"];
  current.humidity = !humVar.isNull()
                    ? static_cast<int>(std::round(humVar.as<float>()))
                    : fallback.humidity;

  JsonVariant dewVar = doc["dewPoint"]["degrees"];
  current.dew_point = !dewVar.isNull() ? celsius_to_kelvin(dewVar.as<float>())
                                        : fallback.dew_point;

  JsonVariant pressVar = doc["airPressure"]["meanSeaLevelMillibars"];
  current.pressure = !pressVar.isNull()
                    ? static_cast<int>(std::round(pressVar.as<float>()))
                    : 0;  // not available

  // METRIC visibility is in kilometres; the display expects metres.
  JsonVariant visVar = doc["visibility"]["distance"];
  current.visibility = !visVar.isNull()
                      ? static_cast<int>(visVar.as<float>() * 1000.f)
                      : -1; // not available

  // METRIC wind is in km/h; every other source stores m/s.
  JsonVariant spdVar = doc["wind"]["speed"]["value"];
  current.wind_speed = !spdVar.isNull() ? spdVar.as<float>() / 3.6f
                                         : fallback.wind_speed;

  JsonVariant gustVar = doc["wind"]["gust"]["value"];
  current.wind_gust = !gustVar.isNull() ? gustVar.as<float>() / 3.6f
                                         : current.wind_speed;

  JsonVariant dirVar = doc["wind"]["direction"]["degrees"];
  current.wind_deg = !dirVar.isNull() ? static_cast<int>(dirVar.as<float>())
                                       : fallback.wind_deg;

  JsonVariant cloudVar = doc["cloudCover"];
  if (!cloudVar.isNull())
  {
    current.clouds = static_cast<int>(std::round(cloudVar.as<float>()));
  }

  return error;
} // end deserializeGoogleCurrent

/* Parses weather.gov's /alerts/active endpoint.
 */
DeserializationError deserializeNWSAlerts(WiFiClient &json,
                                          std::vector<owm_alerts_t> &alerts)
{
  JsonDocument filter;
  JsonObject props = filter["features"][0]["properties"].to<JsonObject>();
  props["event"]     = true;
  props["effective"] = true;
  props["expires"]   = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  int i = 0;
  for (JsonObject feature : doc["features"].as<JsonArray>())
  {
    JsonObject props = feature["properties"];
    owm_alerts_t new_alert = {};
    new_alert.event = props["event"].as<const char *>();
    new_alert.start = parseISO8601(props["effective"].as<const char *>());
    new_alert.end   = parseISO8601(props["expires"]  .as<const char *>());
    // weather.gov does not provide a short category tag like OpenWeatherMap
    // did; reuse the event text so exact-duplicate alerts (ex. issued for
    // multiple overlapping zones) can still be deduplicated.
    new_alert.tags  = new_alert.event;
    alerts.push_back(new_alert);

    if (i == OWM_NUM_ALERTS - 1)
    {
      break;
    }
    ++i;
  }

  return error;
} // end deserializeNWSAlerts


/* ---- Israel Meteorological Service ------------------------------------ */

/* IMS sends every number as a string ("16", "0.00") and the odd one as a
 * number (wind_speed in now_analysis). as<float>() on a string is 0 in
 * ArduinoJson, so read both forms.
 */
static float jnum(JsonVariantConst v, float fallback = NAN)
{
  if (v.isNull())
  {
    return fallback;
  }
  if (v.is<const char *>())
  {
    const char *str = v.as<const char *>();
    if (!str || !*str)
    {
      return fallback;
    }
    char *end = nullptr;
    float f = strtof(str, &end);
    return (end == str) ? fallback : f;
  }
  return v.as<float>();
}

/* "YYYY-MM-DD HH:MM:SS" (or just "YYYY-MM-DD") in the device's TZ, which for
 * an Israeli location is Israel's -- IMS times are local time without a
 * zone. Returns 0 when the string does not parse.
 */
static int64_t parseIMSLocalTime(const char *str)
{
  if (!str || strlen(str) < 10)
  {
    return 0;
  }
  tm t = {};
  t.tm_year = atoi(str) - 1900;
  t.tm_mon  = atoi(str + 5) - 1;
  t.tm_mday = atoi(str + 8);
  if (strlen(str) >= 16)
  {
    t.tm_hour = atoi(str + 11);
    t.tm_min  = atoi(str + 14);
  }
  t.tm_isdst = -1;                 // let mktime work out summer time
  time_t result = mktime(&t);
  return (result == static_cast<time_t>(-1)) ? 0 : static_cast<int64_t>(result);
}

/* Dew point from temperature (C) and relative humidity (%), Magnus formula.
 * IMS gives humidity but no dew point per hour.
 */
static float dewPointC(float tempC, float rh)
{
  if (std::isnan(tempC) || std::isnan(rh) || rh <= 0.f)
  {
    return NAN;
  }
  const float a = 17.62f, b = 243.12f;
  const float gamma = logf(rh / 100.f) + a * tempC / (b + tempC);
  return b * gamma / (a - gamma);
}

/* IMS weather codes (https://ims.gov.il/en/weather_codes) to the
 * OpenWeatherMap-style condition id the icon selection runs on, and an
 * estimated cloud cover. IMS has 23 codes: sky, rain in three strengths,
 * snow in three, and a set of "feel" codes (hot, cold, muggy...) that
 * describe a clear-ish day.
 */
struct ImsCodeMapEntry { int code; int id; int clouds; const char *en; const char *he; };
static const ImsCodeMapEntry IMS_CODE_MAP[] = {
  {1250, 800, 0, "Clear", "בהיר"},  // Clear
  {1220, 802, 40, "Partly cloudy", "מעונן חלקית"},  // Partly cloudy
  {1230, 804, 90, "Cloudy", "מעונן"},  // Cloudy
  {1570, 731, 30, "Dust", "אביך"},  // Dust
  {1010, 761, 40, "Sandstorms", "סופות חול"},  // Sandstorms
  {1160, 741, 85, "Fog", "ערפל"},  // Fog
  {1310, 800, 5, "Hot", "חם"},  // Hot
  {1580, 800, 5, "Extremely hot", "חם מאוד"},  // Extremely hot
  {1270, 801, 20, "Muggy", "הביל"},  // Muggy
  {1320, 800, 5, "Cold", "קר"},  // Cold
  {1590, 800, 5, "Extremely cold", "קר מאוד"},  // Extremely cold
  {1300, 800, 5, "Frost", "קרה"},  // Frost
  {1530, 500, 50, "Partly cloudy, possible rain", "מעונן חלקית, ייתכן גשם"},  // Partly cloudy, possible rain
  {1540, 500, 80, "Cloudy, possible rain", "מעונן, ייתכן גשם"},  // Cloudy, possible rain
  {1560, 500, 90, "Cloudy, light rain", "מעונן עם גשם קל"},  // Cloudy, light rain
  {1140, 501, 90, "Rainy", "גשום"},  // Rainy
  {1020, 211, 90, "Thunderstorms", "סופות רעמים"},  // Thunderstorms
  {1510, 212, 95, "Stormy", "סוער"},  // Stormy
  {1260, 800, 10, "Windy", "רוחות חזקות"},  // Windy (the renderer adds the wind to the icon)
  {1080, 611, 90, "Sleet", "שלג מעורב בגשם"},  // Sleet
  {1070, 600, 90, "Light snow", "שלג קל"},  // Light snow
  {1060, 601, 90, "Snow", "שלג"},  // Snow
  {1520, 602, 95, "Heavy snow", "שלג כבד"},  // Heavy snow
};

static void applyImsCode(int code, owm_weather_t &weather, int &clouds)
{
  int id = 804, cl = 50;
  const char *en = "", *he = "";
  for (const ImsCodeMapEntry &e : IMS_CODE_MAP)
  {
    if (e.code == code)
    {
      id = e.id;
      cl = e.clouds;
      en = e.en;
      he = e.he;
      break;
    }
  }
  weather.id = id;
  weather.description = (OWM_LANG == "he") ? he : en;
  // day/night is decided by the renderer from the computed sun times
  weather.icon = "d";
  clouds = cl;
}

/* IMS wind_direction_id (https://ims.gov.il/en/wind_directions): 1 = N
 * (360), 2 = NNE ... 16 = NNW, 17 = N (0).
 */
static int imsWindDegrees(int id)
{
  if (id >= 1 && id <= 16)
  {
    return static_cast<int>((id - 1) * 22.5f + 0.5f) % 360;
  }
  return 0;
}

static const float KMH_TO_MS = 1.f / 3.6f;

/* /{lang}/locations_info: every IMS location with its coordinates. Picks
 * the one with id wantLid, or when that is 0 the one nearest (lat, lon),
 * and returns its id, warning region and name.
 */
DeserializationError deserializeIMSLocations(WiFiClient &json, double lat,
                                             double lon, int wantLid,
                                             int &lid, int &rid, String &name)
{
  JsonDocument filter;
  JsonObject loc = filter["data"]["*"].to<JsonObject>();
  loc["lid"]  = true;
  loc["lat"]  = true;
  loc["lon"]  = true;
  loc["rid"]  = true;
  loc["name"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
  if (error)
  {
    return error;
  }

  lid = 0;
  rid = 0;
  double best = 1e18;
  const double cosLat = cos(lat * M_PI / 180.0);
  for (JsonPair kv : doc["data"].as<JsonObject>())
  {
    JsonObject o = kv.value().as<JsonObject>();
    const float la = jnum(o["lat"]);
    const float lo = jnum(o["lon"]);
    if (std::isnan(la) || std::isnan(lo))
    {
      continue;
    }
    const int thisLid = static_cast<int>(jnum(o["lid"], 0));
    // equirectangular distance is plenty over 500 km
    const double dLat = la - lat;
    const double dLon = (lo - lon) * cosLat;
    double d = dLat * dLat + dLon * dLon;
    if (wantLid > 0)
    {
      d = (thisLid == wantLid) ? -1.0 : 1e18;
    }
    if (d < best)
    {
      best = d;
      lid  = static_cast<int>(jnum(o["lid"], 0));
      rid  = static_cast<int>(jnum(o["rid"], 0));
      name = o["name"].as<const char *>();
    }
  }
  if (lid <= 0)
  {
    return DeserializationError::EmptyInput;
  }
  return error;
} // end deserializeIMSLocations

/* /{lang}/forecast_data/{lid}: seven days, each a `daily` block and 24
 * `hourly` blocks (today's from the current hour). The hours run on into
 * hourly[] from the current hour; daily[] takes the daily blocks' extremes
 * and condition, and sums the hours' rain.
 */
DeserializationError deserializeIMSForecast(WiFiClient &json,
                                            owm_hourly_t *hourly,
                                            owm_daily_t *daily)
{
  JsonDocument filter;
  JsonObject day = filter["data"]["*"].to<JsonObject>();
  JsonObject d = day["daily"].to<JsonObject>();
  d["forecast_date"]       = true;
  d["weather_code"]        = true;
  d["minimum_temperature"] = true;
  d["maximum_temperature"] = true;
  d["maximum_uvi"]         = true;
  JsonObject h = day["hourly"]["*"].to<JsonObject>();
  h["forecast_time"]     = true;
  h["weather_code"]      = true;
  h["rain_chance"]       = true;
  h["temperature"]       = true;
  h["precise_temperature"] = true;
  h["relative_humidity"] = true;
  h["gust_speed"]        = true;
  h["rain"]              = true;
  h["wind_direction_id"] = true;
  h["wind_speed"]        = true;
  h["heat_stress"]       = true;
  h["heat_stress_level"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  for (int i = 0; i < OWM_NUM_HOURLY; ++i)
  {
    hourly[i] = {};
  }
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    daily[i] = {};
    daily[i].temp.min = NAN;
    daily[i].temp.max = NAN;
    daily[i].rain = NAN;
    daily[i].dew_point = NAN;
  }

  const int64_t now = static_cast<int64_t>(time(nullptr));
  int hi = 0;      // next hourly slot
  int di = 0;      // next daily slot
  for (JsonPair dayKv : doc["data"].as<JsonObject>())
  {
    JsonObject dayObj = dayKv.value().as<JsonObject>();
    JsonObject dBlock = dayObj["daily"];
    JsonObject hours  = dayObj["hourly"];

    // the day
    int64_t dayStart = parseIMSLocalTime(dayKv.key().c_str());
    bool haveDaily = false;
    if (di < OWM_NUM_DAILY && dayStart > 0)
    {
      owm_daily_t &dd = daily[di];
      dd.dt = dayStart;
      float tmin = NAN, tmax = NAN;
      if (!dBlock.isNull())
      {
        tmin = jnum(dBlock["minimum_temperature"]);
        tmax = jnum(dBlock["maximum_temperature"]);
        dd.uvi = jnum(dBlock["maximum_uvi"], 0.f);
        applyImsCode(static_cast<int>(jnum(dBlock["weather_code"], 1230)),
                     dd.weather, dd.clouds);
      }
      dd.temp.min = std::isnan(tmin) ? NAN : celsius_to_kelvin(tmin);
      dd.temp.max = std::isnan(tmax) ? NAN : celsius_to_kelvin(tmax);
      dd.pop  = 0.f;
      dd.rain = 0.f;
      dd.snow = 0.f;
      dd.precip_src = PRECIP_SRC_NWS;   // the forecast provider's own total
      haveDaily = true;
    }

    // its hours
    float hmin = NAN, hmax = NAN;
    int   dayCode = -1;
    for (JsonPair hKv : hours)
    {
      JsonObject ho = hKv.value().as<JsonObject>();
      const int64_t t = parseIMSLocalTime(ho["forecast_time"].as<const char *>());
      if (t <= 0)
      {
        continue;
      }
      float tempC = jnum(ho["precise_temperature"]);
      if (std::isnan(tempC))
      {
        tempC = jnum(ho["temperature"]);
      }
      const float rh    = jnum(ho["relative_humidity"]);
      const float rain  = jnum(ho["rain"], 0.f);
      const float pop   = jnum(ho["rain_chance"], 0.f) / 100.f;
      const int   code  = static_cast<int>(jnum(ho["weather_code"], 1230));

      if (haveDaily)
      {
        owm_daily_t &dd = daily[di];
        dd.pop  = std::max(dd.pop, pop);
        dd.rain += std::isnan(rain) ? 0.f : rain;
        if (!std::isnan(tempC))
        {
          hmin = std::isnan(hmin) ? tempC : std::min(hmin, tempC);
          hmax = std::isnan(hmax) ? tempC : std::max(hmax, tempC);
        }
        // a daytime hour's code stands in for a missing daily block
        tm lt;
        time_t tt = static_cast<time_t>(t);
        localtime_r(&tt, &lt);
        if (dayCode < 0 && lt.tm_hour >= 12)
        {
          dayCode = code;
        }
      }

      // the hourly series starts at the current hour
      if (hi >= OWM_NUM_HOURLY || t < now - 3600)
      {
        continue;
      }
      owm_hourly_t &hh = hourly[hi];
      hh = {};
      hh.dt   = t;
      hh.temp = std::isnan(tempC) ? NAN : celsius_to_kelvin(tempC);
      hh.feels_like = hh.temp;
      hh.humidity   = std::isnan(rh) ? 0 : static_cast<int>(std::round(rh));
      const float dp = dewPointC(tempC, rh);
      hh.dew_point  = std::isnan(dp) ? NAN : celsius_to_kelvin(dp);
      hh.pop        = pop;
      hh.precip     = rain;
      hh.wind_speed = jnum(ho["wind_speed"], 0.f) * KMH_TO_MS;
      hh.wind_gust  = jnum(ho["gust_speed"], NAN);
      hh.wind_gust  = std::isnan(hh.wind_gust) ? hh.wind_speed
                                               : hh.wind_gust * KMH_TO_MS;
      hh.wind_deg   = imsWindDegrees(static_cast<int>(jnum(ho["wind_direction_id"], 0)));
      hh.heat_stress       = jnum(ho["heat_stress"], 0.f);
      hh.heat_stress_level = static_cast<int>(jnum(ho["heat_stress_level"], 0.f));
      applyImsCode(code, hh.weather, hh.clouds);
      ++hi;
    }

    if (haveDaily)
    {
      owm_daily_t &dd = daily[di];
      if (std::isnan(dd.temp.min) && !std::isnan(hmin))
      {
        dd.temp.min = celsius_to_kelvin(hmin);
      }
      if (std::isnan(dd.temp.max) && !std::isnan(hmax))
      {
        dd.temp.max = celsius_to_kelvin(hmax);
      }
      if (dBlock.isNull())
      {
        applyImsCode(dayCode < 0 ? 1230 : dayCode, dd.weather, dd.clouds);
      }
      // the wind shown for the day: the strongest hour's
      ++di;
    }
  }

  if (hi == 0 || di == 0)
  {
    return DeserializationError::EmptyInput;
  }
  return error;
} // end deserializeIMSForecast

/* /{lang}/now_analysis/{lid}: IMS's current conditions for the location,
 * refreshed through the hour. Pressure, visibility and real-time sky cover
 * (now_analysis copies weather_code from the forecast) are filled in by
 * finishWeather from Open-Meteo when reachable; the rest maps straight across.
 */
DeserializationError deserializeIMSCurrent(WiFiClient &json, int lid,
                                           const owm_hourly_t &fallback,
                                           owm_current_t &current)
{
  JsonDocument filter;
  JsonObject c = filter["data"]["*"].to<JsonObject>();
  c["temperature"]       = true;
  c["feels_like"]        = true;
  c["due_point_Temp"]    = true;
  c["relative_humidity"] = true;
  c["wind_speed"]        = true;
  c["gust_speed"]        = true;
  c["wind_direction_id"] = true;
  c["weather_code"]      = true;
  c["u_v_index"]         = true;
  c["heat_stress"]       = true;
  c["heat_stress_level"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    fillCurrentFromFallback(fallback, current);
    return error;
  }
  JsonObject o = doc["data"][String(lid)];
  if (o.isNull())
  {
    fillCurrentFromFallback(fallback, current);
    return DeserializationError::EmptyInput;
  }

  current = {};
  current.dt      = time(nullptr);
  current.weather = fallback.weather;
  current.clouds  = fallback.clouds;
  const int code = static_cast<int>(jnum(o["weather_code"], -1));
  if (code > 0)
  {
    applyImsCode(code, current.weather, current.clouds);
  }

  const float tempC = jnum(o["temperature"]);
  current.temp = std::isnan(tempC) ? fallback.temp : celsius_to_kelvin(tempC);
  const float feel = jnum(o["feels_like"]);
  current.feels_like = std::isnan(feel) ? current.temp : celsius_to_kelvin(feel);
  const float rh = jnum(o["relative_humidity"]);
  current.humidity = std::isnan(rh) ? fallback.humidity
                                    : static_cast<int>(std::round(rh));
  float dew = jnum(o["due_point_Temp"]);
  if (std::isnan(dew))
  {
    dew = dewPointC(tempC, rh);
  }
  current.dew_point = std::isnan(dew) ? fallback.dew_point : celsius_to_kelvin(dew);
  current.pressure   = 0;      // "--"
  current.visibility = -1;     // "--"
  const float spd = jnum(o["wind_speed"]);
  current.wind_speed = std::isnan(spd) ? fallback.wind_speed : spd * KMH_TO_MS;
  const float gust = jnum(o["gust_speed"]);
  current.wind_gust = std::isnan(gust) ? current.wind_speed : gust * KMH_TO_MS;
  const float dir = jnum(o["wind_direction_id"]);
  current.wind_deg = std::isnan(dir) ? fallback.wind_deg
                                     : imsWindDegrees(static_cast<int>(dir));
  current.uvi = jnum(o["u_v_index"], NAN);
  current.heat_stress       = jnum(o["heat_stress"], 0.f);
  current.heat_stress_level = static_cast<int>(jnum(o["heat_stress_level"], 0.f));
  return error;
} // end deserializeIMSCurrent

/* /{lang}/warnings: every current warning, by day, then by region
 * ("r-<rid>"), then by warning id. Only the location's land region (rid)
 * and general-public / road / news-flash warnings are kept (skipping sea,
 * aviation and agriculture advisories), deduplicated per hazard at the
 * highest active severity, and ordered Red > Orange > Yellow.
 *
 * Since October 2026 /warnings omits warnings_metadata (still carried on
 * /city_portal/{lid}), so IMS's official severity_id and warning_type_id
 * tables are built in below. The severity name goes in alert.description so
 * the renderers colour the alert badge Yellow / Orange / Red like the
 * air-quality and pollen chips while alert.event stays the concise hazard
 * name.
 */
struct ImsSeverityEntry { int id; int rank; bool early; const char *en; const char *he; };
static const ImsSeverityEntry IMS_SEVERITIES[] = {
  {7, 6, false, "Red Warning",          "אזהרה אדומה"},
  {6, 5, true,  "Red Early Warning",    "התראה מוקדמת אדומה"},
  {4, 4, false, "Orange Warning",       "אזהרה כתומה"},
  {5, 3, true,  "Orange Early Warning", "התראה מוקדמת כתומה"},
  {3, 2, false, "Yellow Warning",       "אזהרה צהובה"},
  {2, 1, true,  "Yellow Early Warning", "התראה מוקדמת צהובה"},
};

struct ImsWarningTypeEntry { int id; const char *en; const char *he; };
static const ImsWarningTypeEntry IMS_WARNING_TYPES[] = {
  { 2, "Strong Winds",              "רוחות חזקות"},
  { 5, "Flash Floods",              "שטפונות"},
  { 6, "Snow",                      "שלג"},
  { 7, "Flooding",                  "הצפות"},
  { 8, "Heat Stress",               "עומס חום"},
  { 9, "Frost",                     "קרה"},
  {10, "Fire Danger",               "סכנת שריפות"},
  {11, "Thunderstorms",             "סופות רעמים"},
  {12, "Reduced Visibility",        "הגבלת ראות"},
  {33, "Heat Wave",                 "גל חום"},
  {53, "Significant Precipitation", "כמויות גשם משמעותיות"},
  {54, "Extreme Temperature",       "טמפרטורות קיצוניות"},
};

static bool isImsSeaType(int typeId)
{
  return typeId == 3 || typeId == 4; // High Sea Swimming Danger, High Sea
}

static bool isImsSpecialistType(int typeId)
{
  // IMS warning types with for_app == 0 (aviation turbulence, wind shear,
  // icing, low ceiling, mountain wave/obscuration, burning index, ash, etc.)
  switch (typeId)
  {
  case 13: case 14: case 15: case 16: case 35:
  case 39: case 40: case 41: case 42:
    return true;
  default:
    return false;
  }
}

/* Fallback title from a warning's full sentence (used for News Flashes,
 * warning_type_id == 51, or any unmapped type): strips a leading "update: "
 * prefix and cuts before the region list or time span.
 */
static String imsWarningTitle(const char *full, bool he)
{
  if (!full)
  {
    return "";
  }
  String s(full);
  s.trim();
  if (he)
  {
    if (s.startsWith("עדכון:"))
    {
      s = s.substring(strlen("עדכון:"));
      s.trim();
    }
  }
  else if (s.startsWith("update:") || s.startsWith("Update:"))
  {
    s = s.substring(7);
    s.trim();
  }
  int cut = -1;
  auto cutBefore = [&](const char *sep) {
    const int p = s.indexOf(sep);
    if (p > 0 && (cut < 0 || p < cut))
    {
      cut = p;
    }
  };
  if (he)
  {
    cutBefore(" מ-");
    cutBefore(" ב-");
  }
  else
  {
    cutBefore(" in ");
    cutBefore(" on ");
    cutBefore(" from ");
  }
  if (cut > 0)
  {
    s = s.substring(0, cut);
  }
  return s;
}

DeserializationError deserializeIMSAlerts(WiFiClient &json, int rid,
                                          std::vector<owm_alerts_t> &alerts)
{
  JsonDocument filter;
  JsonObject w = filter["data"]["full_warnings_data"]["*"]["*"]["*"].to<JsonObject>();
  w["wid"]             = true;
  w["warning_type_id"] = true;
  w["severity_id"]     = true;
  w["groups"]          = true;
  w["valid_from_unix"] = true;
  w["valid_to"]        = true;
  w["full_he"]         = true;
  w["full_en"]         = true;
  filter["data"]["warnings_metadata"]["ims_warning_type"]["*"]["name"] = true;
  filter["data"]["warnings_metadata"]["warning_severity"]["*"]["severity_name"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  JsonObject types = doc["data"]["warnings_metadata"]["ims_warning_type"];
  JsonObject sevs  = doc["data"]["warnings_metadata"]["warning_severity"];
  const bool he = (OWM_LANG == "he");
  const String regionKey = "r-" + String(rid);

  struct Candidate
  {
    owm_alerts_t alert;
    int          typeId;
    int          rank;
  };
  std::vector<Candidate> cands;
  std::vector<String> seen;
  const int64_t now = static_cast<int64_t>(time(nullptr));

  for (JsonPair dayKv : doc["data"]["full_warnings_data"].as<JsonObject>())
  {
    JsonObject region = dayKv.value()[regionKey];
    if (region.isNull())
    {
      continue;
    }
    for (JsonPair wKv : region)
    {
      JsonObject a = wKv.value().as<JsonObject>();
      String wid = a["wid"].as<const char *>();
      bool dup = false;
      for (const String &s : seen)
      {
        if (s == wid) { dup = true; break; }
      }
      if (dup)
      {
        continue;
      }
      seen.push_back(wid);

      const int typeId = static_cast<int>(jnum(a["warning_type_id"], 0));
      const int sevId  = static_cast<int>(jnum(a["severity_id"], 0));
      if (sevId == 0 || sevId == 8 || isImsSeaType(typeId))
      {
        continue; // inactive (green / gray) or sea conditions
      }

      // Filter specialist and marine audience groups (17 = aviation,
      // 27 = shipping, 36 = coast/seamanship, 50 = agriculture), keeping
      // general public (18), news flashes (51/52) and roads (56).
      bool hasPublicGroup = true;
      bool hasGroup18     = false;
      JsonArray groups = a["groups"].as<JsonArray>();
      if (!groups.isNull() && groups.size() > 0)
      {
        hasPublicGroup = false;
        for (JsonVariant gv : groups)
        {
          const int gid = static_cast<int>(jnum(gv, 0));
          if (gid == 18)
          {
            hasGroup18 = true;
            hasPublicGroup = true;
          }
          else if (gid == 51 || gid == 52 || gid == 56)
          {
            hasPublicGroup = true;
          }
        }
      }
      if (!hasPublicGroup || (isImsSpecialistType(typeId) && !hasGroup18))
      {
        continue;
      }

      owm_alerts_t alert = {};
      alert.start = static_cast<int64_t>(jnum(a["valid_from_unix"], 0.f));
      alert.end   = parseIMSLocalTime(a["valid_to"].as<const char *>());
      if (alert.end > 0 && alert.end < now)
      {
        continue;                    // already over
      }

      int  sevRank  = 1;
      bool isEarly  = false;
      const char *sevLabel = sevs[String(sevId)]["severity_name"].as<const char *>();
      for (const ImsSeverityEntry &se : IMS_SEVERITIES)
      {
        if (se.id == sevId)
        {
          sevRank = se.rank;
          isEarly = se.early;
          if (!sevLabel || !*sevLabel)
          {
            sevLabel = he ? se.he : se.en;
          }
          break;
        }
      }
      if (sevLabel && *sevLabel)
      {
        alert.description = sevLabel;
      }

      const char *typeLabel = nullptr;
      if (typeId != 51) // News Flash uses its sentence rather than "News Flash"
      {
        typeLabel = types[String(typeId)]["name"].as<const char *>();
        if (!typeLabel || !*typeLabel)
        {
          for (const ImsWarningTypeEntry &te : IMS_WARNING_TYPES)
          {
            if (te.id == typeId)
            {
              typeLabel = he ? te.he : te.en;
              break;
            }
          }
        }
      }

      if (typeLabel && *typeLabel)
      {
        alert.event = isEarly
                    ? String(he ? "התראה מוקדמת: " : "Early Warning: ") + typeLabel
                    : String(typeLabel);
      }
      else
      {
        alert.event = imsWarningTitle(a[he ? "full_he" : "full_en"]
                                        .as<const char *>(), he);
        if (alert.event.isEmpty())
        {
          alert.event = (sevLabel && *sevLabel) ? sevLabel
                      : (he ? "אזהרת מזג אוויר" : "Weather warning");
        }
      }
      alert.tags = (typeLabel && *typeLabel) ? String(typeLabel)
                                             : String(typeId);

      bool merged = false;
      if (typeId > 0 && typeId != 51)
      {
        for (Candidate &existing : cands)
        {
          if (existing.typeId == typeId)
          {
            if (sevRank > existing.rank
                || (sevRank == existing.rank && alert.end > existing.alert.end))
            {
              existing.alert = alert;
              existing.rank  = sevRank;
            }
            merged = true;
            break;
          }
        }
      }
      if (!merged)
      {
        cands.push_back({alert, typeId, sevRank});
      }
    }
  }

  std::stable_sort(cands.begin(), cands.end(),
                   [](const Candidate &p, const Candidate &q) {
                     return p.rank > q.rank;
                   });
  for (size_t i = 0; i < cands.size() && alerts.size() < OWM_NUM_ALERTS; ++i)
  {
    alerts.push_back(cands[i].alert);
  }
  return error;
} // end deserializeIMSAlerts


/* ---- Israel Ministry of Environmental Protection ----------------------- */

/* /v1/envista/regions: 16 regions, each with its stations, each station
 * with its coordinates, monitors and a page of metadata. Only the id,
 * coordinates and active flag survive the filter (~250 stations). Returns
 * the active stations within SVIVA_RADIUS_KM of (lat, lon), nearest first,
 * at most SVIVA_MAX_STATIONS of them -- and always the nearest one, however
 * far. Stations without coordinates (some mobile units) are skipped.
 */
static const double SVIVA_RADIUS_KM = 15.0;
static const size_t SVIVA_MAX_STATIONS = 4;

DeserializationError deserializeSvivaStations(WiFiClient &json, double lat,
                                              double lon,
                                              std::vector<int> &stationIds)
{
  JsonDocument filter;
  JsonObject st = filter[0]["stations"][0].to<JsonObject>();
  st["stationId"]             = true;
  st["active"]                = true;
  st["location"]["latitude"]  = true;
  st["location"]["longitude"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
  stationIds.clear();
  if (error)
  {
    return error;
  }
  struct Near { double km; int id; };
  std::vector<Near> near;
  const double cosLat = cos(lat * M_PI / 180.0);
  for (JsonObject region : doc.as<JsonArray>())
  {
    for (JsonObject o : region["stations"].as<JsonArray>())
    {
      if (!(o["active"] | true))
      {
        continue;
      }
      JsonVariant la = o["location"]["latitude"], lo = o["location"]["longitude"];
      const int id = o["stationId"] | 0;
      if (la.isNull() || lo.isNull() || id <= 0)
      {
        continue;
      }
      const double dLat = (la.as<double>() - lat) * 111.2;
      const double dLon = (lo.as<double>() - lon) * 111.2 * cosLat;
      near.push_back({sqrt(dLat * dLat + dLon * dLon), id});
    }
  }
  std::sort(near.begin(), near.end(),
            [](const Near &a, const Near &b) { return a.km < b.km; });
  for (size_t i = 0; i < near.size() && stationIds.size() < SVIVA_MAX_STATIONS; ++i)
  {
    if (i > 0 && near[i].km > SVIVA_RADIUS_KM)
    {
      break;
    }
    stationIds.push_back(near[i].id);
  }
  if (stationIds.empty())
  {
    return DeserializationError::EmptyInput;
  }
  return error;
} // end deserializeSvivaStations

/* /v1/envista/stations/index/latest: one row per station in `data` (the
 * regionsIds parameter is ignored for a guest, so the whole country comes
 * back), each with the station's current index and the pollutant that set
 * it. Keeps only the id and index of each row and takes the lowest (worst)
 * index among the given stations.
 */
DeserializationError deserializeSvivaIndex(WiFiClient &json,
                                           const std::vector<int> &stationIds,
                                           bool &found, int &index)
{
  // the reply is an object (the first station's row with a `data` array of
  // every station's) -- only the array matters
  JsonDocument filter;
  filter["data"][0]["stationId"] = true;
  filter["data"][0]["index"]     = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
  found = false;
  if (error)
  {
    return error;
  }
  for (JsonObject row : doc["data"].as<JsonArray>())
  {
    const int id = row["stationId"] | 0;
    if (row["index"].isNull())
    {
      continue;
    }
    for (int want : stationIds)
    {
      if (id == want)
      {
        const int v = static_cast<int>(std::round(row["index"].as<float>()));
        if (!found || v < index)
        {
          index = v;
        }
        found = true;
        break;
      }
    }
  }
  return error;
} // end deserializeSvivaIndex

/* Parses Open-Meteo's Air Quality API response, used for UV index and air
 * pollutant concentrations (weather.gov does not provide either). The most
 * recent OWM_NUM_AIR_POLLUTION hourly values are kept, oldest first, matching
 * the ordering expected by calc_aqi()/avg_conc().
 */
DeserializationError deserializeAirQuality(WiFiClient &json,
                                           owm_resp_air_pollution_t &r,
                                           float &uvi)
{
  // Keep only the pollutant arrays that are read. The unfiltered document
  // also held the `time` array (one ISO string per hour -- the single
  // largest allocation in the response), `hourly_units`, and the request
  // echo. Whole arrays are kept when the filter marks the key true.
  JsonDocument filter;
  JsonObject hourlyFilter = filter["hourly"].to<JsonObject>();
  hourlyFilter["pm10"]             = true;
  hourlyFilter["pm2_5"]            = true;
  hourlyFilter["carbon_monoxide"]  = true;
  hourlyFilter["nitrogen_dioxide"] = true;
  hourlyFilter["sulphur_dioxide"]  = true;
  hourlyFilter["ozone"]            = true;
  hourlyFilter["uv_index"]         = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  JsonObject hourly   = doc["hourly"];
  JsonArray  pm10arr  = hourly["pm10"];
  JsonArray  pm25arr  = hourly["pm2_5"];
  JsonArray  coarr    = hourly["carbon_monoxide"];
  JsonArray  no2arr   = hourly["nitrogen_dioxide"];
  JsonArray  so2arr   = hourly["sulphur_dioxide"];
  JsonArray  o3arr    = hourly["ozone"];
  JsonArray  uviarr   = hourly["uv_index"];

  int n = pm10arr.size();
  for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
  {
    int srcIdx = n - OWM_NUM_AIR_POLLUTION + i;
    r.components.no[i]  = 0.f; // not provided by Open-Meteo
    r.components.nh3[i] = 0.f; // not provided by Open-Meteo
    if (srcIdx < 0)
    { // an hour the reply does not reach is unknown, not clean air: NAN here
      // keeps it out of the count of real hours below, and the fill after
      // that gives it the nearest hour that has a value
      r.components.pm10[i]  = NAN;
      r.components.pm2_5[i] = NAN;
      r.components.co[i]    = NAN;
      r.components.no2[i]   = NAN;
      r.components.so2[i]   = NAN;
      r.components.o3[i]    = NAN;
      continue;
    }
    r.components.pm10[i]  = pm10arr[srcIdx] | NAN;
    r.components.pm2_5[i] = pm25arr[srcIdx] | NAN;
    r.components.co[i]    = coarr[srcIdx]   | NAN;
    r.components.no2[i]   = no2arr[srcIdx]  | NAN;
    r.components.so2[i]   = so2arr[srcIdx]  | NAN;
    r.components.o3[i]    = o3arr[srcIdx]   | NAN;
  }

  // An hour the model has no value for used to be read as 0 -- clean air --
  // and pulled the index down. It now takes the nearest hour that has one,
  // and the whole reading only counts when at least half of its hours of
  // fine particles or ozone are real.
  int real = 0;
  for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
  {
    if (!std::isnan(r.components.pm2_5[i]) || !std::isnan(r.components.o3[i]))
    {
      ++real;
    }
  }
  float *series[] = {r.components.pm10, r.components.pm2_5, r.components.co,
                     r.components.no2, r.components.so2, r.components.o3};
  for (float *s : series)
  {
    float last = NAN;
    for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
    {
      if (std::isnan(s[i])) {s[i] = last;} else {last = s[i];}
    }
    last = NAN;
    for (int i = OWM_NUM_AIR_POLLUTION - 1; i >= 0; --i)
    {
      if (std::isnan(s[i])) {s[i] = last;} else {last = s[i];}
    }
    for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
    {
      if (std::isnan(s[i])) {s[i] = 0.f;} // a pollutant with no readings at all
    }
  }
  r.valid = (real >= OWM_NUM_AIR_POLLUTION / 2);

  uvi = (n > 0) ? (uviarr[n - 1] | NAN) : NAN;

  return error;
} // end deserializeAirQuality

/* Parses AirNow's current observations endpoint
 * (/aq/data/ site-level NowCast feed). The response is an array of per-
 * station, per-pollutant rows (O3, PM2.5, PM10, ...) each carrying an
 * official pre-computed NowCast US EPA AQI. Per EPA practice, the reported
 * overall AQI is the maximum across rows.
 *
 * aqi is left untouched if the response contains no observations (no
 * monitoring station within the search radius), so callers should
 * initialize it to -1.
 */
DeserializationError deserializeAirNow(WiFiClient &json, int &aqi)
{
  JsonDocument filter;
  filter[0]["AQI"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  for (JsonObject obs : doc.as<JsonArray>())
  {
    int paramAqi = obs["AQI"] | -1;
    if (paramAqi > aqi)
    {
      aqi = paramAqi;
    }
  }

  return error;
} // end deserializeAirNow

/* Parses the Google Pollen API forecast:lookup response (days=1):
 * dailyInfo[0].pollenTypeInfo[] entries keyed by code TREE/GRASS/WEED,
 * each with indexInfo.value = Universal Pollen Index 0-5. indexInfo is
 * absent out of season, which counts as 0.
 */
DeserializationError deserializePollen(WiFiClient &json,
                                       pollen_info_t &pollen)
{
  JsonDocument filter;
  filter["dailyInfo"][0]["pollenTypeInfo"][0]["code"] = true;
  filter["dailyInfo"][0]["pollenTypeInfo"][0]["indexInfo"]["value"] = true;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json,
                                         DeserializationOption::Filter(filter));
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif
  if (error)
  {
    return error;
  }

  pollen.tree = pollen.grass = pollen.weed = 0;
  for (JsonObject t : doc["dailyInfo"][0]["pollenTypeInfo"].as<JsonArray>())
  {
    const char *code = t["code"] | "";
    int upi = t["indexInfo"]["value"] | 0;
    if      (strcmp(code, "TREE") == 0)  {pollen.tree = upi;}
    else if (strcmp(code, "GRASS") == 0) {pollen.grass = upi;}
    else if (strcmp(code, "WEED") == 0)  {pollen.weed = upi;}
  }
  pollen.max_upi = std::max(pollen.tree,
                            std::max(pollen.grass, pollen.weed));

  return error;
} // end deserializePollen
