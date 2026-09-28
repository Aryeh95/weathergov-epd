/* Last good weather, kept for redrawing, for esp32-weather-epd.
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

#include <LittleFS.h>
#include <type_traits>
#include "snapshot.h"

namespace
{

const char *const PATH = "/last.bin";
const char *const PATH_TMP = "/last.tmp";
const uint32_t MAGIC = 0x31584557; // "WEX1"
const uint16_t VERSION = 1;
const size_t   MAX_TEXT = 200;     // longest string kept
const size_t   MAX_BYTES = 32768;  // a file larger than this is not ours

// Anything that changes how the fields below are laid out changes this
// number, and a file written by another build is then left unread.
const uint32_t LAYOUT = static_cast<uint32_t>(
    OWM_NUM_HOURLY * 1000003u + OWM_NUM_DAILY * 10007u
  + OWM_NUM_AIR_POLLUTION * 101u + sizeof(owm_components_t)
  + sizeof(int64_t) * 7u + sizeof(int) * 13u + sizeof(pollen_info_t));

struct Header
{
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t layout;
  uint32_t size;  // of what follows the header
  uint32_t crc;   // of what follows the header
};

uint32_t crc32(const uint8_t *p, size_t n)
{
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i)
  {
    c ^= p[i];
    for (int k = 0; k < 8; ++k)
    {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
  }
  return ~c;
}

/* Writes fields to a buffer or reads them back from it. Each structure has
 * one list of its fields, below, which serves both directions -- so that
 * what is read is what was written, in the same order.
 */
class Archive
{
public:
  Archive(std::vector<uint8_t> &buffer, bool reading)
    : buf(buffer), loading(reading), at(0), ok(true) {}

  template <typename T> void pod(T &v)
  {
    static_assert(std::is_trivially_copyable<T>::value, "not plain data");
    raw(&v, sizeof(v));
  }

  void str(String &s)
  {
    uint16_t n = loading ? 0
               : static_cast<uint16_t>(std::min<size_t>(s.length(), MAX_TEXT));
    pod(n);
    if (!loading)
    {
      buf.insert(buf.end(), s.c_str(), s.c_str() + n);
      return;
    }
    if (!ok || n > MAX_TEXT || at + n > buf.size())
    {
      ok = false;
      return;
    }
    char text[MAX_TEXT + 1];
    memcpy(text, buf.data() + at, n);
    text[n] = '\0';
    at += n;
    s = text;
  }

  bool good() const { return ok; }
  bool atEnd() const { return at == buf.size(); }
  bool reading() const { return loading; }

private:
  void raw(void *p, size_t n)
  {
    if (!loading)
    {
      const uint8_t *b = static_cast<const uint8_t *>(p);
      buf.insert(buf.end(), b, b + n);
      return;
    }
    if (!ok || at + n > buf.size())
    {
      ok = false;
      return;
    }
    memcpy(p, buf.data() + at, n);
    at += n;
  }

  std::vector<uint8_t> &buf;
  bool loading;
  size_t at;
  bool ok;
};

void io(Archive &a, owm_weather_t &w)
{
  a.pod(w.id);
  a.str(w.main);
  a.str(w.description);
  a.str(w.icon);
}

void io(Archive &a, owm_current_t &c)
{
  a.pod(c.dt);
  a.pod(c.sunrise);
  a.pod(c.sunset);
  a.pod(c.temp);
  a.pod(c.feels_like);
  a.pod(c.pressure);
  a.pod(c.humidity);
  a.pod(c.dew_point);
  a.pod(c.clouds);
  a.pod(c.uvi);
  a.pod(c.visibility);
  a.pod(c.wind_speed);
  a.pod(c.wind_gust);
  a.pod(c.wind_deg);
  io(a, c.weather);
}

void io(Archive &a, owm_hourly_t &h)
{
  a.pod(h.dt);
  a.pod(h.temp);
  a.pod(h.feels_like);
  a.pod(h.pressure);
  a.pod(h.humidity);
  a.pod(h.dew_point);
  a.pod(h.clouds);
  a.pod(h.uvi);
  a.pod(h.visibility);
  a.pod(h.wind_speed);
  a.pod(h.wind_gust);
  a.pod(h.wind_deg);
  a.pod(h.pop);
  io(a, h.weather);
}

void io(Archive &a, owm_daily_t &d)
{
  a.pod(d.dt);
  a.pod(d.temp);
  a.pod(d.feels_like);
  a.pod(d.pressure);
  a.pod(d.humidity);
  a.pod(d.dew_point);
  a.pod(d.clouds);
  a.pod(d.uvi);
  a.pod(d.visibility);
  a.pod(d.wind_speed);
  a.pod(d.wind_gust);
  a.pod(d.wind_deg);
  a.pod(d.pop);
  a.pod(d.rain);
  a.pod(d.snow);
  a.pod(d.precip_src);
  io(a, d.weather);
}

void io(Archive &a, owm_alerts_t &x)
{
  a.str(x.sender_name);
  a.str(x.event);
  a.pod(x.start);
  a.pod(x.end);
  a.str(x.description);
  a.str(x.tags);
}

void io(Archive &a, std::vector<owm_alerts_t> &alerts)
{
  uint16_t n = static_cast<uint16_t>(
                 std::min<size_t>(alerts.size(), OWM_NUM_ALERTS));
  a.pod(n);
  if (a.reading())
  {
    if (!a.good() || n > OWM_NUM_ALERTS)
    {
      n = 0;
    }
    alerts.clear();
    alerts.resize(n);
  }
  for (uint16_t i = 0; i < n; ++i)
  {
    io(a, alerts[i]);
  }
}

void io(Archive &a, owm_resp_air_pollution_t &r)
{
  a.pod(r.coord);
  a.pod(r.main_aqi);
  a.pod(r.components);
  a.pod(r.dt);
  a.pod(r.us_aqi);
  a.pod(r.valid);
}

void io(Archive &a, snapshot_meta_t &m)
{
  a.pod(m.when);
  a.pod(m.graphHours);
  a.pod(m.inTemp);
  a.pod(m.inHumidity);
  a.pod(m.pollen);
  a.str(m.refreshTime);
  a.str(m.date);
}

void everything(Archive &a, owm_current_t &current, owm_hourly_t *hourly,
                owm_daily_t *daily, std::vector<owm_alerts_t> &alerts,
                owm_resp_air_pollution_t &air, snapshot_meta_t &meta)
{
  io(a, meta);
  io(a, current);
  for (int i = 0; i < OWM_NUM_HOURLY; ++i)
  {
    io(a, hourly[i]);
  }
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    io(a, daily[i]);
  }
  io(a, alerts);
  io(a, air);
}

} // namespace

bool snapshotSave(const owm_current_t &current, const owm_hourly_t *hourly,
                  const owm_daily_t *daily,
                  const std::vector<owm_alerts_t> &alerts,
                  const owm_resp_air_pollution_t &air,
                  const snapshot_meta_t &meta)
{
  std::vector<uint8_t> body;
  body.reserve(8192);
  Archive a(body, false);
  // the one list of fields serves both directions and so takes everything
  // by reference; nothing is changed when writing
  everything(a, const_cast<owm_current_t &>(current),
             const_cast<owm_hourly_t *>(hourly),
             const_cast<owm_daily_t *>(daily),
             const_cast<std::vector<owm_alerts_t> &>(alerts),
             const_cast<owm_resp_air_pollution_t &>(air),
             const_cast<snapshot_meta_t &>(meta));

  Header h = {};
  h.magic = MAGIC;
  h.version = VERSION;
  h.layout = LAYOUT;
  h.size = static_cast<uint32_t>(body.size());
  h.crc = crc32(body.data(), body.size());

  if (!LittleFS.begin(false))
  {
    return false;
  }
  // to a second file first, so that a wake cut short leaves the old one
  File f = LittleFS.open(PATH_TMP, "w");
  if (!f)
  {
    return false;
  }
  const bool written =
       f.write(reinterpret_cast<const uint8_t *>(&h), sizeof(h)) == sizeof(h)
    && f.write(body.data(), body.size()) == body.size();
  f.close();
  if (!written)
  {
    LittleFS.remove(PATH_TMP);
    return false;
  }
  if (LittleFS.exists(PATH))
  {
    LittleFS.remove(PATH);
  }
  return LittleFS.rename(PATH_TMP, PATH);
} // end snapshotSave

bool snapshotLoad(owm_current_t &current, owm_hourly_t *hourly,
                  owm_daily_t *daily, std::vector<owm_alerts_t> &alerts,
                  owm_resp_air_pollution_t &air, snapshot_meta_t &meta)
{
  if (!LittleFS.begin(false) || !LittleFS.exists(PATH))
  {
    return false;
  }
  File f = LittleFS.open(PATH, "r");
  if (!f)
  {
    return false;
  }
  Header h = {};
  std::vector<uint8_t> body;
  bool ok = f.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h)
         && h.magic == MAGIC && h.version == VERSION && h.layout == LAYOUT
         && h.size <= MAX_BYTES && f.size() == sizeof(h) + h.size;
  if (ok)
  {
    body.resize(h.size);
    ok = f.read(body.data(), body.size()) == body.size()
      && crc32(body.data(), body.size()) == h.crc;
  }
  f.close();
  if (!ok)
  {
    return false;
  }
  Archive a(body, true);
  everything(a, current, hourly, daily, alerts, air, meta);
  return a.good() && a.atEnd();
} // end snapshotLoad

void snapshotForget()
{
  if (LittleFS.begin(false))
  {
    if (LittleFS.exists(PATH))
    {
      LittleFS.remove(PATH);
    }
    if (LittleFS.exists(PATH_TMP))
    {
      LittleFS.remove(PATH_TMP);
    }
  }
} // end snapshotForget
