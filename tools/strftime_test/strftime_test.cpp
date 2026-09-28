// Desktop check for platformio/src/_strftime.cpp: a format string comes from
// config.json, so nothing it asks for may write past a buffer. See README.md.
#include "_locale.h"
#include "_strftime.h"
#include <cstdio>
#include <cstring>

const char *LC_D_T_FMT = "%a %b %e %H:%M:%S %Y";
const char *LC_D_FMT = "%m/%d/%y";
const char *LC_T_FMT = "%H:%M:%S";
const char *LC_T_FMT_AMPM = "%I:%M:%S %p";
const char *LC_AM_STR = "AM";
const char *LC_PM_STR = "PM";
const char *LC_DAY[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *LC_ABDAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *LC_MON[12] = {"January", "February", "March", "April", "May", "June", "July",
                          "August", "September", "October", "November", "December"};
const char *LC_ABMON[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *LC_ERA = "";
const char *LC_ERA_D_FMT = "";
const char *LC_ERA_D_T_FMT = "";
const char *LC_ERA_T_FMT = "";

int main()
{
  tm t = {};
  t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 28; t.tm_hour = 15; t.tm_min = 40; t.tm_wday = 1; t.tm_yday = 270;
  int fails = 0;
  struct { const char *format; const char *want; } SAME[] = {
    {"%a, %B %e", "Mon, September 28"}, {"%l:%M%P", " 3:40pm"}, {"%H:%M", "15:40"},
    {"%Y-%m-%d", "2026-09-28"}, {"%06Y", "002026"}, {"%F", "2026-09-28"}, {"100%%", "100%"}};
  for (auto &c : SAME)
  {
    char out[64] = {};
    _strftime(out, sizeof(out), c.format, &t);
    const bool ok = strcmp(out, c.want) == 0;
    printf("%-12s -> \"%s\"  %s\n", c.format, out, ok ? "ok" : "MISMATCH");
    fails += !ok;
  }
  // Widths no date needs. Each of these wrote past the formatter's 100-byte
  // scratch buffer; now the width is capped and the output still fits.
  const char *WIDE[] = {"%0100Y", "%0999999999Y", "%099999999999999999999Y", "%0100C", "%0100F", "%0100G",
                        "%+0100Y"};
  for (const char *format : WIDE)
  {
    char out[256];
    memset(out, 'x', sizeof(out));
    const size_t n = _strftime(out, sizeof(out), format, &t);
    const bool ok = n <= 80 && strlen(out) == n;
    printf("%-26s -> %3u characters  %s\n", format, static_cast<unsigned>(n), ok ? "ok" : "TOO LONG");
    fails += !ok;
  }
  // and an output buffer smaller than the field is truncated, not overrun
  {
    char out[8];
    char guard[8];
    memset(guard, 'g', sizeof(guard));
    _strftime(out, sizeof(out), "%064Y", &t);
    bool ok = strlen(out) < sizeof(out);
    for (char g : guard) {ok = ok && g == 'g';}
    printf("small output buffer          %s\n", ok ? "ok" : "OVERRUN");
    fails += !ok;
  }
  printf("%s\n", fails ? "FAILURES" : "ALL OK");
  return fails;
}
