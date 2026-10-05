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
#include "api_response.h" // OWM_NUM_HOURLY
#include "fonts/font_names.h"
#include "_locale.h" // LC_RTL

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

/* Reads and parses one configuration file. False if it is not there, is
 * empty, or is not JSON.
 */
static bool readConfig(const char *path, JsonDocument &doc)
{
  if (!LittleFS.exists(path))
  {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f)
  {
    return false;
  }
  const size_t size = f.size();
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error || size == 0 || !doc.is<JsonObject>())
  {
    Serial.println("[settings] cannot use " + String(path) + " ("
                   + String(error ? error.c_str() : "not a settings file")
                   + ")");
    return false;
  }
  return true;
} // end readConfig

// one number of a configuration: absent is fine, anything else has to be a
// number within [lo, hi]
static String numberProblem(const char *name, JsonVariantConst v, long lo,
                            long hi)
{
  if (v.isNull())
  {
    return "";
  }
  if (!v.is<long>() && !v.is<double>())
  {
    return String(name) + " must be a number";
  }
  const double n = v.as<double>();
  if (n < lo || n > hi)
  {
    return String(name) + " must be between " + String(lo) + " and "
           + String(hi);
  }
  return "";
}

// one text of a configuration: absent is fine, otherwise it has to fit
static String textProblem(const char *name, JsonVariantConst v, size_t most)
{
  if (v.isNull())
  {
    return "";
  }
  const char *t = v.as<const char *>();
  if (!t)
  {
    return String(name) + " must be text";
  }
  if (strlen(t) > most)
  {
    return String(name) + " is longer than " + String(most)
           + " characters";
  }
  return "";
}

String settingsProblem(JsonVariantConst doc)
{
  if (!doc.is<JsonObjectConst>())
  {
    return "not a settings file";
  }
  JsonVariantConst wifi = doc["wifi"], loc = doc["location"],
                   t = doc["time"], sleep = doc["sleep"],
                   battery = doc["battery"], portal = doc["portal"];
  const String problems[] = {
    textProblem("wifi.ssid", wifi["ssid"], sizeof(WIFI_SSID) - 1),
    textProblem("wifi.password", wifi["password"],
                sizeof(WIFI_PASSWORD) - 1),
    numberProblem("wifi.timeout_ms", wifi["timeout_ms"], 1000, 300000),
    textProblem("time.timezone", t["timezone"], sizeof(TIMEZONE) - 1),
    textProblem("time.time_format", t["time_format"],
                sizeof(TIME_FORMAT) - 1),
    textProblem("time.hour_format", t["hour_format"],
                sizeof(HOUR_FORMAT) - 1),
    textProblem("time.date_format", t["date_format"],
                sizeof(DATE_FORMAT) - 1),
    textProblem("time.refresh_time_format", t["refresh_time_format"],
                sizeof(REFRESH_TIME_FORMAT) - 1),
    numberProblem("time.ntp_timeout_ms", t["ntp_timeout_ms"], 1000,
                  120000),
    numberProblem("sleep.sleep_duration_minutes",
                  sleep["sleep_duration_minutes"], 2, 1440),
    numberProblem("sleep.wifi_retry_interval_minutes",
                  sleep["wifi_retry_interval_minutes"], 1, 1440),
    numberProblem("sleep.outage_grace_minutes",
                  sleep["outage_grace_minutes"], 0, 1440),
    numberProblem("sleep.bed_time_hour", sleep["bed_time_hour"], 0, 23),
    numberProblem("sleep.wake_time_hour", sleep["wake_time_hour"], 0, 23),
    numberProblem("sleep.hourly_graph_max", sleep["hourly_graph_max"], 8,
                  OWM_NUM_HOURLY),
    numberProblem("forecast_days", doc["forecast_days"], 5, 7),
    numberProblem("widget_rows", doc["widget_rows"], 5, 6),
    numberProblem("battery.max_voltage_mv", battery["max_voltage_mv"],
                  2500, 5500),
    numberProblem("battery.min_voltage_mv", battery["min_voltage_mv"],
                  2500, 5500),
    numberProblem("portal.timeout_minutes", portal["timeout_minutes"], 1,
                  240),
  };
  for (const String &p : problems)
  {
    if (!p.isEmpty())
    {
      return p;
    }
  }
  // coordinates are kept as text, since that is what the APIs are sent
  if (!loc["latitude"].isNull()
      && fabs(loc["latitude"].as<String>().toDouble()) > 90.0)
  {
    return "location.latitude must be between -90 and 90";
  }
  if (!loc["longitude"].isNull()
      && fabs(loc["longitude"].as<String>().toDouble()) > 180.0)
  {
    return "location.longitude must be between -180 and 180";
  }
  if (!battery["max_voltage_mv"].isNull()
      && !battery["min_voltage_mv"].isNull()
      && battery["max_voltage_mv"].as<long>()
           <= battery["min_voltage_mv"].as<long>())
  {
    return "battery.max_voltage_mv must be above battery.min_voltage_mv";
  }
  // WPA2 will not start a hotspot with a shorter password
  const char *ap = portal["ap_password"].as<const char *>();
  if (ap && strlen(ap) > 0 && (strlen(ap) < 8 || strlen(ap) > 63))
  {
    return "portal.ap_password must be 8 to 63 characters, or empty";
  }
  return "";
} // end settingsProblem

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

  // The portal keeps the previous configuration as /config.bak. If the
  // current one is missing or cannot be read -- a save cut short by a flat
  // battery, say -- the previous one is a far better fallback than the
  // compiled-in defaults, which know no WiFi network at all.
  JsonDocument doc;
  if (!readConfig("/config.json", doc))
  {
    if (readConfig("/config.bak", doc))
    {
      Serial.println("[settings] /config.json missing or unreadable, using "
                     "the previous configuration (/config.bak)");
    }
    else
    {
      Serial.println("[settings] no usable /config.json, using compiled-in "
                     "defaults from config.cpp. Run "
                     "`pio run --target uploadfs` after creating "
                     "data/config.json to apply your own settings.");
      return false;
    }
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
  // A device has to boot with whatever file it holds, so numbers that are
  // out of range are brought into it rather than refused. Each of these
  // crashes or reads out of bounds otherwise: the interval is a divisor,
  // the graph length an array index.
  SLEEP_DURATION      = constrain(SLEEP_DURATION, 2, 1440);
  WIFI_RETRY_INTERVAL = constrain(WIFI_RETRY_INTERVAL, 1, 1440);
  BED_TIME            = constrain(BED_TIME, 0, 23);
  WAKE_TIME           = constrain(WAKE_TIME, 0, 23);
  HOURLY_GRAPH_MAX    = constrain(HOURLY_GRAPH_MAX, 8, OWM_NUM_HOURLY);
  OUTAGE_GRACE = sleep["outage_grace_minutes"] | OUTAGE_GRACE;
  OUTAGE_GRACE = constrain(OUTAGE_GRACE, 0, 1440);

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

  // Font families by name. Only the families compiled in (config.h
  // FONT_INCLUDE_<Family>) are known. "font" is the main family (11 pt and
  // up: header, forecast, widget values, the big temperature) and falls
  // back to the first; "font_small" draws the labels, axis text and tags
  // below that and follows "font" when empty or unknown.
  auto fontIndex = [](const char *name, int fallback) {
    for (int i = 0; i < FONT_FAMILY_NAME_COUNT; ++i)
    {
      if (String(name).equalsIgnoreCase(FONT_FAMILY_NAMES[i]))
      {
        return i;
      }
    }
    return fallback;
  };
  FONT_FAMILY_INDEX       = fontIndex(doc["font"] | "", 0);
  FONT_SMALL_FAMILY_INDEX = fontIndex(doc["font_small"] | "",
                                      FONT_FAMILY_INDEX);
  // A right-to-left locale's text is transcoded to ISO-8859-8 (renderer.cpp,
  // shapeText), so both families must carry the Hebrew alphabet in their
  // high slots; a Latin-1 family there would draw accented letters for
  // every Hebrew word. Fall back to the first Hebrew family compiled in.
  if (LC_RTL)
  {
    int hebrew = -1;
    for (int i = 0; i < FONT_FAMILY_NAME_COUNT; ++i)
    {
      if (FONT_FAMILY_HEBREW[i]) { hebrew = i; break; }
    }
    if (hebrew < 0)
    {
      Serial.println("[font] WARNING: right-to-left locale but no Hebrew "
                     "font family is compiled in (FONT_INCLUDE_Heebo)");
    }
    else
    {
      if (!FONT_FAMILY_HEBREW[FONT_FAMILY_INDEX])
      {
        Serial.println("[font] " + String(FONT_FAMILY_NAMES[FONT_FAMILY_INDEX])
                       + " has no Hebrew, using "
                       + FONT_FAMILY_NAMES[hebrew]);
        FONT_FAMILY_INDEX = hebrew;
      }
      if (!FONT_FAMILY_HEBREW[FONT_SMALL_FAMILY_INDEX])
      {
        FONT_SMALL_FAMILY_INDEX = hebrew;
      }
    }
  }
  Serial.println("[font] " + String(FONT_FAMILY_NAMES[FONT_FAMILY_INDEX])
                 + ", small text "
                 + String(FONT_FAMILY_NAMES[FONT_SMALL_FAMILY_INDEX]));

  JsonObjectConst battery = doc["battery"];
  WARN_BATTERY_VOLTAGE     = battery["warn_voltage_mv"]     | WARN_BATTERY_VOLTAGE;
  LOW_BATTERY_VOLTAGE      = battery["low_voltage_mv"]      | LOW_BATTERY_VOLTAGE;
  VERY_LOW_BATTERY_VOLTAGE = battery["very_low_voltage_mv"] | VERY_LOW_BATTERY_VOLTAGE;
  CRIT_LOW_BATTERY_VOLTAGE = battery["crit_low_voltage_mv"] | CRIT_LOW_BATTERY_VOLTAGE;
  LOW_BATTERY_SLEEP_INTERVAL      = battery["low_sleep_interval_minutes"]      | LOW_BATTERY_SLEEP_INTERVAL;
  VERY_LOW_BATTERY_SLEEP_INTERVAL = battery["very_low_sleep_interval_minutes"] | VERY_LOW_BATTERY_SLEEP_INTERVAL;
  MAX_BATTERY_VOLTAGE = battery["max_voltage_mv"] | MAX_BATTERY_VOLTAGE;
  MIN_BATTERY_VOLTAGE = battery["min_voltage_mv"] | MIN_BATTERY_VOLTAGE;
  if (MAX_BATTERY_VOLTAGE <= MIN_BATTERY_VOLTAGE)
  { // the battery percentage divides by their difference
    MAX_BATTERY_VOLTAGE = MIN_BATTERY_VOLTAGE + 1000;
  }

  JsonObjectConst api = doc["api"];
  NWS_USER_AGENT = api["nws_user_agent"] | NWS_USER_AGENT;
  AIRNOW_APIKEY  = api["airnow_api_key"] | AIRNOW_APIKEY;
  POLLEN_APIKEY  = api["pollen_api_key"] | POLLEN_APIKEY;
  CURRENT_SOURCE = api["current_source"] | CURRENT_SOURCE;
  if (CURRENT_SOURCE != "google" && CURRENT_SOURCE != "nws")
  {
    CURRENT_SOURCE = "open-meteo";
  }
  FORECAST_SOURCE = api["forecast_source"] | FORECAST_SOURCE;
  if (FORECAST_SOURCE != "ims")
  {
    FORECAST_SOURCE = "nws";
  }
  IMS_LOCATION_ID = api["ims_location_id"] | IMS_LOCATION_ID;
  AQI_SOURCE = api["aqi_source"] | AQI_SOURCE;
  if (AQI_SOURCE != "airnow" && AQI_SOURCE != "israel" && AQI_SOURCE != "model")
  {
    AQI_SOURCE = "auto";
  }
  IL_AQ_STATION_ID = api["il_aq_station_id"] | IL_AQ_STATION_ID;

  JsonObjectConst portal = doc["portal"];
  PORTAL_AP_PASSWORD = portal["ap_password"]     | PORTAL_AP_PASSWORD;
  PORTAL_TIMEOUT     = portal["timeout_minutes"] | PORTAL_TIMEOUT;
  PORTAL_TIMEOUT     = constrain(PORTAL_TIMEOUT, 1, 240);
  HTTP_CLIENT_TCP_TIMEOUT = api["http_client_tcp_timeout_ms"] | HTTP_CLIENT_TCP_TIMEOUT;

  JsonObjectConst widgets = doc["widget_positions"];
  POS_SUNRISE    = widgets["sunrise"]     | POS_SUNRISE;
  POS_SUNSET     = widgets["sunset"]      | POS_SUNSET;
  POS_WIND       = widgets["wind"]        | POS_WIND;
  POS_HUMIDITY   = widgets["humidity"]    | POS_HUMIDITY;
  POS_DEWPOINT   = widgets["dewpoint"]    | POS_DEWPOINT;
  POS_HEAT_STRESS = widgets["heat_stress"] | POS_HEAT_STRESS;
  // Where heat stress is the everyday measure (the Hebrew locale), a config
  // that does not place the widget gets it in the dew point's slot.
  if (LC_PREFER_HEAT_STRESS && widgets["heat_stress"].isNull())
  {
    POS_HEAT_STRESS = POS_DEWPOINT;
    POS_DEWPOINT = -1;
  }
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
