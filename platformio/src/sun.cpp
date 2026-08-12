/* Sunrise/sunset calculation for esp32-weather-epd.
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

#include <cmath>
#include "sun.h"

// Note: these calculations must be done in double precision. A Julian Day
// Number is already ~7 significant digits before the fractional part that
// carries the time of day, which a 32-bit float cannot represent.

static const double DEG = M_PI / 180.0;

/* Returns the Julian Day Number for a Gregorian calendar date.
 */
static int64_t julianDayNumber(int y, int m, int d)
{
  int a  = (14 - m) / 12;
  int yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400
         - 32045;
}

bool calcSunriseSunset(int year, int month, int day, double lat, double lon,
                       int64_t &sunrise, int64_t &sunset)
{
  const int64_t jdn = julianDayNumber(year, month, day);

  // Julian centuries since J2000.0, taken at 00:00 UT on the given date.
  const double JD = static_cast<double>(jdn) - 0.5;
  const double T  = (JD - 2451545.0) / 36525.0;

  // geometric mean longitude and mean anomaly of the sun (degrees)
  const double L0 = fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
  const double M  = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  // eccentricity of earth's orbit
  const double e  = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);

  // equation of the center
  const double C = sin(M * DEG)     * (1.914602 - T * (0.004817 + 0.000014 * T))
                 + sin(2 * M * DEG) * (0.019993 - 0.000101 * T)
                 + sin(3 * M * DEG) * 0.000289;

  // apparent longitude of the sun, corrected for nutation and aberration
  const double trueLong = L0 + C;
  const double omega    = 125.04 - 1934.136 * T;
  const double lambda   = trueLong - 0.00569 - 0.00478 * sin(omega * DEG);

  // obliquity of the ecliptic, corrected
  const double eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059
                      - T * 0.001813))) / 60.0) / 60.0;
  const double eps  = eps0 + 0.00256 * cos(omega * DEG);

  const double decl = asin(sin(eps * DEG) * sin(lambda * DEG));

  // equation of time (minutes)
  const double y = tan(eps / 2.0 * DEG) * tan(eps / 2.0 * DEG);
  const double eqTime = 4.0 / DEG
                      * ( y * sin(2 * L0 * DEG)
                        - 2 * e * sin(M * DEG)
                        + 4 * e * y * sin(M * DEG) * cos(2 * L0 * DEG)
                        - 0.5 * y * y * sin(4 * L0 * DEG)
                        - 1.25 * e * e * sin(2 * M * DEG));

  // hour angle at sunrise/sunset. 90.833 degrees of zenith accounts for
  // atmospheric refraction and the apparent radius of the solar disc.
  const double cosHA = cos(90.833 * DEG) / (cos(lat * DEG) * cos(decl))
                     - tan(lat * DEG) * tan(decl);
  if (cosHA > 1.0 || cosHA < -1.0)
  { // the sun never crosses the horizon on this date at this latitude
    return false;
  }
  const double ha = acos(cosHA) / DEG;

  // minutes past 00:00 UTC
  const double solarNoon   = 720.0 - 4.0 * lon - eqTime;
  const double riseMinutes = solarNoon - 4.0 * ha;
  const double setMinutes  = solarNoon + 4.0 * ha;

  // Unix time of 00:00 UTC on the given date. Sunrise/sunset may fall outside
  // that UTC day (a late sunset west of Greenwich lands after 00:00 UTC the
  // next day), which the minute offsets below handle naturally.
  const int64_t midnightUTC = (jdn - 2440588LL) * 86400LL;

  sunrise = midnightUTC + static_cast<int64_t>(llround(riseMinutes * 60.0));
  sunset  = midnightUTC + static_cast<int64_t>(llround(setMinutes  * 60.0));
  return true;
} // end calcSunriseSunset
