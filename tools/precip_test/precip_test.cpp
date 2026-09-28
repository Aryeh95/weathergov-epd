#include "precip.h"
#include "precip_test_data.h" // snapshot, see README.md
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
int main() {
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1); tzset();   // the device's own POSIX TZ string
  int fails = 0;
  // duration parser
  struct { const char *s; int32_t v; } D[] = {{"PT6H",21600},{"PT4H",14400},{"P1D",86400},{"P1DT6H",108000},{"PT30M",1800},{"P1M",0},{"garbage",0},{"",0}};
  for (auto &d : D) { int32_t got = parseIsoDurationSeconds(d.s); if (got != d.v) { printf("DUR FAIL %s -> %d (want %d)\n", d.s, got, d.v); fails++; } }
  std::vector<qpf_bucket_t> q(BUCKETS, BUCKETS + sizeof(BUCKETS)/sizeof(BUCKETS[0]));
  om_daily_precip_t om = {}; om.n = OM_N; for (int k = 0; k < OM_N; ++k) { om.time[k] = OM_T[k]; om.mm[k] = OM_MM[k]; }
  for (int i = 0; i < N_ANCHORS; ++i) {
    daily_precip_pick_t p = pickDailyPrecip(ANCHORS[i], NOW_TS, q, om);
    const char *src = p.src == PRECIP_SRC_NWS ? "NWS" : p.src == PRECIP_SRC_OPEN_METEO ? "OM" : "NONE";
    bool ok = strcmp(src, EXP_SRC[i]) == 0 && ((std::isnan(p.mm) && std::isnan(EXP_MM[i])) || fabsf(p.mm - EXP_MM[i]) < 0.01f);
    time_t a = ANCHORS[i]; char buf[32]; strftime(buf, sizeof buf, "%a %m-%d", localtime(&a));
    printf("%s  %-4s %6.2f in   %s\n", buf, src, p.mm / 25.4f, ok ? "ok" : "MISMATCH");
    if (!ok) fails++;
  }
  // No QPF at all (fetch failed) -> every day Open-Meteo
  std::vector<qpf_bucket_t> none;
  daily_precip_pick_t p0 = pickDailyPrecip(ANCHORS[0], NOW_TS, none, om);
  printf("no-QPF fallback: %s\n", p0.src == PRECIP_SRC_OPEN_METEO ? "OM ok" : "FAIL"); if (p0.src != PRECIP_SRC_OPEN_METEO) fails++;
  // Neither source -> NaN
  om_daily_precip_t empty = {}; daily_precip_pick_t p1 = pickDailyPrecip(ANCHORS[6], NOW_TS, none, empty);
  printf("no-source: %s\n", std::isnan(p1.mm) && p1.src == PRECIP_SRC_NONE ? "NaN ok" : "FAIL"); if (!(std::isnan(p1.mm) && p1.src == PRECIP_SRC_NONE)) fails++;
  // The last hour of a day, no NWS bucket reaching it. The hour of slack the
  // picker allows is not coverage: this used to come back as a confirmed
  // 0.0 from NWS, with or without the other source.
  const long long late = OM_T[0] + 23 * 3600 + 1800; // 23:30 on the first day
  daily_precip_pick_t p2 = pickDailyPrecip(late, late, none, om);
  bool ok2 = p2.src == PRECIP_SRC_OPEN_METEO && fabsf(p2.mm - OM_MM[0]) < 0.01f;
  printf("23:30, no QPF: %s\n", ok2 ? "OM ok" : "FAIL"); if (!ok2) fails++;
  daily_precip_pick_t p3 = pickDailyPrecip(late, late, none, empty);
  bool ok3 = std::isnan(p3.mm) && p3.src == PRECIP_SRC_NONE;
  printf("23:30, no source: %s\n", ok3 ? "NaN ok" : "FAIL"); if (!ok3) fails++;
  printf("%s\n", fails ? "FAILURES" : "ALL OK"); return fails;
}
