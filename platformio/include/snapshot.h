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

#ifndef __SNAPSHOT_H__
#define __SNAPSHOT_H__

#include <Arduino.h>
#include <vector>
#include "api_response.h"

/* The device fetches everything afresh each wake and keeps nothing, so with
 * no network there is nothing to draw. To be able to say "this forecast is
 * old" on the screen itself, the page has to be drawn again, and for that
 * the weather of the last good wake is kept in a file (/last.bin, a few
 * kilobytes) on the flash filesystem.
 */

// What was on the page besides the forecast itself.
typedef struct snapshot_meta
{
  int64_t       when;        // when the page was drawn, Unix UTC
  int32_t       graphHours;  // hours the graph showed (HOURLY_GRAPH_MAX then)
  float         inTemp;      // indoor readings, Celsius and %; NAN = none
  float         inHumidity;
  pollen_info_t pollen;
  String        refreshTime; // the status bar's time, as it was written
  String        date;        // the date under the city, as it was written
} snapshot_meta_t;

bool snapshotSave(const owm_current_t &current, const owm_hourly_t *hourly,
                  const owm_daily_t *daily,
                  const std::vector<owm_alerts_t> &alerts,
                  const owm_resp_air_pollution_t &air,
                  const snapshot_meta_t &meta);

// False, with everything passed in left in an unknown state, if there is no
// snapshot or it does not check out.
bool snapshotLoad(owm_current_t &current, owm_hourly_t *hourly,
                  owm_daily_t *daily, std::vector<owm_alerts_t> &alerts,
                  owm_resp_air_pollution_t &air, snapshot_meta_t &meta);

// The weather kept is for another place or source now.
void snapshotForget();

#endif
