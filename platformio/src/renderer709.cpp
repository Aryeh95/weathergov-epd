/* Renderer for the 1600x1200 layout (GDEB0709E01) of esp32-weather-epd.
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

/* The 7.09in panel is the size of the 800x480 ones with four times the dots,
 * so the extra resolution buys sharpness and fine screens of ink, not room.
 * The layout was designed for it rather than scaled up:
 *
 *   left    where you are and what it is like now; the sun's path through
 *           the day with the moon under the horizon; eight readings
 *   right   alerts, if any; the week; the next 48 hours as a ribbon of named
 *           weather over a graph of temperature, dew point and rain
 *
 * Coordinates are in pixels of the landscape page, origin top left. They
 * were settled in a series of pixel-for-pixel mockups and are carried over
 * as numbers; the mockups anchored text the way this file's drawText() does.
 */

#include "config.h"

#if defined(DISP_7C_709)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "_locale.h"
#include "_strftime.h"
#include "api_response.h"
#include "conversions.h"
#include "display_utils.h"
#include "history.h"
#include "renderer.h"
#include "sun.h"
#include "text709.h"

#include "assets709/fonts709.h"
#include "assets709/icons709.h"
#include "assets709/moon709.h"
#include "icons/icons_196x196.h"

Epd709 display(PIN_EPD_CS, PIN_EPD_CS2, PIN_EPD_DC, PIN_EPD_RST,
               PIN_EPD_BUSY);

#ifdef EPD709_HOST
time_t hostNow = 0; // the PC preview decides what time it is
static time_t nowUtc() { return hostNow; }
#else
// the page's own moment: the clock, or the time of the weather when
// the last page is drawn again during an outage (display_utils)
static time_t nowUtc() { return pageTime(); }
#endif

static const uint16_t K = GxEPD_BLACK, W = GxEPD_WHITE, R = GxEPD_RED,
                      Y = GxEPD_YELLOW, G = GxEPD_GREEN, B = GxEPD_BLUE;

// the page
static const int LEFT0 = 36, LEFT1 = 548, LEFTC = 292; // left column
static const int RIGHT0 = 640, RIGHT1 = 1564;          // right column
static const int PLOT0 = 700, PLOT1 = 1480;          // the graph's plot

/* =========================================================================
 * Ink
 * ====================================================================== */

/* The panel has six inks and nothing between them. Every other colour is a
 * screen: a regular pattern of dots of one ink over another, fine enough at
 * 282 ppi for the eye to blend. The pattern is keyed to the pixel's place on
 * the page, so neighbouring shapes continue one screen instead of each
 * starting its own.
 */
static const uint8_t BAYER8[8][8] = {
  { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
  {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
  { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
  {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};

static inline bool screen(int x, int y, float share)
{
  return share * 64.0f > BAYER8[y & 7][x & 7] + 0.5f;
}

/* `top` over `under`, `share` of the area being `top`. With cover below 1
 * the mixture is itself screened against bare paper (a second, offset
 * screen): the pale navy of a mostly clear night is black over blue, 82%
 * of it there.
 */
struct Tone
{
  uint16_t top;
  float share;
  uint16_t under;
  float cover;
};

static inline uint16_t toneAt(const Tone &t, int x, int y)
{
  if (t.cover < 1.0f && !screen(x + 3, y + 5, t.cover))
  {
    return W;
  }
  return screen(x, y, t.share) ? t.top : t.under;
}

static const Tone T_AMBER     = {R, 0.50f, Y, 1.0f};
static const Tone T_PLUM      = {R, 0.60f, B, 1.0f};
static const Tone T_MAROON    = {R, 0.50f, K, 1.0f};
static const Tone T_ADVISORY  = {Y, 0.55f, W, 1.0f};
static const Tone T_RAIN_BAR  = {B, 0.40f, W, 1.0f};

/* =========================================================================
 * Shapes
 * ====================================================================== */

static inline void px(int x, int y, uint16_t ink)
{
  display.drawPixel(x, y, ink);
}

// is (x, y) inside the box with corners rounded to radius r? Corners
// inclusive, as the mockups' drawing library counted them.
static inline bool inRound(int x, int y, int x0, int y0, int x1, int y1,
                           int r)
{
  if (r <= 0)
  {
    return true;
  }
  const int cx = (x < x0 + r) ? x0 + r : (x > x1 - r) ? x1 - r : x;
  const int cy = (y < y0 + r) ? y0 + r : (y > y1 - r) ? y1 - r : y;
  const int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r;
}

static void fillBox(int x0, int y0, int x1, int y1, int r, uint16_t ink)
{
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      if (inRound(x, y, x0, y0, x1, y1, r))
      {
        px(x, y, ink);
      }
    }
  }
}

static void fillTone(int x0, int y0, int x1, int y1, int r, const Tone &t)
{
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      if (inRound(x, y, x0, y0, x1, y1, r))
      {
        px(x, y, toneAt(t, x, y));
      }
    }
  }
}

// outline, `w` pixels wide, drawn inward from the box's edge
static void strokeBox(int x0, int y0, int x1, int y1, int r, int w,
                      uint16_t ink)
{
  const int ri = std::max(0, r - w);
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      if (!inRound(x, y, x0, y0, x1, y1, r))
      {
        continue;
      }
      const bool inner = x >= x0 + w && x <= x1 - w && y >= y0 + w
                      && y <= y1 - w
                      && inRound(x, y, x0 + w, y0 + w, x1 - w, y1 - w, ri);
      if (!inner)
      {
        px(x, y, ink);
      }
    }
  }
}

static void disc(float cx, float cy, float r, uint16_t ink)
{
  const int x0 = static_cast<int>(floorf(cx - r)), x1 = static_cast<int>(ceilf(cx + r));
  const int y0 = static_cast<int>(floorf(cy - r)), y1 = static_cast<int>(ceilf(cy + r));
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      const float dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= r * r)
      {
        px(x, y, ink);
      }
    }
  }
}

static void ring(float cx, float cy, float r, float w, uint16_t ink)
{
  const float ri = r - w;
  const int x0 = static_cast<int>(floorf(cx - r)), x1 = static_cast<int>(ceilf(cx + r));
  const int y0 = static_cast<int>(floorf(cy - r)), y1 = static_cast<int>(ceilf(cy + r));
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      const float d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 <= r * r && d2 > ri * ri)
      {
        px(x, y, ink);
      }
    }
  }
}

// a line `w` pixels thick with round ends, so that joined lines make a
// smooth curve
static void stroke(float x0, float y0, float x1, float y1, int w,
                   uint16_t ink)
{
  const float dx = x1 - x0, dy = y1 - y0;
  const int n = std::max(1, static_cast<int>(
                  ceilf(std::max(fabsf(dx), fabsf(dy)))));
  const float r = w / 2.0f;
  for (int i = 0; i <= n; ++i)
  {
    const float x = x0 + dx * i / n, y = y0 + dy * i / n;
    if (w <= 1)
    {
      px(static_cast<int>(lroundf(x)), static_cast<int>(lroundf(y)), ink);
    }
    else
    {
      disc(x, y, r, ink);
    }
  }
}

static void dashH(int x0, int x1, int y, int w, int on, int off,
                  uint16_t ink)
{
  for (int x = x0; x <= x1; ++x)
  {
    if ((x - x0) % (on + off) < on)
    {
      for (int j = 0; j < w; ++j)
      {
        px(x, y + j, ink);
      }
    }
  }
}

static void dashV(int x, int y0, int y1, int w, int on, int off,
                  uint16_t ink)
{
  for (int y = y0; y <= y1; ++y)
  {
    if ((y - y0) % (on + off) < on)
    {
      for (int j = 0; j < w; ++j)
      {
        px(x + j, y, ink);
      }
    }
  }
}

static void triangle(float ax, float ay, float bx, float by, float cx,
                     float cy, uint16_t ink)
{
  const int x0 = static_cast<int>(floorf(std::min({ax, bx, cx})));
  const int x1 = static_cast<int>(ceilf(std::max({ax, bx, cx})));
  const int y0 = static_cast<int>(floorf(std::min({ay, by, cy})));
  const int y1 = static_cast<int>(ceilf(std::max({ay, by, cy})));
  const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  if (fabsf(area) < 1e-3f)
  {
    return;
  }
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      const float w0 = ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) / area;
      const float w1 = ((cx - bx) * (y - by) - (cy - by) * (x - bx)) / area;
      const float w2 = ((ax - cx) * (y - cy) - (ay - cy) * (x - cx)) / area;
      if (w0 >= 0 && w1 >= 0 && w2 >= 0)
      {
        px(x, y, ink);
      }
    }
  }
}

/* =========================================================================
 * Text
 * ====================================================================== */

/* Where (x, y) sits on the text, as two letters: across, l m r (left,
 * middle, right); up and down, a m s d (ascender line, middle, baseline,
 * descender line). "ma" centres a word under a point; "lm" sets it to the
 * right of one, centred on it.
 */
enum Anchor : uint8_t { LA, MA, RA, LM, MM, RM, LS, MS, RS, LD, MD, RD };

/* The strings are Latin-1 (one byte, one character), like the locale
 * files. Text that arrives as UTF-8 -- a city name from config.json -- has
 * its two-byte Latin-1 characters and the en dash folded back; anything
 * else outside Latin-1 is dropped.
 */
static const char *nextChar(const char *s, int &code)
{
  const uint8_t c = static_cast<uint8_t>(s[0]);
  const uint8_t c1 = static_cast<uint8_t>(s[1]);
  if ((c == 0xC2 || c == 0xC3) && (c1 & 0xC0) == 0x80)
  {
    code = ((c & 0x03) << 6) | (c1 & 0x3F);
    return s + 2;
  }
  if (c == 0xE2 && c1 == 0x80 && static_cast<uint8_t>(s[2]) == 0x93)
  {
    code = 0x7F; // en dash
    return s + 3;
  }
  code = c;
  return s + 1;
}

static int textWidth(const char *s, const Font709 &f)
{
  int w = 0;
  while (*s)
  {
    int code;
    s = nextChar(s, code);
    if (code >= f.first && code <= f.last)
    {
      w += f.glyphs[code - f.first].xAdv;
    }
  }
  return w;
}
static int textWidth(const String &s, const Font709 &f)
{
  return textWidth(s.c_str(), f);
}

// Returns the x the next character would start at.
static int drawText(int x, int y, const char *s, const Font709 &f,
                    uint16_t ink = K, Anchor a = LA, bool grey = false)
{
  const int w = textWidth(s, f);
  switch (a)
  {
  case MA: case MM: case MS: case MD: x -= w / 2; break;
  case RA: case RM: case RS: case RD: x -= w;     break;
  default: break;
  }
  int base = y;
  switch (a)
  {
  case LA: case MA: case RA: base = y + f.ascent;                   break;
  case LM: case MM: case RM: base = y + (f.ascent - f.descent) / 2; break;
  case LD: case MD: case RD: base = y - f.descent;                  break;
  default: break;
  }
  display.setStipple(grey);
  while (*s)
  {
    int code;
    s = nextChar(s, code);
    if (code < f.first || code > f.last)
    {
      continue;
    }
    const Glyph709 &g = f.glyphs[code - f.first];
    const uint8_t *bits = f.bitmaps + g.offset;
    uint32_t n = 0;
    for (int j = 0; j < g.h; ++j)
    {
      for (int i = 0; i < g.w; ++i, ++n)
      {
        if (bits[n >> 3] & (0x80 >> (n & 7)))
        {
          px(x + g.xOff + i, base + g.yOff + j, ink);
        }
      }
    }
    x += g.xAdv;
  }
  display.setStipple(false);
  return x;
}
static int drawText(int x, int y, const String &s, const Font709 &f,
                    uint16_t ink = K, Anchor a = LA, bool grey = false)
{
  return drawText(x, y, s.c_str(), f, ink, a, grey);
}

// the largest of the given faces the text fits in, or the last of them
static const Font709 &fitFont(const String &s, int room,
                              std::initializer_list<const Font709 *> faces)
{
  const Font709 *last = nullptr;
  for (const Font709 *f : faces)
  {
    last = f;
    if (textWidth(s, *f) <= room)
    {
      return *f;
    }
  }
  return *last;
}

// text on a patch of bare paper, so that it reads over lines and screens
static void labelOnPaper(int cx, int cy, const String &s, const Font709 &f,
                         uint16_t ink)
{
  const int w = textWidth(s, f) + 12;
  const int h = f.ascent + f.descent - 4;
  fillBox(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, 6, W);
  drawText(cx, cy, s, f, ink, MM);
}

/* =========================================================================
 * Pictures
 * ====================================================================== */

static void drawIcon(const uint8_t *data, int size, int x, int y)
{
  static const uint16_t INK[7] = {0, K, W, R, Y, G, B};
  if (!data)
  {
    return;
  }
  for (int32_t n = 0; n < static_cast<int32_t>(size) * size; ++n)
  {
    const uint8_t b = pgm_read_byte(&data[n >> 1]);
    const uint8_t v = (n & 1) ? (b & 0x0F) : (b >> 4);
    if (v && v < 7)
    {
      px(x + (n % size), y + (n / size), INK[v]);
    }
  }
}

/* The condition icon for an OpenWeatherMap-style id, at 220 or 104 px. The
 * table that turns an id into one of the set's pictures is the one the
 * 800x480 colour panels use (display_utils.cpp, getColorConditionsIcon),
 * so both agree on what a forecast looks like.
 */
static const uint8_t *conditionIcon(int id, bool day, int size)
{
  const char *base;
  if      (id >= 200 && id < 300)  {base = "11";}
  else if (id >= 300 && id < 500)  {base = "09";}
  else if (id >= 500 && id < 511)  {base = "10";}
  else if (id == 511)              {base = "13";}
  else if (id > 511 && id < 600)   {base = "09";}
  else if (id >= 600 && id < 700)  {base = "13";}
  else if (id >= 700 && id < 800)  {base = "50";}
  else if (id == 800)              {base = "01";}
  else if (id == 801 || id == 802) {base = "022";}
  else if (id == 803)              {base = "02";}
  else                             {base = "04";}
  char want[8], wantDay[8];
  snprintf(want, sizeof(want), "%s%c", base, day ? 'd' : 'n');
  snprintf(wantDay, sizeof(wantDay), "%sd", base);
  const Icon709 *found = nullptr;
  for (const Icon709 &ic : ICONS709)
  {
    if (strcmp(ic.code, want) == 0)
    {
      found = &ic;
      break;
    }
    if (strcmp(ic.code, wantDay) == 0)
    {
      found = &ic; // the day picture stands in where there is no night one
    }
  }
  if (!found)
  {
    return nullptr;
  }
  return (size == 220) ? found->px220 : found->px104;
}

/* The sun: yellow at the centre deepening to orange at the rim, in a halo
 * that fades into the paper. `risen` false draws only what stands above
 * the horizon at y.
 */
static void drawSun(int cx, int cy, bool whole)
{
  const float core = 26.0f, glow = 50.0f;
  for (int y = cy - 50; y <= (whole ? cy + 50 : cy); ++y)
  {
    for (int x = cx - 50; x <= cx + 50; ++x)
    {
      const float d = sqrtf(static_cast<float>((x - cx) * (x - cx)
                                               + (y - cy) * (y - cy)));
      if (d <= core)
      {
        px(x, y, screen(x, y, 0.65f * powf(d / core, 2.2f)) ? R : Y);
      }
      else if (d <= core + 2.0f)
      {
        px(x, y, R);
      }
      else if (d <= glow)
      {
        const float k = powf((glow - d) / (glow - core), 1.6f) * 0.7f;
        if (screen(x, y, k))
        {
          px(x, y, Y);
        }
      }
    }
  }
}

/* The moon at a phase (0 = new .. MOON709_STEPS / 2 = full), from NASA's
 * map of its surface. The side the sun does not reach is black.
 */
static void drawMoon(int cx, int cy, int phase)
{
  const uint8_t *data = MOON709[phase % MOON709_STEPS];
  const int half = MOON709_PX / 2, row = (MOON709_PX + 7) / 8;
  for (int j = 0; j < MOON709_PX; ++j)
  {
    for (int i = 0; i < MOON709_PX; ++i)
    {
      const float dx = i - half + 0.5f, dy = j - half + 0.5f;
      if (dx * dx + dy * dy > static_cast<float>(half * half))
      {
        continue;
      }
      const uint8_t b = pgm_read_byte(&data[j * row + (i >> 3)]);
      px(cx - half + i, cy - half + j, (b & (0x80 >> (i & 7))) ? W : K);
    }
  }
  ring(cx - 0.5f, cy - 0.5f, half, 2, K);
}

// a 1-bit picture (a cleared bit is ink), each of its dots drawn 2x2
static void drawBitmap2x(int x, int y, const uint8_t *bitmap, int w, int h,
                         uint16_t ink)
{
  const int byteWidth = (w + 7) / 8;
  for (int j = 0; j < h; ++j)
  {
    for (int i = 0; i < w; ++i)
    {
      const uint8_t b = pgm_read_byte(&bitmap[j * byteWidth + (i >> 3)]);
      if (!((b >> (7 - (i & 7))) & 1))
      {
        px(x + 2 * i,     y + 2 * j,     ink);
        px(x + 2 * i + 1, y + 2 * j,     ink);
        px(x + 2 * i,     y + 2 * j + 1, ink);
        px(x + 2 * i + 1, y + 2 * j + 1, ink);
      }
    }
  }
}

/* =========================================================================
 * Weather, in words
 * ====================================================================== */

// Order matters: TXT709_KIND and RIBBON follow it, and everything from
// K_SHOWERS on falls from the sky.
enum Kind : uint8_t { K_SUNNY, K_MSUNNY, K_CLEAR, K_MCLEAR, K_PARTLY,
                      K_MCLOUDY, K_CLOUDY, K_FOG, K_SHOWERS, K_RAIN,
                      K_TSTORM, K_SNOW, K_ICE };
static inline bool isWet(Kind k) { return k >= K_SHOWERS; }

static int64_t sunUp = 0, sunDown = 0; // today's, for judging any hour

static bool daylightAt(int64_t t, bool fallback)
{
  if (sunUp <= 0 || sunDown <= sunUp)
  {
    return fallback;
  }
  const int64_t day = 86400;
  const int64_t since = ((t - sunUp) % day + day) % day;
  return since < (sunDown - sunUp);
}

static Kind skyKind(int clouds, bool day)
{
  if (clouds <= 10) {return day ? K_SUNNY : K_CLEAR;}
  if (clouds <= 30) {return day ? K_MSUNNY : K_MCLEAR;}
  if (clouds <= 60) {return K_PARTLY;}
  if (clouds <= 85) {return K_MCLOUDY;}
  return K_CLOUDY;
}

/* What to call the weather of a forecast. NWS names a wet hour by what may
 * fall even when the chance is slight; a spell of the ribbon is only called
 * wet from a 30% chance up, and below that goes by its cloud cover.
 */
static Kind kindOf(int id, int clouds, bool day, float pop, bool byChance)
{
  Kind wet = K_SUNNY;
  bool falls = true;
  if      (id >= 200 && id < 300) {wet = K_TSTORM;}
  else if (id >= 300 && id < 400) {wet = K_SHOWERS;}
  else if (id >= 500 && id < 511) {wet = K_RAIN;}
  else if (id == 511)             {wet = K_ICE;}
  else if (id > 511 && id < 600)  {wet = K_SHOWERS;}
  else if (id >= 611 && id <= 613) {wet = K_ICE;}
  else if (id >= 600 && id < 700) {wet = K_SNOW;}
  else if (id == 771)             {wet = K_RAIN;}   // hurricane, tropical storm
  else if (id == 781)             {wet = K_TSTORM;} // tornado
  else                            {falls = false;}
  if (falls)
  {
    if (!byChance || pop >= 0.30f)
    {
      return wet;
    }
    return skyKind(clouds, day);
  }
  if (id >= 700 && id < 800) {return K_FOG;}
  switch (id)
  {
  case 800: return day ? K_SUNNY : K_CLEAR;
  case 801: return day ? K_MSUNNY : K_MCLEAR;
  case 802: return K_PARTLY;
  case 803: return K_MCLOUDY;
  case 804: return K_CLOUDY;
  default:  return skyKind(clouds, day);
  }
}

static int toUnits(float kelvin)
{
#if defined(UNITS_TEMP_KELVIN)
  return static_cast<int>(lroundf(kelvin));
#elif defined(UNITS_TEMP_CELSIUS)
  return static_cast<int>(lroundf(kelvin_to_celsius(kelvin)));
#else
  return static_cast<int>(lroundf(kelvin_to_fahrenheit(kelvin)));
#endif
}
static float toUnitsF(float kelvin)
{
#if defined(UNITS_TEMP_KELVIN)
  return kelvin;
#elif defined(UNITS_TEMP_CELSIUS)
  return kelvin_to_celsius(kelvin);
#else
  return kelvin_to_fahrenheit(kelvin);
#endif
}
static String tempText(float kelvin)
{
  return String(toUnits(kelvin)) + "\xB0";
}

static String clock(time_t t, const char *format)
{
  char buf[24] = {};
  tm local;
  localtime_r(&t, &local);
  _strftime(buf, sizeof(buf), format, &local);
  String s = buf;
  s.trim();
  return s;
}
// to the hour when it is on the hour, to the minute when it is not
static String clockShort(time_t t)
{
  tm local;
  localtime_r(&t, &local);
  return clock(t, local.tm_min ? TIME_FORMAT : HOUR_FORMAT);
}

static String span(long seconds)
{
  const long minutes = (seconds + 30) / 60;
  String s = String(minutes / 60) + " " TXT709_HOURS;
  if (minutes % 60)
  {
    s += " " + String(minutes % 60) + " " TXT709_MINUTES;
  }
  return s;
}

static String sentenceCase(String s)
{
  s.toLowerCase();
  if (s.length())
  {
    s.setCharAt(0, toupper(s.charAt(0)));
  }
  return s;
}

/* The locale files write their labels in Title Case ("Dew Point"), which
 * suited the small capitals-heavy 800x480 layouts. This one sets them as
 * a sentence would ("Dew point") -- in English only, where lowering a
 * capital costs nothing; German nouns keep theirs.
 */
static String label(const char *text)
{
  if (!OWM_LANG.startsWith("en"))
  {
    return text;
  }
  String s = sentenceCase(text);
  s.replace("Uv ", "UV ");
  return s;
}

/* =========================================================================
 * Alerts
 * ====================================================================== */

struct Alert709
{
  String event;
  String until;
  int rank; // 0 warning, 1 watch, 2 advisory and everything else
};
static std::vector<Alert709> notices;
static int alertTop = 0; // room the strip takes from what is under it
static const owm_hourly_t *hours = nullptr;

/* Called once, before the page is drawn. The alert strip pushes the rest of
 * the right column down, so how many alerts there are has to be known
 * before the first of it is drawn -- and main.cpp draws the alerts last.
 */
void layout709Begin(std::vector<owm_alerts_t> &alerts,
                    const owm_hourly_t *hourly)
{
  hours = hourly;
  notices.clear();
  alertTop = 0;
#if DISPLAY_ALERTS
  if (alerts.empty())
  {
    return;
  }
  int ignore[OWM_NUM_ALERTS] = {0};
  filterAlerts(alerts, ignore);
  const time_t now = nowUtc();
  tm today;
  localtime_r(&now, &today);
  for (size_t i = 0; i < alerts.size() && i < OWM_NUM_ALERTS; ++i)
  {
    if (ignore[i])
    {
      continue;
    }
    Alert709 a;
    a.event = alerts[i].event;
    toTitleCase(a.event);
    a.event = label(a.event.c_str());
    String lower = alerts[i].event;
    lower.toLowerCase();
    a.rank = (lower.indexOf("warning") >= 0) ? 0
           : (lower.indexOf("watch") >= 0)   ? 1
                                             : 2;
    if (alerts[i].end > 0)
    {
      const time_t end = static_cast<time_t>(alerts[i].end);
      tm last;
      localtime_r(&end, &last);
      a.until = String(TXT709_UNTIL " ") + clockShort(end);
      if (last.tm_yday != today.tm_yday || last.tm_year != today.tm_year)
      {
        a.until += String(" ") + LC_DAY[last.tm_wday];
      }
    }
    notices.push_back(a);
  }
  std::stable_sort(notices.begin(), notices.end(),
                   [](const Alert709 &p, const Alert709 &q)
                   { return p.rank < q.rank; });
  if (notices.size() == 1)
  {
    const String line = notices[0].event + TXT709_DOT + notices[0].until;
    alertTop = (textWidth(line, F_BITTER_36) <= RIGHT1 - RIGHT0 - 96) ? 84 : 100;
  }
  else if (notices.size() > 1)
  {
    alertTop = 100;
  }
#else
  (void)alerts;
#endif
} // end layout709Begin

static void drawNotice(int x0, int y0, int x1, int y1, const Alert709 &a,
                       bool oneLine)
{
  uint16_t ink = K, sign = Y;
  if (a.rank == 0)
  { // a warning: the strongest thing on the page
    fillBox(x0, y0, x1, y1, 10, R);
    ink = W;
    sign = R;
  }
  else
  {
    fillTone(x0, y0, x1, y1, 10, (a.rank == 1) ? T_AMBER : T_ADVISORY);
    strokeBox(x0, y0, x1, y1, 10, 2, K);
  }
  const int cy = (y0 + y1) / 2;
  triangle(x0 + 22, cy + 17, x0 + 42, cy - 19, x0 + 62, cy + 17, ink);
  triangle(x0 + 29, cy + 13, x0 + 42, cy - 11, x0 + 55, cy + 13, sign);
  fillBox(x0 + 40, cy - 4, x0 + 44, cy + 5, 0, ink);
  fillBox(x0 + 40, cy + 8, x0 + 44, cy + 11, 0, ink);
  const int room = x1 - x0 - 96;
  if (oneLine)
  {
    drawText(x0 + 80, cy, a.event + TXT709_DOT + a.until, F_BITTER_36, ink,
             LM);
    return;
  }
  drawText(x0 + 80, cy - 16, a.event,
           fitFont(a.event, room, {&F_BITTER_34, &F_BITTER_30, &F_BITTER_27,
                                   &F_BITTER_24, &F_BITTER_22}),
           ink, LM);
  drawText(x0 + 80, cy + 20, a.until,
           fitFont(a.until, room, {&F_SANS_25, &F_SANS_22}), ink, LM);
}

/* One alert has the strip to itself. Two share it side by side, the more
 * serious on the left. With more than two, the two most serious are shown
 * and a tag says how many are not.
 */
void drawAlerts(std::vector<owm_alerts_t> &alerts, const String &city,
                const String &date)
{
  (void)alerts; (void)city; (void)date; // prepared by layout709Begin
  if (notices.empty())
  {
    return;
  }
  if (notices.size() == 1)
  {
    if (alertTop == 84)
    {
      drawNotice(RIGHT0, 28, RIGHT1, 92, notices[0], true);
    }
    else
    {
      drawNotice(RIGHT0, 28, RIGHT1, 108, notices[0], false);
    }
    return;
  }
  const int more = static_cast<int>(notices.size()) - 2;
  const int right = RIGHT1 - (more ? 92 : 0);
  const int end = right - (more ? 12 : 0);
  // a long name takes room from a neighbour that does not need it
  float need[2];
  for (int i = 0; i < 2; ++i)
  {
    need[i] = 96 + std::max(textWidth(notices[i].event, F_BITTER_34),
                            textWidth(notices[i].until, F_SANS_25));
  }
  const float half = (end - RIGHT0 - 12) / 2.0f;
  float w0 = half;
  if (need[0] > half && need[1] < half)
  {
    w0 = std::min(need[0], end - RIGHT0 - 12 - need[1]);
  }
  else if (need[1] > half && need[0] < half)
  {
    w0 = std::max(need[0], end - RIGHT0 - 12 - need[1]);
  }
  const int mid = static_cast<int>(RIGHT0 + w0 + 6);
  drawNotice(RIGHT0, 28, mid - 6, 108, notices[0], false);
  drawNotice(mid + 6, 28, end, 108, notices[1], false);
  if (more)
  {
    strokeBox(right, 28, RIGHT1, 108, 10, 2, K);
    drawText((right + RIGHT1) / 2, 56, "+" + String(more), F_BITTER_36, K, MM);
    drawText((right + RIGHT1) / 2, 88, TXT709_MORE, F_SANS_24, K, MM);
  }
} // end drawAlerts

/* =========================================================================
 * Left: where, and what it is like now
 * ====================================================================== */

void drawLocationDate(const String &city, const String &date)
{
  drawText(LEFT0, 28, city,
           fitFont(city, LEFT1 - LEFT0, {&F_BITTERSB_72, &F_BITTER_52,
                                     &F_BITTER_40, &F_BITTER_32}),
           K, LA);
  drawText(LEFT0, 114, date,
           fitFont(date, LEFT1 - LEFT0, {&F_BITTER_40, &F_BITTER_32}), K, LA);
}

/* A chip: the word for a risk level on its colour. */
#define RISK_PLAIN  0
#define RISK_GREEN  1
#define RISK_YELLOW 2
#define RISK_AMBER  3
#define RISK_RED    4
#define RISK_PURPLE 5
#define RISK_MAROON 6

static int chipWidth(const char *word)
{
  return textWidth(word, F_SANSB_25) + 26;
}

static void drawChip(int x, int y, const char *word, int level)
{
  const int w = chipWidth(word), h = 39;
  uint16_t ink = W;
  switch (level)
  {
  case RISK_GREEN:  fillBox(x, y, x + w, y + h, 8, G);              break;
  case RISK_YELLOW: fillBox(x, y, x + w, y + h, 8, Y); ink = K;     break;
  case RISK_AMBER:  fillTone(x, y, x + w, y + h, 8, T_AMBER); ink = K; break;
  case RISK_RED:    fillBox(x, y, x + w, y + h, 8, R);              break;
  case RISK_PURPLE: fillTone(x, y, x + w, y + h, 8, T_PLUM);        break;
  case RISK_MAROON: fillTone(x, y, x + w, y + h, 8, T_MAROON);      break;
  default:          ink = K;                                        break;
  }
  drawText(x + w / 2, y + h / 2, word, F_SANSB_25, ink, MM);
}

static void drawTrend(int x, int y, int trend)
{
  // y is the top of the value's line; the arrow sits in the digits' height
  if (trend == 1)       // rising
  {
    triangle(x, y + 26, x + 9, y + 10, x + 18, y + 26, K);
  }
  else if (trend == -1) // falling
  {
    triangle(x, y + 12, x + 9, y + 28, x + 18, y + 12, K);
  }
}

struct Cell
{
  String label;
  String tag;        // small print after the label: "(EPA)", "(weed)"
  String value;
  const char *word;  // the risk level, or nullptr
  const char *brief; // a shorter word for it, or nullptr
  int level;
  int trend;         // 1 rising, -1 falling, anything else draws nothing
};

static void drawCell(int i, const Cell &c)
{
  const int x = LEFT0 + (i % 2) * 264;
  const int y = 842 + (i / 2) * 79;
  const int edge = x + 252; // where the column ends
  int lx = drawText(x, y, c.label, F_SANS_25, K, LA, true);
  if (c.tag.length() && lx + 8 + textWidth(c.tag, F_SANS_22) <= edge)
  {
    drawText(lx + 8, y + 3, c.tag, F_SANS_22, K, LA, true);
  }
  int vx = drawText(x, y + 28, c.value, F_BITTER_36, K, LA);
  if (c.trend == 1 || c.trend == -1)
  {
    drawTrend(vx + 8, y + 28, c.trend);
  }
  if (!c.word)
  {
    return;
  }
  const char *word = c.word;
  if (vx + 14 + chipWidth(word) > edge && c.brief)
  {
    word = c.brief;
  }
  if (vx + 14 + chipWidth(word) <= edge)
  {
    drawChip(vx + 14, y + 31, word, c.level);
  }
}

static String windText(const owm_current_t &current)
{
  String s;
#if defined(UNITS_SPEED_METERSPERSECOND)
  s = String(static_cast<int>(lroundf(current.wind_speed)))
    + " " + TXT_UNITS_SPEED_METERSPERSECOND;
#elif defined(UNITS_SPEED_FEETPERSECOND)
  s = String(static_cast<int>(lroundf(
        meterspersecond_to_feetpersecond(current.wind_speed))))
    + " " + TXT_UNITS_SPEED_FEETPERSECOND;
#elif defined(UNITS_SPEED_KILOMETERSPERHOUR)
  s = String(static_cast<int>(lroundf(
        meterspersecond_to_kilometersperhour(current.wind_speed))))
    + " " + TXT_UNITS_SPEED_KILOMETERSPERHOUR;
#elif defined(UNITS_SPEED_MILESPERHOUR)
  s = String(static_cast<int>(lroundf(
        meterspersecond_to_milesperhour(current.wind_speed))))
    + " " + TXT_UNITS_SPEED_MILESPERHOUR;
#elif defined(UNITS_SPEED_KNOTS)
  s = String(static_cast<int>(lroundf(
        meterspersecond_to_knots(current.wind_speed))))
    + " " + TXT_UNITS_SPEED_KNOTS;
#else
  s = String(meterspersecond_to_beaufort(current.wind_speed))
    + " " + TXT_UNITS_SPEED_BEAUFORT;
#endif
#if !defined(WIND_INDICATOR_NONE)
  // sixteen points of the compass, from the locale's table of 32
  const int deg = ((current.wind_deg % 360) + 360) % 360;
  const int point = static_cast<int>(lroundf(deg / 22.5f)) % 16;
  s += String(" ") + COMPASS_POINT_NOTATION[point * 2];
#endif
  return s;
}

static String pressureText(const owm_current_t &current)
{
  if (current.pressure <= 0)
  {
    return "--";
  }
  const float hpa = current.pressure;
#if defined(UNITS_PRES_HECTOPASCALS)
  return String(current.pressure) + " " + TXT_UNITS_PRES_HECTOPASCALS;
#elif defined(UNITS_PRES_PASCALS)
  return String(static_cast<int>(lroundf(hectopascals_to_pascals(hpa))))
       + " " + TXT_UNITS_PRES_PASCALS;
#elif defined(UNITS_PRES_MILLIMETERSOFMERCURY)
  return String(static_cast<int>(lroundf(
           hectopascals_to_millimetersofmercury(hpa))))
       + " " + TXT_UNITS_PRES_MILLIMETERSOFMERCURY;
#elif defined(UNITS_PRES_INCHESOFMERCURY)
  return String(hectopascals_to_inchesofmercury(hpa), 2)
       + " " + TXT_UNITS_PRES_INCHESOFMERCURY;
#elif defined(UNITS_PRES_MILLIBARS)
  return String(static_cast<int>(lroundf(hectopascals_to_millibars(hpa))))
       + " " + TXT_UNITS_PRES_MILLIBARS;
#elif defined(UNITS_PRES_ATMOSPHERES)
  return String(hectopascals_to_atmospheres(hpa), 3)
       + " " + TXT_UNITS_PRES_ATMOSPHERES;
#elif defined(UNITS_PRES_GRAMSPERSQUARECENTIMETER)
  return String(static_cast<int>(lroundf(
           hectopascals_to_gramspersquarecentimeter(hpa))))
       + " " + TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER;
#else
  return String(hectopascals_to_poundspersquareinch(hpa), 2)
       + " " + TXT_UNITS_PRES_POUNDSPERSQUAREINCH;
#endif
}

static String visibilityText(const owm_current_t &current)
{
  if (current.visibility < 0)
  {
    return "--";
  }
#if defined(UNITS_DIST_KILOMETERS)
  const float vis = meters_to_kilometers(current.visibility);
  const float far = 10;
  const char *unit = TXT_UNITS_DIST_KILOMETERS;
#else
  const float vis = meters_to_miles(current.visibility);
  const float far = 6;
  const char *unit = TXT_UNITS_DIST_MILES;
#endif
  String s = (vis < 1.95f) ? String(vis, 1)
                           : String(static_cast<int>(lroundf(vis)));
  if (vis >= far)
  {
    s = "> " + s;
  }
  return s + " " + unit;
}

/* The sun's path, the moon under the horizon.
 *
 * By day the arc runs from sunrise to sunset and the sun rides it: solid
 * behind, dotted ahead. At night the arc is tomorrow's, all of it dotted,
 * with the sun half risen at its start and a count of the hours to
 * sunrise. The moon stays under the horizon at all hours: where it really
 * is in the sky is not known here (it keeps its own hours, and they are
 * not computed), so the picture never claims that it is up.
 */
static void drawSky(const owm_current_t &current)
{
  const int cy = 694, rx = 204, ry = 104;
  const time_t now = nowUtc();
  auto onArc = [&](float t, float &x, float &y)
  {
    x = LEFTC - rx * cosf(static_cast<float>(M_PI) * t);
    y = cy - ry * sinf(static_cast<float>(M_PI) * t);
  };
  auto dotted = [&](int from, float sx, float sy, float keep)
  {
    for (int i = from; i < 100; i += 3)
    {
      float x, y;
      onArc(i / 100.0f, x, y);
      if (hypotf(x - sx, y - sy) > keep)
      {
        fillBox(static_cast<int>(x) - 1, static_cast<int>(y) - 1,
                static_cast<int>(x) + 1, static_cast<int>(y) + 1, 0, K);
      }
    }
  };

  fillBox(LEFT0, cy, LEFT1, cy + 1, 0, K); // the horizon

  int64_t rise = current.sunrise, set = current.sunset;
  if (rise > 0 && set > rise)
  {
    const bool night = now < rise || now >= set;
    String top, bottom;
    if (!night)
    {
      const float f = static_cast<float>(now - rise) / (set - rise);
      float sx, sy;
      onArc(f, sx, sy);
      float px0 = 0, py0 = 0;
      for (int i = 0; i <= static_cast<int>(f * 200); ++i)
      {
        float x, y;
        onArc(i / 200.0f, x, y);
        if (i && hypotf(x - sx, y - sy) > 33 && hypotf(px0 - sx, py0 - sy) > 33)
        {
          stroke(px0, py0, x, y, 3, K);
        }
        px0 = x;
        py0 = y;
      }
      dotted(static_cast<int>(f * 100) + 1, sx, sy, 39);
      drawSun(static_cast<int>(sx), static_cast<int>(sy), true);
      top = span(static_cast<long>(set - rise));
      bottom = TXT709_OF_DAYLIGHT;
    }
    else
    {
      if (now >= set)
      { // the next sunrise is tomorrow's
        const time_t next = now + 86400;
        tm tomorrow;
        localtime_r(&next, &tomorrow);
        int64_t r2 = 0, s2 = 0;
        if (calcSunriseSunset(tomorrow.tm_year + 1900, tomorrow.tm_mon + 1,
                              tomorrow.tm_mday, LAT.toDouble(),
                              LON.toDouble(), r2, s2))
        {
          rise = r2;
          set = s2;
        }
        else
        {
          rise += 86400;
          set += 86400;
        }
      }
      dotted(1, LEFTC - rx, cy, 40);
      drawSun(LEFTC - rx, cy, false);
      fillBox(LEFTC - rx - 52, cy, LEFTC - rx + 52, cy + 1, 0, K);
      top = TXT709_SUNRISE_IN;
      bottom = span(static_cast<long>(rise - now));
    }
    drawText(LEFTC, cy - 54, top, F_SANS_22, K, MA, true);
    drawText(LEFTC, cy - 29, bottom, F_SANS_22, K, MA, true);
    drawText(LEFT0, cy + 32, clock(static_cast<time_t>(rise), TIME_FORMAT),
             F_BITTER_32, K, LA);
    drawText(LEFT1, cy + 32, clock(static_cast<time_t>(set), TIME_FORMAT),
             F_BITTER_32, K, RA);
  }

  int lit = 0;
  const int phase16 = calcMoonPhase(static_cast<int64_t>(now), &lit);
  // the same clock as calcMoonPhase, at the finer step the pictures have
  const double age = fmod((static_cast<double>(now) - 947182440.0) / 86400.0,
                          29.530588853);
  const double frac = (age < 0 ? age + 29.530588853 : age) / 29.530588853;
  const int phase = static_cast<int>(lround(frac * MOON709_STEPS))
                    % MOON709_STEPS;
  drawMoon(LEFTC, cy + 50, phase);
  drawText(LEFTC, cy + 92,
           label(getMoonPhaseDesc(phase16)) + TXT709_DOT + String(lit)
           + TXT709_LIT,
           F_SANS_23, K, MA, true);
} // end drawSky

void drawCurrentConditions(const owm_current_t &current,
                           const owm_daily_t &today,
                           const owm_resp_air_pollution_t &air,
                           const pollen_info_t &pollen,
                           float inTemp, float inHumidity)
{
  (void)today;
  sunUp = current.sunrise;
  sunDown = current.sunset;
  const bool day = daylightAt(nowUtc(), current.weather.icon.endsWith("d"));

  drawIcon(conditionIcon(current.weather.id, day, 220), 220, 26, 180);
  const String temp = tempText(current.temp);
  drawText(262, 176, temp,
           fitFont(temp, 584 - 262 - 8, {&F_BITTER_190D, &F_BITTER_150D}),
           K, LA);
  drawText(LEFT0, 424, label(TXT_FEELS_LIKE) + " "
                     + tempText(current.feels_like),
           F_BITTER_52, K, LA);
  drawText(LEFT0, 490,
           TXT709_KIND[kindOf(current.weather.id, current.clouds, day, 1.0f,
                              false)],
           F_SANS_38, K, LA, true);

  drawSky(current);

  Cell c[8];
  for (Cell &cell : c)
  {
    cell.word = nullptr;
    cell.brief = nullptr;
    cell.level = RISK_PLAIN;
    cell.trend = 0;
  }
  c[0].label = TXT_HUMIDITY;
  c[0].value = String(current.humidity) + "%";
  c[1].label = label(TXT_DEWPOINT);
  c[1].value = tempText(current.dew_point);
  c[2].label = TXT_WIND;
  c[2].value = windText(current);
  c[3].label = TXT_PRESSURE;
  c[3].value = pressureText(current);
  c[3].trend = historyPressureTrend();

  c[4].label = label(TXT_UV_INDEX);
  if (std::isnan(current.uvi))
  { // not fetched: no number, and no chip to call it low
    c[4].value = "--";
  }
  else
  {
    const unsigned uvi = static_cast<unsigned>(
                           std::max(lroundf(current.uvi), 0L));
    c[4].value = String(uvi);
    c[4].word = getUVIdesc(uvi);
    c[4].level = (uvi <= 2)  ? RISK_GREEN
               : (uvi <= 5)  ? RISK_YELLOW
               : (uvi <= 7)  ? RISK_AMBER
               : (uvi <= 10) ? RISK_RED
                             : RISK_PURPLE;
  }

  // air quality: AirNow's official figure when there is one, otherwise
  // worked out from Open-Meteo's modelled pollutants
  const bool epa = (air.us_aqi >= 0);
  const bool airKnown = epa || air.valid;
  int aqi = 0, aqiMax = 0;
  if (epa)
  {
    aqi = air.us_aqi;
    aqiMax = UNITED_STATES_AQI_MAX;
  }
  else if (airKnown)
  {
    const owm_components_t &p = air.components;
    aqi = calc_aqi(AQI_SCALE, p.co, p.nh3, p.no, p.no2, p.o3, NULL, p.so2,
                   p.pm10, p.pm2_5);
    aqiMax = aqi_scale_max(AQI_SCALE);
  }
  const bool usScale = epa || (AQI_SCALE == UNITED_STATES_AQI);
  c[5].label = label((epa || aqi_desc_type(AQI_SCALE) == AIR_QUALITY_DESC)
                     ? TXT_AIR_QUALITY : TXT_AIR_POLLUTION);
  if (!airKnown)
  { // neither source answered
    c[5].value = "--";
  }
  else
  {
    c[5].tag = epa ? TXT709_TAG_EPA : TXT709_TAG_MODEL;
    c[5].value = (aqi > aqiMax) ? "> " + String(aqiMax) : String(aqi);
    c[5].word = epa ? united_states_aqi_desc(aqi)
                    : aqi_desc(AQI_SCALE, aqi);
  }
  if (airKnown && usScale)
  {
    static const int BAND[6] = {RISK_GREEN, RISK_YELLOW, RISK_AMBER,
                                RISK_RED, RISK_PURPLE, RISK_MAROON};
    const int band = (aqi <= 50) ? 0 : (aqi <= 100) ? 1 : (aqi <= 150) ? 2
                   : (aqi <= 200) ? 3 : (aqi <= 300) ? 4 : 5;
    c[5].level = BAND[band];
    c[5].brief = UNITED_STATES_AQI_SHORT_TXT[band];
  }

  c[6].label = TXT_POLLEN;
  if (pollen.max_upi < 0)
  {
    c[6].value = "--";
  }
  else
  {
    if (pollen.max_upi > 0)
    { // which kind is driving the index
      const bool all = pollen.tree == pollen.max_upi
                    && pollen.grass == pollen.max_upi
                    && pollen.weed == pollen.max_upi;
      String types;
      if (all)
      {
        types = TXT_POLLEN_ALL;
      }
      else
      {
        if (pollen.tree == pollen.max_upi) {types += TXT_POLLEN_TREE;}
        if (pollen.grass == pollen.max_upi)
        {
          if (types.length()) {types += "+";}
          types += TXT_POLLEN_GRASS;
        }
        if (pollen.weed == pollen.max_upi)
        {
          if (types.length()) {types += "+";}
          types += TXT_POLLEN_WEED;
        }
      }
      c[6].tag = "(" + types + ")";
    }
    c[6].value = String(pollen.max_upi);
    c[6].word = (pollen.max_upi <= 2) ? TXT_UV_LOW
              : (pollen.max_upi == 3) ? TXT_UV_MODERATE
              : (pollen.max_upi == 4) ? TXT_UV_HIGH
                                      : TXT_UV_VERY_HIGH;
    c[6].level = (pollen.max_upi <= 2) ? RISK_GREEN
               : (pollen.max_upi == 3) ? RISK_AMBER
                                       : RISK_RED;
  }

  if (std::isnan(inTemp) && std::isnan(inHumidity))
  { // no sensor, or it did not answer: the cell goes to visibility
    c[7].label = TXT_VISIBILITY;
    c[7].value = visibilityText(current);
  }
  else
  {
    c[7].label = TXT709_INDOOR;
#if defined(UNITS_TEMP_KELVIN)
    const float in = celsius_to_kelvin(inTemp);
#elif defined(UNITS_TEMP_CELSIUS)
    const float in = inTemp;
#else
    const float in = celsius_to_fahrenheit(inTemp);
#endif
    c[7].value = (std::isnan(inTemp) ? String("--")
                  : String(static_cast<int>(lroundf(in))) + "\xB0")
               + TXT709_DOT
               + (std::isnan(inHumidity) ? String("--")
                  : String(static_cast<int>(lroundf(inHumidity))) + "%");
    c[7].trend = historyIndoorTrend();
  }
  for (int i = 0; i < 8; ++i)
  {
    drawCell(i, c[i]);
  }
  dashV(584, 40, 1170, 1, 2, 6, K); // between the columns
} // end drawCurrentConditions

void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft,
                   bool stale)
{
  if (stale)
  { // the weather on the page is old: when it is from, in red, and why
    const String when = String(TXT709_LAST_UPDATED " ") + refreshTimeStr;
    String s = when + TXT709_DOT + statusStr;
    if (statusStr.isEmpty() || textWidth(s, F_SANS_24) > LEFT1 - LEFT0)
    {
      s = when;
    }
    drawText(LEFT0, 1168, s, F_SANS_24, R, LA);
    return;
  }
  if (!statusStr.isEmpty())
  { // something went wrong this wake: say so, in the place of the rest
    String s = statusStr + TXT709_DOT + TXT709_UPDATED " " + refreshTimeStr;
    if (textWidth(s, F_SANS_24) > LEFT1 - LEFT0)
    {
      s = statusStr;
    }
    drawText(LEFT0, 1168, s, F_SANS_24, R, LA);
    return;
  }
  String s = String(TXT709_UPDATED " ") + refreshTimeStr;
#if BATTERY_MONITORING
  const uint32_t percent = calcBatPercent(batVoltage, MIN_BATTERY_VOLTAGE,
                                          MAX_BATTERY_VOLTAGE);
  String bat = String(TXT709_DOT) + TXT709_BATTERY " " + String(percent) + "%";
  if (batDaysLeft >= 0)
  {
    bat += ", " + String(batDaysLeft) + " " TXT709_DAYS_LEFT;
  }
  if (textWidth(s + bat, F_SANS_24) <= LEFT1 - LEFT0)
  {
    s += bat;
  }
#else
  (void)batVoltage; (void)batDaysLeft;
#endif
  if (rssi < -70)
  { // only worth the room when it is poor
    const String wifi = String(TXT709_DOT) + TXT709_WIFI " "
                      + label(getWiFidesc(rssi));
    if (textWidth(s + wifi, F_SANS_24) <= LEFT1 - LEFT0)
    {
      s += wifi;
    }
  }
  const bool low = BATTERY_MONITORING && batVoltage < WARN_BATTERY_VOLTAGE;
  drawText(LEFT0, 1168, s, F_SANS_24, low ? R : K, LA, !low);
} // end drawStatusBar

/* =========================================================================
 * Right: the week
 * ====================================================================== */

static int severity(int id)
{
  if (id == 781 || (id >= 200 && id < 300)) {return 5;}
  if (id == 511 || (id >= 600 && id < 700)) {return 4;}
  if (id >= 500 && id < 511)                {return 3;}
  if (id >= 300 && id < 600)                {return 2;}
  if (id >= 700 && id < 800)                {return 1;}
  return 0;
}

/* The icon of a day follows the most telling weather of its 24 hours. NWS's
 * own belongs to the daytime half, so a dry day followed by a night of rain
 * would wear a sun over "80%"; where the hourly forecast reaches, the wet
 * hours of that date get their say.
 */
static int tellingId(const owm_daily_t &day)
{
  int id = day.weather.id;
  if (day.pop < 0.5f || severity(id) >= 2 || !hours)
  {
    return id;
  }
  const time_t start = static_cast<time_t>(day.dt);
  tm d;
  localtime_r(&start, &d);
  int best = 0;
  for (int i = 0; i < OWM_NUM_HOURLY; ++i)
  {
    const time_t t = static_cast<time_t>(hours[i].dt);
    tm h;
    localtime_r(&t, &h);
    if (h.tm_yday == d.tm_yday && hours[i].pop >= 0.5f
        && severity(hours[i].weather.id) > best)
    {
      best = severity(hours[i].weather.id);
      id = hours[i].weather.id;
    }
  }
  return best ? id : 500; // rain is likely, whatever the daytime icon said
}

static String amountText(const owm_daily_t &day)
{
  const float mm = day.snow + day.rain;
  if (std::isnan(mm) || mm <= 0.0f)
  {
    return "";
  }
#if defined(UNITS_DAILY_PRECIP_MILLIMETERS)
  const float v = roundf(mm);
  return ((v <= 0) ? String("<1") : String(static_cast<int>(v)))
       + " " + TXT_UNITS_PRECIP_MILLIMETERS;
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
  const float v = roundf(millimeters_to_centimeters(mm) * 10) / 10.0f;
  return ((v <= 0) ? String("<0.1") : String(v, 1))
       + " " + TXT_UNITS_PRECIP_CENTIMETERS;
#elif defined(UNITS_DAILY_PRECIP_INCHES)
  const float v = roundf(millimeters_to_inches(mm) * 10) / 10.0f;
  return ((v <= 0) ? String("<0.1") : String(v, 1))
       + " " + TXT_UNITS_PRECIP_INCHES;
#else
  return "";
#endif
}

/* Name, icon, high, low -- and rain only when it is likely (30% and up), so
 * that a dry week is a quiet one.
 */
void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  const int y0 = 84 + alertTop;
  drawText(RIGHT0, 28 + alertTop, TXT709_THIS_WEEK, F_BITTER_40, K, LA);
  const int days = std::min(std::max(FORECAST_DAYS, 5), OWM_NUM_DAILY);
  const float cw = (RIGHT1 - RIGHT0) / static_cast<float>(days);
  for (int i = 0; i < days; ++i)
  {
    const int cx = static_cast<int>(RIGHT0 + cw * (i + 0.5f));
    const char *name = (i == 0) ? TXT709_TODAY
                                : LC_ABDAY[(timeInfo.tm_wday + i) % 7];
    drawText(cx, y0 + 6, name, F_BITTER_40, K, MA);
    if (daily[i].dt <= 0 || std::isnan(daily[i].temp.max))
    { // the forecast did not reach this day
      drawText(cx, y0 + 196, "--", F_BITTER_42D, K, MA, true);
      continue;
    }
    drawIcon(conditionIcon(tellingId(daily[i]), true, 104), 104, cx - 52,
             y0 + 70);
    drawText(cx + 6, y0 + 196, tempText(daily[i].temp.max), F_BITTER_62D, K,
             MA);
    drawText(cx + 4, y0 + 274, tempText(daily[i].temp.min), F_BITTER_42D, K,
             MA, true);
#if DISPLAY_DAILY_PRECIP
    if (daily[i].pop >= 0.295f)
    {
      drawText(cx, y0 + 350,
               String(static_cast<int>(lroundf(daily[i].pop * 100))) + "%",
               F_SANS_32, B, MA);
      const String amount = amountText(daily[i]);
      if (amount.length())
      {
        drawText(cx, y0 + 388, amount, F_SANS_25, B, MA, true);
      }
    }
#endif
  }
} // end drawForecast

/* =========================================================================
 * Right: the hours ahead
 * ====================================================================== */

struct Spell
{
  Kind kind;
  float a, b; // hours from the start of the graph
};

/* The ribbon shows the weather as spells: a run of hours of one kind, as
 * wide as it lasts and named once. A dry spell under three hours joins the
 * longer of its dry neighbours; anything wet is kept, however brief.
 */
static int spellsOf(const owm_hourly_t *hourly, int n, Spell *out)
{
  int count = 0;
  for (int i = 0; i < n; ++i)
  {
    const bool day = daylightAt(hourly[i].dt + 1800,
                                hourly[i].weather.icon.endsWith("d"));
    const Kind k = kindOf(hourly[i].weather.id, hourly[i].clouds, day,
                          hourly[i].pop, true);
    if (count && out[count - 1].kind == k)
    {
      out[count - 1].b = i + 1;
    }
    else
    {
      out[count++] = {k, static_cast<float>(i), static_cast<float>(i + 1)};
    }
  }
  bool again = true;
  while (again && count > 1)
  {
    again = false;
    for (int i = 0; i < count; ++i)
    {
      if (isWet(out[i].kind) || out[i].b - out[i].a >= 3.0f)
      {
        continue;
      }
      const bool left = i > 0 && !isWet(out[i - 1].kind);
      const bool right = i + 1 < count && !isWet(out[i + 1].kind);
      if (!left && !right)
      {
        continue;
      }
      if (!right || (left && out[i - 1].b - out[i - 1].a
                               >= out[i + 1].b - out[i + 1].a))
      {
        out[i - 1].b = out[i].b;
      }
      else
      {
        out[i + 1].a = out[i].a;
      }
      for (int j = i; j + 1 < count; ++j)
      {
        out[j] = out[j + 1];
      }
      --count;
      again = true;
      break;
    }
  }
  int kept = 0;
  for (int i = 0; i < count; ++i)
  {
    if (kept && out[kept - 1].kind == out[i].kind)
    {
      out[kept - 1].b = out[i].b;
    }
    else
    {
      out[kept++] = out[i];
    }
  }
  return kept;
}

struct Ribbon
{
  Tone tone;      // share < 0: paper
  bool solid;     // one ink, tone.top
  uint16_t edge;
  int edgeWidth;  // 1 is drawn as a hairline of every other dot
  uint16_t text;
};
static const Ribbon RIBBON[13] = {
  /* sunny   */ {{Y, 0.90f, W, 1}, false, K, 1, K},
  /* msunny  */ {{Y, 0.50f, W, 1}, false, K, 1, K},
  /* clear   */ {{K, 0.50f, B, 1}, false, K, 1, W},
  /* mclear  */ {{K, 0.50f, B, 0.82f}, false, K, 1, W},
  /* partly  */ {{K, 0.10f, W, 1}, false, K, 1, K},
  /* mcloudy */ {{K, 0.21f, W, 1}, false, K, 1, K},
  /* cloudy  */ {{K, 0.33f, W, 1}, false, K, 1, K},
  /* fog     */ {{K, 0.08f, W, 1}, false, K, 1, K},
  /* showers */ {{B, 0.34f, W, 1}, false, B, 2, K},
  /* rain    */ {{B, 1, B, 1}, true, B, 0, W},
  /* storms  */ {{B, 1, B, 1}, true, B, 0, W},
  /* snow    */ {{W, 1, W, 1}, true, B, 2, B},
  /* ice     */ {{R, 0.30f, W, 1}, false, R, 2, K},
};

struct Lines
{
  int n = 0;
  String text[2];
  const Font709 *face[2] = {nullptr, nullptr};
};

static bool oneLine(Lines &l, const String &s, int room,
                    std::initializer_list<const Font709 *> faces)
{
  for (const Font709 *f : faces)
  {
    if (textWidth(s, *f) + 12 <= room)
    {
      l.n = 1;
      l.text[0] = s;
      l.face[0] = f;
      return true;
    }
  }
  return false;
}

static bool twoLines(Lines &l, const String &top, const String &bottom,
                     int room, const Font709 *ft, const Font709 *fb)
{
  if (std::max(textWidth(top, *ft), textWidth(bottom, *fb)) + 12 <= room)
  {
    l.n = 2;
    l.text[0] = top;
    l.text[1] = bottom;
    l.face[0] = ft;
    l.face[1] = fb;
    return true;
  }
  return false;
}

/* The most a segment has room for: the word and its hours on one line,
 * then on two, then the word alone, then nothing. Only wet spells carry
 * hours.
 */
static Lines wording(const Spell &s, int hoursShown, int room, time_t start)
{
  Lines l;
  const String word = TXT709_KIND[s.kind];
  if (isWet(s.kind))
  {
    const String from = clockShort(start + static_cast<long>(s.a * 3600));
    const String to = clockShort(start + static_cast<long>(s.b * 3600));
    String when;
    if (s.a <= 0)
    {
      when = String(TXT709_UNTIL " ") + to;
    }
    else if (s.b >= hoursShown)
    {
      when = String(TXT709_FROM " ") + from;
    }
    else
    {
      when = from + " \x7F " + to;
    }
    if (oneLine(l, word + "  " + when, room, {&F_SANS_30, &F_SANS_27})
     || twoLines(l, word, when, room, &F_SANS_27, &F_SANS_23))
    {
      return l;
    }
  }
  if (oneLine(l, word, room, {&F_SANS_30, &F_SANS_27}))
  {
    return l;
  }
  const int gap = word.indexOf(' ');
  if (gap > 0)
  {
    const String first = word.substring(0, gap), rest = word.substring(gap + 1);
    if (twoLines(l, first, rest, room, &F_SANS_25, &F_SANS_25)
     || twoLines(l, first, rest, room, &F_SANS_22, &F_SANS_22))
    {
      return l;
    }
  }
  oneLine(l, word, room, {&F_SANS_24, &F_SANS_22});
  return l;
}

static void drawRibbon(int x0, int x1, int y, const Spell *spells, int count,
                       int hoursShown, time_t start)
{
  const int h = 56;
  for (int i = 0; i < count; ++i)
  {
    const Spell &s = spells[i];
    const Ribbon &look = RIBBON[s.kind];
    const int xa = static_cast<int>(x0 + (x1 - x0) * s.a / hoursShown) + 2;
    const int xb = static_cast<int>(x0 + (x1 - x0) * s.b / hoursShown) - 2;
    if (xb - xa < 4)
    {
      continue;
    }
    if (look.solid)
    {
      fillBox(xa, y, xb, y + h, 8, look.tone.top);
    }
    else
    {
      fillTone(xa, y, xb, y + h, 8, look.tone);
    }
    if (look.edgeWidth == 1)
    {
      display.setStipple(true);
      strokeBox(xa, y, xb, y + h, 8, 1, look.edge);
      display.setStipple(false);
    }
    else if (look.edgeWidth > 1)
    {
      strokeBox(xa, y, xb, y + h, 8, look.edgeWidth, look.edge);
    }

    const int bolt = (s.kind == K_TSTORM) ? 30 : 0;
    const Lines l = wording(s, hoursShown, xb - xa - bolt, start);
    int tw = 0;
    for (int k = 0; k < l.n; ++k)
    {
      tw = std::max(tw, textWidth(l.text[k], *l.face[k]));
    }
    const int cx = (xa + xb) / 2, cy = y + h / 2;
    const int gapA = l.n ? cx - tw / 2 - 10 - bolt / 2 : cx;
    const int gapB = l.n ? cx + tw / 2 + 10 + bolt / 2 : cx;

    // texture, kept clear of the words
    if (s.kind == K_FOG)
    {
      display.setStipple(true);
      for (int j = y + 9; j < y + h - 6; j += 7)
      {
        if (gapA - (xa + 8) > 6) {fillBox(xa + 8, j, gapA, j + 1, 0, K);}
        if ((xb - 8) - gapB > 6) {fillBox(gapB, j, xb - 8, j + 1, 0, K);}
      }
      display.setStipple(false);
    }
    else if (s.kind == K_SNOW)
    {
      int row = 0;
      for (int yy = y + 9; yy < y + h - 8; yy += 10, ++row)
      {
        for (int xx = xa + 9 + (row % 2) * 8; xx < xb - 9; xx += 16)
        {
          if (xx < gapA || xx > gapB)
          {
            fillBox(xx, yy, xx + 2, yy + 2, 0, B);
          }
        }
      }
    }
    else if (s.kind == K_CLEAR)
    { // a few stars; where they fall is fixed by where the segment is
      uint32_t seed = static_cast<uint32_t>(xa) * 2654435761u + 12345u;
      for (int gx = xa + 12; gx < xb - 12; gx += 22)
      {
        seed = seed * 1664525u + 1013904223u;
        const int xx = gx + static_cast<int>((seed >> 8) % 13) - 6;
        const int yy = y + 8 + static_cast<int>((seed >> 16) % (h - 18));
        if (xx < gapA || xx > gapB)
        {
          fillBox(xx, yy, xx + 1, yy + 1, 0, W);
        }
      }
    }
    else if (s.kind == K_TSTORM && xb - xa >= 30)
    {
      const float bx = l.n ? gapA + 6 : cx - 9;
      // a bolt, as two triangles and the bar between them
      triangle(bx + 12, cy - 18, bx + 1, cy + 3, bx + 9, cy + 3, Y);
      triangle(bx + 12, cy - 18, bx + 16, cy - 18, bx + 9, cy + 3, Y);
      triangle(bx + 16, cy - 18, bx + 10, cy - 5, bx + 9, cy + 3, Y);
      triangle(bx + 10, cy - 5, bx + 19, cy - 5, bx + 4, cy + 18, Y);
      triangle(bx + 9, cy + 3, bx + 10, cy - 5, bx + 4, cy + 18, Y);
    }

    const int tx = cx + bolt / 2;
    if (l.n == 1)
    {
      drawText(tx, cy, l.text[0], *l.face[0], look.text, MM);
    }
    else if (l.n == 2)
    {
      drawText(tx, cy - 12, l.text[0], *l.face[0], look.text, MM);
      drawText(tx, cy + 13, l.text[1], *l.face[1], look.text, MM);
    }
  }
} // end drawRibbon

void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
  (void)daily; (void)timeInfo;
  const int n = std::min(std::max(HOURLY_GRAPH_MAX, 8), OWM_NUM_HOURLY);
  const int gy = 540 + alertTop;
  const time_t start = static_cast<time_t>(hourly[0].dt);

  char title[32];
  snprintf(title, sizeof(title), TXT709_NEXT_HOURS, n);
  drawText(RIGHT0, gy, title, F_BITTER_40, K, LA);

  // what the lines mean
  const int ly = gy + 28;
  fillBox(1040, ly - 3, 1080, ly + 2, 0, R);
  drawText(1088, ly, TXT709_TEMPERATURE, F_SANS_26, K, LM, true);
  if (GRAPH_DEWPOINT)
  {
    fillBox(1262, ly - 1, 1302, ly + 1, 0, B);
    drawText(1310, ly, TXT709_DEW_POINT, F_SANS_26, K, LM, true);
  }
  fillTone(1446, ly - 12, 1470, ly + 12, 0, T_RAIN_BAR);
  drawText(1478, ly, TXT709_RAIN, F_SANS_26, K, LM, true);

  static Spell spells[OWM_NUM_HOURLY];
  const int count = spellsOf(hourly, n, spells);
  const int top = gy + 64;
  drawRibbon(PLOT0, PLOT1, top, spells, count, n, start);

  const int x0 = PLOT0, x1 = PLOT1, y0 = top + 56 + 22, y1 = 1132;
  // the scale: round numbers that leave the curve room at both ends
  float tMin = 1e9f, tMax = -1e9f, low = 1e9f;
  for (int i = 0; i < n; ++i)
  {
    const float t = toUnitsF(hourly[i].temp);
    tMin = std::min(tMin, t);
    tMax = std::max(tMax, t);
    low = std::min(low, t);
    if (GRAPH_DEWPOINT && !std::isnan(hourly[i].dew_point))
    {
      low = std::min(low, toUnitsF(hourly[i].dew_point));
    }
  }
#if defined(UNITS_TEMP_FAHRENHEIT)
  const int step = 10;
#else
  const int step = 5;
#endif
  const int lo = static_cast<int>(floorf((low - 3) / step)) * step;
  int hi = static_cast<int>(ceilf((tMax + 6) / step)) * step;
  if (hi <= lo)
  {
    hi = lo + step;
  }
  auto X = [&](float h) { return x0 + (x1 - x0) * h / n; };
  auto Yt = [&](float t) { return y1 - (y1 - y0) * (t - lo) / (hi - lo); };

  // rain, as bars behind everything else
  int peakHour = 0;
  float peak = 0;
  for (int i = 0; i < n; ++i)
  {
    const float p = hourly[i].pop * 100.0f;
    if (p > peak)
    {
      peak = p;
      peakHour = i;
    }
    if (p > 2)
    {
      fillTone(static_cast<int>(X(i)) + 2,
               static_cast<int>(y1 - (y1 - y0) * p / 100.0f),
               static_cast<int>(X(i + 1)) - 2, y1, 0, T_RAIN_BAR);
    }
  }
  // the scale's lines and figures
  for (int t = lo; t <= hi; t += step)
  {
    const int y = static_cast<int>(Yt(t));
    dashH(x0, x1, y, 1, 2, 8, K);
    drawText(x0 - 12, y, String(t) + "\xB0", F_SANS_30, K, RM, true);
  }
  fillBox(x0, y1, x1, y1 + 1, 0, K);
  for (int h = 0; h <= n; h += 6)
  {
    const int x = static_cast<int>(X(h));
    fillBox(x, y1, x + 1, y1 + 8, 0, K);
    drawText(x, y1 + 14, clock(start + h * 3600L, HOUR_FORMAT), F_SANS_30, K,
             MA, true);
  }
  // midnights, by the name of the day they begin
  int midnightX[4], midnights = 0;
  for (int i = 0; i < n; ++i)
  {
    const time_t t = start + i * 3600L;
    tm local;
    localtime_r(&t, &local);
    if (local.tm_hour == 0 && i > 0)
    {
      const int x = static_cast<int>(X(i));
      dashV(x, top + 60, y1, 1, 8, 6, K);
      if (x + 10 + textWidth(LC_ABDAY[local.tm_wday], F_BITTER_34) < x1)
      {
        drawText(x + 10, y0 + 8, LC_ABDAY[local.tm_wday], F_BITTER_34, K, LA);
      }
      if (midnights < 4)
      {
        midnightX[midnights++] = x;
      }
    }
  }
  // dew point under temperature, each on a halo of paper so that it reads
  // across the rain
  if (GRAPH_DEWPOINT)
  {
    for (int pass = 0; pass < 2; ++pass)
    {
      for (int i = 1; i < n; ++i)
      {
        if (std::isnan(hourly[i].dew_point)
            || std::isnan(hourly[i - 1].dew_point))
        {
          continue;
        }
        stroke(X(i - 1), Yt(toUnitsF(hourly[i - 1].dew_point)), X(i),
               Yt(toUnitsF(hourly[i].dew_point)), pass ? 3 : 8,
               pass ? B : W);
      }
    }
  }
  for (int pass = 0; pass < 2; ++pass)
  {
    for (int i = 1; i < n; ++i)
    {
      stroke(X(i - 1), Yt(toUnitsF(hourly[i - 1].temp)), X(i),
             Yt(toUnitsF(hourly[i].temp)), pass ? 6 : 12, pass ? R : W);
    }
  }
  // highs and lows: a dot on the curve and the figure beside it
  for (int i = 2; i < n - 1; ++i)
  {
    const float t = toUnitsF(hourly[i].temp);
    const float before = toUnitsF(hourly[i - 1].temp);
    const float after = toUnitsF(hourly[i + 1].temp);
    const bool high = t > before && t >= after;
    const bool low2 = t < before && t <= after;
    if (!high && !low2)
    {
      continue;
    }
    // a turn only counts if it is the furthest in its neighbourhood, so a
    // wobble in a flat stretch does not get a label of its own
    bool furthest = true;
    for (int j = std::max(0, i - 4); j <= std::min(n - 1, i + 4); ++j)
    {
      const float other = toUnitsF(hourly[j].temp);
      if ((high && other > t) || (low2 && other < t))
      {
        furthest = false;
      }
    }
    if (!furthest)
    {
      continue;
    }
    const int x = static_cast<int>(X(i)), y = static_cast<int>(Yt(t));
    disc(x, y, 11, W);
    disc(x, y, 7, R);
    int ly2 = y + (high ? -36 : 38);
    ly2 = std::max(y0 + 20, std::min(y1 - 22, ly2));
    labelOnPaper(x, ly2, String(static_cast<int>(lroundf(t))) + "\xB0",
                 F_BITTER_36, K);
  }
  if (peak > 10)
  {
    int pxl = static_cast<int>(X(peakHour + 0.5f));
    Anchor anchor = MD;
    for (int k = 0; k < midnights; ++k)
    { // keep clear of a midnight line and the day's name beside it
      if (midnightX[k] - pxl > -40 && midnightX[k] - pxl < 120)
      {
        pxl = midnightX[k] - 10;
        anchor = RD;
      }
    }
    const int py = static_cast<int>(y1 - (y1 - y0) * peak / 100.0f) - 8;
    drawText(pxl, std::max(py, y0 + 30),
             String(static_cast<int>(lroundf(peak / 10.0f)) * 10) + "%",
             F_SANS_28, B, anchor);
  }
} // end drawOutlookGraph

/* =========================================================================
 * The panel, and the screens that are not the weather
 * ====================================================================== */

void fillDisplayBackground()
{
  // paper; this layout has no dark version
}

void initDisplay()
{
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    delay(100); // let the panel's supply come up before it is spoken to
  }
  display.init();
  display.setRotation(EPD709_ROTATION);
  display.setFullWindow();
  display.firstPage();
} // end initDisplay

void powerOffDisplay()
{
  display.hibernate();
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    digitalWrite(PIN_EPD_PWR, LOW);
  }
} // end powerOffDisplay

static void centred(int y, const String &s,
                    std::initializer_list<const Font709 *> faces)
{
  drawText(DISP_WIDTH / 2, y, s, fitFont(s, DISP_WIDTH - 120, faces), K, MA);
}

void drawConfigPortalScreen(const String &line1, const String &line2,
                            const String &line3)
{
  drawBitmap2x(DISP_WIDTH / 2 - 196, 110, wifi_196x196, 196, 196, R);
  centred(560, TXT709_SETUP_MODE, {&F_BITTERSB_72});
  const String lines[3] = {line1, line2, line3};
  int y = 700;
  for (const String &line : lines)
  {
    if (line.length())
    {
      centred(y, line, {&F_BITTER_52, &F_BITTER_40, &F_BITTER_32});
    }
    y += 96;
  }
} // end drawConfigPortalScreen

void drawError(const uint8_t *bitmap_196x196, const String &errMsgLn1,
               const String &errMsgLn2)
{
  drawBitmap2x(DISP_WIDTH / 2 - 196, 230, bitmap_196x196, 196, 196, R);
  String first = errMsgLn1, second = errMsgLn2;
  if (second.isEmpty() && textWidth(first, F_BITTER_52) > DISP_WIDTH - 120)
  { // one long line: break it at the space nearest its middle
    const int middle = first.length() / 2;
    int best = -1;
    for (int i = 0; i < static_cast<int>(first.length()); ++i)
    {
      if (first.charAt(i) == ' '
          && (best < 0 || abs(i - middle) < abs(best - middle)))
      {
        best = i;
      }
    }
    if (best > 0)
    {
      second = first.substring(best + 1);
      first = first.substring(0, best);
    }
  }
  centred(690, first, {&F_BITTERSB_72, &F_BITTER_52, &F_BITTER_40});
  if (second.length())
  {
    centred(800, second, {&F_BITTER_52, &F_BITTER_40, &F_BITTER_32});
  }
} // end drawError

/* =========================================================================
 * Test card
 * ====================================================================== */

/* What to look at the first time a panel is lit (build with
 * -e seeed_xiao_ee02_709_testcard). It answers, on real ink, the questions
 * the PC preview cannot:
 *
 *   - is the picture the right way up and the right way round? The corners
 *     are named, and the two halves of the glass (one controller each)
 *     meet down the middle of the panel's short side
 *   - do single dots hold? Every screen the layout uses is here as a patch
 *   - how small can type go? The ladder runs from the largest label to the
 *     smallest, solid and grey
 */
void drawTestCard709()
{
  const int w = DISP_WIDTH, h = DISP_HEIGHT;
  // the corners, to the very last pixel
  fillBox(0, 0, 79, 2, 0, K);           fillBox(0, 0, 2, 79, 0, K);
  fillBox(w - 80, 0, w - 1, 2, 0, K);   fillBox(w - 3, 0, w - 1, 79, 0, K);
  fillBox(0, h - 3, 79, h - 1, 0, K);   fillBox(0, h - 80, 2, h - 1, 0, K);
  fillBox(w - 80, h - 3, w - 1, h - 1, 0, K);
  fillBox(w - 3, h - 80, w - 1, h - 1, 0, K);
  drawText(14, 12, "top left", F_SANS_26, K, LA);
  drawText(w - 14, 12, "top right", F_SANS_26, K, RA);
  drawText(14, h - 12, "bottom left", F_SANS_26, K, LD);
  drawText(w - 14, h - 12, "bottom right", F_SANS_26, K, RD);

  drawText(w / 2, 22, "GDEB0709E01 test card", F_BITTER_52, K, MA);
  // where the two controllers meet
  dashH(200, w - 200, h / 2, 1, 2, 10, K);
  drawText(w - 210, h / 2 - 4, "the two controllers meet along this line",
           F_SANS_22, K, RD, true);

  // the six inks
  static const struct { uint16_t ink; const char *name; } INKS[6] = {
    {K, "black"}, {W, "white"}, {R, "red"}, {Y, "yellow"}, {G, "green"},
    {B, "blue"}};
  for (int i = 0; i < 6; ++i)
  {
    const int x = 60 + i * 250;
    fillBox(x, 110, x + 219, 219, 0, INKS[i].ink);
    strokeBox(x, 110, x + 219, 219, 0, 1, K);
    drawText(x + 110, 226, INKS[i].name, F_SANS_26, K, MA);
  }

  // screens of black on paper: the greys of clouds and ribbon
  static const float GREY[8] = {0.06f, 0.10f, 0.16f, 0.21f, 0.26f, 0.33f,
                                0.50f, 0.75f};
  for (int i = 0; i < 8; ++i)
  {
    const int x = 60 + i * 187;
    const Tone t = {K, GREY[i], W, 1};
    fillTone(x, 280, x + 159, 369, 0, t);
    drawText(x + 80, 376,
             String(static_cast<int>(lroundf(GREY[i] * 100))) + "% black",
             F_SANS_24, K, MA);
  }
  // screens of two inks, and of ink on paper
  static const struct { Tone tone; const char *name; } MIX[8] = {
    {{R, 0.50f, Y, 1}, "amber"},        {{R, 0.60f, B, 1}, "plum"},
    {{R, 0.50f, K, 1}, "maroon"},       {{K, 0.50f, B, 1}, "navy"},
    {{K, 0.50f, B, 0.82f}, "pale navy"}, {{Y, 0.55f, W, 1}, "55% yellow"},
    {{B, 0.40f, W, 1}, "40% blue"},     {{R, 0.19f, Y, 1}, "sun gold"}};
  for (int i = 0; i < 8; ++i)
  {
    const int x = 60 + i * 187;
    fillTone(x, 430, x + 159, 519, 0, MIX[i].tone);
    drawText(x + 80, 526, MIX[i].name, F_SANS_24, K, MA);
  }

  // type, largest label to smallest, solid on the left and grey on the right
  static const Font709 *const SANS[7] = {&F_SANS_38, &F_SANS_32, &F_SANS_30,
                                         &F_SANS_28, &F_SANS_26, &F_SANS_24,
                                         &F_SANS_22};
  static const int PX[7] = {38, 32, 30, 28, 26, 24, 22};
  int y = 640;
  for (int i = 0; i < 7; ++i)
  {
    const String s = "Partly cloudy 7pm \x7F 5am";
    drawText(60, y, String(PX[i]) + " px", *SANS[i], K, LA);
    drawText(170, y, s, *SANS[i], K, LA);
    drawText(620, y, s, *SANS[i], K, LA, true);
    y += PX[i] + 12;
  }
  static const Font709 *const SERIF[6] = {&F_BITTER_40, &F_BITTER_36,
                                          &F_BITTER_32, &F_BITTER_27,
                                          &F_BITTER_24, &F_BITTER_22};
  static const int SPX[6] = {40, 36, 32, 27, 24, 22};
  y = 920;
  for (int i = 0; i < 6; ++i)
  {
    drawText(60, y, String(SPX[i]) + " px", *SERIF[i], K, LA);
    drawText(170, y, "Severe thunderstorm warning 68\xB0", *SERIF[i], K, LA);
    y += SPX[i] + 10;
  }
  // text on ink, as the ribbon and the chips set it
  fillBox(830, 930, 950, 985, 8, B);
  drawText(890, 957, "Rain", F_SANS_30, W, MM);
  fillTone(970, 930, 1090, 985, 8, {K, 0.50f, B, 1});
  drawText(1030, 957, "Clear", F_SANS_30, W, MM);
  drawChip(830, 1004, "Good", RISK_GREEN);
  drawChip(930, 1004, "Moderate", RISK_YELLOW);
  drawChip(830, 1054, "High", RISK_AMBER);
  drawChip(925, 1054, "Very High", RISK_RED);
  drawChip(830, 1104, "Extreme", RISK_PURPLE);
  drawChip(970, 1104, "Hazardous", RISK_MAROON);

  // lines: 1, 2, 3 and 6 dots thick, and the two dashes the layout uses
  for (int i = 0; i < 4; ++i)
  {
    static const int T[4] = {1, 2, 3, 6};
    fillBox(1080, 650 + i * 22, 1300, 650 + i * 22 + T[i] - 1, 0, K);
    fillBox(1080 + i * 22, 760, 1080 + i * 22 + T[i] - 1, 880, 0, K);
  }
  dashH(1180, 1300, 780, 1, 2, 8, K);
  dashH(1180, 1300, 800, 1, 8, 6, K);
  stroke(1180, 870, 1300, 820, 6, R);
  stroke(1180, 880, 1300, 850, 3, B);

  // pictures
  drawIcon(conditionIcon(802, true, 220), 220, 1340, 600);
  drawIcon(conditionIcon(500, true, 104), 104, 1340, 840);
  drawIcon(conditionIcon(800, false, 104), 104, 1460, 840);
  drawSun(1160, 1020, true);
  drawMoon(1280, 1020, MOON709_STEPS / 4);
  drawMoon(1380, 1020, MOON709_STEPS / 2);
  drawMoon(1480, 1020, MOON709_STEPS * 7 / 8);
} // end drawTestCard709

// The rest of renderer.h belongs to the 800x480 layouts. Nothing outside
// their renderer calls it, and this one has its own text routines.

#endif // DISP_7C_709
