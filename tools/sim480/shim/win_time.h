/* MinGW's C runtime has no localtime_r; the firmware's sources (and the
 * simulator) call it. build.py force-includes this file on Windows.
 */
#pragma once
#ifdef _WIN32
#include <time.h>
#ifndef SIM_HAVE_LOCALTIME_R
#define SIM_HAVE_LOCALTIME_R
inline struct tm *localtime_r(const time_t *t, struct tm *out)
{
  localtime_s(out, t);
  return out;
}
#endif
// ...nor strlcpy, which settings.cpp uses
#ifdef __cplusplus
#include <cstring>
static inline size_t strlcpy(char *dst, const char *src, size_t size)
{
  const size_t n = strlen(src);
  if (size)
  {
    const size_t k = n < size - 1 ? n : size - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
  }
  return n;
}
#endif
#endif
