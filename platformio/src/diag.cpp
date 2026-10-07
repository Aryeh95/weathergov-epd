/* Wake diagnostics for esp32-weather-epd.
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

#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "diag.h"

wake_diag_t wakeDiag = {};

void diagOutcome(uint8_t outcome, int code, const String &note)
{
  wakeDiag.outcome = outcome;
  wakeDiag.code = static_cast<int16_t>(code);
  if (!note.isEmpty())
  {
    strlcpy(wakeDiag.note, note.c_str(), sizeof(wakeDiag.note));
  }
}

void diagSave(unsigned long startTime, uint32_t sleepSeconds)
{
  wakeDiag.version = DIAG_VERSION;
  wakeDiag.awakeMs = millis() - startTime;
  wakeDiag.sleepS = sleepSeconds;
  const time_t now = time(nullptr);
  wakeDiag.when = (now > 1600000000L) ? static_cast<int64_t>(now) : 0;

  Preferences store;
  store.begin(NVS_NAMESPACE, false);
  store.putBytes("diag", &wakeDiag, sizeof(wakeDiag));
  if (wakeDiag.outcome != DIAG_OK)
  {
    store.putBytes("diagErr", &wakeDiag, sizeof(wakeDiag));
  }
  store.end();
}

static const char *outcomeText(uint8_t outcome)
{
  switch (outcome)
  {
  case DIAG_OK:      return "Weather updated";
  case DIAG_HELD:    return "Could not update; the last weather was left on the screen";
  case DIAG_WIFI:    return "No WiFi connection";
  case DIAG_CLOCK:   return "Could not get the time";
  case DIAG_WEATHER: return "The weather service did not answer";
  case DIAG_BATTERY: return "Battery low; did not update";
  case DIAG_STALE:   return "Could not update; the last weather was drawn again, marked as old";
  default:           return "Unknown";
  }
}

static void recordToJson(JsonObject o, const wake_diag_t &d)
{
  o["outcome"]    = d.outcome;
  o["summary"]    = outcomeText(d.outcome);
  o["code"]       = d.code;
  o["when"]       = d.when;
  o["awake_ms"]   = d.awakeMs;
  o["wifi_ms"]    = d.wifiMs;
  o["clock_ms"]   = d.clockMs;
  o["fetch_ms"]   = d.fetchMs;
  o["draw_ms"]    = d.drawMs;
  o["sleep_s"]    = d.sleepS;
  o["battery_mv"] = d.batteryMv;
  o["rssi"]       = d.rssi;
  o["cause"]      = d.cause;
  // the text was cut to fit; make sure it ends
  char note[sizeof(d.note) + 1];
  memcpy(note, d.note, sizeof(d.note));
  note[sizeof(d.note)] = '\0';
  o["note"] = String(note);
}

void diagToJson(JsonDocument &doc)
{
  Preferences store;
  store.begin(NVS_NAMESPACE, true);
  static const char *const KEYS[2][2] = {{"diag", "last_wake"},
                                         {"diagErr", "last_problem"}};
  for (const auto &key : KEYS)
  {
    wake_diag_t d;
    if (store.isKey(key[0])
        && store.getBytesLength(key[0]) == sizeof(d)
        && store.getBytes(key[0], &d, sizeof(d)) == sizeof(d)
        && d.version == DIAG_VERSION)
    {
      recordToJson(doc[key[1]].to<JsonObject>(), d);
    }
  }
  store.end();
}
