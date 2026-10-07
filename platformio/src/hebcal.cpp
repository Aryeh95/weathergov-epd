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

// ---- holidays ---------------------------------------------------------

int hebcalWeekday(int hYear, int hMonth, int hDay)
{
  // R.D. day 1 was a Monday, so day 0 is a Sunday
  long f = hebrewToFixed(hYear, hMonth, hDay);
  return static_cast<int>(((f % 7) + 7) % 7);
}

// LC_HEB_HOLIDAYS indices
enum {
  H_ROSH_HASHANAH = 0, H_TZOM_GEDALIAH, H_YOM_KIPPUR, H_SUKKOT, H_CHOL_SUKKOT,
  H_HOSHANA_RABBAH, H_SIMCHAT_TORAH, H_HANUKKAH, H_ASARA_BTEVET, H_TU_BISHVAT,
  H_TAANIT_ESTHER, H_PURIM, H_SHUSHAN_PURIM, H_PESACH, H_CHOL_PESACH,
  H_SHVII_PESACH, H_YOM_HASHOAH, H_YOM_HAZIKARON, H_YOM_HAATZMAUT,
  H_LAG_BAOMER, H_YOM_YERUSHALAYIM, H_SHAVUOT, H_SHIVA_ASAR_BTAMMUZ,
  H_TISHA_BAV, H_TU_BAV, H_COUNT };

int hebcalHoliday(int hy, int hm, int hd, bool &yomTov)
{
  yomTov = false;
  const bool leap = hebcalIsLeapYear(hy);
  const int wd = hebcalWeekday(hy, hm, hd);
  switch (hm)
  {
    case 7: // Tishrei
      if (hd == 1 || hd == 2) { yomTov = true; return H_ROSH_HASHANAH; }
      // Tzom Gedaliah, 3 Tishrei, Sunday the 4th when the 3rd is Shabbat
      if (hd == 3 && wd != 6) return H_TZOM_GEDALIAH;
      if (hd == 4 && wd == 0 && hebcalWeekday(hy, 7, 3) == 6) return H_TZOM_GEDALIAH;
      if (hd == 10) { yomTov = true; return H_YOM_KIPPUR; }
      if (hd == 15) { yomTov = true; return H_SUKKOT; }
      if (hd >= 16 && hd <= 20) return H_CHOL_SUKKOT;
      if (hd == 21) return H_HOSHANA_RABBAH;
      if (hd == 22) { yomTov = true; return H_SIMCHAT_TORAH; }
      break;
    case 9: // Kislev: Hanukkah from the 25th
      if (hd >= 25) return H_HANUKKAH;
      break;
    case 10: // Tevet: Hanukkah's last days, then the fast of the 10th
    {
      const long day = hebrewToFixed(hy, 10, hd) - hebrewToFixed(hy, 9, 25);
      if (day >= 0 && day < 8) return H_HANUKKAH;
      if (hd == 10) return H_ASARA_BTEVET;
      break;
    }
    case 11: // Shevat
      if (hd == 15) return H_TU_BISHVAT;
      break;
    case 12: // Adar (or Adar I in a leap year)
      if (leap) break;
      // fallthrough: Purim is in the last Adar
    case 13:
      if (hm == 13 || !leap)
      {
        // Taanit Esther, 13 Adar; Thursday the 11th when the 13th is Shabbat
        if (hd == 13 && wd != 6) return H_TAANIT_ESTHER;
        if (hd == 11 && wd == 4 && hebcalWeekday(hy, hm, 13) == 6) return H_TAANIT_ESTHER;
        if (hd == 14) return H_PURIM;
        if (hd == 15) return H_SHUSHAN_PURIM;
      }
      break;
    case 1: // Nisan
      if (hd == 15) { yomTov = true; return H_PESACH; }
      if (hd >= 16 && hd <= 20) return H_CHOL_PESACH;
      if (hd == 21) { yomTov = true; return H_SHVII_PESACH; }
      {
        // Yom HaShoah, 27 Nisan: Thursday the 26th when the 27th is a
        // Friday, Monday the 28th when it is a Sunday
        const int wd27 = hebcalWeekday(hy, 1, 27);
        const int shoah = (wd27 == 5) ? 26 : (wd27 == 0) ? 28 : 27;
        if (hd == shoah) return H_YOM_HASHOAH;
      }
      break;
    case 2: // Iyar
    {
      // Yom HaAtzmaut, 5 Iyar: on a Friday or Shabbat it moves back to the
      // Thursday, on a Monday forward to the Tuesday; Yom HaZikaron is the
      // day before it
      const int wd5 = hebcalWeekday(hy, 2, 5);
      const int atzmaut = (wd5 == 5) ? 4 : (wd5 == 6) ? 3 : (wd5 == 1) ? 6 : 5;
      if (hd == atzmaut) return H_YOM_HAATZMAUT;
      if (hd == atzmaut - 1) return H_YOM_HAZIKARON;
      if (hd == 18) return H_LAG_BAOMER;
      if (hd == 28) return H_YOM_YERUSHALAYIM;
      break;
    }
    case 3: // Sivan
      if (hd == 6) { yomTov = true; return H_SHAVUOT; }
      break;
    case 4: // Tammuz: the fast of the 17th, Sunday the 18th after Shabbat
      if (hd == 17 && wd != 6) return H_SHIVA_ASAR_BTAMMUZ;
      if (hd == 18 && wd == 0 && hebcalWeekday(hy, 4, 17) == 6) return H_SHIVA_ASAR_BTAMMUZ;
      break;
    case 5: // Av: the fast of the 9th, Sunday the 10th after Shabbat
      if (hd == 9 && wd != 6) return H_TISHA_BAV;
      if (hd == 10 && wd == 0 && hebcalWeekday(hy, 5, 9) == 6) return H_TISHA_BAV;
      if (hd == 15) return H_TU_BAV;
      break;
    default:
      break;
  }
  return -1;
}

// the civil date of `t`, moved to the next day after sunset
static void civilHebrewDate(const tm *t, int &y, int &m, int &d)
{
  y = t->tm_year + 1900; m = t->tm_mon + 1; d = t->tm_mday;
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
}

const char *hebcalHolidayName(const tm *t)
{
  if (!t)
  {
    return "";
  }
  int y, m, d;
  civilHebrewDate(t, y, m, d);
  int hy, hm, hd;
  hebcalFromGregorian(y, m, d, hy, hm, hd);
  bool yt;
  const int h = hebcalHoliday(hy, hm, hd, yt);
  return (h >= 0 && h < H_COUNT) ? LC_HEB_HOLIDAYS[h] : "";
}

int hebcalEvening(const tm *civilDay, bool &isYomTov)
{
  isYomTov = false;
  if (!civilDay)
  {
    return HEBCAL_EVE_NONE;
  }
  // the Hebrew dates of this civil day and of the next (the one its sunset
  // begins)
  const int y = civilDay->tm_year + 1900, m = civilDay->tm_mon + 1;
  int hy, hm, hd, hy2, hm2, hd2;
  hebcalFromGregorian(y, m, civilDay->tm_mday, hy, hm, hd);
  tm next = *civilDay;
  next.tm_mday += 1;
  next.tm_hour = 12;
  next.tm_isdst = -1;
  mktime(&next);
  hebcalFromGregorian(next.tm_year + 1900, next.tm_mon + 1, next.tm_mday,
                      hy2, hm2, hd2);
  bool todayYT = false, tomorrowYT = false;
  hebcalHoliday(hy, hm, hd, todayYT);
  hebcalHoliday(hy2, hm2, hd2, tomorrowYT);
  const int wd = civilDay->tm_wday;      // 0 = Sunday
  const bool todayHoly = todayYT || wd == 6;
  const bool tomorrowHoly = tomorrowYT || wd == 5;   // Friday's sunset starts Shabbat
  if (tomorrowHoly)
  {
    isYomTov = tomorrowYT;
    return todayHoly ? HEBCAL_EVE_CANDLES_LATE : HEBCAL_EVE_CANDLES;
  }
  if (todayHoly)
  {
    isYomTov = todayYT;
    return HEBCAL_EVE_HAVDALAH;
  }
  return HEBCAL_EVE_NONE;
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
