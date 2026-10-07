/* Hebrew calendar for esp32-weather-epd.
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

#ifndef __HEBCAL_H__
#define __HEBCAL_H__

#include <cstddef>
#include <cstdint>
#include <ctime>

/* The Hebrew date of a Gregorian one (proleptic Gregorian, any year).
 * month is 1-13 in the Nisan-based numbering of the arithmetic calendar
 * (1 Nisan ... 6 Elul, 7 Tishrei ... 12 Adar / Adar I, 13 Adar II); day
 * 1-30. hebcalMonthName() turns the month into the index of its name in
 * LC_HEB_MON (Tishrei first; Adar, Adar I and Adar II separately).
 */
void hebcalFromGregorian(int year, int month, int day,
                         int &hYear, int &hMonth, int &hDay);
int  hebcalMonthName(int hYear, int hMonth);
bool hebcalIsLeapYear(int hYear);

/* The day's sunset (unix, UTC) for the date in question, so that the Hebrew
 * date turns over at sunset like the real one; 0 = unknown, turn over at
 * midnight. display_utils' setSunTimes() passes the computed sunset in.
 */
void hebcalSetSunset(int64_t sunsetUnix);

/* Writes the Hebrew date for the moment `t` (local time, as the clock has
 * it) in the locale's style: letters with geresh / gershayim in Hebrew
 * ("כ\"ד תשרי תשפ\"ז"), digits and transliterated names otherwise
 * ("24 Tishrei 5787"). withYear = false stops after the month; withPrefix
 * puts the locale's preposition before the month ("כ\"ד בתשרי", the form
 * of running text and holiday names, where the bare form is the one on
 * calendars and datelines). UTF-8, at most n-1 bytes. Behind _strftime's
 * %K / %J (bare) and %L / %N (with the prefix).
 */
void hebcalFormat(char *out, size_t n, const tm *t, bool withYear,
                  bool withPrefix = false);

/* Israel's holidays and observances for a Hebrew date: an index into
 * LC_HEB_HOLIDAYS (see _locale.h), or -1. yomTov is set for the days work
 * stops and candles are lit the evening before (Rosh Hashanah, Yom Kippur,
 * the first and last days of Sukkot and Pesach, Shavuot) -- Israeli
 * observance, one day. Observances fixed to weekdays (Yom HaShoah, Yom
 * HaZikaron, Yom HaAtzmaut, and the fasts that cannot fall on Shabbat) are
 * moved as the law moves them.
 */
int  hebcalHoliday(int hYear, int hMonth, int hDay, bool &yomTov);
/* The weekday (0 = Sunday) of a Hebrew date. */
int  hebcalWeekday(int hYear, int hMonth, int hDay);
/* The holiday name for the moment `t` (after sunset, the next day's), or "". */
const char *hebcalHolidayName(const tm *t);

/* What this civil day's sunset means: HEBCAL_EVE_NONE; HEBCAL_EVE_CANDLES,
 * Shabbat or a yom tov begins (candle lighting before sunset);
 * HEBCAL_EVE_HAVDALAH, Shabbat or a yom tov ends (after sunset);
 * HEBCAL_EVE_CANDLES_LATE, one holy day runs into another (candles after
 * nightfall). isYomTov tells a holiday from Shabbat for the label.
 */
enum { HEBCAL_EVE_NONE = 0, HEBCAL_EVE_CANDLES, HEBCAL_EVE_HAVDALAH,
       HEBCAL_EVE_CANDLES_LATE };
int  hebcalEvening(const tm *civilDay, bool &isYomTov);

/* A number 1-9999 in Hebrew letters (gematria): 15 and 16 as ט"ו / ט"ז, a
 * gershayim (ASCII ") before the last letter, a geresh (ASCII ') after a
 * single one; thousands are dropped as the year usually is. UTF-8.
 */
void hebcalNumeral(int n, char *out, size_t outSize);

#endif
