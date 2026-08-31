/* Local measurement history for esp32-weather-epd.
 *
 * Persists a small ring of recent readings in NVS across deep-sleep wakes
 * and derives display hints from it:
 *   - barometric pressure trend over ~3h (the classic weather-change signal)
 *   - indoor temperature trend over ~3h
 *   - estimated days of battery runtime left, from the multi-day discharge
 *     slope
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

#ifndef __HISTORY_H__
#define __HISTORY_H__

#include <Arduino.h>
#include <Preferences.h>

// Trend direction constants (returned by the trend getters).
#define TREND_UNKNOWN (-2)
#define TREND_FALLING (-1)
#define TREND_STEADY    0
#define TREND_RISING    1

/* Records the current readings into the NVS history (at most one hourly
 * sample and one daily battery sample per call interval) and computes the
 * trends/estimate below. Call once per wake, after the sensor read, with
 * prefs open. pressureHpa/inTempC may be NAN when unavailable.
 */
void historyUpdate(time_t now, float pressureHpa, float inTempC,
                   uint32_t batMv, Preferences &prefs);

int historyPressureTrend();   // TREND_* over ~3h
int historyIndoorTrend();     // TREND_* over ~3h
int historyBatteryDaysLeft(); // days until LOW_BATTERY_VOLTAGE, -1 unknown

#endif
