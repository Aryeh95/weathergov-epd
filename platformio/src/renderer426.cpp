/* Renderer for the XIAO ePaper EE05's 4.26in panel, for esp32-weather-epd.
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* Seeed's 4.26in panel is Good Display's GDEQ0426T82 glass: 800x480 at
 * 219 ppi, black and white, with a waveform for four greys. The layout is
 * its own, in pages:
 *
 *   0  home      now, three tiles (chosen in the portal: HOME_TILES), the
 *                next 24 hours as a graph and five days -- in the four
 *                greys, drawn by each scheduled update (a full refresh,
 *                about 4 s)
 *   1  hours     the next 24 hours, every two: temperature, what it feels
 *                like, the chance of rain, the wind
 *   2  week      seven days: rain, wind, and each day's range against the
 *                week's
 *
 * Pages 1 and 2 are turned to with KEY3 and drawn from the weather the last
 * update kept (snapshot.h), without the network, in black and white: a
 * full refresh in about 1.6 s. main.cpp decides which page is drawn when.
 *
 * Greys on this glass: light grey reads well; dark grey comes out lighter
 * than its name, so it is kept for areas (the rain bars), and lines and
 * text stay black. In black and white anything not white is black, so the
 * black-and-white pages draw with outlines, not fills.
 */

#include "config.h"

#if defined(DISP_4G_426)

#include <algorithm>
#include <cmath>
#include <SPI.h>
#include <GxEPD2_4G_4G.h>
#include <GxEPD2_4G_BW.h>
#include <aqi.h>
#include "_locale.h"
#include "_strftime.h"
#include "renderer.h"
#include "api_response.h"
#include "conversions.h"
#include "display_utils.h"
#include "rtl.h"
#include "sun.h"

#include "fonts/Bitter/Bitter_8pt8b.h"
#include "fonts/Bitter/Bitter_9pt8b.h"
#include "fonts/Bitter/Bitter_10pt8b.h"
#include "fonts/Bitter/Bitter_11pt8b.h"
#include "fonts/Bitter/Bitter_12pt8b.h"
#include "fonts/Bitter/Bitter_14pt8b.h"
#include "fonts/Bitter/Bitter_16pt8b.h"
#include "fonts/Bitter/Bitter_18pt8b.h"
#include "fonts/Bitter/Bitter_20pt8b.h"
#include "fonts/Bitter/Bitter_26pt8b.h"
#include "fonts/Bitter/Bitter_48pt8b_temperature.h"

#include "icons/icons_24x24.h"
#include "icons/icons_48x48.h"
#include "icons/icons_196x196.h"
#include "icons/icons_moon.h"
#include "icons/icons_pollen.h"

typedef GxEPD2_426_GDEQ0426T82 Panel426;
// The greys in four bands of 120 rows: with a buffer for the whole page
// (96000 bytes) the library's fillScreen() never ends, as it counts with a
// uint16_t.
static GxEPD2_4G_4G<Panel426, Panel426::HEIGHT / 4> grey(
  Panel426(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
// Black and white in two halves of 240 rows, on the same driver. Not the
// whole page at once: the driver indexes the buffer with an int16_t, so
// past byte 32767 (row 327 of a 48000-byte page) it sends what lies before
// the buffer instead -- the top of the page, as the board sits, was noise.
static GxEPD2_4G_BW_R<Panel426, Panel426::HEIGHT / 2> bw(grey.epd2);

Display426 display;

static bool s_wantGrey = true;   // what the next initDisplay() sets up
static bool s_grey = true;       // what the page being drawn is

static const uint16_t K  = GxEPD_BLACK;
static const uint16_t DG = GxEPD_DARKGREY;
static const uint16_t LG = GxEPD_LIGHTGREY;
static const uint16_t W  = GxEPD_WHITE;

/* More tones than the four levels: two levels dot by dot, a checker, which
 * at 219 ppi reads as a tone of its own. Measured (L*, black 16, dark grey
 * 69, light grey 81, white 100): black + dark grey 39, black + light grey
 * 47, dark + light grey 74, dark grey + white 89, light grey + white 93.
 * Drawn as these stand-in colours, which nothing else uses (black is 0).
 */
static const uint16_t T_KDG  = 0x0001;
static const uint16_t T_KLG  = 0x0002;
static const uint16_t T_DGLG = 0x0003;
static const uint16_t T_DGW  = 0x0004;
static const uint16_t T_LGW  = 0x0005;

/* Everything is drawn through this: it turns the stand-in colours into
 * their checkers and hands each dot to the display set up for the page.
 */
class Mixer426 : public Adafruit_GFX
{
public:
  Mixer426() : Adafruit_GFX(DISP_WIDTH, DISP_HEIGHT) {}
  Adafruit_GFX *target = &grey;
  void drawPixel(int16_t x, int16_t y, uint16_t color) override
  {
    if (color >= T_KDG && color <= T_LGW)
    {
      static const uint16_t A[6] = {0, K, K, DG, DG, LG};
      static const uint16_t B[6] = {0, DG, LG, LG, W, W};
      color = ((x + y) & 1) ? A[color] : B[color];
    }
    target->drawPixel(x, y, color);
  }
};
static Mixer426 mixer;
static Adafruit_GFX *g = &mixer;  // what the page is drawn on

/* The home page's tones: the header band and its ink, the tiles, the rain
 * bars, every other forecast day, and the area under the temperature
 * (white: none). PALETTE_426 picks one.
 */
struct Palette426
{
  uint16_t header, headerInk, tile, rain, dayAlt, tempFill;
};
static const Palette426 PALETTES_426[] = {
  {K,     W, LG,    LG,    LG,    W},      // 0 as it was
  {T_KDG, W, LG,    DG,    LG,    W},      // 1 charcoal header
  {K,     W, T_LGW, T_KLG, T_LGW, W},      // 2 soft tiles, dark rain
  {LG,    K, T_LGW, DG,    T_LGW, W},      // 3 light header
  {K,     W, LG,    T_KLG, LG,    T_LGW},  // 4 filled temperature
  {K,     W, DG,    LG,    T_DGW, W},      // 5 bold tiles
};
int PALETTE_426 = 2;   // chosen on the panel 2026-10-18
int HOURS_LAYOUT_426 = 1;   // the tables, chosen 2026-10-18

/* The palette of the page being drawn: the black-and-white pages keep the
 * first, whose header is black.
 */
static const Palette426 &pal()
{
  const int n = sizeof(PALETTES_426) / sizeof(PALETTES_426[0]);
  return PALETTES_426[(s_grey && PALETTE_426 >= 0 && PALETTE_426 < n)
                      ? PALETTE_426 : 0];
}

static const int HEADER_H = 54;

bool Display426::nextPage()
{
  return s_grey ? grey.nextPage() : bw.nextPage();
}

void setGreyMode426(bool wantGrey)
{
  s_wantGrey = wantGrey;
}

/* ------------------------------------------------------------------ text */

uint16_t getStringWidth(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(shapeText(text), 0, 0, &x1, &y1, &w, &h);
  return w;
}

uint16_t getStringHeight(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(shapeText(text), 0, 0, &x1, &y1, &w, &h);
  return h;
}

void drawString(int16_t x, int16_t y, const String &text, alignment_t alignment,
                uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  g->setTextColor(color);
  const String shaped = shapeText(text);
  g->getTextBounds(shaped, x, y, &x1, &y1, &w, &h);
  // align the ink, not the cursor (see renderer.cpp)
  const int16_t bearing = x1 - x;
  if (alignment == RIGHT)
  {
    x = x - w - bearing;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2 - bearing;
  }
  g->setCursor(x, y);
  g->print(shaped);
}

void drawMultiLnString(int16_t x, int16_t y, const String &text,
                       alignment_t alignment, uint16_t max_width,
                       uint16_t max_lines, int16_t line_spacing,
                       uint16_t color)
{
  String rest = text;
  rest.trim();
  for (uint16_t line = 0; line < max_lines && !rest.isEmpty(); ++line)
  {
    // the longest run of words that fits
    int end = rest.length();
    while (end > 0 && getStringWidth(rest.substring(0, end)) > max_width)
    {
      const int sp = rest.lastIndexOf(' ', end - 1);
      if (sp <= 0)
      {
        break;
      }
      end = sp;
    }
    String part = rest.substring(0, end);
    rest = rest.substring(end);
    rest.trim();
    if (line == max_lines - 1 && !rest.isEmpty())
    {
      part += "...";
    }
    drawString(x, y + line * line_spacing, part, alignment, color);
  }
}

/* The text cut short with ".." until it fits in `maxW`. */
static String fitted(const String &text, int maxW)
{
  if (text.isEmpty() || getStringWidth(text) <= maxW)
  {
    return text;
  }
  String s = text;
  while (s.length() > 1 && getStringWidth(s + "..") > maxW)
  {
    s.remove(s.length() - 1);
  }
  s.trim();
  return s + "..";
}

/* ---------------------------------------------------------------- shapes */

/* A 1-bit icon of the project's (a 0 bit is ink) in one colour. */
static void drawIcon(int16_t x, int16_t y, const uint8_t *bmp, int16_t w,
                     int16_t h, uint16_t color)
{
  if (!bmp)
  {
    return;
  }
  const int16_t stride = (w + 7) / 8;
  for (int16_t j = 0; j < h; ++j)
  {
    for (int16_t i = 0; i < w; ++i)
    {
      const uint8_t b = pgm_read_byte(&bmp[j * stride + i / 8]);
      if (!(b & (0x80 >> (i & 7))))
      {
        g->drawPixel(x + i, y + j, color);
      }
    }
  }
}

/* A dithered icon (icons_moon.h format: 4 bits a dot, 0 nothing, 1 black,
 * 2 white), both inks drawn so it keeps its contrast on a grey tile.
 */
static void drawDithered(int16_t x, int16_t y, const uint8_t *data,
                         int16_t size)
{
  for (int32_t n = 0; n < static_cast<int32_t>(size) * size; ++n)
  {
    const uint8_t b = pgm_read_byte(&data[n >> 1]);
    const uint8_t v = (n & 1) ? (b & 0x0F) : (b >> 4);
    if (v == 1 || v == 2)
    {
      g->drawPixel(x + n % size, y + n / size, (v == 1) ? K : W);
    }
  }
}

/* The moon as it is tonight, in the panel's greys: the lit side white with
 * its seas light grey, the night side dark grey (earthshine), the limb
 * black. `frac` is moonPhaseFraction(); `south` turns it over, as it looks
 * from the southern hemisphere.
 */
static void drawGreyMoon(int16_t cx, int16_t cy, int16_t r, double frac,
                         bool south)
{
  // the near side's seas: centre and radii in moon radii, x toward Mare
  // Crisium, y down, as seen from the northern hemisphere
  static const float MARIA[][4] = {
    { 0.62f, -0.30f, 0.14f, 0.11f},   // Crisium
    { 0.47f,  0.10f, 0.13f, 0.20f},   // Fecunditatis
    { 0.28f, -0.06f, 0.20f, 0.15f},   // Tranquillitatis
    { 0.17f, -0.38f, 0.15f, 0.14f},   // Serenitatis
    { 0.30f,  0.33f, 0.09f, 0.08f},   // Nectaris
    {-0.22f, -0.44f, 0.26f, 0.19f},   // Imbrium
    {-0.55f, -0.02f, 0.22f, 0.40f},   // Procellarum
    {-0.10f,  0.40f, 0.17f, 0.12f},   // Nubium
    {-0.40f,  0.38f, 0.10f, 0.09f},   // Humorum
    { 0.00f, -0.72f, 0.32f, 0.06f}};  // Frigoris
  const float c = static_cast<float>(std::cos(2.0 * M_PI * frac));
  const float rr = static_cast<float>(r) * r;
  const float limb = (r - 1.2f) * (r - 1.2f);
  for (int16_t j = -r; j < r; ++j)
  {
    for (int16_t i = -r; i < r; ++i)
    {
      const float d2 = (i + 0.5f) * (i + 0.5f) + (j + 0.5f) * (j + 0.5f);
      if (d2 > rr)
      {
        continue;
      }
      float x = (i + 0.5f) / r, y = (j + 0.5f) / r;
      if (south)
      {
        x = -x;
        y = -y;
      }
      const float w = std::sqrt(std::max(0.0f, 1.0f - y * y));
      // waxing, lit from the right; waning, from the left
      const bool lit = (frac <= 0.5) ? (x > w * c) : (x < -w * c);
      uint16_t v;
      if (d2 > limb)
      {
        v = K;
      }
      else if (!lit)
      {
        v = DG;
      }
      else
      {
        v = W;
        for (const auto &m : MARIA)
        {
          const float u = (x - m[0]) / m[2], t = (y - m[1]) / m[3];
          if (u * u + t * t <= 1.0f)
          {
            v = LG;
            break;
          }
        }
      }
      g->drawPixel(cx + i, cy + j, v);
    }
  }
}

/* A dotted horizontal rule. */
static void dottedLine(int16_t x0, int16_t x1, int16_t y, uint16_t color)
{
  for (int16_t x = x0; x <= x1; x += 6)
  {
    g->drawPixel(x, y, color);
  }
}

/* A rounded box: filled light grey on the grey page, outlined in black and
 * white.
 */
static void tile(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (s_grey)
  {
    g->fillRoundRect(x, y, w, h, 12, pal().tile);
  }
  else
  {
    g->drawRoundRect(x, y, w, h, 12, K);
    g->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, K);
  }
}

/* ----------------------------------------------------------------- units */

static float toTempUnit(float kelvin)
{
#if defined(UNITS_TEMP_CELSIUS)
  return kelvin_to_celsius(kelvin);
#elif defined(UNITS_TEMP_FAHRENHEIT)
  return kelvin_to_fahrenheit(kelvin);
#else
  return kelvin;
#endif
}

/* "40°", or "--" for no reading */
static String deg(float kelvin)
{
  if (std::isnan(kelvin))
  {
    return "--";
  }
  return String(static_cast<int>(std::lround(toTempUnit(kelvin)))) + "\260";
}

static const char *tempUnit()
{
#if defined(UNITS_TEMP_CELSIUS)
  return TXT_UNITS_TEMP_CELSIUS;
#elif defined(UNITS_TEMP_FAHRENHEIT)
  return TXT_UNITS_TEMP_FAHRENHEIT;
#else
  return TXT_UNITS_TEMP_KELVIN;
#endif
}

/* a wind speed (m/s) as a number in the display's unit */
static float speedNum(float ms)
{
#if defined(UNITS_SPEED_METERSPERSECOND)
  return ms;
#elif defined(UNITS_SPEED_FEETPERSECOND)
  return meterspersecond_to_feetpersecond(ms);
#elif defined(UNITS_SPEED_KILOMETERSPERHOUR)
  return meterspersecond_to_kilometersperhour(ms);
#elif defined(UNITS_SPEED_KNOTS)
  return meterspersecond_to_knots(ms);
#elif defined(UNITS_SPEED_BEAUFORT)
  return meterspersecond_to_beaufort(ms);
#else
  return meterspersecond_to_milesperhour(ms);
#endif
}

/* a wind speed (m/s) as "12 mph" */
static String speed(float ms)
{
#if defined(UNITS_SPEED_METERSPERSECOND)
  return String(static_cast<int>(std::lround(ms))) + " "
         + TXT_UNITS_SPEED_METERSPERSECOND;
#elif defined(UNITS_SPEED_FEETPERSECOND)
  return String(static_cast<int>(std::lround(
         meterspersecond_to_feetpersecond(ms)))) + " "
         + TXT_UNITS_SPEED_FEETPERSECOND;
#elif defined(UNITS_SPEED_KILOMETERSPERHOUR)
  return String(static_cast<int>(std::lround(
         meterspersecond_to_kilometersperhour(ms)))) + " "
         + TXT_UNITS_SPEED_KILOMETERSPERHOUR;
#elif defined(UNITS_SPEED_KNOTS)
  return String(static_cast<int>(std::lround(
         meterspersecond_to_knots(ms)))) + " " + TXT_UNITS_SPEED_KNOTS;
#elif defined(UNITS_SPEED_BEAUFORT)
  return String(meterspersecond_to_beaufort(ms)) + " "
         + TXT_UNITS_SPEED_BEAUFORT;
#else
  return String(static_cast<int>(std::lround(
         meterspersecond_to_milesperhour(ms)))) + " "
         + TXT_UNITS_SPEED_MILESPERHOUR;
#endif
}

/* sea-level pressure (hPa) as the number and its unit */
static void pressure(int hPa, String &value, String &unit)
{
  if (hPa <= 0)
  {
    value = "--";
    unit = "";
    return;
  }
#if defined(UNITS_PRES_INCHESOFMERCURY)
  value = String(hectopascals_to_inchesofmercury(hPa), 2);
  unit = TXT_UNITS_PRES_INCHESOFMERCURY;
#elif defined(UNITS_PRES_MILLIMETERSOFMERCURY)
  value = String(static_cast<int>(std::lround(
          hectopascals_to_millimetersofmercury(hPa))));
  unit = TXT_UNITS_PRES_MILLIMETERSOFMERCURY;
#elif defined(UNITS_PRES_MILLIBARS)
  value = String(static_cast<int>(std::lround(
          hectopascals_to_millibars(hPa))));
  unit = TXT_UNITS_PRES_MILLIBARS;
#else
  // (pascals, atmospheres and the rest: hectopascals here)
  value = String(hPa);
  unit = TXT_UNITS_PRES_HECTOPASCALS;
#endif
}

/* visibility (m) as the number and its unit */
static void distance(int metres, String &value, String &unit)
{
  if (metres < 0)
  {
    value = "--";
    unit = "";
    return;
  }
#if defined(UNITS_DIST_KILOMETERS)
  const float d = meters_to_kilometers(metres);
  unit = TXT_UNITS_DIST_KILOMETERS;
#else
  const float d = meters_to_miles(metres);
  unit = TXT_UNITS_DIST_MILES;
#endif
  value = (d >= 10.0f) ? String(static_cast<int>(std::lround(d)))
                       : String(d, 1);
}

/* a day's precipitation (mm) as "0.25 in"; empty for none known */
static String precip(float mm)
{
  if (std::isnan(mm))
  {
    return "";
  }
#if defined(UNITS_DAILY_PRECIP_MILLIMETERS)
  return String(mm, (mm < 10.0f) ? 1 : 0) + " " + TXT_UNITS_PRECIP_MILLIMETERS;
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
  return String(millimeters_to_centimeters(mm), 1) + " "
         + TXT_UNITS_PRECIP_CENTIMETERS;
#else
  return String(millimeters_to_inches(mm), 2) + " " + TXT_UNITS_PRECIP_INCHES;
#endif
}

static String clockTime(int64_t t, const char *format)
{
  const time_t ts = static_cast<time_t>(t);
  tm when = {};
  localtime_r(&ts, &when);
  char buf[32] = {};
  _strftime(buf, sizeof(buf), format, &when);
  return String(buf);
}

/* the air quality index and its description; false if there is none */
static bool airQuality(const owm_resp_air_pollution_t &air, String &value,
                       String &desc)
{
  if (air.us_aqi >= 0)
  {
    value = String(air.us_aqi);
    desc = united_states_aqi_desc(air.us_aqi);
    return true;
  }
  if (air.il_valid)
  {
    const int il = air.il_index;
    value = String(il);
    desc = TXT_IL_AQI[(il > 50) ? 0 : (il >= 0) ? 1 : (il >= -200) ? 2 : 3];
    return true;
  }
  if (!air.valid)
  {
    value = "--";
    desc = "";
    return false;
  }
  const owm_components_t &c = air.components;
  const int aqi = calc_aqi(AQI_SCALE, c.co, c.nh3, c.no, c.no2, c.o3, NULL,
                           c.so2, c.pm10, c.pm2_5);
  const int aqiMax = aqi_scale_max(AQI_SCALE);
  value = (aqi > aqiMax) ? String(TXT_MORE_THAN) + String(aqiMax)
                         : String(aqi);
  desc = aqi_desc(AQI_SCALE, aqi);
  return true;
}

/* "Pollen 3 grass", or empty without a forecast */
static String pollenLine(const pollen_info_t &pollen)
{
  if (pollen.max_upi < 0)
  {
    return "";
  }
  String s = String(TXT_POLLEN) + " " + String(pollen.max_upi);
  if (pollen.max_upi > 0)
  {
    String types;
    if (pollen.tree == pollen.max_upi && pollen.grass == pollen.max_upi
        && pollen.weed == pollen.max_upi)
    {
      types = TXT_POLLEN_ALL;
    }
    else
    {
      if (pollen.tree == pollen.max_upi) {types += TXT_POLLEN_TREE;}
      if (pollen.grass == pollen.max_upi)
      {
        types += (types.isEmpty() ? "" : ", ") + String(TXT_POLLEN_GRASS);
      }
      if (pollen.weed == pollen.max_upi)
      {
        types += (types.isEmpty() ? "" : ", ") + String(TXT_POLLEN_WEED);
      }
    }
    s += " " + types;
  }
  return s;
}

/* ---------------------------------------------------------------- header */

/* The band at the top: the city, the page dots, the date. */
static void drawHeader(const String &city, const String &date, int page)
{
  const uint16_t ink = pal().headerInk;
  g->fillRect(0, 0, DISP_WIDTH, HEADER_H, pal().header);
  g->setFont(&Bitter_18pt8b);
  drawString(16, 38, fitted(city, 300), LEFT, ink);
  // where this page is among the pages
  const int r = 5, gap = 18;
  const int x0 = DISP_WIDTH / 2 - (PAGES_426 - 1) * gap / 2;
  for (int i = 0; i < PAGES_426; ++i)
  {
    if (i == page)
    {
      g->fillCircle(x0 + i * gap, HEADER_H / 2, r, ink);
    }
    else
    {
      g->drawCircle(x0 + i * gap, HEADER_H / 2, r, ink);
    }
  }
  g->setFont(&Bitter_11pt8b);
  drawString(DISP_WIDTH - 14, 23, date, RIGHT, ink);
}

/* Under the date: the time of the update, the battery, a warning sign if
 * something could not be fetched. A white box round the time marks a page
 * whose weather is old.
 */
static void drawStatus(const String &statusStr, const String &refreshTime,
                       uint32_t batVoltage, bool stale)
{
  g->setFont(&Bitter_9pt8b);
  const uint16_t ink = pal().headerInk;
  const int y = 46;
  int x = DISP_WIDTH - 14;
  const int tw = getStringWidth(refreshTime);
  if (stale)
  {
    g->fillRoundRect(x - tw - 6, y - 15, tw + 10, 20, 4, ink);
    drawString(x - 2, y, refreshTime, RIGHT, ink == W ? K : W);
    x -= tw + 6 + 12;
  }
  else
  {
    drawString(x, y, refreshTime, RIGHT, ink);
    x -= tw + 12;
  }
#if BATTERY_MONITORING
  if (batVoltage != UINT32_MAX)
  {
    const uint32_t pct = calcBatPercent(batVoltage, MIN_BATTERY_VOLTAGE,
                                        MAX_BATTERY_VOLTAGE);
    const String p = String(pct) + "%";
    drawString(x, y, p, RIGHT, ink);
    x -= getStringWidth(p) + 4;
    drawIcon(x - 24, y - 18, getBatBitmap24(pct), 24, 24, ink);
    x -= 24 + 10;
  }
#else
  (void)batVoltage;
#endif
  if (!statusStr.isEmpty())
  {
    drawIcon(x - 24, y - 18, warning_icon_24x24, 24, 24, ink);
  }
}

/* ------------------------------------------------------- the home page */

void fillDisplayBackground()
{
  // white: firstPage() and nextPage() clear to it
}

void initDisplay()
{
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    delay(20);
  }
  SPI.end();
  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
  s_grey = s_wantGrey;
  // not "initial": a full refresh does not depend on what the panel showed
  // before, and the panel loses its power while the board sleeps anyway
  if (s_grey)
  {
    grey.init(115200, false, 10, false, SPI,
              SPISettings(4000000, MSBFIRST, SPI_MODE0));
    mixer.target = &grey;
  }
  else
  {
    bw.init(115200, false, 10, false, SPI,
            SPISettings(4000000, MSBFIRST, SPI_MODE0));
    mixer.target = &bw;
  }
  // the EE05's connector puts the glass's first row at the bottom of the
  // page as the board sits
  mixer.target->setRotation(2);
  g->setTextSize(1);
  g->setTextColor(K);
  g->setTextWrap(false);
  if (s_grey)
  {
    grey.setFullWindow();
    grey.firstPage();
  }
  else
  {
    bw.setFullWindow();
    bw.firstPage();
  }
}

void powerOffDisplay()
{
  grey.hibernate();
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    digitalWrite(PIN_EPD_PWR, LOW);
  }
}

void drawLocationDate(const String &city, const String &date)
{
  drawHeader(city, date, 0);
}

void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft,
                   bool stale)
{
  (void)rssi;
  (void)batDaysLeft;
  drawStatus(statusStr, refreshTimeStr, batVoltage, stale);
}

/* One of the home page's three tiles: an icon, a value, a line under it. */
static void homeTile(int16_t x, int16_t y, const uint8_t *icon48,
                     const String &value, const String &sub)
{
  const int w = 124, h = 128;
  tile(x, y, w, h);
  drawIcon(x + (w - 48) / 2, y + 10, icon48, 48, 48, K);
  g->setFont(&Bitter_16pt8b);
  drawString(x + w / 2, y + 94, fitted(value, w - 10), CENTER, K);
  g->setFont(&Bitter_8pt8b);
  drawString(x + w / 2, y + 116, fitted(sub, w - 10), CENTER, K);
}

/* The home page tile in place `slot` (0 to 2, left to right), showing the
 * widget `key` (config.json "home_tiles"). The keys are the other layouts'
 * widgets; "indoor" (the temperature over the humidity) and "sun" (sunrise
 * over sunset) put two readings in one tile.
 */
static void drawHomeTile(int slot, const String &key,
                         const owm_current_t &current,
                         const owm_resp_air_pollution_t &air,
                         const pollen_info_t &pollen, float inTemp,
                         float inHumidity)
{
  if (key.isEmpty())
  {
    return;
  }
  const int16_t x = 404 + slot * 132, y = HEADER_H + 8;
  const uint8_t *icon = nullptr;
  const uint8_t *dithered = nullptr;
  String value = "--", sub;
  if (key == "wind")
  {
    icon = wi_strong_wind_48x48;
    value = speed(current.wind_speed);
    sub = getCompassPointNotation(current.wind_deg);
  }
  else if (key == "air_quality")
  {
    icon = air_filter_48x48;
    airQuality(air, value, sub);
    if (sub.isEmpty())
    {
      sub = TXT_AIR_QUALITY;
    }
  }
  else if (key == "indoor" && std::isnan(inTemp))
  { // no sensor: the humidity outside
    icon = wi_humidity_48x48;
    value = String(current.humidity) + "%";
    sub = String(TXT_DEWPOINT) + " " + deg(current.dew_point);
  }
  else if (key == "indoor")
  {
    icon = house_thermometer_48x48;
    value = deg(celsius_to_kelvin(inTemp));
    if (!std::isnan(inHumidity))
    {
      sub = String(static_cast<int>(std::lround(inHumidity))) + "% "
            + TXT_HUMIDITY;
    }
  }
  else if (key == "intemp")
  {
    icon = house_thermometer_48x48;
    if (!std::isnan(inTemp))
    {
      value = deg(celsius_to_kelvin(inTemp));
    }
    sub = TXT_INDOOR_TEMPERATURE;
  }
  else if (key == "inhumidity")
  {
    icon = house_humidity_48x48;
    if (!std::isnan(inHumidity))
    {
      value = String(static_cast<int>(std::lround(inHumidity))) + "%";
    }
    sub = TXT_INDOOR_HUMIDITY;
  }
  else if (key == "humidity")
  {
    icon = wi_humidity_48x48;
    value = String(current.humidity) + "%";
    sub = TXT_HUMIDITY;
  }
  else if (key == "dewpoint")
  {
    icon = wi_raindrops_48x48;
    value = deg(current.dew_point);
    sub = TXT_DEWPOINT;
  }
  else if (key == "heat_stress")
  {
    icon = wi_hot_48x48;
    float index;
    int level;
    if (current.heat_stress > 0.f && current.heat_stress_level >= 0)
    { // reported by the source (IMS)
      index = current.heat_stress;
      level = std::min(current.heat_stress_level, 5);
    }
    else
    {
      index = heatStressIndex(kelvin_to_celsius(current.temp),
                              current.humidity);
      level = heatStressLevel(index);
    }
    sub = TXT_HEAT_STRESS;
    if (level >= 0)
    {
      value = String(static_cast<int>(std::floor(index + 0.4999f)));
      sub = TXT_HEAT_STRESS_LEVEL[level];
    }
  }
  else if (key == "pressure")
  {
    icon = wi_barometer_48x48;
    pressure(current.pressure, value, sub);
    if (sub.isEmpty())
    {
      sub = TXT_PRESSURE;
    }
  }
  else if (key == "uvi")
  {
    icon = wi_day_sunny_48x48;
    sub = TXT_UV_INDEX;
    if (!std::isnan(current.uvi))
    {
      const unsigned int uvi = static_cast<unsigned int>(
                               std::max(0.0f, std::round(current.uvi)));
      value = String(uvi);
      sub = String(TXT_UV_INDEX) + " " + getUVIdesc(uvi);
    }
  }
  else if (key == "pollen")
  {
    dithered = pollen_line_48;
    sub = TXT_POLLEN;
    if (pollen.max_upi >= 0)
    {
      value = String(pollen.max_upi);
      sub = pollenLine(pollen);
    }
  }
  else if (key == "visibility")
  {
    icon = visibility_icon_48x48;
    String unit;
    distance(current.visibility, value, unit);
    if (!unit.isEmpty())
    {
      value += " " + unit;
    }
    sub = TXT_VISIBILITY;
  }
  else if (key == "sun")
  {
    icon = wi_sunrise_48x48;
    value = clockTime(current.sunrise, TIME_FORMAT);
    sub = String(TXT_SUNSET) + " " + clockTime(current.sunset, TIME_FORMAT);
  }
  else if (key == "moon_phase")
  {
    int illum = 0;
    const int phase = calcMoonPhase(static_cast<int64_t>(pageTime()), &illum);
    value = String(illum) + "%";
    sub = getMoonPhaseDesc(phase);
    if (s_grey)
    { // drawn in the greys over the tile, in the icon's place
      homeTile(x, y, nullptr, value, sub);
      drawGreyMoon(x + 62, y + 34, 24, moonPhaseFraction(
                   static_cast<int64_t>(pageTime())), LAT.toFloat() < 0);
      return;
    }
    dithered = getMoonPhaseDithered(phase, 48);
  }
  else
  { // a key this layout does not know
    return;
  }
  homeTile(x, y, icon, value, sub);
  if (dithered)
  {
    drawDithered(x + (124 - 48) / 2, y + 10, dithered, 48);
  }
}

void drawCurrentConditions(const owm_current_t &current,
                           const owm_daily_t &today,
                           const owm_resp_air_pollution_t &air,
                           const pollen_info_t &pollen,
                           float inTemp, float inHumidity)
{
  // the sky, the temperature, what it feels like, today's high and low
  drawIcon(8, HEADER_H + 6, getCurrentConditionsBitmap128(current), 128, 128,
           K);
  g->setFont(&Bitter_48pt8b_temperature);
  const String t = std::isnan(current.temp)
                 ? String("--")
                 : String(static_cast<int>(std::lround(
                          toTempUnit(current.temp))));
  drawString(146, 140, t, LEFT, K);
  g->setFont(&Bitter_14pt8b);
  drawString(g->getCursorX() + 2, 92, tempUnit(), LEFT, K);
  g->setFont(&Bitter_11pt8b);
  drawString(150, 170, String(TXT_FEELS_LIKE) + " " + deg(current.feels_like),
             LEFT, K);
  drawString(150, 196, "H " + deg(today.temp.max) + "   L "
             + deg(today.temp.min), LEFT, K);

  // the three tiles chosen in the portal
  for (int i = 0; i < 3; ++i)
  {
    drawHomeTile(i, HOME_TILES[i], current, air, pollen, inTemp, inHumidity);
  }
}

// the graph's box; right of it, the rain chance's 0-100 % scale
static const int GX0 = 58, GX1 = 746, GY0 = 232, GY1 = 370;

/* Draws a line `width` dots thick through the samples, column by column:
 * each column fills from the previous sample's height to its own, so steep
 * stretches stay unbroken. `dash` > 0 leaves gaps of that many columns.
 */
static void plot(const int16_t *ys, int n, int width, uint16_t color,
                 int dash = 0, int16_t x0 = -1)
{
  for (int i = 0; i < n; ++i)
  {
    if (ys[i] == INT16_MIN)
    {
      continue;
    }
    if (dash > 0 && (i / dash) % 2 == 1)
    {
      continue;
    }
    const int16_t prev = (i > 0 && ys[i - 1] != INT16_MIN) ? ys[i - 1] : ys[i];
    const int16_t top = std::min(prev, ys[i]) - width / 2;
    const int16_t bot = std::max(prev, ys[i]) + (width - 1) / 2;
    g->drawFastVLine((x0 < 0 ? GX0 : x0) + i, top, bot - top + 1, color);
  }
}

void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
  (void)daily;
  (void)timeInfo;
  const int hours = std::min(24, OWM_NUM_HOURLY - 1);
  // the range, in the display's unit, in steps of 10
  float lo = INFINITY, hi = -INFINITY;
  for (int i = 0; i < hours; ++i)
  {
    const float t = toTempUnit(hourly[i].temp);
    lo = std::min(lo, t);
    hi = std::max(hi, t);
    if (!std::isnan(hourly[i].dew_point))
    {
      lo = std::min(lo, toTempUnit(hourly[i].dew_point));
    }
  }
  if (!std::isfinite(lo) || !std::isfinite(hi))
  {
    return;
  }
  int yLo = static_cast<int>(std::floor(lo / 10.0f)) * 10;
  int yHi = static_cast<int>(std::ceil(hi / 10.0f)) * 10;
  if (yHi - yLo < 20)
  {
    yHi = yLo + 20;
  }
  const int step = (yHi - yLo > 40) ? 20 : 10;
  auto Y = [&](float t) {
    return static_cast<int16_t>(std::lround(
      GY1 - (GY1 - GY0) * (t - yLo) / static_cast<float>(yHi - yLo)));
  };
  const int n = GX1 - GX0 + 1;
  const float perHour = (GX1 - GX0) / static_cast<float>(hours);
  // the samples sit mid-hour; between them the line is straight
  auto sample = [&](int col, bool dew) -> float {
    const float h = col / perHour - 0.5f;
    const int a = std::max(0, std::min(hours - 1, static_cast<int>(std::floor(h))));
    const int b = std::min(hours - 1, a + 1);
    const float f = std::max(0.0f, std::min(1.0f, h - a));
    const float va = dew ? hourly[a].dew_point : hourly[a].temp;
    const float vb = dew ? hourly[b].dew_point : hourly[b].temp;
    if (std::isnan(va) || std::isnan(vb))
    {
      return NAN;
    }
    return toTempUnit(va + (vb - va) * f);
  };
  static int16_t temp[GX1 - GX0 + 1];
  static int16_t dew[GX1 - GX0 + 1];
  for (int c = 0; c < n; ++c)
  {
    const float t = sample(c, false);
    temp[c] = std::isnan(t) ? INT16_MIN : Y(t);
    const float d = sample(c, true);
    dew[c] = std::isnan(d) ? INT16_MIN : Y(d);
  }

  // under the temperature, if the palette fills it
  if (s_grey && pal().tempFill != W)
  {
    for (int c = 0; c < n; ++c)
    {
      if (temp[c] != INT16_MIN)
      {
        g->drawFastVLine(GX0 + c, temp[c], GY1 - temp[c], pal().tempFill);
      }
    }
  }
  // the chance of rain, behind everything, on its own scale: a full bar is
  // 100 %. Light grey, the grey this panel draws best; so the temperature
  // goes without the fill under it.
  for (int i = 0; i < hours; ++i)
  {
    const int pop = static_cast<int>(std::lround(hourly[i].pop * 100));
    if (pop < 5)
    {
      continue;
    }
    const int bh = pop * (GY1 - GY0) / 100;
    const int x0 = GX0 + static_cast<int>(i * perHour) + 2;
    const int x1 = GX0 + static_cast<int>((i + 1) * perHour) - 2;
    if (s_grey)
    {
      g->fillRect(x0, GY1 - bh, x1 - x0, bh, pal().rain);
    }
    else
    {
      g->drawRect(x0, GY1 - bh, x1 - x0, bh, K);
    }
  }
  g->setFont(&Bitter_8pt8b);
  for (int v = 0; v <= 100; v += 50)
  {
    drawString(GX1 + 6, GY1 - (GY1 - GY0) * v / 100 + 5, String(v) + "%",
               LEFT, K);
  }
  // the scale
  g->setFont(&Bitter_9pt8b);
  for (int t = yLo; t <= yHi; t += step)
  {
    const int y = Y(t);
    dottedLine(GX0, GX1, y, K);
    drawString(GX0 - 8, y + 6, String(t) + "\260", RIGHT, K);
  }
  plot(dew, n, 2, K, 7);
  plot(temp, n, 4, K);
  g->drawFastHLine(GX0, GY1, n, K);
  g->drawFastHLine(GX0, GY1 + 1, n, K);
  // the day's highest and lowest
  int iHi = 0, iLo = 0;
  for (int i = 1; i < hours; ++i)
  {
    if (hourly[i].temp > hourly[iHi].temp) {iHi = i;}
    if (hourly[i].temp < hourly[iLo].temp) {iLo = i;}
  }
  g->setFont(&Bitter_11pt8b);
  for (int k = 0; k < 2; ++k)
  {
    const int i = k ? iLo : iHi;
    const int x = GX0 + static_cast<int>((i + 0.5f) * perHour);
    const int y = Y(toTempUnit(hourly[i].temp));
    g->fillCircle(x, y, 6, K);
    const int lx = std::max(GX0 + 22, std::min(GX1 - 22, x));
    const bool below = k && (y + 32 < GY1 - 10);
    drawString(lx, below ? y + 30 : y - 12, deg(hourly[i].temp), CENTER, K);
  }
  // the hours: a tick each, a time every three
  g->setFont(&Bitter_9pt8b);
  for (int i = 0; i <= hours; ++i)
  {
    const int x = GX0 + static_cast<int>(i * perHour);
    if (i % 3)
    {
      g->drawFastVLine(x, GY1, 3, K);
      continue;
    }
    g->drawFastVLine(x, GY1, 6, K);
    drawString(std::max(GX0 + 16, std::min(GX1 - 16, x)), GY1 + 24,
               clockTime(hourly[0].dt + i * 3600LL, HOUR_FORMAT), CENTER, K);
  }
  // what the lines are (an alert, if there is one, is drawn over this)
  g->setFont(&Bitter_9pt8b);
  int lx = GX0;
  g->fillRect(lx, GY0 - 18, 28, 4, K);
  drawString(lx + 34, GY0 - 10, TXT_TEMPERATURE, LEFT, K);
  lx = g->getCursorX() + 22;
  for (int d = 0; d < 28; d += 14)
  {
    g->fillRect(lx + d, GY0 - 17, 7, 2, K);
  }
  drawString(lx + 34, GY0 - 10, TXT_DEWPOINT, LEFT, K);
  // and the bars: a swatch and an umbrella
  lx = g->getCursorX() + 22;
  if (s_grey)
  {
    g->fillRect(lx, GY0 - 22, 14, 13, pal().rain);
  }
  g->drawRect(lx, GY0 - 22, 14, 13, K);
  drawIcon(lx + 16, GY0 - 28, wi_umbrella_24x24, 24, 24, K);
}

void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  (void)timeInfo;
  const int y0 = 404, cw = DISP_WIDTH / 5;
  for (int k = 0; k < 5; ++k)
  {
    const owm_daily_t &d = daily[k + 1];
    const int x0 = k * cw;
    if (k % 2 == 0 && s_grey)
    {
      g->fillRect(x0, y0, cw, DISP_HEIGHT - y0, pal().dayAlt);
    }
    drawIcon(x0 + 8, y0 + 10, getDailyForecastBitmap48(d), 48, 48, K);
    g->setFont(&Bitter_12pt8b);
    drawString(x0 + 64, y0 + 24, clockTime(d.dt, "%a"), LEFT, K);
    drawString(x0 + 64, y0 + 52, deg(d.temp.max) + " / " + deg(d.temp.min),
               LEFT, K);
    const int pop = static_cast<int>(std::lround(d.pop * 100));
    if (pop >= 10)
    {
      g->setFont(&Bitter_8pt8b);
      drawString(x0 + 64, y0 + 70, String(pop) + "%", LEFT, K);
    }
  }
}

void drawAlerts(std::vector<owm_alerts_t> &alerts, const String &city,
                const String &date)
{
  (void)city;
  (void)date;
  if (alerts.empty())
  {
    return;
  }
  const int y = GY0 - 32, h = 26;
  g->fillRect(0, y, DISP_WIDTH, h, K);
  drawIcon(12, y + 1, warning_icon_24x24, 24, 24, W);
  String title = alerts[0].event;
  toTitleCase(title);
  truncateExtraAlertInfo(title);
  if (alerts.size() > 1)
  {
    title += "  (+" + String(alerts.size() - 1) + ")";
  }
  g->setFont(&Bitter_11pt8b);
  drawString(46, y + 20, fitted(title, DISP_WIDTH - 60), LEFT, W);
}

/* ------------------------------------------- the black-and-white pages */

/* Page 1: the next 24 hours, every two, in two rows of six: the sky, the
 * temperature, what it feels like, the chance of rain and the wind.
 */
static void drawHoursPage(const owm_hourly_t *hourly)
{
  const int cw = DISP_WIDTH / 6;
  const int top = HEADER_H + 4, rh = (DISP_HEIGHT - top) / 2;
  for (int k = 0; k < 12; ++k)
  {
    const owm_hourly_t &h = hourly[std::min(k * 2, OWM_NUM_HOURLY - 1)];
    const int x0 = (k % 6) * cw, y0 = top + (k / 6) * rh, cx = x0 + cw / 2;
    if (k % 6)
    {
      g->drawFastVLine(x0, y0 + 10, rh - 20, K);
    }
    g->setFont(&Bitter_11pt8b);
    drawString(cx, y0 + 26, clockTime(h.dt, HOUR_FORMAT), CENTER, K);
    drawIcon(cx - 24, y0 + 32, getHourlyForecastBitmap48(h), 48, 48, K);
    g->setFont(&Bitter_20pt8b);
    drawString(cx, y0 + 114, deg(h.temp), CENTER, K);
    g->setFont(&Bitter_9pt8b);
    drawString(cx, y0 + 138, fitted(String(TXT_FEELS_LIKE) + " "
               + deg(h.feels_like), cw - 8), CENTER, K);
    // the chance of rain, 0 % included
    const String p = String(static_cast<int>(std::lround(h.pop * 100))) + "%";
    g->setFont(&Bitter_10pt8b);
    const int pw = getStringWidth(p) + 26;
    drawIcon(cx - pw / 2, y0 + 147, wi_umbrella_24x24, 24, 24, K);
    drawString(cx - pw / 2 + 26, y0 + 166, p, LEFT, K);
    g->setFont(&Bitter_9pt8b);
    drawString(cx, y0 + 194, fitted(speed(h.wind_speed) + " "
               + getCompassPointNotation(h.wind_deg), cw - 8), CENTER, K);
  }
  g->drawFastHLine(10, top + rh, DISP_WIDTH - 20, K);
}

/* Page 2: seven days: the chance of rain, the wind, the rain expected, and
 * each day's range drawn against the week's.
 */
static void drawWeekPage(const owm_daily_t *daily)
{
  const int top = HEADER_H + 4, rh = (DISP_HEIGHT - top) / OWM_NUM_DAILY;
  float wLo = INFINITY, wHi = -INFINITY;
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    if (!std::isnan(daily[i].temp.min)) {wLo = std::min(wLo, daily[i].temp.min);}
    if (!std::isnan(daily[i].temp.max)) {wHi = std::max(wHi, daily[i].temp.max);}
  }
  const int bx0 = 500, bx1 = 700;
  for (int i = 0; i < OWM_NUM_DAILY; ++i)
  {
    const owm_daily_t &d = daily[i];
    const int y0 = top + i * rh, base = y0 + rh / 2 + 9;
    if (i)
    {
      g->drawFastHLine(10, y0, DISP_WIDTH - 20, K);
    }
    g->setFont(&Bitter_14pt8b);
    drawString(16, base + 2, clockTime(d.dt, "%a"), LEFT, K);
    drawIcon(96, y0 + (rh - 48) / 2, getDailyForecastBitmap48(d), 48, 48, K);
    g->setFont(&Bitter_11pt8b);
    const int pop = static_cast<int>(std::lround(d.pop * 100));
    if (pop >= 5)
    {
      drawIcon(160, y0 + (rh - 24) / 2, wi_umbrella_24x24, 24, 24, K);
      drawString(188, base, String(pop) + "%", LEFT, K);
    }
    drawIcon(244, y0 + (rh - 24) / 2, wi_strong_wind_24x24, 24, 24, K);
    drawString(274, base, fitted(speed(d.wind_speed) + " "
               + getCompassPointNotation(d.wind_deg), 110), LEFT, K);
#if DAILY_PRECIP_AMOUNTS
    const String amount = precip(d.rain + d.snow);
    if (!amount.isEmpty() && d.rain + d.snow > 0.0f)
    {
      drawString(392, base, amount, LEFT, K);
    }
#endif
    g->setFont(&Bitter_14pt8b);
    drawString(bx0 - 14, base + 2, deg(d.temp.min), RIGHT, K);
    drawString(bx1 + 14, base + 2, deg(d.temp.max), LEFT, K);
    // the week's range as a rule, the day's as a bar along it
    const int yb = y0 + rh / 2;
    g->drawFastHLine(bx0, yb, bx1 - bx0, K);
    if (std::isfinite(wLo) && wHi > wLo && !std::isnan(d.temp.min)
        && !std::isnan(d.temp.max))
    {
      const int a = bx0 + static_cast<int>((d.temp.min - wLo) / (wHi - wLo)
                                           * (bx1 - bx0));
      const int b = bx0 + static_cast<int>((d.temp.max - wLo) / (wHi - wLo)
                                           * (bx1 - bx0));
      g->fillRoundRect(a, yb - 6, std::max(12, b - a), 12, 6, K);
    }
  }
}


/* ------------------------------------------------ hours page, other layouts */

/* `n` values, one an hour, sampled at every column of a box `w` dots wide
 * (the values sit mid-hour, straight between), as rows by Y().
 */
template <typename F>
static void columns(const float *v, int n, int w, int16_t *out, F Y)
{
  const float per = w / static_cast<float>(n);
  for (int c = 0; c < w; ++c)
  {
    const float h = c / per - 0.5f;
    const int a = std::max(0, std::min(n - 1, static_cast<int>(std::floor(h))));
    const int b = std::min(n - 1, a + 1);
    const float f = std::max(0.0f, std::min(1.0f, h - a));
    out[c] = (std::isnan(v[a]) || std::isnan(v[b]))
           ? INT16_MIN : Y(v[a] + (v[b] - v[a]) * f);
  }
}

/* A scale from `lo` to `hi` in steps of `step`, rounded out to them. */
static void niceRange(float lo, float hi, int step, int &a, int &b, int span)
{
  a = static_cast<int>(std::floor(lo / step)) * step;
  b = static_cast<int>(std::ceil(hi / step)) * step;
  if (b - a < span)
  {
    b = a + span;
  }
}

// the hours drawn: 24, one an hour
static const int HN = 24;
// (the 32px hourly icon asks for today, and does not use it)
static const owm_daily_t NO_DAY = {};

static void hourSeries(const owm_hourly_t *hourly, float *temp, float *feel,
                       float *pop, float *wind)
{
  for (int i = 0; i < HN; ++i)
  {
    temp[i] = toTempUnit(hourly[i].temp);
    feel[i] = std::isnan(hourly[i].feels_like) ? temp[i]
                                                : toTempUnit(hourly[i].feels_like);
    pop[i] = hourly[i].pop * 100.0f;
    wind[i] = speedNum(hourly[i].wind_speed);
  }
}

/* The x axis shared by the graphs: a time every three hours. */
static void hourAxis(const owm_hourly_t *hourly, int x0, int x1, int y,
                     int labelY)
{
  const float per = (x1 - x0) / static_cast<float>(HN);
  g->setFont(&Bitter_9pt8b);
  for (int i = 0; i <= HN; ++i)
  {
    const int x = x0 + static_cast<int>(i * per);
    g->drawFastVLine(x, y, (i % 3) ? 3 : 6, K);
    if (i % 3 == 0)
    {
      drawString(std::max(x0 + 16, std::min(x1 - 16, x)), labelY,
                 clockTime(hourly[0].dt + i * 3600LL, HOUR_FORMAT), CENTER, K);
    }
  }
}

/* Layout 1: two tables of twelve hours, every hour: the time, the sky, the
 * temperature, what it feels like, the chance of rain, the wind.
 */
static void drawHoursTable(const owm_hourly_t *hourly)
{
  const int top = HEADER_H + 4, headH = 30, rh = 32;
  for (int t = 0; t < 2; ++t)
  {
    const int x = t * 400 + 8;
    // the column heads: icons and a word where there is room
    drawIcon(x + 92 + 15, top + 3, wi_thermometer_24x24, 24, 24, K);
    g->setFont(&Bitter_8pt8b);
    drawString(x + 186, top + 20, TXT_FEELS_LIKE, CENTER, K);
    drawIcon(x + 240, top + 3, wi_umbrella_24x24, 24, 24, K);
    drawIcon(x + 316, top + 3, wi_strong_wind_24x24, 24, 24, K);
    g->drawFastHLine(x, top + headH - 2, 384, K);
    for (int r = 0; r < 12; ++r)
    {
      const owm_hourly_t &h = hourly[t * 12 + r];
      const int y = top + headH + r * rh, base = y + 22;
      g->setFont(&Bitter_11pt8b);
      drawString(x + 52, base, clockTime(h.dt, HOUR_FORMAT), RIGHT, K);
      drawIcon(x + 60, y, getHourlyForecastBitmap32(h, NO_DAY), 32, 32, K);
      g->setFont(&Bitter_12pt8b);
      drawString(x + 146, base + 1, deg(h.temp), RIGHT, K);
      g->setFont(&Bitter_11pt8b);
      drawString(x + 210, base, deg(h.feels_like), RIGHT, K);
      drawString(x + 276, base, String(static_cast<int>(std::lround(h.pop * 100))) + "%",
                 RIGHT, K);
      drawIcon(x + 290, y + 4, getWindBitmap24(h.wind_deg), 24, 24, K);
      g->setFont(&Bitter_10pt8b);
      drawString(x + 318, base, speed(h.wind_speed), LEFT, K);
      if (r < 11)
      {
        dottedLine(x, x + 384, y + rh - 1, K);
      }
    }
  }
  g->drawFastVLine(400, top + 4, DISP_HEIGHT - top - 10, K);
}

/* Layout 2: three graphs over the same 24 hours: the temperature and what it
 * feels like, the chance of rain, the wind with its direction.
 */
static void drawHoursGraphs(const owm_hourly_t *hourly)
{
  float temp[HN], feel[HN], pop[HN], wind[HN];
  hourSeries(hourly, temp, feel, pop, wind);
  const int x0 = 58, x1 = 746, w = x1 - x0;
  static int16_t ys[800], ys2[800];
  const float per = w / static_cast<float>(HN);

  // the temperature and the feel of it
  float lo = INFINITY, hi = -INFINITY;
  for (int i = 0; i < HN; ++i)
  {
    lo = std::min(lo, std::min(temp[i], feel[i]));
    hi = std::max(hi, std::max(temp[i], feel[i]));
  }
  int a, b;
  niceRange(lo, hi, 10, a, b, 20);
  const int t0 = 88, t1 = 206;
  auto YT = [&](float v) { return static_cast<int16_t>(std::lround(t1 - (t1 - t0) * (v - a) / float(b - a))); };
  g->setFont(&Bitter_9pt8b);
  for (int v = a; v <= b; v += 10)
  {
    dottedLine(x0, x1, YT(v), K);
    drawString(x0 - 8, YT(v) + 6, String(v) + "\260", RIGHT, K);
  }
  columns(feel, HN, w, ys2, YT);
  plot(ys2, w, 2, K, 7, x0);
  columns(temp, HN, w, ys, YT);
  plot(ys, w, 4, K, 0, x0);
  int iHi = 0, iLo = 0;
  for (int i = 1; i < HN; ++i)
  {
    if (temp[i] > temp[iHi]) {iHi = i;}
    if (temp[i] < temp[iLo]) {iLo = i;}
  }
  g->setFont(&Bitter_10pt8b);
  for (int k = 0; k < 2; ++k)
  {
    const int i = k ? iLo : iHi;
    const int x = x0 + static_cast<int>((i + 0.5f) * per), y = YT(temp[i]);
    g->fillCircle(x, y, 5, K);
    drawString(std::max(x0 + 20, std::min(x1 - 20, x)),
               k && y + 26 < t1 ? y + 24 : y - 10, deg(hourly[i].temp), CENTER, K);
  }
  g->setFont(&Bitter_9pt8b);
  g->fillRect(x0, 70, 28, 4, K);
  drawString(x0 + 34, 78, TXT_TEMPERATURE, LEFT, K);
  int lx = g->getCursorX() + 22;
  for (int d = 0; d < 28; d += 14)
  {
    g->fillRect(lx + d, 71, 7, 2, K);
  }
  drawString(lx + 34, 78, TXT_FEELS_LIKE, LEFT, K);

  // the chance of rain: bars in the grey a black/white checker makes
  const int r0 = 238, r1 = 298;
  drawIcon(x0 - 46, r0 + 6, wi_umbrella_24x24, 24, 24, K);
  for (int i = 0; i < HN; ++i)
  {
    const int bh = static_cast<int>(pop[i] * (r1 - r0) / 100.0f);
    if (bh < 2)
    {
      continue;
    }
    const int bx0 = x0 + static_cast<int>(i * per) + 2;
    const int bx1 = x0 + static_cast<int>((i + 1) * per) - 2;
    g->fillRect(bx0, r1 - bh, bx1 - bx0, bh, T_LGW);
    g->drawRect(bx0, r1 - bh, bx1 - bx0, bh, K);
  }
  g->drawFastHLine(x0, r1, w, K);
  dottedLine(x0, x1, r0, K);
  g->setFont(&Bitter_8pt8b);
  drawString(x1 + 6, r0 + 5, "100%", LEFT, K);
  drawString(x1 + 6, r1 + 3, "0%", LEFT, K);
  int iPop = 0;
  for (int i = 1; i < HN; ++i)
  {
    if (pop[i] > pop[iPop]) {iPop = i;}
  }
  if (pop[iPop] >= 10)
  {
    g->setFont(&Bitter_9pt8b);
    drawString(x0 + static_cast<int>((iPop + 0.5f) * per),
               r1 - static_cast<int>(pop[iPop] * (r1 - r0) / 100.0f) - 5,
               String(static_cast<int>(std::lround(pop[iPop]))) + "%", CENTER, K);
  }

  // the wind: its speed as a line, its direction as arrows
  const int w0 = 330, w1 = 396;
  float wHi = 0;
  for (int i = 0; i < HN; ++i)
  {
    wHi = std::max(wHi, wind[i]);
  }
  const int wTop = std::max(10, static_cast<int>(std::ceil(wHi / 5.0f)) * 5);
  auto YW = [&](float v) { return static_cast<int16_t>(std::lround(w1 - (w1 - w0) * v / float(wTop))); };
  drawIcon(x0 - 46, w0 + 18, wi_strong_wind_24x24, 24, 24, K);
  dottedLine(x0, x1, w0, K);
  g->setFont(&Bitter_8pt8b);
  drawString(x1 + 6, w0 + 5, String(wTop), LEFT, K);
  drawString(x1 + 6, w1 + 3, "0", LEFT, K);
  columns(wind, HN, w, ys, YW);
  plot(ys, w, 3, K, 0, x0);
  g->drawFastHLine(x0, w1, w, K);
  for (int i = 1; i < HN; i += 3)
  {
    const int x = x0 + static_cast<int>((i + 0.5f) * per);
    drawIcon(x - 12, w1 + 4, getWindBitmap24(hourly[i].wind_deg), 24, 24, K);
  }
  hourAxis(hourly, x0, x1, w1 + 30, w1 + 54);
}

/* Layout 3: the temperature and its feel as a graph, then eight three-hour
 * cells: the sky, the chance of rain, the wind.
 */
static void drawHoursCombo(const owm_hourly_t *hourly)
{
  float temp[HN], feel[HN], pop[HN], wind[HN];
  hourSeries(hourly, temp, feel, pop, wind);
  const int x0 = 58, x1 = 782, w = x1 - x0;
  static int16_t ys[800];
  const float per = w / static_cast<float>(HN);
  float lo = INFINITY, hi = -INFINITY;
  for (int i = 0; i < HN; ++i)
  {
    lo = std::min(lo, std::min(temp[i], feel[i]));
    hi = std::max(hi, std::max(temp[i], feel[i]));
  }
  int a, b;
  niceRange(lo, hi, 10, a, b, 20);
  const int t0 = 88, t1 = 230;
  auto YT = [&](float v) { return static_cast<int16_t>(std::lround(t1 - (t1 - t0) * (v - a) / float(b - a))); };
  g->setFont(&Bitter_9pt8b);
  for (int v = a; v <= b; v += 10)
  {
    dottedLine(x0, x1, YT(v), K);
    drawString(x0 - 8, YT(v) + 6, String(v) + "\260", RIGHT, K);
  }
  columns(feel, HN, w, ys, YT);
  plot(ys, w, 2, K, 7, x0);
  columns(temp, HN, w, ys, YT);
  plot(ys, w, 4, K, 0, x0);
  g->drawFastHLine(x0, t1, w, K);
  int iHi = 0, iLo = 0;
  for (int i = 1; i < HN; ++i)
  {
    if (temp[i] > temp[iHi]) {iHi = i;}
    if (temp[i] < temp[iLo]) {iLo = i;}
  }
  g->setFont(&Bitter_10pt8b);
  for (int k = 0; k < 2; ++k)
  {
    const int i = k ? iLo : iHi;
    const int x = x0 + static_cast<int>((i + 0.5f) * per), y = YT(temp[i]);
    g->fillCircle(x, y, 5, K);
    drawString(std::max(x0 + 20, std::min(x1 - 20, x)),
               k && y + 26 < t1 ? y + 24 : y - 10, deg(hourly[i].temp), CENTER, K);
  }
  g->setFont(&Bitter_9pt8b);
  g->fillRect(x0, 70, 28, 4, K);
  drawString(x0 + 34, 78, TXT_TEMPERATURE, LEFT, K);
  int lx = g->getCursorX() + 22;
  for (int d = 0; d < 28; d += 14)
  {
    g->fillRect(lx + d, 71, 7, 2, K);
  }
  drawString(lx + 34, 78, TXT_FEELS_LIKE, LEFT, K);

  // eight cells of three hours, each under its stretch of the graph
  const int cy = 238, cw = w / 8;
  for (int k = 0; k < 8; ++k)
  {
    const owm_hourly_t &h = hourly[k * 3 + 1];
    const int cx0 = x0 + k * cw, cx = cx0 + cw / 2;
    if (k)
    {
      g->drawFastVLine(cx0, cy + 6, DISP_HEIGHT - cy - 12, K);
    }
    g->setFont(&Bitter_10pt8b);
    drawString(cx, cy + 22, clockTime(hourly[k * 3].dt, HOUR_FORMAT), CENTER, K);
    drawIcon(cx - 24, cy + 30, getHourlyForecastBitmap48(h), 48, 48, K);
    int p = 0;
    for (int i = k * 3; i < k * 3 + 3; ++i)
    {
      p = std::max(p, static_cast<int>(std::lround(hourly[i].pop * 100)));
    }
    const String ps = String(p) + "%";
    g->setFont(&Bitter_12pt8b);
    const int pw = getStringWidth(ps) + 26;
    drawIcon(cx - pw / 2, cy + 92, wi_umbrella_24x24, 24, 24, K);
    drawString(cx - pw / 2 + 26, cy + 112, ps, LEFT, K);
    // the rain chance as a bar too, in the checker grey
    const int bw = cw - 24;
    g->drawRect(cx - bw / 2, cy + 122, bw, 10, K);
    g->fillRect(cx - bw / 2 + 1, cy + 123, (bw - 2) * p / 100, 8, T_LGW);
    drawIcon(cx - 12, cy + 142, getWindBitmap24(h.wind_deg), 24, 24, K);
    g->setFont(&Bitter_10pt8b);
    drawString(cx, cy + 190, speed(h.wind_speed), CENTER, K);
  }
  g->drawFastVLine(x0 - 1, cy + 6, DISP_HEIGHT - cy - 12, K);
}

void drawPage426(int page, const owm_current_t &current,
                 const owm_hourly_t *hourly, const owm_daily_t *daily,
                 const owm_resp_air_pollution_t &air,
                 const pollen_info_t &pollen, float inTemp,
                 float inHumidity, const String &city,
                 const String &date, const String &refreshTime,
                 uint32_t batVoltage)
{
  drawHeader(city, date, page);
  drawStatus("", refreshTime, batVoltage, false);
  (void)current;
  (void)air;
  (void)pollen;
  (void)inTemp;
  (void)inHumidity;
  if (page == 2)
  {
    drawWeekPage(daily);
  }
  else if (HOURS_LAYOUT_426 == 1)
  {
    drawHoursTable(hourly);
  }
  else if (HOURS_LAYOUT_426 == 2)
  {
    drawHoursGraphs(hourly);
  }
  else if (HOURS_LAYOUT_426 == 3)
  {
    drawHoursCombo(hourly);
  }
  else
  {
    drawHoursPage(hourly);
  }
}

/* ------------------------------------------------- portal and errors */

void drawConfigPortalScreen(const String &line1, const String &line2,
                            const String &line3)
{
  drawIcon((DISP_WIDTH - 196) / 2, 10, wifi_196x196, 196, 196, K);
  g->setFont(&Bitter_26pt8b);
  drawString(DISP_WIDTH / 2, 262, "Setup Mode", CENTER, K);
  const String lines[3] = {line1, line2, line3};
  int y = 318;
  for (int i = 0; i < 3; ++i)
  {
    if (lines[i].isEmpty())
    {
      continue;
    }
    g->setFont(&Bitter_16pt8b);
    if (getStringWidth(lines[i]) > DISP_WIDTH - 16)
    {
      g->setFont(&Bitter_12pt8b);
    }
    drawString(DISP_WIDTH / 2, y, lines[i], CENTER, K);
    y += 46;
  }
}

void drawError(const uint8_t *bitmap_196x196, const String &errMsgLn1,
               const String &errMsgLn2, bool mono)
{
  (void)mono;
  g->setFont(&Bitter_26pt8b);
  if (!errMsgLn2.isEmpty())
  {
    drawString(DISP_WIDTH / 2, DISP_HEIGHT / 2 + 196 / 2 + 21, errMsgLn1,
               CENTER, K);
    drawString(DISP_WIDTH / 2, DISP_HEIGHT / 2 + 196 / 2 + 21 + 55,
               errMsgLn2, CENTER, K);
  }
  else
  {
    drawMultiLnString(DISP_WIDTH / 2, DISP_HEIGHT / 2 + 196 / 2 + 21,
                      errMsgLn1, CENTER, DISP_WIDTH - 200, 2, 55, K);
  }
  drawIcon(DISP_WIDTH / 2 - 196 / 2, DISP_HEIGHT / 2 - 196 / 2 - 21,
           bitmap_196x196, 196, 196, K);
}

/* ------------------------------------------------------------ test card */

/* The panel's greys, to measure and to judge by eye (the
 * seeed_xiao_ee05_testcard build; the simulator draws it too):
 *   top     the four levels, large, for a scan or photo to measure
 *   middle  each pair of levels as a 50 % checker, dot by dot: a mix may
 *           land between two levels
 *   bottom  black and white text and 1-dot lines on each level
 * Corner marks say which way up the card is.
 */
void drawTestCard426()
{
  static const uint16_t LEVEL[4] = {K, DG, LG, W};
  static const char *const NAME[4] = {"black", "dark grey", "light grey",
                                      "white"};
  g->setFont(&Bitter_9pt8b);
  drawString(8, 18, "top left", LEFT, K);
  drawString(DISP_WIDTH - 8, 18, "top right", RIGHT, K);
  drawString(8, DISP_HEIGHT - 6, "bottom left", LEFT, K);
  drawString(DISP_WIDTH - 8, DISP_HEIGHT - 6, "bottom right", RIGHT, K);
  g->setFont(&Bitter_11pt8b);
  drawString(DISP_WIDTH / 2, 20, "EE05 4.26in: the four greys", CENTER, K);

  // the levels: 4 patches 180 x 130, white with an outline
  for (int i = 0; i < 4; ++i)
  {
    const int x = 20 + i * 192, y = 30;
    g->fillRect(x, y, 180, 130, LEVEL[i]);
    if (LEVEL[i] == W)
    {
      g->drawRect(x - 1, y - 1, 182, 132, K);
    }
    g->setFont(&Bitter_9pt8b);
    drawString(x + 90, y + 148, String(i) + "  " + NAME[i], CENTER, K);
  }

  // the checkers: every pair of levels, 6 patches 116 x 90
  static const int PAIR[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3},
                                 {2, 3}};
  for (int p = 0; p < 6; ++p)
  {
    const int x = 20 + p * 128, y = 200;
    for (int j = 0; j < 90; ++j)
    {
      for (int i = 0; i < 116; ++i)
      {
        g->drawPixel(x + i, y + j, LEVEL[PAIR[p][(i + j) & 1]]);
      }
    }
    g->setFont(&Bitter_8pt8b);
    drawString(x + 58, y + 106, String(PAIR[p][0]) + " + " + String(PAIR[p][1]),
               CENTER, K);
  }

  // text and lines on each level
  for (int i = 0; i < 4; ++i)
  {
    const int x = 20 + i * 192, y = 322;
    g->fillRect(x, y, 180, 128, LEVEL[i]);
    g->drawRect(x - 1, y - 1, 182, 130, K);
    g->setFont(&Bitter_14pt8b);
    drawString(x + 8, y + 30, "Rain 72\260", LEFT, K);
    drawString(x + 8, y + 62, "Rain 72\260", LEFT, W);
    g->setFont(&Bitter_8pt8b);
    drawString(x + 8, y + 84, "small 8pt black", LEFT, K);
    drawString(x + 8, y + 102, "small 8pt white", LEFT, W);
    for (int k = 0; k < 4; ++k)
    {
      g->drawFastVLine(x + 140 + k * 4, y + 72, 46, K);
      g->drawFastVLine(x + 142 + k * 4, y + 72, 46, W);
    }
    g->drawFastHLine(x + 8, y + 116, 120, K);
    g->drawFastHLine(x + 8, y + 120, 120, W);
  }
}

#endif // DISP_4G_426
