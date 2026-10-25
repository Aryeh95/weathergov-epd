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

// All text in Bitter (chosen on the panel, 2026-10-16): the small sizes
// once set in FreeSans are drawn in Bitter at the same pixel size, the
// chips' bold words in Bitter semibold. At 282 ppi Bitter's serifs hold at
// 22 px (on the 800x480 panels' ~125 ppi they do not, hence FreeSans
// there). -D EPD709_SMALL_FREESANS brings FreeSans back for small text.
#if !defined(EPD709_SMALL_FREESANS)
#define F_SANS_22  F_BITTER_22
#define F_SANS_23  F_BITTER_23
#define F_SANS_24  F_BITTER_24
#define F_SANS_25  F_BITTER_25
#define F_SANS_26  F_BITTER_26
#define F_SANS_27  F_BITTER_27
#define F_SANS_28  F_BITTER_28
#define F_SANS_30  F_BITTER_30
#define F_SANS_32  F_BITTER_32
#define F_SANS_38  F_BITTER_38
#define F_SANSB_25 F_BITTERSB_25
#endif
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
/* In dark mode (DARK_MODE, the portal's setting) the paper is the ink and
 * the ink is the paper: px() swaps K and W. Where a colour must stay what it
 * is -- black words on a yellow chip, the white side of the moon, a white
 * cloud -- these two say so; px() maps them to K and W after the swap.
 */
static const uint16_t K_ABS = 0x0101, W_ABS = 0xFEFE;


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

// The footer line's top. 1154, not 1168: the bezel's window sits 0.5 mm
// inside the image (Good Display's E6 design notice) and the panel has a
// little play in the case, which on the first printed bezel (2026-10-15)
// cut the footer's lower half off; it now ends ~20 px clear of the edge.
static const int FOOTER_Y = 1154;

// Orange: half red, half yellow. Scanned on the panel (test card 1,
// 2026-10-25) it measures hue 62 -- the EPA's orange is 60 -- and stays well
// apart from red, where a 35 % mix reads as a mustard.
static const Tone T_ORANGE    = {R, 0.50f, Y, 1.0f};
static const Tone T_ADVISORY  = {Y, 0.55f, W, 1.0f};
// The rain bars in the blue ink itself, a clear mid blue; the dew point
// line keeps its paper halo, so it still reads over them.
static const Tone T_RAIN_BAR  = {B, 1.0f, W, 1.0f};

/* =========================================================================
 * Shapes
 * ====================================================================== */

#if defined(EPD709_HOST)
// The simulator's record of what each dot belongs to (tools/sim709): the
// screen it was drawn with, so its preview can show the colour measured on
// the panel for that screen instead of an average of the inks. Host only;
// the firmware draws exactly as before.
uint32_t g_hostTone = 0;
void hostTag(int x, int y, uint32_t tag);
static uint32_t hostInk(uint16_t c)
{
  if (c == K || c == K_ABS) return 0;
  if (c == W || c == W_ABS) return 1;
  if (c == Y) return 2;
  if (c == R) return 3;
  if (c == B) return 5;
  if (c == G) return 6;
  return 7;
}
// valid bit, top ink, under ink, 2x2 cells, share and cover in thousandths
static uint32_t hostKey(const Tone &t, bool cells2x)
{
  if (DARK_MODE) return 0;               // measured in light mode only
  return (1u << 31) | (hostInk(t.top) << 28) | (hostInk(t.under) << 25)
       | ((cells2x ? 1u : 0u) << 24)
       | (static_cast<uint32_t>(t.share * 1000.0f + 0.5f) << 12)
       | static_cast<uint32_t>(t.cover * 1000.0f + 0.5f);
}
#endif

static inline void px(int x, int y, uint16_t ink)
{
#if defined(EPD709_HOST)
  uint32_t tag = g_hostTone;
#endif
  if (DARK_MODE)
  {
    if (ink == K)      ink = W;
    else if (ink == W) ink = K;
  }
  if (ink == K_ABS)      ink = K;
  else if (ink == W_ABS) ink = W;
#if defined(EPD709_HOST)
  hostTag(x, y, tag);
#endif
  display.drawPixel(x, y, ink);
}

// the curves and bars of the graph, by mode: on the dark ground the panel's
// red and blue are too dark to carry a line, so the temperature goes yellow
// and the dew point white, and the rain bars lighten to half blue on white
static inline uint16_t tempInk() { return DARK_MODE ? Y : R; }
static inline uint16_t dewInk()  { return DARK_MODE ? W_ABS : B; }
static const Tone T_RAIN_BAR_DARK = {B, 0.50f, W_ABS, 1.0f};
static inline const Tone &rainBarTone() { return DARK_MODE ? T_RAIN_BAR_DARK : T_RAIN_BAR; }

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
#if defined(EPD709_HOST)
  g_hostTone = hostKey(t, false);
#endif
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
#if defined(EPD709_HOST)
  g_hostTone = 0;
#endif
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
// fillTone with the screen's dots grouped in 2x2 cells (180 um). Two
// opposed inks in single dots at 282 ppi develop less fully than in cells:
// red over blue is a duller colour dot by dot than in 2x2 cells (test card
// 9 has both).
static void fillTone2x(int x0, int y0, int x1, int y1, int r, const Tone &t)
{
#if defined(EPD709_HOST)
  g_hostTone = hostKey(t, true);
#endif
  for (int y = y0; y <= y1; ++y)
  {
    for (int x = x0; x <= x1; ++x)
    {
      if (inRound(x, y, x0, y0, x1, y1, r))
      {
        px(x, y, toneAt(t, x >> 1, y >> 1));
      }
    }
  }
#if defined(EPD709_HOST)
  g_hostTone = 0;
#endif
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
  const int w = textWidth(s, f) + 18;
  const int h = f.ascent + f.descent + 2;
  fillBox(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, 6, W);
  drawText(cx, cy, s, f, ink, MM);
}

/* =========================================================================
 * Pictures
 * ====================================================================== */

static void drawIcon(const uint8_t *data, int size, int x, int y)
{
  static const uint16_t INK[7] = {0, K_ABS, W_ABS, R, Y, G, B};
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
/* Styles: 0 the gradient above; 1 solid
 * yellow; 2 yellow with a 2 px outline in the ink; 3 yellow with a red rim;
 * 4 yellow with the halo but no red; 5 yellow with yellow rays; 6 solid
 * orange; 7 yellow with an orange outer band; 8 yellow warming to gold at
 * the rim (as the condition icons), ink outline and ink rays; 9-11 shaded as a sphere lit from the upper left -- 9 orange
 * shading with a highlight, 10 the same in a halo, 11 a deeper shade with a
 * red edge on the shadow side only; 12 InkyPi's sun, a flat disc graded
 * from orange at the lower left to yellow at the upper right, and 13 the
 * same in 2x2 cells. SUN_STYLE is the one the layout draws.
 */
static const int SUN_STYLE = 8;   // chosen on the panel, 2026-10-15

static void drawSunStyle(int cx, int cy, bool whole, int style)
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
        uint16_t ink = Y;
        if (style == 0)
        {
          ink = screen(x, y, 0.65f * powf(d / core, 2.2f)) ? R : Y;
        }
        else if (style == 12 || style == 13)
        { // InkyPi's linear gradient: u -1 at the lower left .. +1 upper right
          const float u = ((x - cx) - (y - cy)) / (1.4142f * core);
          const float red = 0.06f + 0.34f * (1.0f - std::max(-1.0f, std::min(1.0f, u))) / 2.0f;
          ink = (style == 13 ? screen(x >> 1, y >> 1, red) : screen(x, y, red)) ? R : Y;
        }
        else if (style >= 9)
        { // a sphere lit from the upper left: how far a point sits on the
          // shadow side, 0 in the highlight .. 1 at the far rim
          const float nx = (x - cx) / core, ny = (y - cy) / core;
          const float nz = sqrtf(std::max(0.0f, 1.0f - nx * nx - ny * ny));
          const float lit = std::max(0.0f, -0.55f * nx - 0.55f * ny + 0.63f * nz);
          const float shade = 1.0f - lit;
          const float deep = (style == 11) ? 0.70f : 0.50f;
          if (lit > 0.93f && screen(x + 1, y + 2, (lit - 0.93f) * 9.0f))
          {
            ink = W_ABS;                                  // the highlight
          }
          else if (style == 11 && d > core - 2.0f && nx + ny > 0.2f)
          {
            ink = R;                                      // edge, shadow side
          }
          else
          {
            ink = screen(x, y, deep * powf(shade, 1.8f)) ? R : Y;
          }
        }
        else if (style == 6 || (style == 7 && d > 18.0f))
        {
          ink = toneAt(T_ORANGE, x, y);
        }
        else if ((style == 2 || style == 8) && d > core - 2.0f)
        {
          ink = K;
        }
        else if (style == 8)
        { // red dots in the yellow, none at the centre, 35 % at the rim
          ink = screen(x, y, 0.35f * powf(d / core, 1.6f)) ? R : Y;
        }
        px(x, y, ink);
      }
      else if (d <= core + 2.0f && (style == 0 || style == 3))
      {
        px(x, y, R);
      }
      else if (d <= glow && (style == 0 || style == 4 || style == 10))
      {
        const float k = powf((glow - d) / (glow - core), 1.6f) * 0.7f;
        if (screen(x, y, k))
        {
          px(x, y, Y);
        }
      }
      else if ((style == 5 || style == 8) && d >= 32.0f && d <= 44.0f)
      { // eight rays: near one of the eight directions
        const float a = atan2f(static_cast<float>(y - cy),
                               static_cast<float>(x - cx));
        const float step = 3.14159265f / 4.0f;
        const float off = a - step * roundf(a / step);
        if (fabsf(d * sinf(off)) <= (style == 5 ? 2.6f : 1.6f))
        {
          px(x, y, style == 5 ? Y : K);
        }
      }
    }
  }
}

static void drawSun(int cx, int cy, bool whole)
{
  drawSunStyle(cx, cy, whole, SUN_STYLE);
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
      px(cx - half + i, cy - half + j, (b & (0x80 >> (i & 7))) ? W_ABS : K_ABS);
    }
  }
  ring(cx - 0.5f, cy - 0.5f, half, 2, DARK_MODE ? W_ABS : K);
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
    ink = W_ABS;
    sign = R;
  }
  else
  {
    // in dark mode as well the notice stays light, a note on the dark page:
    // its paper is true white and its words true black (flipped, the
    // advisory's yellow went dark and its words pale on it)
    Tone t = (a.rank == 1) ? T_ORANGE : T_ADVISORY;
    if (DARK_MODE)
    {
      if (t.under == W) t.under = W_ABS;
      if (t.top == W)   t.top = W_ABS;
      ink = K_ABS;
    }
    fillTone(x0, y0, x1, y1, 10, t);
    strokeBox(x0, y0, x1, y1, 10, 2, DARK_MODE ? K_ABS : K);
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
  uint16_t ink = W_ABS;
  // In the EPA's colours as far as the inks reach, from flatbed scans of
  // test cards 1 and 9 (2026-10-25). Good is the green ink itself, High half
  // red in yellow (T_ORANGE).
  switch (level)
  {
  case RISK_GREEN:  fillBox(x, y, x + w, y + h, 8, G);              break;
  case RISK_YELLOW: fillBox(x, y, x + w, y + h, 8, Y); ink = K_ABS; break;
  case RISK_AMBER:  fillTone(x, y, x + w, y + h, 8, T_ORANGE); ink = K_ABS; break;
  case RISK_RED:    fillBox(x, y, x + w, y + h, 8, R);              break;
  // Extreme: half red over blue in 2x2 cells, a purple (hue 346) -- the
  // nearest the inks come to the EPA's, and clear of red and of maroon.
  case RISK_PURPLE: fillTone2x(x, y, x + w, y + h, 8, {R, 0.50f, B, 1}); break;
  // Hazardous: half red over black, a true maroon -- L* 26, a* +45, b* +18
  // against the EPA's L* 25, a* +48, b* +19 -- with white words.
  case RISK_MAROON: fillTone(x, y, x + w, y + h, 8, {R, 0.50f, K_ABS, 1});
                    ink = W_ABS;                                     break;
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
  int arrowDeg = -1; // wind: where it blows from, in degrees; < 0 no arrow
};

/* The wind's arrow, as the 800x480 layout's: it points the way the air is
 * going (downwind), a notched tail behind. (cx, cy) is its middle.
 */
static void drawWindArrow(float cx, float cy, int fromDeg)
{
  const float b = (fromDeg + 180) * 3.14159265f / 180.0f;
  const float dx = sinf(b), dy = -cosf(b);          // screen y grows down
  const float px = -dy, py = dx;                    // across the arrow
  const float tipX = cx + 15 * dx, tipY = cy + 15 * dy;
  const float bx = cx - 13 * dx, by = cy - 13 * dy;
  const float nX = cx - 5 * dx, nY = cy - 5 * dy;   // the notch in the tail
  triangle(tipX, tipY, bx + 10 * px, by + 10 * py, nX, nY, K);
  triangle(tipX, tipY, nX, nY, bx - 10 * px, by - 10 * py, K);
}

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
  int vx;
  if (c.arrowDeg >= 0)
  {
    drawWindArrow(x + 15, y + 50, c.arrowDeg);
    vx = drawText(x + 36, y + 28, c.value, F_BITTER_36, K, LA);
  }
  else
  {
    vx = drawText(x, y + 28, c.value, F_BITTER_36, K, LA);
  }
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
        if (i && hypotf(x - sx, y - sy) > 46 && hypotf(px0 - sx, py0 - sy) > 46)
        {
          stroke(px0, py0, x, y, 3, K);
        }
        px0 = x;
        py0 = y;
      }
      dotted(static_cast<int>(f * 100) + 1, sx, sy, 47);  // clear of the rays (r 44)
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
      dotted(1, LEFTC - rx, cy, 47);
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

  // every cell the layout can show; PANEL_SLOTS (the portal's choice) says
  // which go where
  enum { CK_HUMIDITY, CK_DEWPOINT, CK_HEAT, CK_WIND, CK_PRESSURE, CK_UVI,
         CK_AIR, CK_POLLEN, CK_VISIBILITY, CK_INDOOR, CK_COUNT };
  static const char *const KEY[CK_COUNT] = {
    "humidity", "dewpoint", "heat_stress", "wind", "pressure", "uvi",
    "air_quality", "pollen", "visibility", "indoor"};
  Cell cells[CK_COUNT];
  for (Cell &cell : cells)
  {
    cell.word = nullptr;
    cell.brief = nullptr;
    cell.level = RISK_PLAIN;
    cell.trend = 0;
  }
  cells[CK_HUMIDITY].label = TXT_HUMIDITY;
  cells[CK_HUMIDITY].value = String(current.humidity) + "%";
  cells[CK_DEWPOINT].label = label(TXT_DEWPOINT);
  cells[CK_DEWPOINT].value = tempText(current.dew_point);
  cells[CK_WIND].label = TXT_WIND;
  cells[CK_WIND].value = windText(current);
#if defined(WIND_INDICATOR_ARROW)
  if (current.wind_speed >= 0.5f)       // calm: no direction worth an arrow
  {
    cells[CK_WIND].arrowDeg = ((current.wind_deg % 360) + 360) % 360;
  }
#endif
  cells[CK_PRESSURE].label = TXT_PRESSURE;
  cells[CK_PRESSURE].value = pressureText(current);
  cells[CK_PRESSURE].trend = historyPressureTrend();

  cells[CK_UVI].label = label(TXT_UV_INDEX);
  if (std::isnan(current.uvi))
  { // not fetched: no number, and no chip to call it low
    cells[CK_UVI].value = "--";
  }
  else
  {
    const unsigned uvi = static_cast<unsigned>(
                           std::max(lroundf(current.uvi), 0L));
    cells[CK_UVI].value = String(uvi);
    cells[CK_UVI].word = getUVIdesc(uvi);
    cells[CK_UVI].level = (uvi <= 2)  ? RISK_GREEN
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
  cells[CK_AIR].label = label((epa || aqi_desc_type(AQI_SCALE) == AIR_QUALITY_DESC)
                     ? TXT_AIR_QUALITY : TXT_AIR_POLLUTION);
  if (!airKnown)
  { // neither source answered
    cells[CK_AIR].value = "--";
  }
  else
  {
    cells[CK_AIR].tag = epa ? TXT709_TAG_EPA : TXT709_TAG_MODEL;
    cells[CK_AIR].value = (aqi > aqiMax) ? "> " + String(aqiMax) : String(aqi);
    cells[CK_AIR].word = epa ? united_states_aqi_desc(aqi)
                    : aqi_desc(AQI_SCALE, aqi);
  }
  if (airKnown && usScale)
  {
    static const int BAND[6] = {RISK_GREEN, RISK_YELLOW, RISK_AMBER,
                                RISK_RED, RISK_PURPLE, RISK_MAROON};
    const int band = (aqi <= 50) ? 0 : (aqi <= 100) ? 1 : (aqi <= 150) ? 2
                   : (aqi <= 200) ? 3 : (aqi <= 300) ? 4 : 5;
    cells[CK_AIR].level = BAND[band];
    cells[CK_AIR].brief = UNITED_STATES_AQI_SHORT_TXT[band];
  }

  cells[CK_POLLEN].label = TXT_POLLEN;
  if (pollen.max_upi < 0)
  {
    cells[CK_POLLEN].value = "--";
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
      cells[CK_POLLEN].tag = "(" + types + ")";
    }
    cells[CK_POLLEN].value = String(pollen.max_upi);
    cells[CK_POLLEN].word = (pollen.max_upi <= 2) ? TXT_UV_LOW
              : (pollen.max_upi == 3) ? TXT_UV_MODERATE
              : (pollen.max_upi == 4) ? TXT_UV_HIGH
                                      : TXT_UV_VERY_HIGH;
    cells[CK_POLLEN].level = (pollen.max_upi <= 2) ? RISK_GREEN
               : (pollen.max_upi == 3) ? RISK_AMBER
                                       : RISK_RED;
  }

  cells[CK_VISIBILITY].label = TXT_VISIBILITY;
  cells[CK_VISIBILITY].value = visibilityText(current);

  // heat stress: IMS's own index and level, else computed from the
  // temperature and humidity (as the 800x480 layouts do)
  {
    float index = current.heat_stress;
    int level = current.heat_stress_level;
    if (!(index > 0.f && level >= 0))
    {
      index = heatStressIndex(kelvin_to_celsius(current.temp), current.humidity);
      level = heatStressLevel(index);
    }
    level = std::min(level, 5);
    cells[CK_HEAT].label = label(TXT_HEAT_STRESS);
    if (level < 0)
    {
      cells[CK_HEAT].value = "--";
    }
    else
    {
      static const int CHIP[6] = {RISK_PLAIN, RISK_GREEN, RISK_YELLOW,
                                  RISK_AMBER, RISK_RED, RISK_PURPLE};
      cells[CK_HEAT].value = String(static_cast<int>(std::floor(index + 0.4999f)));
      cells[CK_HEAT].word = TXT_HEAT_STRESS_LEVEL[level];
      cells[CK_HEAT].level = CHIP[level];
    }
  }

  if (std::isnan(inTemp) && std::isnan(inHumidity))
  { // no sensor, or it did not answer: the indoor cell shows visibility
    cells[CK_INDOOR] = cells[CK_VISIBILITY];
  }
  else
  {
    cells[CK_INDOOR].label = TXT709_INDOOR;
#if defined(UNITS_TEMP_KELVIN)
    const float in = celsius_to_kelvin(inTemp);
#elif defined(UNITS_TEMP_CELSIUS)
    const float in = inTemp;
#else
    const float in = celsius_to_fahrenheit(inTemp);
#endif
    cells[CK_INDOOR].value = (std::isnan(inTemp) ? String("--")
                  : String(static_cast<int>(lroundf(in))) + "\xB0")
               + TXT709_DOT
               + (std::isnan(inHumidity) ? String("--")
                  : String(static_cast<int>(lroundf(inHumidity))) + "%");
    cells[CK_INDOOR].trend = historyIndoorTrend();
  }
  for (int i = 0; i < 8; ++i)
  {
    for (int k = 0; k < CK_COUNT; ++k)
    {
      if (PANEL_SLOTS[i] == KEY[k])
      {
        drawCell(i, cells[k]);
        break;
      }
    }
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
    drawText(LEFT0, FOOTER_Y, s, F_SANS_24, R, LA);
    return;
  }
  if (!statusStr.isEmpty())
  { // something went wrong this wake: say so, in the place of the rest
    String s = statusStr + TXT709_DOT + TXT709_UPDATED " " + refreshTimeStr;
    if (textWidth(s, F_SANS_24) > LEFT1 - LEFT0)
    {
      s = statusStr;
    }
    drawText(LEFT0, FOOTER_Y, s, F_SANS_24, R, LA);
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
  drawText(LEFT0, FOOTER_Y, s, F_SANS_24, low ? R : K, LA, !low);
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
/* The week's highs and lows as one band (planning round 7, option B):
 * a red line through the highs, a blue one through the lows, the space
 * between them shaded from yellow (warm) to blue (cool) on one scale for
 * the whole week, each figure beside its dot. Names, icons and rain stay
 * in their columns.
 */
static const bool WEEK_BAND = true;
// 0: yellow screened over blue (your mockup; olive on this panel), 1: pale
// yellow at the warm edge through white to pale blue at the cool edge
static const int WEEK_BAND_FILL = 1;
// smooth curves through the days, no dots (false: straight segments, dots)
static const bool WEEK_BAND_SMOOTH = true;

/* A smooth curve through values at 0, 1, .. n-1 (cubic Hermite). Where
 * the values turn -- a high or a low -- the curve is flat there, so it does
 * not run past the figure that labels it. Elsewhere the slope is the mean
 * of its two sides, held to three times the gentler one: forecasts come in
 * whole degrees (64, 64, 65 ..), and flattening at every repeat, as strict
 * monotone curves do, terraced a steady warm-up into steps. Across a
 * plateau the curve may stray a few hundredths of a degree.
 */
static float smoothAt(const float *v, int n, float f)
{
  const int i = std::max(0, std::min(static_cast<int>(f), n - 2));
  const float u = f - i;
  auto slope = [&](int k) {
    if (k <= 0) return v[1] - v[0];
    if (k >= n - 1) return v[n - 1] - v[n - 2];
    const float a = v[k] - v[k - 1], b = v[k + 1] - v[k];
    if (a * b < 0 || (a == 0 && b == 0)) return 0.0f;   // a turn, or level
    float m = (a + b) / 2;
    if (a != 0 && b != 0)
    {
      const float lim = 3 * std::min(std::fabs(a), std::fabs(b));
      m = std::max(-lim, std::min(lim, m));
    }
    return m;
  };
  const float m0 = slope(i), m1 = slope(i + 1);
  const float h00 = (1 + 2 * u) * (1 - u) * (1 - u), h10 = u * (1 - u) * (1 - u);
  const float h01 = u * u * (3 - 2 * u), h11 = u * u * (u - 1);
  return h00 * v[i] + h10 * m0 + h01 * v[i + 1] + h11 * m1;
}

static void drawWeekBand(const owm_daily_t *daily, int days, float cw, int y0)
{
  int n = 0;                               // days with a forecast, in a run
  while (n < days && daily[n].dt > 0 && !std::isnan(daily[n].temp.max)
         && !std::isnan(daily[n].temp.min))
  {
    ++n;
  }
  if (n < 2)
  {
    return;
  }
  float hi = -1e9f, lo = 1e9f;
  for (int i = 0; i < n; ++i)
  {
    hi = std::max(hi, toUnitsF(daily[i].temp.max));
    lo = std::min(lo, toUnitsF(daily[i].temp.min));
  }
  if (hi - lo < 1)
  {
    hi = lo + 1;
  }
  const int yT = y0 + 222, yB = y0 + 302;   // the scale: hi at yT, lo at yB
  auto Yof = [&](float t) { return yB - (yB - yT) * (t - lo) / (hi - lo); };
  auto X = [&](int i) { return RIGHT0 + cw * (i + 0.5f); };
  float vh[OWM_NUM_DAILY], vl[OWM_NUM_DAILY];
  for (int i = 0; i < n; ++i)
  {
    vh[i] = toUnitsF(daily[i].temp.max);
    vl[i] = toUnitsF(daily[i].temp.min);
  }
  auto hiAt = [&](float f) {
    if (WEEK_BAND_SMOOTH) return smoothAt(vh, n, f);
    const int i = std::min(static_cast<int>(f), n - 2);
    return vh[i] + (vh[i + 1] - vh[i]) * (f - i);
  };
  auto loAt = [&](float f) {
    if (WEEK_BAND_SMOOTH) return smoothAt(vl, n, f);
    const int i = std::min(static_cast<int>(f), n - 2);
    return vl[i] + (vl[i + 1] - vl[i]) * (f - i);
  };
  // the fill, column by column, yellow to blue by temperature
  for (int x = static_cast<int>(X(0)); x <= static_cast<int>(X(n - 1)); ++x)
  {
    const float f = (x - X(0)) / cw;
    const float th = hiAt(f), tl = loAt(f);
    for (int y = static_cast<int>(Yof(th)); y <= static_cast<int>(Yof(tl)); ++y)
    {
      const float share = std::max(0.0f, std::min(1.0f, (yB - y) / float(yB - yT)));
      if (WEEK_BAND_FILL == 0)
      {
        px(x, y, toneAt({Y, share, B, 1}, x, y));
      }
      else if (share >= 0.5f)
      {
        px(x, y, toneAt({Y, (share - 0.5f) * 1.4f, W, 1}, x, y));
      }
      else
      { // on the dark ground blue dots vanish: white ones read as a cool grey
        px(x, y, toneAt({DARK_MODE ? W_ABS : B, (0.5f - share) * 1.4f, W, 1}, x, y));
      }
    }
  }
  // the lines on a halo of paper, then the dots and the figures; the lows
  // white on the dark ground, as the graph's dew point is
  const uint16_t lowInk = DARK_MODE ? W_ABS : B;
  const float steps = WEEK_BAND_SMOOTH ? 12.0f : 1.0f;   // pieces per day
  for (int pass = 0; pass < 2; ++pass)
  {
    for (int k = 1; k <= static_cast<int>((n - 1) * steps); ++k)
    {
      const float f0 = (k - 1) / steps, f1 = k / steps;
      const float x0 = X(0) + f0 * cw, x1 = X(0) + f1 * cw;
      stroke(x0, Yof(hiAt(f0)), x1, Yof(hiAt(f1)), pass ? 5 : 11, pass ? tempInk() : W);
      stroke(x0, Yof(loAt(f0)), x1, Yof(loAt(f1)), pass ? 5 : 11, pass ? lowInk : W);
    }
  }
  for (int i = 0; i < n; ++i)
  {
    const float yh = Yof(toUnitsF(daily[i].temp.max));
    const float yl = Yof(toUnitsF(daily[i].temp.min));
    if (!WEEK_BAND_SMOOTH)
    {
      disc(X(i), yh, 10, W); disc(X(i), yh, 7, tempInk());
      disc(X(i), yl, 10, W); disc(X(i), yl, 7, lowInk);
    }
    drawText(static_cast<int>(X(i)) + 4, static_cast<int>(yh) - 14,
             tempText(daily[i].temp.max), F_BITTER_42D, tempInk(), MD);
    drawText(static_cast<int>(X(i)) + 4, static_cast<int>(yl) + 14,
             tempText(daily[i].temp.min), F_BITTER_42D, lowInk, MA);
  }
}

// does any day of the week show its rain (30 % and up) under its low?
static bool weekRainRow(const owm_daily_t *daily)
{
#if DISPLAY_DAILY_PRECIP
  const int days = std::min(std::max(FORECAST_DAYS, 5), OWM_NUM_DAILY);
  for (int i = 0; i < days; ++i)
  {
    if (daily[i].dt > 0 && !std::isnan(daily[i].temp.max)
        && daily[i].pop >= 0.295f)
    {
      return true;
    }
  }
#else
  (void)daily;
#endif
  return false;
}

void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  const int y0 = 84 + alertTop;
  drawText(RIGHT0, 28 + alertTop, TXT709_THIS_WEEK, F_BITTER_40, K, LA);
  const int days = std::min(std::max(FORECAST_DAYS, 5), OWM_NUM_DAILY);
  const float cw = (RIGHT1 - RIGHT0) / static_cast<float>(days);
  if (WEEK_BAND)
  {
    drawWeekBand(daily, days, cw, y0);
  }
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
             WEEK_BAND ? y0 + 58 : y0 + 70);
    if (!WEEK_BAND)
    {
      drawText(cx + 6, y0 + 196, tempText(daily[i].temp.max), F_BITTER_62D, K,
               MA);
      drawText(cx + 4, y0 + 274, tempText(daily[i].temp.min), F_BITTER_42D, K,
               MA, true);
    }
#if DISPLAY_DAILY_PRECIP
    const int py = WEEK_BAND ? y0 + 366 : y0 + 350;   // just under the lowest low
    if (daily[i].pop >= 0.295f)
    {
      drawText(cx, py,
               String(static_cast<int>(lroundf(daily[i].pop * 100))) + "%",
               F_SANS_32, dewInk(), MA);
      const String amount = amountText(daily[i]);
      if (amount.length())
      {
        drawText(cx, py + (WEEK_BAND ? 34 : 38), amount, F_SANS_25, dewInk(), MA);
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
  auto coalesce = [&]() {
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
    count = kept;
  };
  coalesce();
  // Then the wet spells: a dry gap under 3 h between two of them goes to
  // the longer, and a wet spell under 2 h beside a longer wet one joins it
  // ("Showers 12am - 8am", not showers, a blank, and a sliver).
  auto len = [&](int i) { return out[i].b - out[i].a; };
  again = true;
  while (again && count > 1)
  {
    again = false;
    for (int i = 0; i < count; ++i)
    {
      const bool wetL = i > 0 && isWet(out[i - 1].kind);
      const bool wetR = i + 1 < count && isWet(out[i + 1].kind);
      int into = -1;
      if (!isWet(out[i].kind) && len(i) < 3.0f && wetL && wetR)
      {
        into = len(i - 1) >= len(i + 1) ? i - 1 : i + 1;
      }
      else if (isWet(out[i].kind) && len(i) < 2.0f && (wetL || wetR))
      {
        into = (wetL && (!wetR || len(i - 1) >= len(i + 1))) ? i - 1 : i + 1;
        if (len(into) <= len(i))
        {
          into = -1;
        }
      }
      if (into >= 0)
      {
        out[i].kind = out[into].kind;
        coalesce();
        again = true;
        break;
      }
    }
  }
  return count;
}

struct Ribbon
{
  Tone tone;      // share < 0: paper
  bool solid;     // one ink, tone.top
  uint16_t edge;
  int edgeWidth;  // 1 is drawn as a hairline of every other dot
  uint16_t text;
};
// Spells: the cloud steps are light grey screens (10 / 21 / 33 %, fog 16 %)
// that read as tints rather than speckle; the sunny spells solid and pale
// yellow (55 %); showers and rain the blue ink itself, their words telling
// them apart; storms dark, with yellow words to echo the lightning (see
// g_stormStyle); clear nights navy and slate, with white words.
static const Ribbon RIBBON[13] = {
  /* sunny   */ {{Y, 1.00f, W_ABS, 1}, false, K, 1, K_ABS},
  /* msunny  */ {{Y, 0.55f, W_ABS, 1}, false, K, 1, K_ABS},
  /* clear   */ {{K_ABS, 0.50f, B, 1}, false, K, 1, W_ABS},
  /* mclear  */ {{K_ABS, 0.30f, B, 1}, false, K, 1, W_ABS},
  /* partly  */ {{K, 0.10f, W, 1}, false, K, 1, K},
  /* mcloudy */ {{K, 0.21f, W, 1}, false, K, 1, K},
  /* cloudy  */ {{K, 0.33f, W, 1}, false, K, 1, K},
  /* fog     */ {{K, 0.16f, W, 1}, false, K, 1, K},
  /* showers */ {{B, 1, B, 1}, true, B, 0, W_ABS},
  /* rain    */ {{B, 1, B, 1}, true, B, 0, W_ABS},
  /* storms  */ {{K_ABS, 0.50f, B, 1}, false, K, 1, Y},
  /* snow    */ {{W_ABS, 1, W_ABS, 1}, true, B, 2, B},
  /* ice     */ {{R, 0.45f, W, 1}, false, R, 2, K},
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

/* Small weather glyphs, about 40 px, for the ribbon where its words do not
 * fit.
 */
namespace tl
{
// ---- small glyphs, about 40 px, drawn by shape tests ----------------------
static bool inCloud(float x, float y, float cx, float cy, float s)
{
  auto in = [&](float ox, float oy, float r) {
    const float dx = x - (cx + ox * s), dy = y - (cy + oy * s);
    return dx * dx + dy * dy <= r * r * s * s;
  };
  const bool base = (y <= cy + 0.42f * s) && (y >= cy - 0.05f * s)
                    && (x >= cx - 0.78f * s) && (x <= cx + 0.78f * s);
  return base || in(-0.45f, 0.12f, 0.32f) || in(0.0f, -0.12f, 0.46f)
         || in(0.46f, 0.08f, 0.34f);
}

static void cloud(float cx, float cy, float s)
{
  const int r = static_cast<int>(s) + 4;
  for (int y = static_cast<int>(cy) - r; y <= cy + r; ++y)
  {
    for (int x = static_cast<int>(cx) - r; x <= cx + r; ++x)
    {
      if (inCloud(x, y, cx, cy, s))
      {
        px(x, y, W_ABS);
      }
      else if (inCloud(x, y, cx, cy, s + 2.5f) || inCloud(x - 2, y, cx, cy, s)
               || inCloud(x + 2, y, cx, cy, s) || inCloud(x, y - 2, cx, cy, s)
               || inCloud(x, y + 2, cx, cy, s))
      {
        px(x, y, K);
      }
    }
  }
}

static void sun(float cx, float cy, float r)
{
  for (int k = 0; k < 8; ++k)
  {
    const float a = k * 3.14159265f / 4;
    stroke(cx + cosf(a) * (r + 3), cy + sinf(a) * (r + 3),
           cx + cosf(a) * (r + 7), cy + sinf(a) * (r + 7), 2, K);
  }
  disc(cx, cy, r, K);
  disc(cx, cy, r - 2, Y);
}

static void moon(float cx, float cy, float r, uint16_t ink)
{
  for (int y = static_cast<int>(cy - r) - 1; y <= cy + r + 1; ++y)
  {
    for (int x = static_cast<int>(cx - r) - 1; x <= cx + r + 1; ++x)
    {
      const float a = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      const float b = (x - cx - r * 0.55f) * (x - cx - r * 0.55f)
                    + (y - cy + r * 0.35f) * (y - cy + r * 0.35f);
      if (a <= r * r && b > r * r * 0.72f)
      {
        px(x, y, ink);
      }
    }
  }
}

static void drop(float cx, float cy, uint16_t ink)
{
  triangle(cx, cy - 7, cx - 4, cy + 1, cx + 4, cy + 1, ink);
  disc(cx, cy + 2, 4, ink);
}

static void bolt(float bx, float cy, uint16_t outline = R, uint16_t fill = Y)
{ // as drawRibbon's, with an outline so it reads on paper
  for (int pass = 0; pass < 2; ++pass)
  {
    const float g = pass ? 0 : 1.6f;
    const uint16_t ink = pass ? fill : outline;
    triangle(bx + 12, cy - 18 - g, bx + 1 - g, cy + 3 + g, bx + 9 + g, cy + 3 + g, ink);
    triangle(bx + 12, cy - 18 - g, bx + 16 + g, cy - 18 - g, bx + 9, cy + 3, ink);
    triangle(bx + 16 + g, cy - 18 - g, bx + 10, cy - 5, bx + 9, cy + 3, ink);
    triangle(bx + 10 - g, cy - 5 - g, bx + 19 + g, cy - 5 - g, bx + 4 - g, cy + 18 + g, ink);
    triangle(bx + 9, cy + 3, bx + 10, cy - 5, bx + 4 - g, cy + 18 + g, ink);
  }
}

static void flake(float cx, float cy, uint16_t ink)
{
  for (int k = 0; k < 3; ++k)
  {
    const float a = k * 3.14159265f / 3 + 0.52f;
    stroke(cx - cosf(a) * 7, cy - sinf(a) * 7, cx + cosf(a) * 7, cy + sinf(a) * 7, 2, ink);
  }
}

// a glyph for a kind, centred on (cx, cy); `onBlue` when the ground is the
// blue ink, so the drops go white
static void glyph(Kind k, float cx, float cy, bool onBlue = false)
{
  const uint16_t dropInk = onBlue ? W_ABS : B;
  switch (k)
  {
  case K_SUNNY:   sun(cx, cy, 9); break;
  case K_MSUNNY:  sun(cx + 7, cy - 6, 7); cloud(cx - 3, cy + 4, 13); break;
  case K_CLEAR:   moon(cx, cy, 11, W_ABS); break;
  case K_MCLEAR:  moon(cx + 7, cy - 7, 8, W_ABS); cloud(cx - 3, cy + 4, 13); break;
  case K_PARTLY:  sun(cx + 7, cy - 6, 7); cloud(cx - 3, cy + 5, 15); break;
  case K_MCLOUDY: cloud(cx + 6, cy - 5, 11); cloud(cx - 3, cy + 4, 15); break;
  case K_CLOUDY:  cloud(cx, cy, 18); break;
  case K_FOG:
    for (int j = -1; j <= 1; ++j)
    {
      fillBox(static_cast<int>(cx) - 14 + (j & 1) * 4, static_cast<int>(cy) + j * 8 - 1,
              static_cast<int>(cx) + 14 - (j & 1) * 4, static_cast<int>(cy) + j * 8 + 1, 1, K);
    }
    break;
  case K_SHOWERS:
    cloud(cx, cy - 6, 15);
    drop(cx - 6, cy + 14, dropInk); drop(cx + 7, cy + 14, dropInk);
    break;
  case K_RAIN:
    cloud(cx, cy - 6, 15);
    drop(cx - 10, cy + 14, dropInk); drop(cx, cy + 16, dropInk); drop(cx + 10, cy + 14, dropInk);
    break;
  case K_TSTORM:  bolt(cx - 10, cy); break;
  case K_SNOW:    cloud(cx, cy - 6, 15); flake(cx - 6, cy + 15, dropInk); flake(cx + 8, cy + 15, dropInk); break;
  case K_ICE:     cloud(cx, cy - 6, 15); drop(cx - 6, cy + 14, dropInk); flake(cx + 8, cy + 15, dropInk); break;
  }
}

} // namespace tl

/* A 3D look for the timeline's chips:
 * 0 flat (the layout's), 1 bevel, 2 drop shadow, 3 gradient, 4 gloss,
 * 5 gradient and shadow. Light is white dots, shade black dots, so each is
 * a screen laid over the chip's fill before its words.
 */
static int g_chip3d = 3;     // the layout's: the gradient (chosen 2026-10-16)

/* How the cloud and fog chips are filled:
 * 0 white with a grey band, 1 grey 10/21/33 % (fog 16), 2 lighter grey
 * 6/13/21 % (fog 10), 3 grey with a paper patch behind the words, 4 grey
 * in 2x2 cells, 5 dark grey 45/55/65 % (fog 40) with white words.
 */
static int g_cloudStyle = 1;   // the layout's: grey 10/21/33 %, black words (2026-10-16)

/* How the storm chip is drawn: 0 white,
 * red edge, outlined bolt; 1 grey (as cloudy) with a storm cloud, black
 * words; 2 solid blue (as rain) with a yellow bolt, white words; 3 dark grey
 * with a yellow bolt, white words; 4 yellow with a black bolt, black words;
 * 5 white with a storm cloud like the forecast icons, black words.
 */
static int g_stormStyle = 3;   // the layout's: dark grey, yellow bolt, white words (2026-10-16)

static float cloudShare(Kind k, int style)
{
  static const float S[6][4] = {   // partly, mostly cloudy, cloudy, fog
    {0.10f, 0.21f, 0.33f, 0.16f}, {0.10f, 0.21f, 0.33f, 0.16f},
    {0.06f, 0.13f, 0.21f, 0.10f}, {0.10f, 0.21f, 0.33f, 0.16f},
    {0.10f, 0.21f, 0.33f, 0.16f}, {0.45f, 0.55f, 0.65f, 0.40f}};
  const int i = (k == K_PARTLY) ? 0 : (k == K_MCLOUDY) ? 1 : (k == K_CLOUDY) ? 2 : 3;
  return S[style][i];
}

static void chipShadow(int xa, int y, int xb, int yb)
{
  for (int yy = y + 4; yy <= yb + 5; ++yy)
  {
    for (int xx = xa + 4; xx <= xb + 5; ++xx)
    {
      if (inRound(xx, yy, xa + 4, y + 4, xb + 5, yb + 5, 8)
          && !inRound(xx, yy, xa, y, xb, yb, 8) && screen(xx, yy, 0.45f))
      {
        px(xx, yy, K);
      }
    }
  }
}

static void chipLight(int xa, int y, int xb, int yb, int style, bool lightWords = false)
{
  const int h = yb - y;
  for (int yy = y; yy <= yb; ++yy)
  {
    const float t = (yy - y) / static_cast<float>(h);   // 0 top .. 1 bottom
    for (int xx = xa; xx <= xb; ++xx)
    {
      if (!inRound(xx, yy, xa, y, xb, yb, 8))
      {
        continue;
      }
      const bool inner = inRound(xx, yy, xa + 3, y + 3, xb - 3, yb - 3, 6);
      if (style == 1)
      { // a raised edge: lit above, shaded below
        if (!inner && screen(xx, yy, 0.75f))
        {
          px(xx, yy, (yy < y + h / 2) ? W_ABS : K);
        }
      }
      else if (style == 3 || style == 5)
      { // lighter at the top, darker at the bottom
        // under white words the light stays in a band at the top and the
        // shade near the bottom: white dots behind white letters wash
        // them out (2026-10-16)
        const float lite = lightWords ? std::max(0.0f, 0.45f * (1 - t / 0.2f))
                                      : std::max(0.0f, 0.45f * (1 - t * 2));
        const float dark = lightWords ? std::max(0.0f, 0.35f * (t - 0.65f) / 0.35f)
                                      : std::max(0.0f, 0.35f * (t * 2 - 1));
        if (lite > 0 && screen(xx + 2, yy + 3, lite))
        {
          px(xx, yy, W_ABS);
        }
        else if (dark > 0 && screen(xx + 5, yy + 1, dark))
        {
          px(xx, yy, K);
        }
      }
      else if (style == 4 && inner && t > 0.08f && t < 0.45f)
      { // a soft highlight across the upper part
        const float g = 0.5f * sinf((t - 0.08f) / 0.37f * 3.14159265f);
        if (screen(xx + 1, yy + 2, g))
        {
          px(xx, yy, W_ABS);
        }
      }
    }
  }
}

// the storm's mark, centred on (cx, cy), in the chip's style
static void stormMark(float cx, float cy)
{
  switch (g_stormStyle)
  {
  case 1: case 5:                       // a storm cloud: grey cloud, bolt below
    tl::cloud(cx, cy - 8, 13);
    tl::bolt(cx - 10, cy + 8, K, Y);
    break;
  case 2: case 3: tl::bolt(cx - 10, cy, Y, Y); break;   // plain yellow
  case 4: tl::bolt(cx - 10, cy, K, K); break;           // black on yellow
  default: tl::bolt(cx - 10, cy); break;                // yellow, red edge
  }
}

static void drawRibbon(int x0, int x1, int y, const Spell *spells, int count,
                       int hoursShown, time_t start)
{
  const int h = 56;
  // edges in pixels; a segment narrower than a glyph borrows from its wider
  // neighbour, so a lone wet hour still shows what falls (only the drawing
  // moves, by a fraction of an hour)
  int edge[OWM_NUM_HOURLY + 1];
  for (int i = 0; i <= count; ++i)
  {
    edge[i] = static_cast<int>(x0 + (x1 - x0)
                               * (i < count ? spells[i].a : spells[count - 1].b)
                               / hoursShown);
  }
  const int minW = 52;
  for (int i = 0; i < count; ++i)
  {
    const int need = minW - (edge[i + 1] - edge[i]);
    if (need <= 0)
    {
      continue;
    }
    const int roomL = i > 0 ? (edge[i] - edge[i - 1]) - minW : -1;
    const int roomR = i + 1 < count ? (edge[i + 2] - edge[i + 1]) - minW : -1;
    if (roomR >= roomL && roomR >= need)
    {
      edge[i + 1] += need;
    }
    else if (roomL >= need)
    {
      edge[i] -= need;
    }
  }
  for (int i = 0; i < count; ++i)
  {
    const Spell &s = spells[i];
    const Ribbon &look = RIBBON[s.kind];
    const int xa = edge[i] + 2;
    const int xb = edge[i + 1] - 2;
    if (xb - xa < 4)
    {
      continue;
    }
    // the grey spells (style 0 sets them on paper, their grey as a band
    // along the bottom)
    const bool cloudy = (s.kind == K_PARTLY || s.kind == K_MCLOUDY
                         || s.kind == K_CLOUDY || s.kind == K_FOG);
    const bool plain = cloudy && g_cloudStyle == 0;
    if (g_chip3d == 2 || g_chip3d == 5)
    {
      chipShadow(xa, y, xb, y + h);
    }
    if (s.kind == K_TSTORM)
    { // style 0: on paper with a red edge, the bolt on white
      switch (g_stormStyle)
      {
      case 1: fillTone(xa, y, xb, y + h, 8, {K, 0.33f, W, 1}); break;
      case 2: fillBox(xa, y, xb, y + h, 8, B); break;
      case 3:
        // the gradient in the fill itself, shaped as the white-word chips'
        // is: lighter in a band at the top (45 %), darker near the bottom
        // (to 88 %), an even 70 % where the words are
        for (int yy = y; yy <= y + h; ++yy)
        {
          const float t = (yy - y) / static_cast<float>(h);
          float share = 0.70f;
          if (g_chip3d == 3 && t < 0.2f)   {share = 0.45f + 0.25f * t / 0.2f;}
          if (g_chip3d == 3 && t > 0.65f)  {share = 0.70f + 0.18f * (t - 0.65f) / 0.35f;}
          const Tone row = {K, share, W, 1};
          for (int xx = xa; xx <= xb; ++xx)
          {
            if (inRound(xx, yy, xa, y, xb, y + h, 8))
            {
              px(xx, yy, toneAt(row, xx, yy));
            }
          }
        }
        break;
      case 4: fillBox(xa, y, xb, y + h, 8, Y); break;
      case 5: fillBox(xa, y, xb, y + h, 8, W);
              display.setStipple(true);
              strokeBox(xa, y, xb, y + h, 8, 1, K);
              display.setStipple(false);
              break;
      default: fillBox(xa, y, xb, y + h, 8, W);
               strokeBox(xa, y, xb, y + h, 8, 3, R);
               break;
      }
    }
    else if (plain)
    {
      fillBox(xa, y, xb, y + h, 8, W);
      // twice the fill's share, so the steps still tell apart in a band
      const Tone band = {look.tone.top, std::min(1.0f, look.tone.share * 2),
                         look.tone.under, look.tone.cover};
      for (int yy = y + h - 10; yy <= y + h; ++yy)
      {
        for (int xx = xa; xx <= xb; ++xx)
        {
          if (inRound(xx, yy, xa, y, xb, y + h, 8))
          {
            px(xx, yy, toneAt(band, xx, yy));
          }
        }
      }
    }
    else if (cloudy)
    {
      const Tone t = {K, cloudShare(s.kind, g_cloudStyle), W, 1};
      if (g_cloudStyle == 4) {fillTone2x(xa, y, xb, y + h, 8, t);}
      else                   {fillTone(xa, y, xb, y + h, 8, t);}
    }
    else if (look.solid)
    {
      fillBox(xa, y, xb, y + h, 8, look.tone.top);
    }
    else
    {
      fillTone(xa, y, xb, y + h, 8, look.tone);
    }
    if (s.kind == K_TSTORM)
    {
    }
    else if (look.edgeWidth == 1)
    {
      display.setStipple(true);
      strokeBox(xa, y, xb, y + h, 8, 1, look.edge);
      display.setStipple(false);
    }
    else if (look.edgeWidth > 1)
    {
      strokeBox(xa, y, xb, y + h, 8, look.edgeWidth, look.edge);
    }
    const bool ownGradient = s.kind == K_TSTORM && g_stormStyle == 3 && g_chip3d == 3;
    if ((g_chip3d == 1 || g_chip3d >= 3) && !ownGradient)
    {
      const bool lightWords = look.text == W_ABS
                              || (s.kind == K_TSTORM && (g_stormStyle == 2 || g_stormStyle == 3))
                              || (cloudy && g_cloudStyle == 5);
      chipLight(xa, y, xb, y + h, g_chip3d, lightWords);
    }

    const int bolt = (s.kind == K_TSTORM) ? 34 : 0;
    const Lines l = wording(s, hoursShown, xb - xa - bolt, start);
    int tw = 0;
    for (int k = 0; k < l.n; ++k)
    {
      tw = std::max(tw, textWidth(l.text[k], *l.face[k]));
    }
    const int cx = (xa + xb) / 2, cy = plain ? y + (h - 10) / 2 : y + h / 2;
    const int gapA = l.n ? cx - tw / 2 - 10 - bolt / 2 : cx;
    const int gapB = l.n ? cx + tw / 2 + 10 + bolt / 2 : cx;

    // texture, kept clear of the words
    if (s.kind == K_FOG)
    {
      display.setStipple(true);
      for (int j = y + 7; j < y + h - (plain ? 14 : 6); j += 7)
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
          fillBox(xx, yy, xx + 1, yy + 1, 0, W_ABS);
        }
      }
    }
    else if (s.kind == K_TSTORM && l.n)
    {
      stormMark(gapA + 16, cy);
    }
    if (l.n == 0 && xb - xa >= 30)
    { // no room for words: what the spell is, as a glyph
      if (s.kind == K_TSTORM) {stormMark(cx, cy);}
      else {tl::glyph(s.kind, cx, cy, look.solid && look.tone.top == B);}
    }

    const int tx = cx + bolt / 2;
    uint16_t ink = (s.kind == K_TSTORM) ? K : look.text;
    if (cloudy && g_cloudStyle == 5)
    {
      ink = W_ABS;
    }
    if (s.kind == K_TSTORM && (g_stormStyle == 2 || g_stormStyle == 3))
    {
      ink = W_ABS;
    }
    if (cloudy && g_cloudStyle == 3 && l.n)
    { // a patch of paper behind the words
      int th = 0;
      for (int k = 0; k < l.n; ++k)
      {
        th += l.face[k]->ascent + l.face[k]->descent;
      }
      fillBox(tx - tw / 2 - 8, cy - th / 2 - 4, tx + tw / 2 + 8, cy + th / 2 + 4, 6, W);
    }
    if (l.n == 1)
    {
      drawText(tx, cy, l.text[0], *l.face[0], ink, MM);
    }
    else if (l.n == 2)
    {
      drawText(tx, cy - 12, l.text[0], *l.face[0], ink, MM);
      drawText(tx, cy + 13, l.text[1], *l.face[1], ink, MM);
    }
  }
} // end drawRibbon

/* =========================================================================
 * Stacked graphs (config.json "graph_layout": "stacked", 2026-10-25): the
 * weather by spell, then up to four smaller graphs under it on one time
 * scale, chosen in GRAPH_SLOTS. The simulator also draws the other layouts
 * tried on 2026-10-17 (GRAPH_LAYOUT 2 to 5): no strip, one tall graph and
 * the rest short, a grid of two by two, a table.
 * ====================================================================== */
namespace sg
{
enum Met { TEMP, TEMP_DEW, TEMP_DEW_RAIN, DEW, WIND, RAIN, AMOUNT, HUMIDITY,
           CLOUDS, FEELS, TEMP_FEELS, RAIN_BOTH, RAIN_CLOUDS, NONE };

static Met metOf(const String &s)
{
  static const struct { const char *k; Met m; } T[] = {
    {"temp", TEMP}, {"temp_dew", TEMP_DEW}, {"temp_dew_rain", TEMP_DEW_RAIN},
    {"dew", DEW}, {"wind", WIND}, {"rain", RAIN}, {"amount", AMOUNT},
    {"humidity", HUMIDITY}, {"clouds", CLOUDS}, {"feels", FEELS},
    {"temp_feels", TEMP_FEELS}, {"rain_both", RAIN_BOTH},
    {"rain_clouds", RAIN_CLOUDS}};
  for (auto &t : T)
  {
    if (s == t.k) return t.m;
  }
  return NONE;
}

// wind in the unit of the wind tile
static float speedOf(float ms)
{
#if defined(UNITS_SPEED_METERSPERSECOND)
  return ms;
#elif defined(UNITS_SPEED_FEETPERSECOND)
  return meterspersecond_to_feetpersecond(ms);
#elif defined(UNITS_SPEED_KILOMETERSPERHOUR)
  return meterspersecond_to_kilometersperhour(ms);
#elif defined(UNITS_SPEED_MILESPERHOUR)
  return meterspersecond_to_milesperhour(ms);
#elif defined(UNITS_SPEED_KNOTS)
  return meterspersecond_to_knots(ms);
#else
  return meterspersecond_to_beaufort(ms);
#endif
}
static String speedUnit()
{
#if defined(UNITS_SPEED_METERSPERSECOND)
  return TXT_UNITS_SPEED_METERSPERSECOND;
#elif defined(UNITS_SPEED_FEETPERSECOND)
  return TXT_UNITS_SPEED_FEETPERSECOND;
#elif defined(UNITS_SPEED_KILOMETERSPERHOUR)
  return TXT_UNITS_SPEED_KILOMETERSPERHOUR;
#elif defined(UNITS_SPEED_MILESPERHOUR)
  return TXT_UNITS_SPEED_MILESPERHOUR;
#elif defined(UNITS_SPEED_KNOTS)
  return TXT_UNITS_SPEED_KNOTS;
#else
  return TXT_UNITS_SPEED_BEAUFORT;
#endif
}

// rain amounts in the forecast row's unit; where that row shows chances,
// inches beside Fahrenheit and millimetres otherwise. The scale's tops,
// smallest first, are round in the decimals given (their halves too).
#if defined(UNITS_DAILY_PRECIP_INCHES) \
    || (defined(UNITS_DAILY_PRECIP_POP) && defined(UNITS_TEMP_FAHRENHEIT))
static float amountOf(float mm) { return millimeters_to_inches(mm); }
static const char *amountUnit() { return TXT_UNITS_PRECIP_INCHES; }
static const float AMOUNT_TOPS[] = {0.1f, 0.2f, 0.4f, 0.5f, 1.0f, 2.0f};
static const int AMOUNT_DECIMALS = 2;
static const float AMOUNT_MIN = 0.004f; // an hour with less shows no bar
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
static float amountOf(float mm) { return millimeters_to_centimeters(mm); }
static const char *amountUnit() { return TXT_UNITS_PRECIP_CENTIMETERS; }
static const float AMOUNT_TOPS[] = {0.2f, 0.4f, 1.0f, 2.0f, 4.0f, 10.0f};
static const int AMOUNT_DECIMALS = 1;
static const float AMOUNT_MIN = 0.01f;
#else
static float amountOf(float mm) { return mm; }
static const char *amountUnit() { return TXT_UNITS_PRECIP_MILLIMETERS; }
static const float AMOUNT_TOPS[] = {2.0f, 4.0f, 10.0f, 20.0f, 40.0f, 100.0f};
static const int AMOUNT_DECIMALS = 0;
static const float AMOUNT_MIN = 0.1f;
#endif

// an amount as the labels write it: inches without the leading 0
static String amountText(float v, bool lead = false)
{
  String s = String(v, AMOUNT_DECIMALS);
  if (!lead && s.startsWith("0.")) s = s.substring(1);
  return s;
}

static const Tone T_GUST   = {K, 0.22f, W, 1.0f};
static const Tone T_CLOUDS = {K, 0.18f, W, 1.0f};

struct Frame
{
  int x0, x1, y0, y1, n;
  time_t start;
  float lo, hi;
  float X(float h) const { return x0 + (x1 - x0) * h / n; }
  float Y(float v) const { return y1 - (y1 - y0) * (v - lo) / (hi - lo); }
};

// a smooth line through v (NaN: a gap), on a halo of paper
static void line(const Frame &f, const float *v, int w, uint16_t ink,
                 bool halo = true, bool dashed = false)
{
  for (int pass = halo ? 0 : 1; pass < 2; ++pass)
  {
    for (int i = 0; i < f.n;)
    {
      if (std::isnan(v[i])) { ++i; continue; }
      int j = i;
      while (j < f.n && !std::isnan(v[j])) { ++j; }
      const int m = j - i;
      const float steps = 6.0f;
      for (int k = 1; m >= 2 && k <= static_cast<int>((m - 1) * steps); ++k)
      {
        const float f0 = (k - 1) / steps, f1 = k / steps;
        if (dashed && (k - 1) % 5 >= 3) continue;
        stroke(f.X(i + f0 + 0.5f), f.Y(smoothAt(v + i, m, f0)),
               f.X(i + f1 + 0.5f), f.Y(smoothAt(v + i, m, f1)),
               pass ? w : w + 6, pass ? ink : W);
      }
      i = j;
    }
  }
}

// the space between two smooth lines, in a screen
static void band(const Frame &f, const float *a, const float *b,
                 const Tone &t)
{
  for (int x = static_cast<int>(f.X(0.5f)); x <= static_cast<int>(f.X(f.n - 0.5f)); ++x)
  {
    const float h = (x - f.x0) * static_cast<float>(f.n) / (f.x1 - f.x0) - 0.5f;
    const float ya = f.Y(smoothAt(a, f.n, h)), yb = f.Y(smoothAt(b, f.n, h));
    fillTone(x, static_cast<int>(std::min(ya, yb)), x,
             static_cast<int>(std::max(ya, yb)), 0, t);
  }
}

// a bar an hour; none where v is NaN or not above minShow
static void bars(const Frame &f, const float *v, float minShow, const Tone &t)
{
  const int gap = ((f.x1 - f.x0) / f.n >= 24) ? 2 : 1;
  for (int i = 0; i < f.n; ++i)
  {
    if (v[i] > minShow)
    {
      fillTone(static_cast<int>(f.X(i)) + gap,
               std::max(f.y0, static_cast<int>(f.Y(v[i]))),
               static_cast<int>(f.X(i + 1)) - gap, f.y1, 0, t);
    }
  }
}

// a scale of round numbers covering lo..hi in no more than `lines` spaces
static void niceScale(float lo, float hi, int lines, const float *steps,
                      int nSteps, bool fromZero, float &outLo, float &outHi,
                      float &outStep)
{
  for (int s = 0; s < nSteps; ++s)
  {
    const float st = steps[s];
    const float a = fromZero ? 0 : floorf(lo / st) * st;
    float b = ceilf(hi / st) * st;
    if (b <= a) b = a + st;
    if ((b - a) / st <= lines + 0.01f || s == nSteps - 1)
    {
      outLo = a; outHi = b; outStep = st;
      return;
    }
  }
}

/* A scale whose ends are the values' own lowest and highest (at least
 * minSpan apart), so that the curves fill the plot and the scale's labels
 * read the day's extremes.
 */
static void fitEnds(Frame &f, float lo, float hi, float minSpan)
{
  if (hi - lo < minSpan)
  {
    const float mid = (lo + hi) / 2;
    lo = mid - minSpan / 2;
    hi = mid + minSpan / 2;
  }
  f.lo = lo;
  f.hi = hi;
}

// its labels: the bottom and the top, and between them the round numbers
// of a step that follows the range (the smallest that puts them 30 px
// apart: 2 for a few degrees, 10 for thirty), each with a dashed line; a
// round number too near an end's label is left out
static void gridEnds(const Frame &f, const char *suffix, const Font709 &font)
{
  static const float STEPS[] = {1, 2, 5, 10, 20, 25, 50};
  const float perUnit = (f.y1 - f.y0) / (f.hi - f.lo);
  float step = STEPS[6];
  for (float s : STEPS)
  {
    if (s * perUnit >= 30) { step = s; break; }
  }
  auto label = [&](float t, bool dashed) {
    const int y = static_cast<int>(f.Y(t));
    if (dashed) dashH(f.x0, f.x1, y, 1, 2, 8, K);
    drawText(f.x0 - 10, y, String(static_cast<int>(lroundf(t))) + suffix,
             font, K, RM, true);
  };
  for (float t = ceilf(f.lo / step) * step; t < f.hi; t += step)
  {
    if ((t - f.lo) * perUnit >= 32 && (f.hi - t) * perUnit >= 32) label(t, true);
  }
  label(f.lo, false);   // the base has its own line
  label(f.hi, true);
  fillBox(f.x0, f.y1, f.x1, f.y1 + 1, 0, K);
}

static void grid(const Frame &f, float step, const char *suffix,
                 const Font709 &font, int decimals, bool right = false)
{
  for (float t = f.lo; t <= f.hi + step * 0.01f; t += step)
  {
    const int y = static_cast<int>(f.Y(t));
    if (t > f.lo + step * 0.01f)
    {
      dashH(f.x0, f.x1, y, 1, 2, 8, K);
    }
    String s = decimals ? String(t, decimals) : String(static_cast<int>(lroundf(t)));
    if (decimals && s.startsWith("0.")) s = s.substring(1);
    if (t < step * 0.01f) s = "0";
    s += suffix;
    if (right)
      drawText(f.x1 + 10, y, s, font, K, LM, true);
    else
      drawText(f.x0 - 10, y, s, font, K, RM, true);
  }
  fillBox(f.x0, f.y1, f.x1, f.y1 + 1, 0, K);
}

static void midnights(const Frame &f, bool names, const Font709 &font)
{
  for (int i = 1; i < f.n; ++i)
  {
    const time_t t = f.start + i * 3600L;
    tm local;
    localtime_r(&t, &local);
    if (local.tm_hour == 0)
    {
      const int x = static_cast<int>(f.X(i));
      dashV(x, f.y0, f.y1, 1, 8, 6, K);
      if (names && x + 8 + textWidth(LC_ABDAY[local.tm_wday], font) < f.x1)
      {
        labelOnPaper(x + 10 + textWidth(LC_ABDAY[local.tm_wday], font) / 2,
                     f.y0 + font.ascent / 2 + 4, LC_ABDAY[local.tm_wday],
                     font, K);
      }
    }
  }
}

static void hourAxis(const Frame &f, int every, const Font709 &font)
{
  for (int h = 0; h <= f.n; h += every)
  {
    const int x = static_cast<int>(f.X(h));
    fillBox(x, f.y1, x + 1, f.y1 + 7, 0, K);
    drawText(x, f.y1 + 12, clock(f.start + h * 3600L, HOUR_FORMAT), font, K,
             MA, true);
  }
}

// the turns worth a label: a high or a low that is the furthest within six
// hours either side
static int turns(const float *v, int n, int *at, bool *high, int most)
{
  int k = 0;
  for (int i = 1; i < n - 1 && k < most; ++i)
  {
    const bool hi = v[i] > v[i - 1] && v[i] >= v[i + 1];
    const bool lo = v[i] < v[i - 1] && v[i] <= v[i + 1];
    if (!hi && !lo) continue;
    bool furthest = true;
    for (int j = std::max(0, i - 6); j <= std::min(n - 1, i + 6); ++j)
    {
      if ((hi && v[j] > v[i]) || (lo && v[j] < v[i])) furthest = false;
    }
    if (furthest)
    {
      at[k] = i;
      high[k++] = hi;
    }
  }
  return k;
}

static void dotLabel(const Frame &f, float h, float v, const String &s,
                     bool above, uint16_t ink, const Font709 &font,
                     bool dot = true)
{
  const int x = static_cast<int>(f.X(h)), y = static_cast<int>(f.Y(v));
  if (dot)
  {
    disc(x, y, 9, W);
    disc(x, y, 6, ink);
  }
  const int half = (font.ascent + font.descent) / 2 + 2;
  int ly = y + (above ? -(half + 10) : (half + 10));
  int lx = x;
  const int hw = textWidth(s, font) / 2 + 9;
  auto fits = [&](int c) { return c - half >= f.y0 && c + half <= f.y1 - 2; };
  if (!fits(ly) && !dot)
  {
    return;                       // a bar's figure: the summary says it
  }
  if (!fits(ly) && fits(y + (above ? (half + 10) : -(half + 10))))
  {
    ly = y + (above ? (half + 10) : -(half + 10));
  }
  else if (!fits(ly))
  { // no room above or below inside the plot: beside the point
    ly = std::max(f.y0 + half, std::min(f.y1 - half - 2, y));
    lx = x + hw + 12;
    if (lx + hw > f.x1) lx = x - hw - 12;
  }
  lx = std::max(f.x0 + hw, std::min(f.x1 - hw, lx));
  labelOnPaper(lx, ly, s, font, ink);
}

// a key: a short line or a box in the ink, then the word; returns the right
static int key(int x, int y, bool box, uint16_t ink, const Tone *t,
               const String &word, const Font709 &font, bool dashed = false)
{
  if (box)
    fillTone(x, y - 11, x + 22, y + 11, 0, t ? *t : rainBarTone());
  else if (dashed)
  {
    fillBox(x, y - 3, x + 9, y + 2, 0, ink);
    fillBox(x + 15, y - 3, x + 24, y + 2, 0, ink);
    fillBox(x + 30, y - 3, x + 34, y + 2, 0, ink);
  }
  else
    fillBox(x, y - 3, x + 34, y + 2, 0, ink);
  x += box ? 30 : 42;
  return drawText(x, y, word, font, K, LM, true) + 22;
}

struct Look
{
  const Font709 *title, *axis, *label, *keyFont;
  int lineW;
  bool keys;      // keys beside the title
};

static String pct10(float v)
{
  return String(static_cast<int>(lroundf(v / 10)) * 10) + "%";
}

// one graph: its title line at yTitle, its plot in f
static void chart(Met m, const owm_hourly_t *hourly, Frame f, int yTitle,
                  const Look &look, bool names)
{
  const int n = f.n;
  float vt[OWM_NUM_HOURLY], vd[OWM_NUM_HOURLY], vw[OWM_NUM_HOURLY],
        vg[OWM_NUM_HOURLY], vp[OWM_NUM_HOURLY], va[OWM_NUM_HOURLY],
        vh[OWM_NUM_HOURLY], vc[OWM_NUM_HOURLY], vf[OWM_NUM_HOURLY];
  bool amounts = false;
  for (int i = 0; i < n; ++i)
  {
    vf[i] = toUnitsF(hourly[i].feels_like);
    vt[i] = toUnitsF(hourly[i].temp);
    vd[i] = std::isnan(hourly[i].dew_point) ? NAN : toUnitsF(hourly[i].dew_point);
    vw[i] = speedOf(hourly[i].wind_speed);
    vg[i] = speedOf(hourly[i].wind_gust);
    vp[i] = hourly[i].pop * 100.0f;
    va[i] = std::isnan(hourly[i].precip) ? NAN : amountOf(hourly[i].precip);
    amounts = amounts || !std::isnan(va[i]);
    vh[i] = hourly[i].humidity;
    vc[i] = hourly[i].clouds;
  }
  // a source without amounts: the combined graph is the chance alone
  if (m == RAIN_BOTH && !amounts) m = RAIN;
  auto mx = [&](const float *v) { float r = -1e9f; for (int i = 0; i < n; ++i) if (!std::isnan(v[i])) r = std::max(r, v[i]); return r; };
  auto mn = [&](const float *v) { float r = 1e9f; for (int i = 0; i < n; ++i) if (!std::isnan(v[i])) r = std::min(r, v[i]); return r; };
  auto argmax = [&](const float *v) { int k = 0; for (int i = 1; i < n; ++i) if (v[i] > v[k]) k = i; return k; };
  auto I = [](float x) { return String(static_cast<int>(lroundf(x))); };
  float total = 0;
  for (int i = 0; i < n; ++i) if (!std::isnan(va[i])) total += va[i];
  // spaces between gridlines: at least three, so that a short graph does
  // not jump to 30-degree steps (36-65 drawn on 30-90, 2026-10-25)
  const int lines = std::max(3, std::min(5, (f.y1 - f.y0) / 32));
  const int tx = f.x0;
  const int ty = yTitle;
  int kx = 0;
  auto title = [&](const String &word) {
    return drawText(tx, ty, word, *look.title, K, LM) + 26;
  };
  auto summary = [&](const String &s) {
    drawText(RIGHT1, ty, s, *look.keyFont, K, RM, true);
  };
  const String speed = speedUnit();
  String sum;
  switch (m)
  {
  case TEMP: case TEMP_DEW: case TEMP_DEW_RAIN: case DEW:
  {
    const bool t = (m != DEW), d = (m != TEMP);
    float lo = 1e9f, hi = -1e9f;
    if (t) { lo = std::min(lo, mn(vt)); hi = std::max(hi, mx(vt)); }
    if (d) { lo = std::min(lo, mn(vd)); hi = std::max(hi, mx(vd)); }
    static const float S[] = {5, 10, 15, 20, 30};
    float step;
    if (m == TEMP_DEW_RAIN)
    { // round ends, which the chance's percentages are read against
      niceScale(lo - 1, hi + 3, lines, S, 5, false, f.lo, f.hi, step);
      if ((f.hi - f.lo) / step == 3 || (f.hi - f.lo) / step == 7)
        f.hi += step;
    }
    else
    {
      fitEnds(f, lo, hi, 4);
    }
    if (t && d && look.keys)
    {
      int x = key(tx, ty, false, tempInk(), nullptr, TXT709_TEMPERATURE, *look.title);
      x = key(x, ty, false, dewInk(), nullptr, TXT709_DEW_POINT, *look.title);
      if (m == TEMP_DEW_RAIN) key(x, ty, true, B, nullptr, TXT709_CHANCE_OF_RAIN, *look.title);
    }
    else
    {
      kx = title(m == DEW ? String(TXT709_DEW_POINT)
                 : (t && d) ? String(TXT709_TEMPERATURE) + ", " + TXT709_DEW_POINT
                 : String(TXT709_TEMPERATURE));
    }
    if (m == TEMP_DEW_RAIN)
    {
      const float spaces = (f.hi - f.lo) / step;
      Frame fr = f; fr.lo = 0; fr.hi = 100;
      bars(fr, vp, 2, rainBarTone());
      for (int j = 1; j <= spaces; ++j)
      {
        const int share = static_cast<int>(100 * j / spaces);
        if ((100 * j) % static_cast<int>(spaces) == 0 && (share % 25 == 0 || share % 10 == 0))
          drawText(f.x1 + 10, static_cast<int>(f.Y(f.lo + j * step)),
                   String(share) + "%", *look.axis, K, LM, true);
      }
    }
    if (m == TEMP_DEW_RAIN) grid(f, step, "\xB0", *look.axis, 0);
    else gridEnds(f, "\xB0", *look.axis);
    midnights(f, names, *look.label);
    if (d) line(f, vd, look.lineW, dewInk());
    if (t) line(f, vt, look.lineW, tempInk());
    const float *v = t ? vt : vd;
    int at[6]; bool up[6];
    const int k = turns(v, n, at, up, 6);
    for (int i = 0; i < k; ++i)
      dotLabel(f, at[i] + 0.5f, v[at[i]], I(v[at[i]]) + "\xB0", !up[i],
               K, *look.label);
    if (m == TEMP_DEW_RAIN)
    {
      const int p = argmax(vp);
      if (vp[p] > 10)
      {
        Frame fr = f; fr.lo = 0; fr.hi = 100;
        dotLabel(fr, p + 0.5f, vp[p], pct10(vp[p]), true, dewInk(),
                 *look.label, false);
      }
    }
    else if (!look.keys || m != TEMP_DEW)
    {
      sum = I(mn(v)) + "\xB0 " + TXT709_TO + " " + I(mx(v)) + "\xB0";
    }
    break;
  }
  case WIND:
  {
    // from calm to the strongest gust, which the top label reads
    fitEnds(f, 0, std::max(2.0f, mx(vg)), 2);
    kx = title(speed.length() ? String(TXT709_WIND) + ", " + speed
                              : String(TXT709_WIND));
    gridEnds(f, "", *look.axis);
    midnights(f, names, *look.label);
    band(f, vw, vg, T_GUST);
    line(f, vw, look.lineW, K);
    if (look.keys)
    {
      int x = key(kx, ty, false, K, nullptr, TXT709_SUSTAINED, *look.keyFont);
      key(x, ty, true, K, &T_GUST, TXT709_GUSTS, *look.keyFont);
    }
    sum = I(mn(vw)) + "\x7F" + I(mx(vw)) + " " + speed + ", " + TXT709_GUSTS
        + " " + I(mx(vg));
    break;
  }
  case RAIN:
  {
    f.lo = 0; f.hi = 100;
    kx = title(TXT709_CHANCE_OF_RAIN);
    grid(f, (lines >= 4) ? 25 : 50, "%", *look.axis, 0);
    midnights(f, names, *look.label);
    bars(f, vp, 2, rainBarTone());
    const int p = argmax(vp);
    if (vp[p] > 10)
    {
      dotLabel(f, p + 0.5f, vp[p], pct10(vp[p]), true, dewInk(),
               *look.label, false);
      sum = String(TXT709_PEAK) + " " + pct10(vp[p]) + " " + TXT709_AT + " "
          + clock(f.start + p * 3600L, HOUR_FORMAT);
      sum.replace("  ", " ");
    }
    else
    {
      sum = TXT709_DRY;
    }
    break;
  }
  case AMOUNT:
  {
    float top = AMOUNT_TOPS[0];
    for (float s : AMOUNT_TOPS) { top = s; if (s >= mx(va)) break; }
    f.lo = 0; f.hi = top;
    kx = title(String(TXT709_RAIN_AMOUNT) + ", " + amountUnit());
    grid(f, top / 2, "", *look.axis, AMOUNT_DECIMALS);
    midnights(f, names, *look.label);
    bars(f, va, AMOUNT_MIN, rainBarTone());
    const int p = argmax(va);
    if (va[p] > AMOUNT_MIN)
    {
      dotLabel(f, p + 0.5f, va[p], amountText(va[p]), true, dewInk(),
               *look.label, false);
    }
    sum = !amounts ? String(TXT709_NO_AMOUNTS)
        : (total > AMOUNT_MIN) ? String(TXT709_TOTAL) + " "
                                 + amountText(total, true) + " " + amountUnit()
        : String(TXT709_DRY);
    break;
  }
  case RAIN_BOTH: case RAIN_CLOUDS:
  {
    // the amount as bars on the left scale, the chance as a line on its
    // own 0-100 % scale at the right; their gridlines coincide (the
    // amount's half steps are the chance's 50 %). RAIN_CLOUDS adds the
    // cloud cover, on the chance's scale, as a grey band behind the rest.
    const bool clouds = (m == RAIN_CLOUDS);
    float top = AMOUNT_TOPS[0];
    for (float s : AMOUNT_TOPS) { top = s; if (s >= mx(va)) break; }
    kx = title(TXT709_RAIN);
    if (look.keys)
    {
      int x = key(kx, ty, true, B, nullptr,
                  String(TXT709_AMOUNT) + ", " + amountUnit(), *look.keyFont);
      x = key(x, ty, false, K, nullptr, TXT709_CHANCE, *look.keyFont);
      if (clouds) key(x, ty, true, K, &T_CLOUDS, TXT709_CLOUDS, *look.keyFont);
    }
    // The chance's peak carries its label above its dot. A high peak would
    // leave it no room under the top, so then both scales stop short of
    // the top by the label's height.
    const int p = argmax(vp);
    const bool peak = vp[p] > 10;
    const int room = look.label->ascent + look.label->descent + 16;
    const float h = static_cast<float>(f.y1 - f.y0);
    const float stretch = peak ? std::max(1.0f, vp[p] / 100.0f * h / (h - room))
                               : 1.0f;
    Frame fa = f;                       // the amounts, 0..top
    fa.y0 = f.y1 - static_cast<int>(lroundf(h / stretch));
    fa.lo = 0; fa.hi = top;
    Frame fp = f;                       // the chance, 0..100 % at fa's top
    fp.lo = 0; fp.hi = 100.0f * stretch;
    if (clouds)
    {
      float zero[OWM_NUM_HOURLY] = {};
      band(fp, zero, vc, T_CLOUDS);
    }
    grid(fa, top / 2, "", *look.axis, AMOUNT_DECIMALS);
    // only 50 % on the right: 100 % would sit under the summary, and the
    // peak carries its own label
    drawText(f.x1 + 10, static_cast<int>(fp.Y(50.0f)), "50%", *look.axis, K, LM, true);
    midnights(f, names, *look.label);
    bars(fa, va, AMOUNT_MIN, rainBarTone());
    line(fp, vp, look.lineW, K);
    if (peak)
      dotLabel(fp, p + 0.5f, vp[p], pct10(vp[p]), true, K, *look.label);
    sum = !amounts ? String(TXT709_NO_AMOUNTS)
        : (total > AMOUNT_MIN) ? String(TXT709_TOTAL) + " "
                                 + amountText(total, true) + " " + amountUnit()
        : String(TXT709_DRY);
    break;
  }
  case HUMIDITY:
  {
    f.lo = 0; f.hi = 100;
    kx = title(TXT709_HUMIDITY);
    grid(f, (lines >= 4) ? 25 : 50, "%", *look.axis, 0);
    midnights(f, names, *look.label);
    line(f, vh, look.lineW, G);
    sum = I(mn(vh)) + "\x7F" + I(mx(vh)) + "%";
    break;
  }
  case CLOUDS:
  {
    f.lo = 0; f.hi = 100;
    kx = title(TXT709_CLOUD_COVER);
    float zero[OWM_NUM_HOURLY] = {};
    band(f, zero, vc, T_CLOUDS);
    grid(f, (lines >= 4) ? 25 : 50, "%", *look.axis, 0);
    midnights(f, names, *look.label);
    line(f, vc, std::max(3, look.lineW - 2), K, false);
    sum = I(mn(vc)) + "\x7F" + I(mx(vc)) + "%";
    break;
  }
  case FEELS: case TEMP_FEELS:
  {
    const bool both = (m == TEMP_FEELS);
    float lo = mn(vf), hi = mx(vf);
    if (both) { lo = std::min(lo, mn(vt)); hi = std::max(hi, mx(vt)); }
    fitEnds(f, lo, hi, 4);
    if (both && look.keys)
    {
      const int x = key(tx, ty, false, tempInk(), nullptr, TXT709_TEMPERATURE, *look.title);
      key(x, ty, false, tempInk(), nullptr, TXT709_FEELS_LIKE, *look.title, true);
    }
    else
    {
      kx = title(both ? String(TXT709_TEMPERATURE) + ", " + TXT709_FEELS_LIKE
                      : String(TXT709_FEELS_LIKE));
    }
    gridEnds(f, "\xB0", *look.axis);
    midnights(f, names, *look.label);
    // feels like first, so that where the two agree the solid line shows
    line(f, vf, look.lineW, tempInk(), true, both);
    if (both) line(f, vt, look.lineW, tempInk());
    const float *v = both ? vt : vf;
    int at[6]; bool up[6];
    const int k = turns(v, n, at, up, 6);
    for (int i = 0; i < k; ++i)
      dotLabel(f, at[i] + 0.5f, v[at[i]], I(v[at[i]]) + "\xB0", !up[i],
               K, *look.label);
    sum = (both ? String(TXT709_FEELS_LIKE) + " " : String()) + I(mn(vf))
        + "\xB0 " + TXT709_TO + " " + I(mx(vf)) + "\xB0";
    break;
  }
  default:
    break;
  }
  if (sum.length() && look.keys)
  {
    summary(sum);
  }
}

#if defined(EPD709_HOST)
// a row of the table (simulator only): the name and the range on the left,
// a line without a scale on the right
static void sparkRow(Met m, const owm_hourly_t *hourly, Frame f, int nameX)
{
  const int n = f.n;
  float v[OWM_NUM_HOURLY], v2[OWM_NUM_HOURLY];
  const char *name = "";
  String big, small;
  uint16_t ink = K;
  bool barsToo = false;
  for (int i = 0; i < n; ++i)
  {
    switch (m)
    {
    case TEMP: case TEMP_DEW: case TEMP_DEW_RAIN:
      v[i] = toUnitsF(hourly[i].temp);
      v2[i] = std::isnan(hourly[i].dew_point) ? NAN : toUnitsF(hourly[i].dew_point);
      break;
    case DEW: v[i] = toUnitsF(hourly[i].dew_point); v2[i] = NAN; break;
    case FEELS: v[i] = toUnitsF(hourly[i].feels_like); v2[i] = NAN; break;
    case TEMP_FEELS: v[i] = toUnitsF(hourly[i].temp); v2[i] = toUnitsF(hourly[i].feels_like); break;
    case WIND: v[i] = speedOf(hourly[i].wind_speed); v2[i] = speedOf(hourly[i].wind_gust); break;
    case RAIN: v[i] = hourly[i].pop * 100; v2[i] = NAN; break;
    case AMOUNT: case RAIN_BOTH: case RAIN_CLOUDS:
      v[i] = std::isnan(hourly[i].precip) ? 0 : amountOf(hourly[i].precip); v2[i] = NAN; break;
    case HUMIDITY: v[i] = hourly[i].humidity; v2[i] = NAN; break;
    default: v[i] = hourly[i].clouds; v2[i] = NAN; break;
    }
  }
  float lo = 1e9f, hi = -1e9f, hi2 = -1e9f, total = 0;
  int at = 0;
  for (int i = 0; i < n; ++i)
  {
    lo = std::min(lo, v[i]);
    if (v[i] > hi) { hi = v[i]; at = i; }
    if (!std::isnan(v2[i])) { lo = std::min(lo, v2[i]); hi2 = std::max(hi2, v2[i]); }
    total += v[i];
  }
  auto I = [](float x) { return String(static_cast<int>(lroundf(x))); };
  const String speed = speedUnit();
  switch (m)
  {
  case TEMP: case TEMP_DEW: case TEMP_DEW_RAIN:
  {
    float tl = 1e9f, th = -1e9f;
    for (int i = 0; i < n; ++i) { tl = std::min(tl, v[i]); th = std::max(th, v[i]); }
    name = "Temperature"; ink = tempInk();
    big = I(tl) + "\xB0 \x7F " + I(th) + "\xB0";
    if (m != TEMP) { float dl = 1e9f, dh = -1e9f; for (int i = 0; i < n; ++i) { dl = std::min(dl, v2[i]); dh = std::max(dh, v2[i]); } small = String("Dew point ") + I(dl) + "\xB0 \x7F " + I(dh) + "\xB0"; }
    if (m == TEMP) { for (int i = 0; i < n; ++i) v2[i] = NAN; }
    f.lo = lo - 1; f.hi = std::max(hi, hi2) + 1;
    break;
  }
  case FEELS: case TEMP_FEELS:
  {
    float fl = 1e9f, fh = -1e9f, tl = 1e9f, th = -1e9f;
    for (int i = 0; i < n; ++i)
    {
      const float fv = (m == FEELS) ? v[i] : v2[i];
      fl = std::min(fl, fv); fh = std::max(fh, fv);
      tl = std::min(tl, v[i]); th = std::max(th, v[i]);
    }
    ink = tempInk();
    if (m == FEELS) { name = "Feels like"; big = I(fl) + "\xB0 \x7F " + I(fh) + "\xB0"; }
    else { name = "Temperature"; big = I(tl) + "\xB0 \x7F " + I(th) + "\xB0";
           small = String("Feels like ") + I(fl) + "\xB0 \x7F " + I(fh) + "\xB0"; }
    f.lo = lo - 1; f.hi = std::max(hi, hi2) + 1;
    break;
  }
  case WIND:
    name = "Wind"; big = I(lo) + "\x7F" + I(hi) + " " + speed;
    small = String("Gusts to ") + I(hi2) + " " + speed;
    f.lo = 0; f.hi = std::max(10.0f, hi2 + 1);
    break;
  case RAIN:
    name = "Chance of rain"; ink = B; barsToo = true;
    big = (hi > 10) ? I(roundf(hi / 10) * 10) + "%" : String("Dry");
    small = (hi > 10) ? String("at ") + clock(f.start + at * 3600L, HOUR_FORMAT) : String("");
    small.replace("  ", " ");
    f.lo = 0; f.hi = 100;
    break;
  case AMOUNT: case RAIN_BOTH: case RAIN_CLOUDS:
    name = "Rain amount"; ink = B; barsToo = true;
    big = amountText(total, true) + " " + amountUnit();
    small = String("most ") + amountText(hi, true) + " " + amountUnit() + " in an hour";
    f.lo = 0; f.hi = std::max(AMOUNT_TOPS[0] / 2, hi * 1.1f);
    break;
  case HUMIDITY:
    name = "Humidity"; ink = G; big = I(lo) + "\x7F" + I(hi) + "%";
    f.lo = 0; f.hi = 100;
    break;
  default:
    name = "Cloud cover"; big = I(lo) + "\x7F" + I(hi) + "%";
    f.lo = 0; f.hi = 100;
    break;
  }
  const int mid = (f.y0 + f.y1) / 2;
  drawText(nameX, mid - 30, name, F_SANS_26, K, LM, true);
  drawText(nameX, mid + 4, big, F_BITTER_36, ink == B ? dewInk() : (ink == G ? K : ink), LM);
  if (small.length())
    drawText(nameX, mid + 36, small, F_SANS_24, K, LM, true);
  // the frame: a hairline at the bottom, the days' edges
  fillBox(f.x0, f.y1, f.x1, f.y1, 0, K);
  midnights(f, false, F_SANS_24);
  if (barsToo)
  {
    bars(f, v, (m == RAIN) ? 2 : AMOUNT_MIN, rainBarTone());
  }
  else if (m == CLOUDS)
  {
    float zero[OWM_NUM_HOURLY] = {};
    band(f, zero, v, T_CLOUDS);
  }
  else
  {
    if (m == WIND) band(f, v, v2, T_GUST);
    else if (m == TEMP_FEELS) line(f, v2, 5, tempInk(), true, true);
    else if (!std::isnan(v2[0])) line(f, v2, 5, dewInk());
    line(f, v, 5, ink);
    int ats[6]; bool up[6];
    const int k = turns(v, n, ats, up, 6);
    for (int i = 0; i < k; ++i)
      dotLabel(f, ats[i] + 0.5f, v[ats[i]], I(v[ats[i]]) + ((m == TEMP || m == TEMP_DEW || m == DEW || m == FEELS || m == TEMP_FEELS) ? "\xB0" : ""),
               !up[i], K, F_SANS_26);
  }
}
#endif // EPD709_HOST

static void ribbonAt(const owm_hourly_t *hourly, int n, int x0, int x1, int y)
{
  static Spell spells[OWM_NUM_HOURLY];
  const int count = spellsOf(hourly, n, spells);
  drawRibbon(x0, x1, y, spells, count, n, static_cast<time_t>(hourly[0].dt));
}

} // namespace sg

static void drawSmallGraphs(const owm_hourly_t *hourly, int n, int gy)
{
  using namespace sg;
  const time_t start = static_cast<time_t>(hourly[0].dt);
  char heading[32];
  snprintf(heading, sizeof(heading), TXT709_NEXT_HOURS, n);
  drawText(RIGHT0, gy, heading, F_BITTER_40, K, LA);
  Met slot[4];
  int count = 0;
  for (int i = 0; i < 4; ++i)
  {
    const Met m = metOf(GRAPH_SLOTS[i]);
    if (m != NONE) slot[count++] = m;
  }
  if (!count)
  { // nothing chosen: the temperature at least
    slot[count++] = TEMP_DEW;
  }
  const int bottom = 1134;        // the last plot's foot; the hours under it
  Look look = {&F_SANS_28, &F_SANS_26, &F_SANS_26, &F_SANS_24, 5, true};
  const int style = GRAPH_LAYOUT;
#if defined(EPD709_HOST)
  if (style == 3)
  { // a grid of two by two, each with its own hours
    const int colW = (RIGHT1 - RIGHT0) / 2;
    const int top = gy + 64;
    const int rowH = (bottom + 38 - top) / 2;
    Look small = {&F_SANS_26, &F_SANS_24, &F_SANS_24, &F_SANS_22, 4, false};
    for (int i = 0; i < count && i < 4; ++i)
    {
      const int c = i % 2, r = i / 2;
      const int x0 = RIGHT0 + c * colW + 58, x1 = RIGHT0 + (c + 1) * colW - (c ? 12 : 40);
      const int yT = top + r * rowH;
      Frame f = {x0, x1, yT + 34, yT + rowH - 50, n, start, 0, 1};
      chart(slot[i], hourly, f, yT + 14, small, true);
      hourAxis(f, n <= 24 ? 6 : 12, F_SANS_24);
    }
    return;
  }
  if (style == 4)
  { // ribbon, then a table: a row a quantity, the line without a scale
    const int sx0 = 944, sx1 = 1548;
    ribbonAt(hourly, n, sx0, sx1, gy + 62);
    const int top = gy + 62 + 56 + 22;
    const int rowH = (bottom - top) / count;
    for (int i = 0; i < count; ++i)
    {
      const int y0 = top + i * rowH;
      Frame f = {sx0, sx1, y0 + 16, y0 + rowH - 14, n, start, 0, 1};
      if (i) dashH(RIGHT0, RIGHT1, y0, 1, 2, 6, K);
      sparkRow(slot[i], hourly, f, RIGHT0);
    }
    Frame f = {sx0, sx1, 0, bottom, n, start, 0, 1};
    hourAxis(f, n <= 24 ? 6 : 12, F_SANS_28);
    return;
  }
#endif
  // the strip of the weather by spell (when it is on; never in the
  // simulator's layout 2), then the graphs stacked on one time scale (the
  // first one taller in its layout 5)
  int top = gy + 66;
  if (style != 2 && GRAPH_STRIP)
  {
    ribbonAt(hourly, n, PLOT0, PLOT1, gy + 62);
    top = gy + 62 + 56 + 14;
  }
  const int head = 40, gapB = 12;
  float weight[4], sumW = 0;
  for (int i = 0; i < count; ++i)
  {
    weight[i] = (style == 5 && i == 0) ? 2.6f : 1.0f;
    sumW += weight[i];
  }
  const int room = bottom - top - count * head - (count - 1) * gapB;
  int y = top;
  for (int i = 0; i < count; ++i)
  {
    const int h = static_cast<int>(room * weight[i] / sumW);
    Frame f = {PLOT0, PLOT1, y + head, y + head + h, n, start, 0, 1};
    chart(slot[i], hourly, f, y + head / 2 - 2, look, i == 0);
    if (i == count - 1)
    {
      hourAxis(f, n <= 24 ? 3 : 6, F_SANS_28);
    }
    y += head + h + gapB;
  }
}

void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
  (void)timeInfo;
  const int n = std::min(std::max(HOURLY_GRAPH_MAX, 8), OWM_NUM_HOURLY);
  // the hours' section starts under the week, which ends 64 px higher
  // when no day has rain to show under its low
  const int gy = (weekRainRow(daily) ? 540 : 476) + alertTop;
  if (GRAPH_LAYOUT)
  {
    drawSmallGraphs(hourly, n, gy);
    return;
  }
  const time_t start = static_cast<time_t>(hourly[0].dt);

  char title[32];
  snprintf(title, sizeof(title), TXT709_NEXT_HOURS, n);
  drawText(RIGHT0, gy, title, F_BITTER_40, K, LA);

  // what the lines mean
  const int ly = gy + 28;
  fillBox(1040, ly - 3, 1080, ly + 2, 0, tempInk());
  drawText(1088, ly, TXT709_TEMPERATURE, F_SANS_26, K, LM, true);
  if (GRAPH_DEWPOINT)
  {
    fillBox(1262, ly - 3, 1302, ly + 2, 0, dewInk());
    drawText(1310, ly, TXT709_DEW_POINT, F_SANS_26, K, LM, true);
  }
  fillTone(1446, ly - 12, 1470, ly + 12, 0, rainBarTone());
  drawText(1478, ly, TXT709_RAIN, F_SANS_26, K, LM, true);

  const int top = gy + 64;
  if (GRAPH_STRIP)
  {
    static Spell spells[OWM_NUM_HOURLY];
    const int count = spellsOf(hourly, n, spells);
    drawRibbon(PLOT0, PLOT1, top, spells, count, n, start);
  }

  const int x0 = PLOT0, x1 = PLOT1, y1 = 1132;
  const int y0 = GRAPH_STRIP ? top + 56 + 22 : top + 8;
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
  // the rain scale on the right shares these lines, 0 % at the bottom and
  // 100 % at the top: 3 or 7 spaces would put them at thirds and sevenths,
  // so one more step on top makes quarters or eighths
  if ((hi - lo) / step == 3 || (hi - lo) / step == 7)
  {
    hi += step;
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
               static_cast<int>(X(i + 1)) - 2, y1, 0, rainBarTone());
    }
  }
  // the scale's lines and figures: temperature on the left, the chance of
  // rain on the right where the share is a round one (halves, quarters,
  // fifths, tenths); no 0 %, which would crowd the last hour's time
  const int spaces = (hi - lo) / step;
  for (int t = lo; t <= hi; t += step)
  {
    const int y = static_cast<int>(Yt(t));
    dashH(x0, x1, y, 1, 2, 8, K);
    drawText(x0 - 12, y, String(t) + "\xB0", F_SANS_30, K, RM, true);
    const int j = (t - lo) / step;
    const int share = 100 * j / spaces;
    if (j > 0 && (100 * j) % spaces == 0 && (share % 10 == 0 || share % 25 == 0))
    {
      drawText(x1 + 10, y, String(share) + "%", F_SANS_30, K, LM, true);
    }
  }
  fillBox(x0, y1, x1, y1 + 1, 0, K);
  // the hours: every 3 while there is room for them (a day or less), else
  // every 6
  const int every = (n <= 24) ? 3 : 6;
  for (int h = 0; h <= n; h += every)
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
  // across the rain; both as smooth curves through the hours that never
  // overshoot them (smoothAt), so a labelled high or low is the curve's
  // own; a run of hours with a gap in it is drawn in pieces
  auto curve = [&](const float *v, int from, int to, int pass, uint16_t ink) {
    const int m = to - from;
    if (m < 2)
    {
      return;
    }
    const float steps = 6.0f;                 // pieces per hour
    for (int k = 1; k <= static_cast<int>((m - 1) * steps); ++k)
    {
      const float f0 = (k - 1) / steps, f1 = k / steps;
      stroke(X(from + f0), Yt(smoothAt(v + from, m, f0)), X(from + f1),
             Yt(smoothAt(v + from, m, f1)), pass ? 6 : 12, pass ? ink : W);
    }                                         // as wide as the temperature: at
                                              // 282 ppi 4 px read as a hairline
  };
  float vt[OWM_NUM_HOURLY], vd[OWM_NUM_HOURLY];
  for (int i = 0; i < n; ++i)
  {
    vt[i] = toUnitsF(hourly[i].temp);
    vd[i] = std::isnan(hourly[i].dew_point) ? NAN : toUnitsF(hourly[i].dew_point);
  }
  if (GRAPH_DEWPOINT)
  {
    for (int pass = 0; pass < 2; ++pass)
    {
      for (int i = 0; i < n;)
      {
        if (std::isnan(vd[i])) { ++i; continue; }
        int j = i;
        while (j < n && !std::isnan(vd[j])) { ++j; }
        curve(vd, i, j, pass, dewInk());
        i = j;
      }
    }
  }
  for (int pass = 0; pass < 2; ++pass)
  {
    curve(vt, 0, n, pass, tempInk());
  }
  // highs and lows: a dot on the curve and the figure beside it; their
  // label boxes are kept so the rain label can stay off them
  struct LabelBox { int x0, y0, x1, y1; };
  LabelBox taken[16];
  int nTaken = 0;
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
    disc(x, y, 7, tempInk());
    int ly2 = y + (high ? -36 : 38);
    ly2 = std::max(y0 + 20, std::min(y1 - 22, ly2));
    const String fig = String(static_cast<int>(lroundf(t))) + "\xB0";
    labelOnPaper(x, ly2, fig, F_BITTER_36, K);
    if (nTaken < 16)
    {
      const int hw = (textWidth(fig, F_BITTER_36) + 18) / 2;
      const int hh = (F_BITTER_36.ascent + F_BITTER_36.descent + 2) / 2;
      taken[nTaken++] = {x - hw, ly2 - hh, x + hw, ly2 + hh};
    }
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
    const String share =
      String(static_cast<int>(lroundf(peak / 10.0f)) * 10) + "%";
    if (anchor == MD)
    { // rain in the first or the last hours: stay inside the plot, clear of
      // the numbers of the temperature axis
      const int half = textWidth(share, F_SANS_28) / 2;
      if (pxl - half < PLOT0 + 8)
      {
        pxl = PLOT0 + 8;
        anchor = LD;
      }
      else if (pxl + half > PLOT1 - 8)
      {
        pxl = PLOT1 - 8;
        anchor = RD;
      }
    }
    const int py = static_cast<int>(y1 - (y1 - y0) * peak / 100.0f) - 8;
    // on bare paper, like the highs and lows: with little rain the label
    // sits low, where the curves run
    const int wide = textWidth(share, F_SANS_28);
    const int cx = (anchor == LD) ? pxl + wide / 2
                 : (anchor == RD) ? pxl - wide / 2 : pxl;
    const int tall = F_SANS_28.ascent + F_SANS_28.descent;
    int cy = std::max(py, y0 + 30) - tall / 2;
    // its paper patch must not land on a high or low figure: above it if
    // there is room, else below
    const int hw = (wide + 18) / 2, hh = (tall + 2) / 2;
    for (int pass = 0; pass < 3; ++pass)
    {
      bool moved = false;
      for (int k = 0; k < nTaken; ++k)
      {
        const LabelBox &b = taken[k];
        if (cx + hw + 4 > b.x0 && cx - hw - 4 < b.x1
            && cy + hh + 4 > b.y0 && cy - hh - 4 < b.y1)
        {
          cy = (b.y0 - hh - 6 >= y0 + hh) ? b.y0 - hh - 6 : b.y1 + hh + 6;
          moved = true;
        }
      }
      if (!moved)
      {
        break;
      }
    }
    labelOnPaper(cx, cy, share, F_SANS_28, dewInk());
  }
} // end drawOutlookGraph

/* =========================================================================
 * The panel, and the screens that are not the weather
 * ====================================================================== */

void fillDisplayBackground()
{
  if (DARK_MODE)
  { // the charcoal ink over the whole page (W swaps to K, K is screened)
    fillBox(0, 0, DISP_WIDTH - 1, DISP_HEIGHT - 1, 0, W);
  }
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
               const String &errMsgLn2, bool mono)
{
  // mono: the icon in black too, one pigment
  drawBitmap2x(DISP_WIDTH / 2 - 196, 230, bitmap_196x196, 196, 196,
               mono ? K_ABS : R);
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
 * Test cards
 * ====================================================================== */

/* The second card (-D EPD709_TEST_CARD_PAGE=9, the target
 * seeed_xiao_ee02_709_testcard9): large patches for an out-of-focus flatbed
 * scan beside white paper, to set the simulator's inks (row 1) and the dot
 * mixes it shows as measured (rows 2 and 3). With test card 1, it is what
 * tools/sim709's colours come from.
 */
void drawTestCard709InkScan()
{
  drawText(DISP_WIDTH / 2, 8, "GDEB0709E01 inks and mixes (scan me beside white paper)",
           F_BITTER_32, K_ABS, MA);
  struct Patch { Tone tone; bool cells2x; bool checker; const char *name; };
  static const Patch P[3][6] = {
    {{{K_ABS, 1, W_ABS, 1}, false, false, "black"},
     {{W_ABS, 1, W_ABS, 1}, false, false, "white"},
     {{Y, 1, W_ABS, 1}, false, false, "yellow"},
     {{R, 1, W_ABS, 1}, false, false, "red"},
     {{B, 1, W_ABS, 1}, false, false, "blue"},
     {{G, 1, W_ABS, 1}, false, false, "green"}},
    {{{K_ABS, 0.50f, B, 1}, false, false, "50% black/blue"},
     {{K_ABS, 0.70f, B, 1}, false, false, "70% black/blue"},
     {{R, 0.35f, Y, 1}, false, false, "35% red/yellow"},
     {{R, 0.50f, B, 1}, true, false, "red/blue, 2x2"},
     {{R, 0.50f, B, 1}, false, false, "red/blue, single dots"},
     {{G, 0.50f, Y, 1}, false, false, "50% green/yellow"}},
    {{{K, 0.10f, W, 1}, false, false, "10% grey"},
     {{K, 0.21f, W, 1}, false, false, "21% grey"},
     {{K, 0.33f, W, 1}, false, false, "33% grey"},
     {{Y, 0.55f, W_ABS, 1}, false, false, "55% yellow"},
     {{B, 0.40f, W_ABS, 1}, false, false, "40% blue"},
     {{K_ABS, 0.5f, W_ABS, 1}, false, true, "black/white checker"}},
  };
  const int w = 236, h = 290, gx = 26, x0 = 40, y0 = 70, gy = 80;
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 6; ++c)
    {
      const Patch &p = P[r][c];
      const int x = x0 + c * (w + gx), y = y0 + r * (h + gy);
      if (p.checker)
      {
        for (int yy = y; yy <= y + h; ++yy)
          for (int xx = x; xx <= x + w; ++xx)
            px(xx, yy, ((xx + yy) & 1) ? K_ABS : W_ABS);
      }
      else if (p.cells2x)
      {
        fillTone2x(x, y, x + w, y + h, 0, p.tone);
      }
      else
      {
        fillTone(x, y, x + w, y + h, 0, p.tone);
      }
      strokeBox(x - 1, y - 1, x + w + 1, y + h + 1, 0, 1, K_ABS);
      drawText(x + w / 2, y + h + 10, p.name, F_SANS_22, K_ABS, MA);
    }
  }
} // end drawTestCard709InkScan

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
