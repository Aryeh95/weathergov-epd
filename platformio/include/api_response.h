/* API response deserialization declarations for esp32-weather-epd.
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

#ifndef __API_RESPONSE_H__
#define __API_RESPONSE_H__

#include <cstdint>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#define OWM_NUM_HOURLY        48 // number of hourly forecast periods retained
#define OWM_NUM_DAILY           7 // weather.gov's 12-hour period forecast covers 7 days
#define OWM_NUM_ALERTS          8 // weather.gov does not specify a limit, but if you need more alerts you are probably doomed.
#define OWM_NUM_AIR_POLLUTION 24 // hourly pollutant concentrations, used for rolling-average AQI calculations

typedef struct owm_weather
{
  int     id;               // Weather condition id (approximated from weather.gov's icon code)
  String  main;             // unused, reserved
  String  description;      // unused, reserved
  String  icon;             // "d" if daytime, "n" if nighttime
} owm_weather_t;

/*
 * Units – default: kelvin, metric: Celsius, imperial: Fahrenheit.
 */
typedef struct owm_temp
{
  float   morn;             // unused, reserved
  float   day;               // unused, reserved
  float   eve;               // unused, reserved
  float   night;             // unused, reserved
  float   min;              // Min daily temperature.
  float   max;              // Max daily temperature.
} owm_temp_t;

/*
 * This accounts for the human perception of weather. Units – default: kelvin, metric: Celsius, imperial: Fahrenheit.
 */
typedef struct owm_feels_like
{
  float   morn;             // unused, reserved
  float   day;               // unused, reserved
  float   eve;               // unused, reserved
  float   night;             // unused, reserved
} owm_owm_feels_like_t;

/*
 * Current weather conditions, assembled from the nearest NWS observation
 * station (falling back to the first hourly forecast period for any field
 * an automated station does not report).
 */
typedef struct owm_current
{
  int64_t dt;               // Current time, Unix, UTC
  int64_t sunrise;          // Sunrise time, Unix, UTC. Computed locally (see sun.h), weather.gov does not report it.
  int64_t sunset;           // Sunset time, Unix, UTC. Computed locally (see sun.h), weather.gov does not report it.
  float   temp;             // Temperature. Units - default: kelvin, metric: Celsius, imperial: Fahrenheit.
  float   feels_like;       // Apparent temperature (heat index or wind chill, when applicable). Units – default: kelvin, metric: Celsius, imperial: Fahrenheit.
  int     pressure;         // Atmospheric pressure on the sea level, hPa
  int     humidity;         // Humidity, %
  float   dew_point;        // Atmospheric temperature (varying according to pressure and humidity) below which water droplets begin to condense and dew can form. Units – default: kelvin, metric: Celsius, imperial: Fahrenheit.
  int     clouds;           // Cloudiness, % (approximated from NWS sky cover category)
  float   uvi;              // Current UV index (from Open-Meteo)
  int     visibility;       // Average visibility, metres.
  float   wind_speed;       // Wind speed. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  float   wind_gust;        // (where available) Wind gust. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  int     wind_deg;         // Wind direction, degrees (meteorological)
  owm_weather_t         weather;
} owm_current_t;

/*
 * Hourly forecast weather data, from weather.gov's gridpoint hourly forecast.
 */
typedef struct owm_hourly
{
  int64_t dt;               // Time of the forecasted data, unix, UTC
  float   temp;             // Temperature. Units - default: kelvin, metric: Celsius, imperial: Fahrenheit.
  float   feels_like;       // unused, reserved
  int     pressure;         // unused, reserved
  int     humidity;         // unused, reserved
  float   dew_point;        // unused, reserved
  int     clouds;           // Cloudiness, % (approximated from NWS sky cover category)
  float   uvi;              // unused, reserved
  int     visibility;       // unused, reserved
  float   wind_speed;       // Wind speed. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  float   wind_gust;        // Wind gust. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  int     wind_deg;         // Wind direction, degrees (meteorological)
  float   pop;              // Probability of precipitation. The values of the parameter vary between 0 and 1, where 0 is equal to 0%, 1 is equal to 100%
  owm_weather_t         weather;
} owm_hourly_t;

/*
 * Daily forecast weather data, merged from weather.gov's day/night 12-hour
 * forecast periods.
 */
typedef struct owm_daily
{
  int64_t dt;               // Time of the forecasted data (start of this day's first period), Unix, UTC
  owm_temp_t            temp;
  owm_owm_feels_like_t  feels_like; // unused, reserved
  int     pressure;         // unused, reserved
  int     humidity;         // unused, reserved
  float   dew_point;        // unused, reserved
  int     clouds;           // Cloudiness, % (approximated from NWS sky cover category)
  float   uvi;              // unused, reserved
  int     visibility;       // unused, reserved
  float   wind_speed;       // Wind speed. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  float   wind_gust;        // Wind gust. Units – default: metre/sec, metric: metre/sec, imperial: miles/hour.
  int     wind_deg;         // Wind direction, degrees (meteorological)
  float   pop;              // Probability of precipitation. The values of the parameter vary between 0 and 1, where 0 is equal to 0%, 1 is equal to 100%
  owm_weather_t         weather;
} owm_daily_t;

/*
 * Active weather alerts, from weather.gov's CAP alert feed.
 */
typedef struct owm_alerts
{
  String  sender_name;      // unused, reserved
  String  event;            // Alert event name
  int64_t start;            // Date and time the alert became effective, Unix, UTC
  int64_t end;               // Date and time the alert expires, Unix, UTC
  String  description;      // unused, reserved
  String  tags;              // lowercased copy of event, used to deduplicate alerts of the same type
} owm_alerts_t;

/*
 * Coordinates from the specified location (latitude, longitude)
 */
typedef struct owm_coord
{
  float   lat;
  float   lon;
} owm_coord_t;

typedef struct owm_components
{
  float   co[OWM_NUM_AIR_POLLUTION];    // Concentration of CO (Carbon monoxide), μg/m^3
  float   no[OWM_NUM_AIR_POLLUTION];    // unused (not provided by Open-Meteo), always 0
  float   no2[OWM_NUM_AIR_POLLUTION];   // Concentration of NO2 (Nitrogen dioxide), μg/m^3
  float   o3[OWM_NUM_AIR_POLLUTION];    // Concentration of O3 (Ozone), μg/m^3
  float   so2[OWM_NUM_AIR_POLLUTION];   // Concentration of SO2 (Sulphur dioxide), μg/m^3
  float   pm2_5[OWM_NUM_AIR_POLLUTION]; // Concentration of PM2.5 (Fine particles matter), μg/m^3
  float   pm10[OWM_NUM_AIR_POLLUTION];  // Concentration of PM10 (Coarse particulate matter), μg/m^3
  float   nh3[OWM_NUM_AIR_POLLUTION];   // unused (not provided by Open-Meteo), always 0
} owm_components_t;

/*
 * Air quality data. us_aqi holds the official US EPA AQI from AirNow
 * (airnow.gov) when an API key is configured and a nearby monitor reported;
 * otherwise -1 and the AQI is computed from Open-Meteo's hourly pollutant
 * concentrations instead.
 */
typedef struct owm_resp_air_pollution
{
  owm_coord_t      coord;   // unused, reserved
  int              main_aqi[OWM_NUM_AIR_POLLUTION]; // unused, reserved
  owm_components_t components;
  int64_t          dt[OWM_NUM_AIR_POLLUTION];       // unused, reserved
  int              us_aqi;  // official US EPA AQI from AirNow, -1 if unavailable
} owm_resp_air_pollution_t;

DeserializationError deserializeNWSPoints(WiFiClient &json, String &forecastUrl,
                                          String &forecastHourlyUrl,
                                          String &stationsUrl);
DeserializationError deserializeNWSForecastDaily(WiFiClient &json,
                                                 owm_daily_t *daily);
DeserializationError deserializeNWSForecastHourly(WiFiClient &json,
                                                  owm_hourly_t *hourly);
DeserializationError deserializeNWSStations(WiFiClient &json, String &stationId);
DeserializationError deserializeNWSObservation(WiFiClient &json,
                                               const owm_hourly_t &fallback,
                                               owm_current_t &current);
DeserializationError deserializeOpenMeteoCurrent(WiFiClient &json,
                                                 const owm_hourly_t &fallback,
                                                 owm_current_t &current);
void fillCurrentFromFallback(const owm_hourly_t &fallback, owm_current_t &current);
DeserializationError deserializeNWSAlerts(WiFiClient &json,
                                          std::vector<owm_alerts_t> &alerts);
DeserializationError deserializeAirQuality(WiFiClient &json,
                                           owm_resp_air_pollution_t &r,
                                           float &uvi);
DeserializationError deserializeAirNow(WiFiClient &json, int &aqi);

/* Pollen forecast (Google Pollen API). Universal Pollen Index per type,
 * 0-5 (0 also covers out-of-season/no-data); max_upi is the highest of
 * the three, -1 when the fetch failed or no key is configured.
 */
typedef struct pollen_info
{
  int tree;
  int grass;
  int weed;
  int max_upi;
} pollen_info_t;
DeserializationError deserializePollen(WiFiClient &json,
                                       pollen_info_t &pollen);

#endif
