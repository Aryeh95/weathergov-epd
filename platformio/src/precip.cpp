/* Daily precipitation amounts -- see precip.h. Desktop-compilable on purpose.
 */
#include "precip.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <ctime>

int32_t parseIsoDurationSeconds(const char *s)
{
  if (!s || *s != 'P')
  {
    return 0;
  }
  ++s;
  bool inTime = false;
  int64_t total = 0;
  while (*s)
  {
    if (*s == 'T')
    {
      inTime = true;
      ++s;
      continue;
    }
    char *end = nullptr;
    long n = strtol(s, &end, 10);
    if (end == s || n < 0)
    {
      return 0;
    }
    s = end;
    switch (*s)
    {
      case 'D': total += n * 86400LL; break;
      case 'H': total += n * 3600LL;  break;
      case 'M':
        if (!inTime) return 0; // months: not a fixed length, never used here
        total += n * 60LL;
        break;
      case 'S': total += n; break;
      default:  return 0;
    }
    ++s;
  }
  return (total > INT32_MAX) ? 0 : static_cast<int32_t>(total);
} // end parseIsoDurationSeconds

/* Local midnight at or before t. */
static int64_t localMidnight(int64_t t)
{
  time_t tt = static_cast<time_t>(t);
  tm lt;
  localtime_r(&tt, &lt);
  lt.tm_hour = 0;
  lt.tm_min  = 0;
  lt.tm_sec  = 0;
  lt.tm_isdst = -1; // let mktime decide -- a DST switch day is 23 or 25 h
  return static_cast<int64_t>(mktime(&lt));
} // end localMidnight

daily_precip_pick_t pickDailyPrecip(int64_t dayAnchor, int64_t now,
                                    const std::vector<qpf_bucket_t> &qpf,
                                    const om_daily_precip_t &om)
{
  daily_precip_pick_t out = { NAN, PRECIP_SRC_NONE };
  if (dayAnchor <= 0)
  {
    return out;
  }

  const int64_t dayStart = localMidnight(dayAnchor);
  // 36 h on, floored to midnight: the next midnight whatever DST does.
  const int64_t dayEnd   = localMidnight(dayStart + 36 * 3600LL);

  // For today, what matters is what is still to come: a bucket that is
  // already behind us counts for nothing, and one straddling "now" counts
  // pro rata. Other days need covering from midnight.
  const int64_t needStart = std::max(dayStart, now);
  const int64_t need      = dayEnd - needStart;

  if (need > 0)
  {
    int64_t covered = 0;
    double  sum     = 0.0;
    for (const qpf_bucket_t &b : qpf)
    {
      if (b.seconds <= 0)
      {
        continue;
      }
      const int64_t s = std::max(b.start, needStart);
      const int64_t e = std::min(b.start + b.seconds, dayEnd);
      if (e > s)
      {
        covered += (e - s);
        sum     += static_cast<double>(b.mm) * static_cast<double>(e - s)
                   / static_cast<double>(b.seconds);
      }
    }
    // One hour of slack: the first bucket can start a little after "now"
    // when the grid was issued a few minutes ago, and that must not push a
    // whole day over to the other source.
    if (covered >= need - 3600)
    {
      out.mm  = static_cast<float>(sum);
      out.src = PRECIP_SRC_NWS;
      return out;
    }
  }

  // Open-Meteo stamps each day at local midnight for the coordinate's own
  // zone; allow a few hours in case that zone and TZ disagree on an edge.
  for (int k = 0; k < om.n; ++k)
  {
    if (std::llabs(om.time[k] - dayStart) <= 3 * 3600LL && !std::isnan(om.mm[k]))
    {
      out.mm  = om.mm[k];
      out.src = PRECIP_SRC_OPEN_METEO;
      return out;
    }
  }
  return out;
} // end pickDailyPrecip
