/* Right-to-left text shaping for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
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

#ifndef __RTL_H__
#define __RTL_H__

#include <Arduino.h>

/* UTF-8 text to what the panel font draws. For a right-to-left locale
 * (LC_RTL) the line comes back in visual order, transcoded to the Hebrew
 * font family's ISO-8859-8 page; otherwise unchanged. drawString and the
 * width helpers in renderer.cpp apply it to every string they draw or
 * measure. Standalone (depends only on String and LC_RTL) so that
 * tools/rtl_test can run it on a PC.
 */
String shapeText(const String &text);

#endif
