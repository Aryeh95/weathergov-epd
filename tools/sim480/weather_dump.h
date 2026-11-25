/* The weather of one moment as the firmware's parsers left it, in a plain
 * text file: sim480 writes it (SIM480_DUMP=<file>, --locale=en_US) after
 * parsing saved replies, and sim709 draws from it (build.py --weather=), so
 * the 7.09in preview shows the same weather as the 800x480 ones.
 * Copyright (C) 2026
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

#ifndef __WEATHER_DUMP_H__
#define __WEATHER_DUMP_H__

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "api_response.h"

/* Everything a page is drawn from, besides the settings. */
struct weather_dump_t
{
  long long now = 0;          // the moment drawn, unix
  std::string city, tz;       // CITY_STRING, the POSIX time zone
  std::string lat, lon;       // LAT, LON
  float inTemp = NAN, inHumidity = NAN;  // the indoor reading, Celsius, %
  owm_current_t current = {};
  owm_hourly_t hourly[OWM_NUM_HOURLY] = {};
  owm_daily_t daily[OWM_NUM_DAILY] = {};
  owm_resp_air_pollution_t air = {};
  pollen_info_t pollen = {};
  std::vector<owm_alerts_t> alerts;
};

namespace weather_dump
{
// one value a line: "key value", strings to the end of the line
inline void f(FILE *o, const char *k, double v)
{
  if (std::isnan(v)) fprintf(o, "%s nan\n", k);
  // whole numbers (times above all) exactly; the rest to a float's digits
  else if (v == std::floor(v) && std::fabs(v) < 1e15) fprintf(o, "%s %.0f\n", k, v);
  else fprintf(o, "%s %.9g\n", k, v);
}
inline void s(FILE *o, const char *k, const char *v)
{
  std::string t(v ? v : "");
  for (char &c : t) if (c == '\n' || c == '\r') c = ' ';
  fprintf(o, "%s %s\n", k, t.c_str());
}

/* The file's lines in order, each "key value". */
struct reader
{
  std::vector<std::pair<std::string, std::string>> lines;
  size_t at = 0;
  bool ok = true;
  const std::string &next(const char *key)
  {
    static const std::string none;
    if (at >= lines.size() || lines[at].first != key)
    {
      if (ok) fprintf(stderr, "weather file: expected '%s' at line %u\n", key, (unsigned)(at + 1));
      ok = false;
      return none;
    }
    return lines[at++].second;
  }
  double num(const char *key)
  {
    const std::string &v = next(key);
    return v.empty() ? NAN : strtod(v.c_str(), nullptr);
  }
};
} // namespace weather_dump

inline bool writeWeatherDump(const char *path, const weather_dump_t &w)
{
  using namespace weather_dump;
  FILE *o = fopen(path, "w");
  if (!o) return false;
  fprintf(o, "weather_dump 1\n");
  f(o, "now", (double)w.now);
  s(o, "city", w.city.c_str());
  s(o, "tz", w.tz.c_str());
  s(o, "lat", w.lat.c_str());
  s(o, "lon", w.lon.c_str());
  f(o, "in_temp", w.inTemp);
  f(o, "in_humidity", w.inHumidity);
  const owm_current_t &c = w.current;
  f(o, "c.dt", (double)c.dt); f(o, "c.sunrise", (double)c.sunrise); f(o, "c.sunset", (double)c.sunset);
  f(o, "c.temp", c.temp); f(o, "c.feels_like", c.feels_like); f(o, "c.pressure", c.pressure);
  f(o, "c.humidity", c.humidity); f(o, "c.dew_point", c.dew_point); f(o, "c.clouds", c.clouds);
  f(o, "c.uvi", c.uvi); f(o, "c.visibility", c.visibility); f(o, "c.wind_speed", c.wind_speed);
  f(o, "c.wind_gust", c.wind_gust); f(o, "c.wind_deg", c.wind_deg);
  f(o, "c.heat_stress", c.heat_stress); f(o, "c.heat_stress_level", c.heat_stress_level);
  f(o, "c.id", c.weather.id); s(o, "c.icon", c.weather.icon.c_str());
  s(o, "c.description", c.weather.description.c_str());
  for (const owm_hourly_t &h : w.hourly)
  {
    f(o, "h.dt", (double)h.dt); f(o, "h.temp", h.temp); f(o, "h.feels_like", h.feels_like);
    f(o, "h.humidity", h.humidity); f(o, "h.dew_point", h.dew_point); f(o, "h.clouds", h.clouds);
    f(o, "h.wind_speed", h.wind_speed); f(o, "h.wind_gust", h.wind_gust); f(o, "h.wind_deg", h.wind_deg);
    f(o, "h.pop", h.pop); f(o, "h.precip", h.precip); f(o, "h.id", h.weather.id);
    s(o, "h.icon", h.weather.icon.c_str());
  }
  for (const owm_daily_t &d : w.daily)
  {
    f(o, "d.dt", (double)d.dt); f(o, "d.min", d.temp.min); f(o, "d.max", d.temp.max);
    f(o, "d.clouds", d.clouds); f(o, "d.wind_speed", d.wind_speed); f(o, "d.wind_gust", d.wind_gust);
    f(o, "d.wind_deg", d.wind_deg); f(o, "d.pop", d.pop); f(o, "d.rain", d.rain); f(o, "d.snow", d.snow);
    f(o, "d.precip_src", d.precip_src); f(o, "d.id", d.weather.id); s(o, "d.icon", d.weather.icon.c_str());
  }
  const owm_resp_air_pollution_t &a = w.air;
  f(o, "air.us_aqi", a.us_aqi); f(o, "air.valid", a.valid);
  f(o, "air.il_valid", a.il_valid); f(o, "air.il_index", a.il_index);
  for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
  {
    f(o, "air.dt", (double)a.dt[i]); f(o, "air.co", a.components.co[i]);
    f(o, "air.no", a.components.no[i]); f(o, "air.no2", a.components.no2[i]);
    f(o, "air.o3", a.components.o3[i]); f(o, "air.so2", a.components.so2[i]);
    f(o, "air.pm2_5", a.components.pm2_5[i]); f(o, "air.pm10", a.components.pm10[i]);
    f(o, "air.nh3", a.components.nh3[i]);
  }
  f(o, "pollen.tree", w.pollen.tree); f(o, "pollen.grass", w.pollen.grass);
  f(o, "pollen.weed", w.pollen.weed); f(o, "pollen.max_upi", w.pollen.max_upi);
  f(o, "alerts", (double)w.alerts.size());
  for (const owm_alerts_t &al : w.alerts)
  {
    f(o, "a.start", (double)al.start); f(o, "a.end", (double)al.end);
    s(o, "a.event", al.event.c_str()); s(o, "a.tags", al.tags.c_str());
  }
  fprintf(o, "end\n");
  fclose(o);
  return true;
}

inline bool readWeatherDump(const char *path, weather_dump_t &w)
{
  using namespace weather_dump;
  FILE *in = fopen(path, "r");
  if (!in)
  {
    fprintf(stderr, "cannot read %s\n", path);
    return false;
  }
  reader r;
  char buf[4096];
  while (fgets(buf, sizeof(buf), in))
  {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) r.lines.push_back({line, ""});
    else r.lines.push_back({line.substr(0, sp), line.substr(sp + 1)});
  }
  fclose(in);
  if (r.num("weather_dump") != 1) return false;
  w.now = (long long)r.num("now");
  w.city = r.next("city"); w.tz = r.next("tz"); w.lat = r.next("lat"); w.lon = r.next("lon");
  w.inTemp = r.num("in_temp"); w.inHumidity = r.num("in_humidity");
  owm_current_t &c = w.current;
  c.dt = (int64_t)r.num("c.dt"); c.sunrise = (int64_t)r.num("c.sunrise"); c.sunset = (int64_t)r.num("c.sunset");
  c.temp = r.num("c.temp"); c.feels_like = r.num("c.feels_like"); c.pressure = (int)r.num("c.pressure");
  c.humidity = (int)r.num("c.humidity"); c.dew_point = r.num("c.dew_point"); c.clouds = (int)r.num("c.clouds");
  c.uvi = r.num("c.uvi"); c.visibility = (int)r.num("c.visibility"); c.wind_speed = r.num("c.wind_speed");
  c.wind_gust = r.num("c.wind_gust"); c.wind_deg = (int)r.num("c.wind_deg");
  c.heat_stress = r.num("c.heat_stress"); c.heat_stress_level = (int)r.num("c.heat_stress_level");
  c.weather.id = (int)r.num("c.id"); c.weather.icon = r.next("c.icon").c_str();
  c.weather.description = r.next("c.description").c_str();
  for (owm_hourly_t &h : w.hourly)
  {
    h.dt = (int64_t)r.num("h.dt"); h.temp = r.num("h.temp"); h.feels_like = r.num("h.feels_like");
    h.humidity = (int)r.num("h.humidity"); h.dew_point = r.num("h.dew_point"); h.clouds = (int)r.num("h.clouds");
    h.wind_speed = r.num("h.wind_speed"); h.wind_gust = r.num("h.wind_gust"); h.wind_deg = (int)r.num("h.wind_deg");
    h.pop = r.num("h.pop"); h.precip = r.num("h.precip"); h.weather.id = (int)r.num("h.id");
    h.weather.icon = r.next("h.icon").c_str();
  }
  for (owm_daily_t &d : w.daily)
  {
    d.dt = (int64_t)r.num("d.dt"); d.temp.min = r.num("d.min"); d.temp.max = r.num("d.max");
    d.clouds = (int)r.num("d.clouds"); d.wind_speed = r.num("d.wind_speed"); d.wind_gust = r.num("d.wind_gust");
    d.wind_deg = (int)r.num("d.wind_deg"); d.pop = r.num("d.pop"); d.rain = r.num("d.rain"); d.snow = r.num("d.snow");
    d.precip_src = (uint8_t)r.num("d.precip_src"); d.weather.id = (int)r.num("d.id");
    d.weather.icon = r.next("d.icon").c_str();
  }
  owm_resp_air_pollution_t &a = w.air;
  a.us_aqi = (int)r.num("air.us_aqi"); a.valid = r.num("air.valid") != 0;
  a.il_valid = r.num("air.il_valid") != 0; a.il_index = (int)r.num("air.il_index");
  for (int i = 0; i < OWM_NUM_AIR_POLLUTION; ++i)
  {
    a.dt[i] = (int64_t)r.num("air.dt"); a.components.co[i] = r.num("air.co");
    a.components.no[i] = r.num("air.no"); a.components.no2[i] = r.num("air.no2");
    a.components.o3[i] = r.num("air.o3"); a.components.so2[i] = r.num("air.so2");
    a.components.pm2_5[i] = r.num("air.pm2_5"); a.components.pm10[i] = r.num("air.pm10");
    a.components.nh3[i] = r.num("air.nh3");
  }
  w.pollen.tree = (int)r.num("pollen.tree"); w.pollen.grass = (int)r.num("pollen.grass");
  w.pollen.weed = (int)r.num("pollen.weed"); w.pollen.max_upi = (int)r.num("pollen.max_upi");
  const int n = (int)r.num("alerts");
  w.alerts.clear();
  for (int i = 0; i < n && r.ok; ++i)
  {
    owm_alerts_t al;
    al.start = (int64_t)r.num("a.start"); al.end = (int64_t)r.num("a.end");
    al.event = r.next("a.event").c_str(); al.tags = r.next("a.tags").c_str();
    w.alerts.push_back(al);
  }
  r.next("end");
  return r.ok;
}

#endif // __WEATHER_DUMP_H__
