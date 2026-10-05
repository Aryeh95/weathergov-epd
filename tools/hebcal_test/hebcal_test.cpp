/* Desktop check for the Hebrew calendar (src/hebcal.cpp).
 *
 * Rosh Hashanah of several years against the published dates, a run of
 * consecutive days across a year boundary and a leap year's Adars, the
 * gematria numerals, and the sunset turnover.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "_locale.h"
#include "hebcal.h"

const char *LC_HEB_MON[14] = {"Tishrei", "Cheshvan", "Kislev", "Tevet", "Shevat", "Adar", "Adar I",
                              "Adar II", "Nisan", "Iyar", "Sivan", "Tammuz", "Av", "Elul"};
bool  LC_HEBCAL_LETTERS = false;
const char *LC_HEBCAL_MONTH_PREFIX = "";

static int failures = 0;
static void check(bool ok, const char *what)
{
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

int main()
{
  // 1 Tishrei (month 7 in the Nisan-based numbering) of each year
  static const struct { int y, m, d, hy; } RH[] = {
    {2023, 9, 16, 5784}, {2024, 10, 3, 5785}, {2025, 9, 23, 5786},
    {2026, 9, 12, 5787}, {2027, 10, 2, 5788}, {2028, 9, 21, 5789}};
  for (const auto &r : RH)
  {
    int hy, hm, hd;
    hebcalFromGregorian(r.y, r.m, r.d, hy, hm, hd);
    char buf[64];
    snprintf(buf, sizeof(buf), "Rosh Hashanah %d-%02d-%02d -> %d/%d/%d", r.y, r.m, r.d, hd, hm, hy);
    check(hy == r.hy && hm == 7 && hd == 1, buf);
  }
  // the day before is 29 Elul of the year before
  {
    int hy, hm, hd;
    hebcalFromGregorian(2026, 9, 11, hy, hm, hd);
    check(hy == 5786 && hm == 6 && hd == 29, "2026-09-11 is 29 Elul 5786");
    hebcalFromGregorian(2026, 10, 5, hy, hm, hd);
    check(hy == 5787 && hm == 7 && hd == 24, "2026-10-05 is 24 Tishrei 5787");
    // 5787 is a leap year (year 11 of the cycle): Purim 14 Adar II = 2027-03-23
    hebcalFromGregorian(2027, 3, 23, hy, hm, hd);
    check(hebcalIsLeapYear(5787) && hy == 5787 && hm == 13 && hd == 14, "2027-03-23 is 14 Adar II 5787");
    check(strcmp(LC_HEB_MON[hebcalMonthName(5787, 13)], "Adar II") == 0
          && strcmp(LC_HEB_MON[hebcalMonthName(5787, 12)], "Adar I") == 0
          && strcmp(LC_HEB_MON[hebcalMonthName(5786, 12)], "Adar") == 0, "Adar names by year");
    // Pesach 15 Nisan 5786 = 2026-04-02
    hebcalFromGregorian(2026, 4, 2, hy, hm, hd);
    check(hy == 5786 && hm == 1 && hd == 15, "2026-04-02 is 15 Nisan 5786");
  }
  // every day of a year maps to a distinct, consecutive Hebrew date
  {
    int prevY = 0, prevM = 0, prevD = 0;
    bool ok = true;
    for (int doy = 0; doy < 400 && ok; ++doy)
    {
      tm t = {};
      t.tm_year = 2026 - 1900; t.tm_mon = 8; t.tm_mday = 1 + doy; t.tm_hour = 12;
      mktime(&t);
      int hy, hm, hd;
      hebcalFromGregorian(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, hy, hm, hd);
      if (doy > 0)
      {
        const bool nextDay = (hy == prevY && hm == prevM && hd == prevD + 1);
        const bool nextMonth = (hy == prevY && hd == 1 && (prevD == 29 || prevD == 30)
                                && (hm == prevM + 1 || (prevM == 6 && hm == 7) ));
        const bool newYear = (hy == prevY + 1 && hm == 7 && hd == 1 && prevD == 29
                              && (prevM == 6));
        const bool adarWrap = (hy == prevY && hd == 1 && prevD >= 29
                               && ((prevM == 12 && hm == 13) || (prevM == 13 && hm == 1)
                                   || (prevM == 12 && hm == 1)));
        if (!(nextDay || nextMonth || newYear || adarWrap))
        {
          printf("  break at %04d-%02d-%02d: %d/%d/%d after %d/%d/%d\n", t.tm_year + 1900,
                 t.tm_mon + 1, t.tm_mday, hd, hm, hy, prevD, prevM, prevY);
          ok = false;
        }
      }
      prevY = hy; prevM = hm; prevD = hd;
    }
    check(ok, "400 consecutive days step by one");
  }
  // numerals
  {
    char b[32];
    hebcalNumeral(24, b, sizeof(b)); check(strcmp(b, "כ\"ד") == 0, "24 -> כ\"ד");
    hebcalNumeral(15, b, sizeof(b)); check(strcmp(b, "ט\"ו") == 0, "15 -> ט\"ו");
    hebcalNumeral(16, b, sizeof(b)); check(strcmp(b, "ט\"ז") == 0, "16 -> ט\"ז");
    hebcalNumeral(1, b, sizeof(b));  check(strcmp(b, "א'") == 0, "1 -> א'");
    hebcalNumeral(30, b, sizeof(b)); check(strcmp(b, "ל'") == 0, "30 -> ל'");
    hebcalNumeral(5787, b, sizeof(b)); check(strcmp(b, "תשפ\"ז") == 0, "5787 -> תשפ\"ז");
    hebcalNumeral(5800, b, sizeof(b)); check(strcmp(b, "ת\"ת") == 0, "5800 -> ת\"ת");
  }
  // formatting, digits and letters, and the sunset turnover
  {
    setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1);
    tzset();
    tm t = {};
    t.tm_year = 2026 - 1900; t.tm_mon = 9; t.tm_mday = 5; t.tm_hour = 13; t.tm_min = 30; t.tm_isdst = -1;
    char out[64];
    hebcalFormat(out, sizeof(out), &t, true);
    check(strcmp(out, "24 Tishrei 5787") == 0, out);
    hebcalFormat(out, sizeof(out), &t, false);
    check(strcmp(out, "24 Tishrei") == 0, out);
    LC_HEBCAL_LETTERS = true;
    LC_HEBCAL_MONTH_PREFIX = "ב";
    LC_HEB_MON[0] = "תשרי";   // as the Hebrew locale names it
    hebcalFormat(out, sizeof(out), &t, true);
    check(strcmp(out, "כ\"ד תשרי תשפ\"ז") == 0, out);
    hebcalFormat(out, sizeof(out), &t, true, true);
    check(strcmp(out, "כ\"ד בתשרי תשפ\"ז") == 0, out);
    // sunset 18:21 that day: at 19:00 it is already the 25th
    tm s = t; s.tm_hour = 18; s.tm_min = 21;
    hebcalSetSunset(static_cast<int64_t>(mktime(&s)));
    tm e = t; e.tm_hour = 19; e.tm_min = 0; e.tm_isdst = -1;
    hebcalFormat(out, sizeof(out), &e, false);
    check(strcmp(out, "כ\"ה תשרי") == 0, out);
    tm b = t; b.tm_hour = 18; b.tm_min = 0; b.tm_isdst = -1;
    hebcalFormat(out, sizeof(out), &b, false);
    check(strcmp(out, "כ\"ד תשרי") == 0, out);
  }
  printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
