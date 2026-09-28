// Stand-in for platformio/include/_locale.h, which needs Arduino.h. The
// formatter only reads the names of days and months from it.
#ifndef STRFTIME_TEST_LOCALE_H
#define STRFTIME_TEST_LOCALE_H
#include <algorithm>
using std::max;
using std::min;
extern const char *LC_D_T_FMT;
extern const char *LC_D_FMT;
extern const char *LC_T_FMT;
extern const char *LC_T_FMT_AMPM;
extern const char *LC_AM_STR;
extern const char *LC_PM_STR;
extern const char *LC_DAY[7];
extern const char *LC_ABDAY[7];
extern const char *LC_MON[12];
extern const char *LC_ABMON[12];
extern const char *LC_ERA;
extern const char *LC_ERA_D_FMT;
extern const char *LC_ERA_D_T_FMT;
extern const char *LC_ERA_T_FMT;
#endif
