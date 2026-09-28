/* PC preview of the 1600x1200 layout for esp32-weather-epd.
 *
 * Builds the firmware's own renderer (renderer709.cpp) and display class
 * (epd709.cpp) for a PC, feeds them sample weather, and writes what they
 * drew as a picture in the panel's measured ink colours -- so the layout
 * can be checked, and changed, without the panel. See build.py.
 *
 * Part of esp32-weather-epd; GNU General Public License v3 or later.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
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
#include "icons/icons_196x196.h"

SerialStub Serial;
extern time_t hostNow;

// ---------------------------------------------------------------- stand-ins
// for the parts of display_utils.cpp and history.cpp the renderer calls;
// those files themselves need the ESP32's headers.
void toTitleCase(String &text)
{
  text.setCharAt(0, toUpperCase(text.charAt(0)));
  for (unsigned i = 1; i < text.length(); ++i)
  {
    const char before = text.charAt(i - 1);
    text.setCharAt(i, (before == ' ' || before == '-' || before == '(')
                      ? toUpperCase(text.charAt(i))
                      : toLowerCase(text.charAt(i)));
  }
}
void filterAlerts(std::vector<owm_alerts_t> &resp, int *ignore)
{
  for (size_t i = 0; i < resp.size(); ++i)
  {
    resp[i].event.toLowerCase();
    ignore[i] = 0;
  }
}
const char *getUVIdesc(unsigned int uvi)
{
  return (uvi <= 2) ? TXT_UV_LOW : (uvi <= 5) ? TXT_UV_MODERATE
       : (uvi <= 7) ? TXT_UV_HIGH : (uvi <= 10) ? TXT_UV_VERY_HIGH
                                                : TXT_UV_EXTREME;
}
const char *getWiFidesc(int rssi)
{
  return (rssi == 0) ? TXT_WIFI_NO_CONNECTION : (rssi >= -50) ? TXT_WIFI_EXCELLENT
       : (rssi >= -60) ? TXT_WIFI_GOOD : (rssi >= -70) ? TXT_WIFI_FAIR
                                                       : TXT_WIFI_WEAK;
}
const char *getMoonPhaseDesc(int phase)
{
  const int q = MOON_PHASE_STEPS / 4, i = phase % MOON_PHASE_STEPS;
  if (i == 0)     {return TXT_NEW_MOON;}
  if (i < q)      {return TXT_WAXING_CRESCENT;}
  if (i == q)     {return TXT_FIRST_QUARTER;}
  if (i < 2 * q)  {return TXT_WAXING_GIBBOUS;}
  if (i == 2 * q) {return TXT_FULL_MOON;}
  if (i < 3 * q)  {return TXT_WANING_GIBBOUS;}
  if (i == 3 * q) {return TXT_THIRD_QUARTER;}
  return TXT_WANING_CRESCENT;
}
uint32_t calcBatPercent(uint32_t v, uint32_t minv, uint32_t maxv)
{
  const uint32_t p = static_cast<uint32_t>(
    105 - (105 / (1 + pow(1.724 * (v - minv) / (maxv - minv), 5.5))));
  return p >= 100 ? 100 : p;
}
int historyPressureTrend() { return TREND_RISING; }
int historyIndoorTrend() { return TREND_STEADY; }
int historyBatteryDaysLeft() { return 41; }

// ---------------------------------------------------------------- sample weather
static float f2k(float f) { return (f - 32) * 5 / 9 + 273.15f; }

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// the afternoon the mockups were drawn for: hours counted from 3 pm
static float tempAt(float h)
{
  const float T0 = 15, day = (T0 + h) / 24;
  return 61 + 10 * sinf((T0 + h - 9) / 24 * 2 * M_PI)
       - 5 * clamp01((h - 26) / 10) + 1.5f * sinf(day * 5);
}
static float dewAt(float h)
{
  return 47 + 5 * clamp01((h - 18) / 12) + 2 * sinf((15 + h) / 24 * 2 * M_PI);
}
static float popAt(float h)
{
  return std::max(0.0f, 85 * expf(-powf((h - 33) / 5.5f, 2)) - 3);
}
static float cloudAt(float h)
{
  static const float K[][2] = {{0, 20}, {6, 12}, {11, 18}, {16, 40}, {24, 78}, {30, 100},
                               {37, 95}, {40, 70}, {44, 45}, {48, 35}, {96, 30}};
  for (size_t i = 1; i < sizeof(K) / sizeof(K[0]); ++i)
  {
    if (h <= K[i][0])
    {
      return K[i - 1][1] + (K[i][1] - K[i - 1][1]) * (h - K[i - 1][0]) / (K[i][0] - K[i - 1][0]);
    }
  }
  return 30;
}

static time_t utc(int y, int mo, int d, int h, int mi)
{
  tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
#ifdef _WIN32
  return _mkgmtime(&t);
#else
  return timegm(&t);
#endif
}

static owm_current_t current;
static owm_hourly_t hourly[OWM_NUM_HOURLY];
static owm_daily_t daily[OWM_NUM_DAILY];
static owm_resp_air_pollution_t air;
static pollen_info_t pollen;

static void sample(bool night)
{
  // 2026-09-28, Eastern Daylight Time (UTC-4): 3:40 pm, or 11:40 pm
  const int shift = night ? 8 : 0;
  hostNow = utc(2026, 9, 28, 19 + shift, 40);
  const time_t firstHour = utc(2026, 9, 28, 19 + shift, 0);

  current = owm_current_t();
  current.dt = hostNow;
  current.sunrise = utc(2026, 9, 28, 10, 56); // 6:56 am
  current.sunset = utc(2026, 9, 28, 23, 0);   // 7:00 pm
  current.temp = f2k(night ? 54 : 68);
  current.feels_like = f2k(night ? 53 : 66);
  current.humidity = night ? 78 : 64;
  current.dew_point = f2k(47);
  current.pressure = night ? 1021 : 1020;
  current.uvi = night ? 0 : 5;
  current.visibility = 16000;
  current.wind_speed = (night ? 4 : 7) * 0.44704f;
  current.wind_deg = night ? 0 : 315;
  current.clouds = 20;
  current.weather.id = 801;
  current.weather.icon = night ? "n" : "d";

  for (int i = 0; i < OWM_NUM_HOURLY; ++i)
  {
    const float h = i + shift;
    hourly[i] = owm_hourly_t();
    hourly[i].dt = firstHour + i * 3600;
    hourly[i].temp = f2k(tempAt(h));
    hourly[i].dew_point = f2k(dewAt(h));
    hourly[i].pop = popAt(h + 0.5f) / 100.0f;
    hourly[i].clouds = static_cast<int>(cloudAt(h + 0.5f));
    const float c = cloudAt(h + 0.5f);
    hourly[i].weather.id = (hourly[i].pop > 0.30f) ? 500
                         : (c <= 10) ? 800 : (c <= 30) ? 801 : (c <= 60) ? 802
                         : (c <= 85) ? 803 : 804;
    hourly[i].weather.icon = "d"; // NWS's own flag; the renderer goes by the sun
  }

  static const struct { int id; float hi, lo, pop, inches; } WEEK[7] = {
    {801, 73, 50, 0.00f, 0}, {803, 72, 45, 0.80f, 0.4f}, {803, 67, 52, 0.20f, 0.04f},
    {500, 64, 54, 0.70f, 0.3f}, {803, 63, 48, 0.20f, 0}, {800, 69, 46, 0, 0},
    {800, 74, 51, 0, 0}};
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    daily[i] = owm_daily_t();
    daily[i].dt = utc(2026, 9, 28 + i, 10, 0);
    daily[i].temp.max = f2k(WEEK[i].hi);
    daily[i].temp.min = f2k(WEEK[i].lo);
    daily[i].pop = WEEK[i].pop;
    daily[i].rain = WEEK[i].inches * 25.4f;
    daily[i].snow = 0;
    daily[i].clouds = 50;
    daily[i].weather.id = WEEK[i].id;
    daily[i].weather.icon = "d";
  }

  memset(&air.components, 0, sizeof(air.components));
  air.us_aqi = night ? 42 : 39;
  pollen.tree = 1;
  pollen.grass = 2;
  pollen.weed = 4;
  pollen.max_upi = 4;
}

static std::vector<owm_alerts_t> alertsOf(int n)
{
  static const struct { const char *event; int endDay, endHour, endMin; } A[4] = {
    {"Air Quality Alert", 29, 4, 0},            // midnight
    {"Heat Advisory", 30, 0, 0},                // 8 pm Tuesday
    {"Flood Watch", 30, 14, 0},                 // 10 am Wednesday
    {"Severe Thunderstorm Warning", 29, 1, 45}, // 9:45 pm
  };
  std::vector<owm_alerts_t> v;
  for (int i = 0; i < n; ++i)
  {
    owm_alerts_t a;
    a.event = (n == 1) ? "Frost Advisory" : A[i].event;
    a.tags = a.event;
    a.start = hostNow - 3600;
    a.end = (n == 1) ? utc(2026, 9, 30, 13, 0) : utc(2026, 9, A[i].endDay, A[i].endHour, A[i].endMin);
    v.push_back(a);
  }
  return v;
}

// ---------------------------------------------------------------- the picture
// the panel's inks as measured on a Spectra 6 (tools/generate_native_icons.py)
static const uint8_t INK_RGB[8][3] = {
  {31, 34, 38}, {185, 199, 201}, {193, 187, 30}, {98, 32, 30},
  {255, 0, 255}, {35, 63, 142}, {53, 86, 58}, {255, 0, 255}};

static void save(const char *path)
{
  FILE *f = fopen(path, "wb");
  if (!f)
  {
    printf("cannot write %s\n", path);
    return;
  }
  const int w = display.width(), h = display.height();
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  const uint8_t *frame = display.frame();
  for (int y = 0; y < h; ++y)
  {
    for (int x = 0; x < w; ++x)
    {
      int nx, ny;
      switch (EPD709_ROTATION)
      {
      case 1:  nx = Epd709::WIDTH - 1 - y; ny = x;                      break;
      case 2:  nx = Epd709::WIDTH - 1 - x; ny = Epd709::HEIGHT - 1 - y; break;
      case 3:  nx = y;                     ny = Epd709::HEIGHT - 1 - x; break;
      default: nx = x;                     ny = y;                      break;
      }
      const uint8_t b = frame[ny * (Epd709::WIDTH / 2) + (nx >> 1)];
      const uint8_t ink = (nx & 1) ? (b & 0x0F) : (b >> 4);
      fwrite(INK_RGB[ink & 7], 1, 3, f);
    }
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void weather(const char *path, bool night, int alertCount, const char *status = "")
{
  sample(night);
  std::vector<owm_alerts_t> alerts = alertsOf(alertCount);
  tm timeInfo;
  localtime_r(&hostNow, &timeInfo);
  String date, refreshed;
  {
    char buf[48];
    _strftime(buf, sizeof(buf), DATE_FORMAT, &timeInfo);
    date = buf;
    date.replace("  ", " ");
    _strftime(buf, sizeof(buf), TIME_FORMAT, &timeInfo);
    refreshed = buf;
    refreshed.trim();
  }
  layout709Begin(alerts, hourly);
  initDisplay();
  do
  {
    fillDisplayBackground();
    drawCurrentConditions(current, daily[0], air, pollen, night ? 20.0f : 20.6f, night ? 47 : 46);
    drawOutlookGraph(hourly, daily, timeInfo);
    drawForecast(daily, timeInfo);
    drawLocationDate(CITY_STRING, date);
    drawAlerts(alerts, CITY_STRING, date);
    drawStatusBar(status, refreshed, -58, 4050, historyBatteryDaysLeft());
  } while (display.nextPage());
  save(path);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
  _putenv_s("TZ", "EST5EDT");
  _tzset();
#else
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();
#endif
  const std::string out = (argc > 1) ? argv[1] : ".";
  CITY_STRING = "Riverside";
  strcpy(TIME_FORMAT, "%l:%M %P");
  strcpy(HOUR_FORMAT, "%l%P");
  strcpy(DATE_FORMAT, "%A, %B %e");
  HOURLY_GRAPH_MAX = 48;
  GRAPH_DEWPOINT = true;
  FORECAST_DAYS = 7;
  LAT = "40.0";
  LON = "-75.0";

  weather((out + "/sim_day.ppm").c_str(), false, 0);
  weather((out + "/sim_night.ppm").c_str(), true, 0);
  weather((out + "/sim_alert1.ppm").c_str(), false, 1);
  weather((out + "/sim_alert2.ppm").c_str(), false, 2);
  weather((out + "/sim_alert4.ppm").c_str(), false, 4);
  weather((out + "/sim_status.ppm").c_str(), false, 0, "weather.gov Alerts API");

  initDisplay();
  drawTestCard709();
  save((out + "/sim_testcard.ppm").c_str());
  initDisplay();
  drawError(wifi_x_196x196, TXT_WIFI_CONNECTION_FAILED, "'HomeNetwork'");
  save((out + "/sim_error.ppm").c_str());
  initDisplay();
  drawConfigPortalScreen("Connect to WiFi 'weather-epd-setup'", "then open http://192.168.4.1",
                         "Password: see the README");
  save((out + "/sim_portal.ppm").c_str());
  return 0;
}
