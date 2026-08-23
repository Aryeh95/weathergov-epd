/* Sunrise/sunset calculation declarations for esp32-weather-epd.
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

#ifndef __SUN_H__
#define __SUN_H__

#include <cstdint>

/* Calculates sunrise and sunset for the given calendar date and location.
 *
 * weather.gov does not report sunrise/sunset, so they are computed locally
 * using the NOAA solar position algorithm. This needs no network access and
 * agrees with published tables to well under a minute.
 *
 * Pass the LOCAL calendar date (year, full 4-digit; month, 1-12; day, 1-31)
 * and the location in decimal degrees, latitude positive north and longitude
 * positive east. sunrise and sunset are returned as Unix timestamps, UTC.
 *
 * Returns false if the sun neither rises nor sets on that date (polar day or
 * polar night), in which case sunrise and sunset are left untouched.
 */
bool calcSunriseSunset(int year, int month, int day, double lat, double lon,
                       int64_t &sunrise, int64_t &sunset);

/* Calculates the moon phase for the given moment (Unix timestamp, UTC).
 *
 * Returns a phase index 0-7:
 *   0 new moon        4 full moon
 *   1 waxing crescent 5 waning gibbous
 *   2 first quarter   6 last (third) quarter
 *   3 waxing gibbous  7 waning crescent
 * and writes the illuminated fraction (0-100, rounded) to illumPct if
 * non-NULL. Computed from the mean synodic month; accurate to well within a
 * day, which is finer than the 8-phase bucketing can show anyway.
 */
int calcMoonPhase(int64_t t, int *illumPct);

#endif
