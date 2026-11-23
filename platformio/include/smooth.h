/* Smooth curves through hourly or daily samples, shared by the renderers
 * of esp32-weather-epd.
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

#ifndef __SMOOTH_H__
#define __SMOOTH_H__

#include <algorithm>
#include <cmath>

/* A smooth curve through values at 0, 1, .. n-1 (cubic Hermite), read at
 * f (0 <= f <= n-1; n >= 2). Where the values turn -- a high or a low --
 * the curve is flat there, so it does not run past the figure that labels
 * it. Elsewhere the slope is the mean of its two sides, held to three times
 * the gentler one: forecasts come in whole degrees (64, 64, 65 ..), and
 * flattening at every repeat, as strict monotone curves do, terraced a
 * steady warm-up into steps. Across a plateau the curve may stray a few
 * hundredths of a degree.
 */
inline float smoothAt(const float *v, int n, float f)
{
  const int i = std::max(0, std::min(static_cast<int>(f), n - 2));
  const float u = f - i;
  auto slope = [&](int k) {
    if (k <= 0) return v[1] - v[0];
    if (k >= n - 1) return v[n - 1] - v[n - 2];
    const float a = v[k] - v[k - 1], b = v[k + 1] - v[k];
    if (a * b < 0 || (a == 0 && b == 0)) return 0.0f;   // a turn, or level
    float m = (a + b) / 2;
    if (a != 0 && b != 0)
    {
      const float lim = 3 * std::min(std::fabs(a), std::fabs(b));
      m = std::max(-lim, std::min(lim, m));
    }
    return m;
  };
  const float m0 = slope(i), m1 = slope(i + 1);
  const float h00 = (1 + 2 * u) * (1 - u) * (1 - u), h10 = u * (1 - u) * (1 - u);
  const float h01 = u * u * (3 - 2 * u), h11 = u * u * (u - 1);
  return h00 * v[i] + h10 * m0 + h01 * v[i + 1] + h11 * m1;
}

/* smoothAt over values that may have gaps (NaN): the curve runs through the
 * stretch of values around f, and is NaN where f touches a gap. f is
 * clamped to 0 .. n-1. A stretch of one value is that value.
 */
inline float smoothGapsAt(const float *v, int n, float f)
{
  if (n < 1) return NAN;
  f = std::max(0.0f, std::min(static_cast<float>(n - 1), f));
  const int a = static_cast<int>(f);
  const int b = std::min(n - 1, a + 1);
  if (std::isnan(v[a]) || std::isnan(v[b])) return NAN;
  int s = a, e = b;
  while (s > 0 && !std::isnan(v[s - 1])) --s;
  while (e < n - 1 && !std::isnan(v[e + 1])) ++e;
  if (e == s) return v[s];
  return smoothAt(v + s, e - s + 1, f - s);
}

#endif
