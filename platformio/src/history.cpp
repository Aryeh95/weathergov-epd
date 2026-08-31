/* Local measurement history for esp32-weather-epd.
 * See include/history.h for the interface description and license.
 */

#include <cmath>
#include <cstring>
#include "config.h"
#include "history.h"

// Hourly ring: 16 samples (~16h) of pressure + indoor temperature.
// 8 bytes/sample, 128-byte NVS blob rewritten at most once per hour.
struct hour_rec_t
{
  uint32_t ts;      // unix seconds
  int16_t  pres10;  // hPa * 10, INT16_MIN when unknown
  int16_t  temp100; // Celsius * 100, INT16_MIN when unknown
};
#define HOUR_SLOTS    16
#define HOUR_INTERVAL 3300L // record when the newest sample is >55min old

// Daily battery log: 32 first-wake-of-day voltages (~1 month), cleared
// whenever the voltage jumps up (the battery was charged).
struct bat_rec_t
{
  uint32_t ts; // unix seconds
  uint16_t mv;
};
#define BAT_SLOTS 32

static int pressureTrend = TREND_UNKNOWN;
static int indoorTrend   = TREND_UNKNOWN;
static int batDaysLeft   = -1;

/* Returns the trend of a series over roughly the last windowSec, using the
 * oldest sample inside [windowSec/2, windowSec*2] as the reference point.
 * threshold is in the series' storage units.
 */
static int calcTrend(const hour_rec_t *r, int n, time_t now, bool usePres,
                     long windowSec, int threshold)
{
  if (n == 0)
  {
    return TREND_UNKNOWN;
  }
  int16_t latest = usePres ? r[n - 1].pres10 : r[n - 1].temp100;
  if (latest == INT16_MIN)
  {
    return TREND_UNKNOWN;
  }
  for (int i = 0; i < n - 1; ++i)
  {
    long age = static_cast<long>(now - r[i].ts);
    if (age > windowSec * 2 || age < windowSec / 2)
    {
      continue;
    }
    int16_t ref = usePres ? r[i].pres10 : r[i].temp100;
    if (ref == INT16_MIN)
    {
      continue;
    }
    int d = latest - ref;
    if (d >= threshold)  {return TREND_RISING;}
    if (d <= -threshold) {return TREND_FALLING;}
    return TREND_STEADY;
  }
  return TREND_UNKNOWN;
} // end calcTrend

void historyUpdate(time_t now, float pressureHpa, float inTempC,
                   uint32_t batMv, Preferences &prefs)
{
  // ---- hourly pressure/indoor ring
  hour_rec_t ring[HOUR_SLOTS] = {};
  int n = prefs.getBytes("wxhist", ring, sizeof(ring)) / sizeof(hour_rec_t);
  n = constrain(n, 0, HOUR_SLOTS);
  // drop records from the future (clock jumped back) or older than a day
  int w = 0;
  for (int i = 0; i < n; ++i)
  {
    if (ring[i].ts <= static_cast<uint32_t>(now)
        && now - ring[i].ts < 86400L)
    {
      ring[w++] = ring[i];
    }
  }
  n = w;
  if (n == 0 || now - ring[n - 1].ts >= HOUR_INTERVAL)
  {
    if (n == HOUR_SLOTS)
    { // evict the oldest
      memmove(ring, ring + 1, sizeof(hour_rec_t) * (HOUR_SLOTS - 1));
      n = HOUR_SLOTS - 1;
    }
    ring[n].ts = now;
    ring[n].pres10 = std::isnan(pressureHpa)
                         ? INT16_MIN
                         : static_cast<int16_t>(lroundf(pressureHpa * 10.f));
    ring[n].temp100 = std::isnan(inTempC)
                          ? INT16_MIN
                          : static_cast<int16_t>(lroundf(inTempC * 100.f));
    ++n;
    prefs.putBytes("wxhist", ring, sizeof(hour_rec_t) * n);
  }
  // rising/falling: +-1.0 hPa over ~3h; +-0.5 C over ~3h
  pressureTrend = calcTrend(ring, n, now, true, 10800L, 10);
  indoorTrend   = calcTrend(ring, n, now, false, 10800L, 50);

  // ---- daily battery log
#if BATTERY_MONITORING
  bat_rec_t log[BAT_SLOTS] = {};
  int bn = prefs.getBytes("bathist", log, sizeof(log)) / sizeof(bat_rec_t);
  bn = constrain(bn, 0, BAT_SLOTS);
  if (bn > 0 && batMv > log[bn - 1].mv + 60)
  { // voltage jumped up: the battery was charged, old slope is meaningless
    bn = 0;
  }
  if (bn == 0 || now - log[bn - 1].ts >= 86400L)
  {
    if (bn == BAT_SLOTS)
    {
      memmove(log, log + 1, sizeof(bat_rec_t) * (BAT_SLOTS - 1));
      bn = BAT_SLOTS - 1;
    }
    log[bn].ts = now;
    log[bn].mv = static_cast<uint16_t>(batMv);
    ++bn;
    prefs.putBytes("bathist", log, sizeof(bat_rec_t) * bn);
  }
  // least-squares slope in mV/day over the log; needs >=4 points spanning
  // >=3 days to say anything
  batDaysLeft = -1;
  if (bn >= 4 && log[bn - 1].ts - log[0].ts >= 3 * 86400L)
  {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < bn; ++i)
    {
      double x = (log[i].ts - log[0].ts) / 86400.0;
      double y = log[i].mv;
      sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double denom = bn * sxx - sx * sx;
    if (denom > 0)
    {
      double slope = (bn * sxy - sx * sy) / denom; // mV/day
      if (slope < -1.0 && batMv > LOW_BATTERY_VOLTAGE)
      { // meaningfully discharging (a USB-powered device won't be)
        double days = (static_cast<double>(batMv) - LOW_BATTERY_VOLTAGE)
                      / -slope;
        if (days < 3650)
        {
          batDaysLeft = static_cast<int>(days);
        }
      }
    }
  }
#endif // BATTERY_MONITORING
} // end historyUpdate

int historyPressureTrend() {return pressureTrend;}
int historyIndoorTrend()   {return indoorTrend;}
int historyBatteryDaysLeft() {return batDaysLeft;}
