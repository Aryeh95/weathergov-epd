/* Runtime settings loader for esp32-weather-epd.
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

#include <cstring>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "settings.h"

/* Copies a JSON string field into a fixed-size char buffer, truncating
 * safely. Leaves dst untouched if the field is absent/null.
 */
static void loadStrToCharArray(char *dst, size_t dstSize, JsonVariantConst v)
{
  if (v.isNull())
  {
    return;
  }
  const char *s = v.as<const char *>();
  if (s)
  {
    strlcpy(dst, s, dstSize);
  }
} // end loadStrToCharArray

bool loadSettings()
{
  // `true` formats the filesystem if it cannot be mounted (ex. first boot on
  // a fresh device); this only happens once, since a successful format also
  // mounts it.
  if (!LittleFS.begin(true))
  {
    Serial.println("[settings] Failed to mount LittleFS, using compiled-in "
                   "defaults from config.cpp");
    return false;
  }

  if (!LittleFS.exists("/config.json"))
  {
    Serial.println("[settings] /config.json not found, using compiled-in "
                   "defaults from config.cpp. Run "
                   "`pio run --target uploadfs` after creating "
                   "data/config.json to apply your own settings.");
    return false;
  }

  File f = LittleFS.open("/config.json", "r");
  if (!f)
  {
    Serial.println("[settings] Failed to open /config.json, using "
                   "compiled-in defaults from config.cpp");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error)
  {
    Serial.println("[settings] Failed to parse /config.json ("
                   + String(error.c_str())
                   + "), using compiled-in defaults from config.cpp");
    return false;
  }

  JsonObjectConst wifi = doc["wifi"];
  loadStrToCharArray(WIFI_SSID, sizeof(WIFI_SSID), wifi["ssid"]);
  loadStrToCharArray(WIFI_PASSWORD, sizeof(WIFI_PASSWORD), wifi["password"]);
  WIFI_TIMEOUT = wifi["timeout_ms"] | WIFI_TIMEOUT;
  loadStrToCharArray(STATIC_IP, sizeof(STATIC_IP), wifi["static_ip"]);
  loadStrToCharArray(STATIC_GATEWAY, sizeof(STATIC_GATEWAY), wifi["gateway"]);
  loadStrToCharArray(STATIC_SUBNET, sizeof(STATIC_SUBNET), wifi["subnet"]);
  loadStrToCharArray(STATIC_DNS, sizeof(STATIC_DNS), wifi["dns"]);

  JsonObjectConst location = doc["location"];
  LAT         = location["lat"]  | LAT;
  LON         = location["lon"]  | LON;
  CITY_STRING = location["city"] | CITY_STRING;

  JsonObjectConst t = doc["time"];
  loadStrToCharArray(TIMEZONE, sizeof(TIMEZONE), t["timezone"]);
  loadStrToCharArray(TIME_FORMAT, sizeof(TIME_FORMAT), t["time_format"]);
  loadStrToCharArray(HOUR_FORMAT, sizeof(HOUR_FORMAT), t["hour_format"]);
  loadStrToCharArray(DATE_FORMAT, sizeof(DATE_FORMAT), t["date_format"]);
  loadStrToCharArray(REFRESH_TIME_FORMAT, sizeof(REFRESH_TIME_FORMAT),
                     t["refresh_time_format"]);
  loadStrToCharArray(NTP_SERVER_1, sizeof(NTP_SERVER_1), t["ntp_server_1"]);
  loadStrToCharArray(NTP_SERVER_2, sizeof(NTP_SERVER_2), t["ntp_server_2"]);
  NTP_TIMEOUT = t["ntp_timeout_ms"] | NTP_TIMEOUT;

  JsonObjectConst sleep = doc["sleep"];
  SLEEP_DURATION   = sleep["sleep_duration_minutes"] | SLEEP_DURATION;
  WIFI_RETRY_INTERVAL = sleep["wifi_retry_interval_minutes"] | WIFI_RETRY_INTERVAL;
  BED_TIME         = sleep["bed_time_hour"]          | BED_TIME;
  WAKE_TIME        = sleep["wake_time_hour"]         | WAKE_TIME;
  HOURLY_GRAPH_MAX = sleep["hourly_graph_max"]       | HOURLY_GRAPH_MAX;

  GRAPH_DEWPOINT = doc["graph_dewpoint"] | GRAPH_DEWPOINT;

  FORECAST_DAYS = doc["forecast_days"] | FORECAST_DAYS;
  FORECAST_DAYS = constrain(FORECAST_DAYS, 5, 7);

  WIDGET_ROWS = doc["widget_rows"] | WIDGET_ROWS;
  WIDGET_ROWS = constrain(WIDGET_ROWS, 5, 6);

#ifdef MULTICOLOR_DISPLAY
  DARK_MODE = doc["dark_mode"] | DARK_MODE;
#else
  // Dark mode is a color-panel feature: pure white-on-black didn't earn
  // its keep on the single-color panels, so the setting is ignored there.
  DARK_MODE = false;
#endif

  JsonObjectConst battery = doc["battery"];
  WARN_BATTERY_VOLTAGE     = battery["warn_voltage_mv"]     | WARN_BATTERY_VOLTAGE;
  LOW_BATTERY_VOLTAGE      = battery["low_voltage_mv"]      | LOW_BATTERY_VOLTAGE;
  VERY_LOW_BATTERY_VOLTAGE = battery["very_low_voltage_mv"] | VERY_LOW_BATTERY_VOLTAGE;
  CRIT_LOW_BATTERY_VOLTAGE = battery["crit_low_voltage_mv"] | CRIT_LOW_BATTERY_VOLTAGE;
  LOW_BATTERY_SLEEP_INTERVAL      = battery["low_sleep_interval_minutes"]      | LOW_BATTERY_SLEEP_INTERVAL;
  VERY_LOW_BATTERY_SLEEP_INTERVAL = battery["very_low_sleep_interval_minutes"] | VERY_LOW_BATTERY_SLEEP_INTERVAL;
  MAX_BATTERY_VOLTAGE = battery["max_voltage_mv"] | MAX_BATTERY_VOLTAGE;
  MIN_BATTERY_VOLTAGE = battery["min_voltage_mv"] | MIN_BATTERY_VOLTAGE;

  JsonObjectConst api = doc["api"];
  NWS_USER_AGENT = api["nws_user_agent"] | NWS_USER_AGENT;
  AIRNOW_APIKEY  = api["airnow_api_key"] | AIRNOW_APIKEY;
  POLLEN_APIKEY  = api["pollen_api_key"] | POLLEN_APIKEY;

  JsonObjectConst portal = doc["portal"];
  PORTAL_AP_PASSWORD = portal["ap_password"]     | PORTAL_AP_PASSWORD;
  PORTAL_TIMEOUT     = portal["timeout_minutes"] | PORTAL_TIMEOUT;
  HTTP_CLIENT_TCP_TIMEOUT = api["http_client_tcp_timeout_ms"] | HTTP_CLIENT_TCP_TIMEOUT;

  JsonObjectConst widgets = doc["widget_positions"];
  POS_SUNRISE    = widgets["sunrise"]     | POS_SUNRISE;
  POS_SUNSET     = widgets["sunset"]      | POS_SUNSET;
  POS_WIND       = widgets["wind"]        | POS_WIND;
  POS_HUMIDITY   = widgets["humidity"]    | POS_HUMIDITY;
  POS_DEWPOINT   = widgets["dewpoint"]    | POS_DEWPOINT;
  POS_UVI        = widgets["uvi"]         | POS_UVI;
  POS_PRESSURE   = widgets["pressure"]    | POS_PRESSURE;
  POS_AIR_QUALITY = widgets["air_quality"] | POS_AIR_QUALITY;
  POS_MOON_PHASE = widgets["moon_phase"]  | POS_MOON_PHASE;
  POS_POLLEN     = widgets["pollen"]      | POS_POLLEN;
  POS_VISIBILITY = widgets["visibility"]  | POS_VISIBILITY;
  POS_INTEMP     = widgets["intemp"]      | POS_INTEMP;
  POS_INHUMIDITY = widgets["inhumidity"]  | POS_INHUMIDITY;

  Serial.println("[settings] Loaded /config.json");
  return true;
} // end loadSettings
