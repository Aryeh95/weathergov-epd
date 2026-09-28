/* Wording of the 1600x1200 layout (GDEB0709E01) for esp32-weather-epd.
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

#ifndef __TEXT709_H__
#define __TEXT709_H__

/* The words this layout adds to the ones the locale files already carry
 * (labels, units, moon phases and risk levels still come from the locale).
 * ENGLISH ONLY for now: they are kept together here so that they can move
 * into locales/locale_*.inc in one step once the layout has settled.
 *
 * Characters are Latin-1, as everywhere else in the project: \xB0 is the
 * degree sign, \xB7 the middle dot. \x7F is this layout's en dash, which
 * Latin-1 has no place for (see tools/gen_assets709.py).
 */

// the weather, by spell. Order: enum Kind in renderer709.cpp
static const char *const TXT709_KIND[13] = {
  "Sunny", "Mostly sunny", "Clear", "Mostly clear", "Partly cloudy",
  "Mostly cloudy", "Cloudy", "Fog", "Showers", "Rain", "Storms", "Snow",
  "Ice"};

#define TXT709_THIS_WEEK     "This week"
#define TXT709_NEXT_HOURS    "Next %d hours"
#define TXT709_TODAY         "Today"
#define TXT709_TEMPERATURE   "Temperature"
#define TXT709_DEW_POINT     "Dew point"
#define TXT709_RAIN          "Rain"
#define TXT709_UNTIL         "until"
#define TXT709_FROM          "from"
#define TXT709_THROUGH       "through"
#define TXT709_MORE          "more"
#define TXT709_OF_DAYLIGHT   "of daylight"
#define TXT709_SUNRISE_IN    "Sunrise in"
#define TXT709_LIT           "% lit"
#define TXT709_HOURS         "h"
#define TXT709_MINUTES       "min"
#define TXT709_UPDATED       "Updated"
#define TXT709_BATTERY       "Battery"
#define TXT709_DAYS_LEFT     "d left"
#define TXT709_WIFI          "WiFi"
#define TXT709_INDOOR        "Indoor"
#define TXT709_TAG_EPA       "(EPA)"
#define TXT709_TAG_MODEL     "(model)"
#define TXT709_SETUP_MODE    "Setup Mode"
#define TXT709_DOT           "  \xB7  "

#endif
