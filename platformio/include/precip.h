/* Daily precipitation amounts for the forecast row: weather.gov's gridpoint
 * QPF where it reaches, Open-Meteo beyond.
 *
 * Kept free of Arduino types on purpose: the day-bucketing logic below is the
 * part that is easy to get subtly wrong (6-hour UTC buckets against local
 * calendar days, DST, "today" starting now rather than at midnight), and this
 * way it compiles and runs on a desktop against saved API responses.
 */
#ifndef __PRECIP_H__
#define __PRECIP_H__

#include <cstdint>
#include <vector>

/* One quantitativePrecipitation bucket from weather.gov's raw gridpoint
 * (/gridpoints/WFO/x,y). Buckets are 1-6 hours long (occasionally 12),
 * aligned to UTC, and reach ~3 days out; the value is liquid-equivalent
 * precipitation over the whole bucket, in mm.
 */
typedef struct qpf_bucket
{
  int64_t start;    // unix, UTC
  int32_t seconds;  // bucket length
  float   mm;       // liquid-equivalent precipitation over the bucket
} qpf_bucket_t;

/* Open-Meteo daily precipitation_sum, one entry per local calendar day. */
#define OM_DAILY_MAX 8
typedef struct om_daily_precip
{
  int     n;
  int64_t time[OM_DAILY_MAX];  // local midnight, unix (timezone=auto)
  float   mm[OM_DAILY_MAX];    // liquid-equivalent, NaN where absent
} om_daily_precip_t;

// Which source a day's amount came from (owm_daily_t.precip_src).
#define PRECIP_SRC_NONE        0
#define PRECIP_SRC_NWS         1
#define PRECIP_SRC_OPEN_METEO  2

typedef struct daily_precip_pick
{
  float   mm;   // NaN when no source covered the day
  uint8_t src;  // PRECIP_SRC_*
} daily_precip_pick_t;

/* ISO 8601 duration as weather.gov writes it -- P[nD][T[nH][nM][nS]], e.g.
 * "PT6H", "P1D", "P1DT6H". Returns 0 for anything it does not understand
 * (months and years are never used by the gridpoint data).
 */
int32_t parseIsoDurationSeconds(const char *s);

/* The amount to show for the local calendar day containing `dayAnchor`.
 *
 * weather.gov wins wherever its buckets cover the part of that day that is
 * still ahead (from `now` for today, from midnight otherwise); 6-hour buckets
 * that straddle midnight are split pro rata. A day the buckets do not fully
 * reach falls back to Open-Meteo's daily sum for that date, and a day neither
 * source has comes back NaN.
 *
 * Uses the process time zone (TZ must be set, as main.cpp does at boot).
 */
daily_precip_pick_t pickDailyPrecip(int64_t dayAnchor, int64_t now,
                                    const std::vector<qpf_bucket_t> &qpf,
                                    const om_daily_precip_t &om);

#endif
