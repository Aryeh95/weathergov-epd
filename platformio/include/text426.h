/* Wording of the 4.26in layout (DISP_4G_426) for esp32-weather-epd.
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

#ifndef __TEXT426_H__
#define __TEXT426_H__

/* The words this layout adds to the ones the locale files already carry.
 * English, and Hebrew where the locale runs right to left (he_IL, the one
 * such locale): kept together here so that they can move into
 * locales/locale_*.inc in one step once the layout has settled.
 */
#define TXT426_INDOOR (LC_RTL ? "בבית" : "Indoor")
#define TXT426_GUSTS  (LC_RTL ? "משבים" : "gusts")
#define TXT426_SETUP  (LC_RTL ? "מצב הגדרות" : "Setup Mode")
// today's high and low, beside the temperature (in Hebrew the two figures
// stand alone, "26° / 19°")
#define TXT426_HIGH   "H"
#define TXT426_LOW    "L"

#endif
