/* Where the country-specific weather services answer, by coordinates.
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

#ifndef __COVERAGE_H__
#define __COVERAGE_H__

/* weather.gov and AirNow cover the United States and its territories; the
 * Israel Meteorological Service and the Ministry of Environmental
 * Protection's monitoring network cover Israel. The areas are boxes, so
 * they are generous along borders (southern Ontario, the edge of Mexico,
 * Israel's neighbours): they keep a service out where it plainly cannot
 * answer, not where it only might not. The portal page
 * (data/portal.html, COVERAGE) holds the same boxes.
 */
struct CoverageBox { float latLo, latHi, lonLo, lonHi; };

static const CoverageBox COVERAGE_US[] = {
  {24.3f, 49.5f, -125.0f, -66.8f},    // the 48 states
  {51.0f, 71.6f, -180.0f, -129.9f},   // Alaska
  {51.0f, 53.5f, 172.0f, 180.0f},     // the western Aleutians
  {18.8f, 22.4f, -160.4f, -154.6f},   // Hawaii
  {17.6f, 18.6f, -67.4f, -64.5f},     // Puerto Rico, US Virgin Islands
  {13.2f, 20.6f, 144.6f, 146.1f},     // Guam, Northern Marianas
  {-14.6f, -14.1f, -171.1f, -168.1f}, // American Samoa
};
static const CoverageBox COVERAGE_IL[] = {
  {29.45f, 33.35f, 34.25f, 35.90f},   // Israel, Eilat to the Golan
};

template <size_t N>
inline bool inCoverage(const CoverageBox (&boxes)[N], float lat, float lon)
{
  for (const CoverageBox &b : boxes)
  {
    if (lat >= b.latLo && lat <= b.latHi && lon >= b.lonLo && lon <= b.lonHi)
    {
      return true;
    }
  }
  return false;
}

inline bool inUS(float lat, float lon) { return inCoverage(COVERAGE_US, lat, lon); }
inline bool inIsrael(float lat, float lon) { return inCoverage(COVERAGE_IL, lat, lon); }

#endif
