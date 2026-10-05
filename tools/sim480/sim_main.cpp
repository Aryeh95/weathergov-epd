/* PC preview of the 800x480 layout for esp32-weather-epd, on real data.
 *
 * Builds the firmware's own renderer (renderer.cpp, display_utils.cpp) and
 * its own parsers (api_response.cpp) for a PC, feeds the parsers saved API
 * replies from data/, and writes what the renderer drew as a picture in the
 * Spectra 6 panel's measured ink colours. Made for the Hebrew / Israel
 * Meteorological Service build, where there is no panel to try yet; see
 * build.py.
 *
 * Part of esp32-weather-epd; GNU General Public License v3 or later.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "_locale.h"
#include "_strftime.h"
#include "api_response.h"
#include "display_utils.h"
#include "history.h"
#include "renderer.h"
#include "sun.h"
#include "fonts/font_names.h"
#include <WiFi.h>

SerialStub Serial;
SPIStub SPI;

// The clock the firmware reads (the parsers cut the hourly series at "now",
// the renderer dates the page by it).
time_t hostNow = 0;
extern "C" time_t time(time_t *t)
{
  if (t) *t = hostNow;
  return hostNow;
}

// stand-ins for history.cpp, which needs the ESP32's NVS
int historyPressureTrend() { return TREND_STEADY; }
int historyIndoorTrend() { return TREND_STEADY; }
int historyBatteryDaysLeft() { return 37; }

static owm_current_t current;
static owm_hourly_t hourly[OWM_NUM_HOURLY];
static owm_daily_t daily[OWM_NUM_DAILY];
static owm_resp_air_pollution_t air;
static pollen_info_t pollen;
static std::vector<owm_alerts_t> alerts;
static std::string dataDir;

static bool parse(const char *file, std::function<DeserializationError(WiFiClient &)> fn)
{
  WiFiClient c;
  const std::string path = dataDir + "/" + file;
  if (!c.load(path.c_str()))
  {
    fprintf(stderr, "cannot read %s\n", path.c_str());
    return false;
  }
  DeserializationError err = fn(c);
  printf("%-24s %s\n", file, err ? err.c_str() : "ok");
  return !err;
}

// Israel local time (IST/IDT) to unix
static time_t local(int y, int mo, int d, int h, int mi)
{
  tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi;
  t.tm_isdst = -1;
  return mktime(&t);
}

// the panel's inks as measured on a Spectra 6 (tools/generate_native_icons.py)
static const uint8_t INK_RGB[8][3] = {
  {185, 199, 201}, {31, 34, 38}, {185, 199, 201}, {98, 32, 30},
  {193, 187, 30},  {53, 86, 58}, {35, 63, 142},   {168, 112, 46}};

static void savePpm(const char *path)
{
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  const int w = display.width(), h = display.height();
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  const uint8_t *frame = display.frame();
  for (int i = 0; i < w * h; ++i)
  {
    fwrite(INK_RGB[frame[i] & 7], 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void loadWeather(time_t now, bool imsCurrent)
{
  hostNow = now;
  setPageTime(now);
  if (!parse("ims_forecast_he.json", [](WiFiClient &c) { return deserializeIMSForecast(c, hourly, daily); }))
  {
    exit(1);
  }
  if (imsCurrent)
  {
    parse("ims_now_he.json", [](WiFiClient &c) { return deserializeIMSCurrent(c, 7, hourly[0], current); });
  }
  else
  {
    // CURRENT_SOURCE "nws" with a stale now_analysis: the forecast's hour
    fillCurrentFromFallback(hourly[0], current);
    current.uvi = daily[0].uvi;
    current.pressure = 0;      // IMS has neither...
    current.visibility = -1;
  }
  // ...so, as finishWeather does on the device, Open-Meteo's current
  // pressure and visibility fill the two gaps (openmeteo_gaps.json is the
  // reply to the same request, "&current=pressure_msl,visibility")
  {
    om_daily_precip_t omDaily;
    om_gaps_t gaps = {0, -1};
    if (parse("openmeteo_gaps.json", [&](WiFiClient &c) {
          return deserializeOpenMeteoCurrent(c, hourly[0], current, omDaily, false, &gaps); }))
    {
      if (current.pressure <= 0)  current.pressure   = gaps.pressure;
      if (current.visibility < 0) current.visibility = gaps.visibility;
      printf("  gap fill: %d hPa, %d m\n", gaps.pressure, gaps.visibility);
    }
  }
  tm lt;
  localtime_r(&now, &lt);
  if (!calcSunriseSunset(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                         LAT.toDouble(), LON.toDouble(), current.sunrise, current.sunset))
  {
    current.sunrise = current.sunset = 0;
  }
  setSunTimes(current.sunrise, current.sunset);
  current.dt = now;

  float uvi = NAN;
  air.valid = false;
  air.us_aqi = -1;
  parse("openmeteo_air.json", [&](WiFiClient &c) { return deserializeAirQuality(c, air, uvi); });
  if (!std::isnan(uvi) && std::isnan(current.uvi)) current.uvi = uvi;

  // the Ministry of Environmental Protection's index around Netanya: the
  // worst of Derech Raziel (584) and Kiryat HaSharon (321), the two
  // stations within 15 km, as the "israel" aqi_source would show it
  air.il_valid = false;
  air.il_index = 0;
  static const std::vector<int> netanya = {584, 321};
  parse("sviva_index_region4.json", [](WiFiClient &c) {
    return deserializeSvivaIndex(c, netanya, air.il_valid, air.il_index); });
  printf("  Israeli index %s%d\n", air.il_valid ? "" : "(none) ", air.il_index);

  pollen.tree = pollen.grass = pollen.weed = 0;
  pollen.max_upi = -1;
  parse("google_pollen.json", [](WiFiClient &c) { return deserializePollen(c, pollen); });

  alerts.clear();
  parse("ims_warnings_he.json", [](WiFiClient &c) { return deserializeIMSAlerts(c, 101, alerts); });
  printf("  hourly from %lld, %d alerts for region 101\n", (long long)hourly[0].dt, (int)alerts.size());
}

static void drawPage(const char *out, const String &status, bool stale)
{
  tm timeInfo;
  localtime_r(&hostNow, &timeInfo);
  String dateStr, refreshTimeStr;
  getDateStr(dateStr, &timeInfo);
  getRefreshTimeStr(refreshTimeStr, true, &timeInfo);

  initDisplay();
  do
  {
    fillDisplayBackground();
    drawCurrentConditions(current, daily[0], air, pollen, NAN, NAN);
    drawOutlookGraph(hourly, daily, timeInfo);
    drawForecast(daily, timeInfo);
    drawLocationDate(CITY_STRING, dateStr);
    drawAlerts(alerts, CITY_STRING, dateStr);
    drawStatusBar(status, refreshTimeStr, -58, 4050, historyBatteryDaysLeft(), stale);
  } while (display.nextPage());
  savePpm(out);
}

int main(int argc, char **argv)
{
  const std::string outDir = argc > 1 ? argv[1] : ".";
  dataDir = argc > 2 ? argv[2] : "data";

  // Israel, as the device would be configured
  setenv("TZ", "IST-2IDT,M3.4.4/26,M10.5.0", 1);
  tzset();
  strncpy(TIMEZONE, "IST-2IDT,M3.4.4/26,M10.5.0", sizeof(TIMEZONE) - 1);
  CITY_STRING = "נתניה";
  LAT = "32.322106";
  LON = "34.857964";
  strcpy(TIME_FORMAT, "%H:%M");
  strcpy(HOUR_FORMAT, "%H");
  strcpy(DATE_FORMAT, "%A, %e ב%B, %K");
  FORECAST_DAYS = 7;
  FORECAST_SOURCE = "ims";
  CURRENT_SOURCE = "nws";

  // the Hebrew family, as settings.cpp would pick it
  for (int i = 0; i < FONT_FAMILY_NAME_COUNT; ++i)
  {
    if (FONT_FAMILY_HEBREW[i]) { FONT_FAMILY_INDEX = FONT_SMALL_FAMILY_INDEX = i; break; }
  }
  printf("font %s\n", FONT_FAMILY_NAMES[FONT_FAMILY_INDEX]);
  // as settings.cpp does for a Hebrew build: heat stress in the dew point's slot
  if (LC_PREFER_HEAT_STRESS) { POS_HEAT_STRESS = POS_DEWPOINT; POS_DEWPOINT = -1; }

  // 1. the moment the replies were saved (2026-10-05 04:49 IDT), IMS's own
  //    current conditions
  loadWeather(local(2026, 10, 5, 4, 49), true);
  drawPage((outDir + "/sim_netanya_dawn.ppm").c_str(), "", false);

  // 2. the same day at 13:30, current conditions from the forecast hour;
  //    the heat stress widget in the dew point's slot, as an Israeli layout
  //    would have it
  loadWeather(local(2026, 10, 5, 13, 30), false);
  drawPage((outDir + "/sim_netanya_afternoon.ppm").c_str(), "", false);

  // 2b. the pollen widget in the indoor-humidity slot (no sensor in the sample)
  POS_POLLEN = POS_INHUMIDITY;
  POS_INHUMIDITY = -1;
  drawPage((outDir + "/sim_netanya_pollen.ppm").c_str(), "", false);
  printf("  pollen tree %d grass %d weed %d max %d\n", pollen.tree, pollen.grass, pollen.weed, pollen.max_upi);
  POS_INHUMIDITY = POS_POLLEN;
  POS_POLLEN = -1;

  // 3. with a warning, as IMS words them, and a failed request in the status bar
  owm_alerts_t a = {};
  a.event = "רוחות חזקות, אזהרה צהובה";
  a.tags = "רוחות חזקות";
  a.start = hostNow - 3600;
  a.end = local(2026, 10, 5, 22, 0);
  alerts.push_back(a);
  drawPage((outDir + "/sim_netanya_alert.ppm").c_str(),
           "Open-Meteo Air Quality API", false);
  alerts.clear();

  // 4. Friday afternoon: candle lighting in the sunset widget; the date
  //    format with the holiday name (%Q, empty on an ordinary day)
  strcpy(DATE_FORMAT, "%A, %e ב%B, %K, %Q");
  loadWeather(local(2026, 10, 9, 16, 30), false);
  // the visibility widget in the pressure's slot, to see its unit
  POS_VISIBILITY = POS_PRESSURE;
  POS_PRESSURE = -1;
  drawPage((outDir + "/sim_netanya_friday.ppm").c_str(), "", false);
  POS_PRESSURE = POS_VISIBILITY;
  POS_VISIBILITY = -1;

  // 5. Shabbat noon: when it ends
  loadWeather(local(2026, 10, 10, 12, 0), false);
  drawPage((outDir + "/sim_netanya_shabbat.ppm").c_str(), "", false);
  return 0;
}
