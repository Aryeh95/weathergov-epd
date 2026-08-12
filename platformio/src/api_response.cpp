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

/* Converts a METAR-style sky cover abbreviation (as reported in an NWS
 * observation's cloudLayers) to an approximate cloud cover percentage.
 * Returns -1 if the abbreviation is not recognized.
 */
static int cloudAmountToPercent(const char *amount)
{
  String a(amount);
  if (a == "OVC")                    return 95;
  if (a == "BKN")                    return 70;
  if (a == "SCT")                    return 35;
  if (a == "FEW")                    return 15;
  if (a == "SKC" || a == "CLR" || a == "CAVOK") return 0;
  if (a == "VV")                     return 100;
  return -1;
} // end cloudAmountToPercent

/* Fetches the gridpoint forecast URLs and nearest observation stations URL
 * for a given lat/lon from weather.gov's /points endpoint.
 */
DeserializationError deserializeNWSPoints(WiFiClient &json, String &forecastUrl,
                                          String &forecastHourlyUrl,
                                          String &stationsUrl)
{
  JsonDocument filter;
  filter["properties"]["forecast"]             = true;
  filter["properties"]["forecastHourly"]       = true;
  filter["properties"]["observationStations"]  = true;

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

  forecastUrl       = doc["properties"]["forecast"]            .as<const char *>();
  forecastHourlyUrl = doc["properties"]["forecastHourly"]      .as<const char *>();
  stationsUrl       = doc["properties"]["observationStations"] .as<const char *>();

  return error;
} // end deserializeNWSPoints

/* Fetches the nearest observation station identifier from a gridpoint's
 * /stations endpoint.
 */
DeserializationError deserializeNWSStations(WiFiClient &json, String &stationId)
{
  JsonDocument filter;
  filter["features"][0]["properties"]["stationIdentifier"] = true;

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

  JsonArray features = doc["features"].as<JsonArray>();
  if (features.size() > 0)
  {
    stationId = features[0]["properties"]["stationIdentifier"].as<const char *>();
  }

  return error;
} // end deserializeNWSStations

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

  int i = 0;
  for (JsonObject p : doc["properties"]["periods"].as<JsonArray>())
  {
    hourly[i] = {};
    hourly[i].dt   = parseISO8601(p["startTime"].as<const char *>());
    hourly[i].temp = fahrenheit_to_kelvin(p["temperature"].as<float>());

    JsonVariant popVar = p["probabilityOfPrecipitation"]["value"];
    hourly[i].pop = popVar.isNull() ? 0.f : (popVar.as<float>() / 100.f);

    String dayNight, code;
    parseIconUrl(p["icon"].as<const char *>(), dayNight, code);
    applyNwsIcon(code, dayNight, hourly[i].weather, hourly[i].clouds);

    hourly[i].wind_speed = parseWindSpeedMph(p["windSpeed"].as<const char *>());
    hourly[i].wind_gust  = hourly[i].wind_speed;
    hourly[i].wind_deg   = compassToDegrees(p["windDirection"].as<const char *>());

    if (i == OWM_NUM_HOURLY - 1)
    {
      break;
    }
    ++i;
  }

  return error;
} // end deserializeNWSForecastHourly

/* Populates owm_current_t entirely from the first hourly forecast period,
 * used when a station observation is unavailable or could not be
 * fetched/parsed.
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
  current.dew_point   = NAN;
  current.visibility  = 10000;
} // end fillCurrentFromFallback

/* Parses the latest observation from a weather.gov observation station.
 * Automated stations frequently omit individual fields (most commonly wind
 * and visibility); any missing field falls back to the corresponding value
 * from the first hourly forecast period.
 */
DeserializationError deserializeNWSObservation(WiFiClient &json,
                                               const owm_hourly_t &fallback,
                                               owm_current_t &current)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] doc.overflowed() : " + String(doc.overflowed()));
#endif
#if DEBUG_LEVEL >= 2
  serializeJsonPretty(doc, Serial);
#endif

  if (error)
  {
    // could not fetch/parse the observation, fall back entirely to the
    // hourly forecast
    fillCurrentFromFallback(fallback, current);
    return error;
  }

  current = {};
  current.dt      = time(nullptr);
  current.weather = fallback.weather;

  JsonObject p = doc["properties"];

  JsonVariant tempVar = p["temperature"]["value"];
  current.temp = !tempVar.isNull() ? celsius_to_kelvin(tempVar.as<float>())
                                    : fallback.temp;

  JsonVariant dewVar = p["dewpoint"]["value"];
  current.dew_point = !dewVar.isNull() ? celsius_to_kelvin(dewVar.as<float>())
                                        : NAN;

  JsonVariant humVar = p["relativeHumidity"]["value"];
  current.humidity = !humVar.isNull()
                    ? static_cast<int>(std::round(humVar.as<float>()))
                    : 0;

  JsonVariant pressVar = p["barometricPressure"]["value"];
  current.pressure = !pressVar.isNull()
                    ? static_cast<int>(std::round(pressVar.as<float>() / 100.f))
                    : 1013;

  JsonVariant visVar = p["visibility"]["value"];
  current.visibility = !visVar.isNull()
                      ? static_cast<int>(visVar.as<float>())
                      : 10000;

  JsonVariant spdVar = p["windSpeed"]["value"];
  current.wind_speed = !spdVar.isNull()
                      ? kilometersperhour_to_meterspersecond(spdVar.as<float>())
                      : fallback.wind_speed;

  JsonVariant gustVar = p["windGust"]["value"];
  current.wind_gust = !gustVar.isNull()
                     ? kilometersperhour_to_meterspersecond(gustVar.as<float>())
                     : current.wind_speed;

  JsonVariant dirVar = p["windDirection"]["value"];
  current.wind_deg = !dirVar.isNull() ? static_cast<int>(dirVar.as<float>())
                                       : fallback.wind_deg;

  JsonVariant heatVar  = p["heatIndex"]["value"];
  JsonVariant chillVar = p["windChill"]["value"];
  if (!heatVar.isNull())
  {
    current.feels_like = celsius_to_kelvin(heatVar.as<float>());
  }
  else if (!chillVar.isNull())
  {
    current.feels_like = celsius_to_kelvin(chillVar.as<float>());
  }
  else
  {
    current.feels_like = current.temp;
  }

  int clouds = -1;
  for (JsonObject layer : p["cloudLayers"].as<JsonArray>())
  {
    const char *amount = layer["amount"] | "";
    int c = cloudAmountToPercent(amount);
    if (c >= 0)
    {
      clouds = c;
    }
  }
  current.clouds = (clouds >= 0) ? clouds : fallback.clouds;

  return error;
} // end deserializeNWSObservation

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

/* Parses Open-Meteo's Air Quality API response, used for UV index and air
 * pollutant concentrations (weather.gov does not provide either). The most
 * recent OWM_NUM_AIR_POLLUTION hourly values are kept, oldest first, matching
 * the ordering expected by calc_aqi()/avg_conc().
 */
DeserializationError deserializeAirQuality(WiFiClient &json,
                                           owm_resp_air_pollution_t &r,
                                           float &uvi)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
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
    {
      r.components.pm10[i]  = 0.f;
      r.components.pm2_5[i] = 0.f;
      r.components.co[i]    = 0.f;
      r.components.no2[i]   = 0.f;
      r.components.so2[i]   = 0.f;
      r.components.o3[i]    = 0.f;
      continue;
    }
    r.components.pm10[i]  = pm10arr[srcIdx] | 0.f;
    r.components.pm2_5[i] = pm25arr[srcIdx] | 0.f;
    r.components.co[i]    = coarr[srcIdx]   | 0.f;
    r.components.no2[i]   = no2arr[srcIdx]  | 0.f;
    r.components.so2[i]   = so2arr[srcIdx]  | 0.f;
    r.components.o3[i]    = o3arr[srcIdx]   | 0.f;
  }

  uvi = (n > 0) ? (uviarr[n - 1] | 0.f) : 0.f;

  return error;
} // end deserializeAirQuality

/* Parses AirNow's current observations endpoint
 * (/aq/observation/latLong/current/). The response is an array of per-
 * pollutant observations (O3, PM2.5, PM10, ...) each carrying an official
 * pre-computed US EPA AQI. Per EPA practice, the reported overall AQI is
 * the maximum across pollutants.
 *
 * aqi is left untouched if the response contains no observations (no
 * monitoring station within the search radius), so callers should
 * initialize it to -1.
 */
DeserializationError deserializeAirNow(WiFiClient &json, int &aqi)
{
  JsonDocument filter;
  filter[0]["ParameterName"] = true;
  filter[0]["AQI"]           = true;

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
