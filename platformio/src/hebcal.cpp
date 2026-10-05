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
 *
 * The arithmetic Hebrew calendar after Dershowitz & Reingold, "Calendrical
 * Calculations": the molad of Tishrei with its four postponements fixes
 * Rosh Hashanah, the year's length fixes Cheshvan and Kislev, and a 19-year
 * cycle adds Adar II in years 3, 6, 8, 11, 14, 17 and 19.
 */

#include <cstdio>
#include <cstring>
#include "_locale.h"
#include "hebcal.h"

// Fixed (R.D.) day numbers: days since the proleptic Gregorian 1 Jan 1 = 1.
static long gregorianToFixed(int y, int m, int d)
{
  const long y1 = y - 1;
  long days = 365L * y1 + y1 / 4 - y1 / 100 + y1 / 400
            + (367L * m - 362) / 12 + d;
  if (m > 2)
  {
    const bool leap = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
    days -= leap ? 1 : 2;
  }
  return days;
}

static const long HEBREW_EPOCH = -1373427L;  // R.D. of 1 Tishrei A.M. 1

bool hebcalIsLeapYear(int hYear)
{
  return ((7L * hYear + 1) % 19) < 7;
}

static int lastMonthOfYear(int hYear)
{
  return hebcalIsLeapYear(hYear) ? 13 : 12;
}

// Days from the epoch to the molad-based new year, before postponements.
static long elapsedDays(int hYear)
{
  const long months = (235L * hYear - 234) / 19;
  const long parts = 12084L + 13753L * months;
  long day = months * 29 + parts / 25920;
  if ((3L * (day + 1)) % 7 < 3)
  {
    day += 1;
  }
  return day;
}

static long newYearDelay(int hYear)
{
  const long ny0 = elapsedDays(hYear - 1);
  const long ny1 = elapsedDays(hYear);
  const long ny2 = elapsedDays(hYear + 1);
  if (ny2 - ny1 == 356)
  {
    return 2;
  }
  return (ny1 - ny0 == 382) ? 1 : 0;
}

static long hebrewNewYear(int hYear)
{
  return HEBREW_EPOCH + elapsedDays(hYear) + newYearDelay(hYear);
}

static int daysInYear(int hYear)
{
  return static_cast<int>(hebrewNewYear(hYear + 1) - hebrewNewYear(hYear));
}

static int lastDayOfMonth(int hYear, int hMonth)
{
  const int len = daysInYear(hYear);
  if (hMonth == 2 || hMonth == 4 || hMonth == 6 || hMonth == 10 || hMonth == 13)
  {
    return 29;
  }
  if (hMonth == 12 && !hebcalIsLeapYear(hYear))
  {
    return 29;                       // Adar in a common year
  }
  if (hMonth == 8 && len % 10 != 5)
  {
    return 29;                       // Cheshvan unless the year is "complete"
  }
  if (hMonth == 9 && len % 10 == 3)
  {
    return 29;                       // Kislev in a "deficient" year
  }
  return 30;
}

static long hebrewToFixed(int hYear, int hMonth, int hDay)
{
  long days = hebrewNewYear(hYear) + hDay - 1;
  if (hMonth < 7)
  {
    // Nisan .. Adar: the months from Tishrei to the end of the year, then
    // from Nisan up to this month
    for (int m = 7; m <= lastMonthOfYear(hYear); ++m)
    {
      days += lastDayOfMonth(hYear, m);
    }
    for (int m = 1; m < hMonth; ++m)
    {
      days += lastDayOfMonth(hYear, m);
    }
  }
  else
  {
    for (int m = 7; m < hMonth; ++m)
    {
      days += lastDayOfMonth(hYear, m);
    }
  }
  return days;
}

void hebcalFromGregorian(int year, int month, int day,
                         int &hYear, int &hMonth, int &hDay)
{
  const long fixed = gregorianToFixed(year, month, day);
  // a year guess from the mean year length, then step to the right one
  int y = static_cast<int>((fixed - HEBREW_EPOCH) / 365.246822206L) + 1;
  while (hebrewNewYear(y) <= fixed)
  {
    ++y;
  }
  --y;
  hYear = y;
  // the month: Tishrei onward for the first part of the year, Nisan onward
  // for the rest
  int m = (fixed < hebrewToFixed(y, 1, 1)) ? 7 : 1;
  while (fixed > hebrewToFixed(y, m, lastDayOfMonth(y, m)))
  {
    ++m;
  }
  hMonth = m;
  hDay = static_cast<int>(fixed - hebrewToFixed(y, m, 1) + 1);
}

int hebcalMonthName(int hYear, int hMonth)
{
  // LC_HEB_MON: 0 Tishrei 1 Cheshvan 2 Kislev 3 Tevet 4 Shevat 5 Adar
  // 6 Adar I 7 Adar II 8 Nisan 9 Iyar 10 Sivan 11 Tammuz 12 Av 13 Elul
  switch (hMonth)
  {
    case 7:  return 0;   // Tishrei
    case 8:  return 1;   // Cheshvan
    case 9:  return 2;   // Kislev
    case 10: return 3;   // Tevet
    case 11: return 4;   // Shevat
    case 12: return hebcalIsLeapYear(hYear) ? 6 : 5;  // Adar I, or plain Adar
    case 13: return 7;   // Adar II
    case 1:  return 8;   // Nisan
    case 2:  return 9;   // Iyar
    case 3:  return 10;  // Sivan
    case 4:  return 11;  // Tammuz
    case 5:  return 12;  // Av
    case 6:  return 13;  // Elul
    default: return 0;
  }
}

void hebcalNumeral(int n, char *out, size_t outSize)
{
  struct Letter { int value; const char *utf8; };
  static const Letter LETTERS[] = {
    {400, "ת"}, {300, "ש"}, {200, "ר"}, {100, "ק"}, {90, "צ"}, {80, "פ"},
    {70, "ע"}, {60, "ס"}, {50, "נ"}, {40, "מ"}, {30, "ל"}, {20, "כ"},
    {10, "י"}, {9, "ט"}, {8, "ח"}, {7, "ז"}, {6, "ו"}, {5, "ה"}, {4, "ד"},
    {3, "ג"}, {2, "ב"}, {1, "א"}};
  if (outSize == 0)
  {
    return;
  }
  out[0] = 0;
  n %= 1000;                           // the thousands are left off, as usual
  if (n <= 0)
  {
    return;
  }
  const char *parts[8];
  int count = 0;
  int rest = n;
  while (rest > 0 && count < 8)
  {
    // 15 and 16 would spell a divine name: ט"ו and ט"ז instead
    if (rest == 15) { parts[count++] = "ט"; parts[count++] = "ו"; break; }
    if (rest == 16) { parts[count++] = "ט"; parts[count++] = "ז"; break; }
    for (const Letter &l : LETTERS)
    {
      if (l.value <= rest)
      {
        parts[count++] = l.utf8;
        rest -= l.value;
        break;
      }
    }
  }
  size_t used = 0;
  for (int i = 0; i < count; ++i)
  {
    if (count > 1 && i == count - 1)
    {
      if (used + 1 < outSize) out[used++] = '"';   // gershayim before the last
    }
    const size_t len = strlen(parts[i]);
    if (used + len >= outSize) break;
    memcpy(out + used, parts[i], len);
    used += len;
  }
  if (count == 1 && used + 1 < outSize)
  {
    out[used++] = '\'';                            // geresh after a single letter
  }
  out[used] = 0;
}

static int64_t s_sunset = 0;

void hebcalSetSunset(int64_t sunsetUnix)
{
  s_sunset = sunsetUnix;
}

void hebcalFormat(char *out, size_t n, const tm *t, bool withYear,
                  bool withPrefix)
{
  if (n == 0)
  {
    return;
  }
  out[0] = 0;
  if (!t)
  {
    return;
  }
  int y = t->tm_year + 1900, m = t->tm_mon + 1, d = t->tm_mday;
  // the Hebrew day begins at sunset
  if (s_sunset > 0)
  {
    tm copy = *t;
    copy.tm_isdst = -1;
    const time_t when = mktime(&copy);
    if (when != static_cast<time_t>(-1)
        && static_cast<int64_t>(when) >= s_sunset
        && static_cast<int64_t>(when) < s_sunset + 12 * 3600)
    {
      copy = *t;
      copy.tm_mday += 1;
      copy.tm_hour = 12;
      copy.tm_isdst = -1;
      if (mktime(&copy) != static_cast<time_t>(-1))
      {
        y = copy.tm_year + 1900; m = copy.tm_mon + 1; d = copy.tm_mday;
      }
    }
  }
  int hy, hm, hd;
  hebcalFromGregorian(y, m, d, hy, hm, hd);
  const char *month = LC_HEB_MON[hebcalMonthName(hy, hm)];
  const char *prefix = withPrefix ? LC_HEBCAL_MONTH_PREFIX : "";
  if (LC_HEBCAL_LETTERS)
  {
    char dayS[16], yearS[24];
    hebcalNumeral(hd, dayS, sizeof(dayS));
    hebcalNumeral(hy, yearS, sizeof(yearS));
    if (withYear)
    {
      snprintf(out, n, "%s %s%s %s", dayS, prefix, month, yearS);
    }
    else
    {
      snprintf(out, n, "%s %s%s", dayS, prefix, month);
    }
  }
  else if (withYear)
  {
    snprintf(out, n, "%d %s%s %d", hd, prefix, month, hy);
  }
  else
  {
    snprintf(out, n, "%d %s%s", hd, prefix, month);
  }
}
