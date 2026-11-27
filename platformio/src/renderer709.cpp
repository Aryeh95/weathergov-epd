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
#ifndef EPD709_HOST
#include <Preferences.h>
#include "epd709_lut.h"
#endif
#include "_locale.h"
#include "_strftime.h"
#include "api_response.h"
#include "conversions.h"
#include "display_utils.h"
#include "history.h"
#include "renderer.h"
#include "smooth.h"
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
#include "assets709/bluenoise709.h"
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

/* =========================================================================
 * Ink
 * ====================================================================== */

/* The panel has six inks and nothing between them. Every other colour is a
 * screen: dots of one ink over another, fine enough at 282 ppi for the eye
 * to blend. The dots come from a 64x64 blue-noise matrix, which spreads
 * every share evenly without lining the dots up; Bayer's 8x8 matrix, used
 * until 2026-10-28, left a grid of little crosses in the greys (the two
 * were compared side by side on the panel). The pattern is keyed to the pixel's place on
 * the page, so neighbouring shapes continue one screen instead of each
 * starting its own.
 */
static inline bool screen(int x, int y, float share)
{
  return share * 256.0f > BLUE_NOISE709[y & 63][x & 63] + 0.5f;
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
  // grey: half the dots on paper; in dark mode three in four, as white
  // dots alone on the black develop weakly and the checker's words all but
  // vanished on the panel (2026-10-28)
  display.setStipple(grey ? (DARK_MODE ? 2 : 1) : 0);
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

static bool s_skyDark = false;
static bool s_skySoftSub = false; // only soft stippled ink on airy Sunny / Partly skies
static int s_skyMood = 0;

static void drawIconOnSky(const uint8_t *data, int size, int x, int y,
                          bool darkSky, bool isFog = false)
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
      const int pxX = x + (n % size), pxY = y + (n / size);
      uint16_t ink = INK[v];
      if (darkSky && v == 6) // raindrops, snow, night crescent: bright ice-blue over dark indigo
      {
        ink = screen(pxX, pxY, 0.58f) ? W_ABS : B;
      }
      else if (darkSky && isFog && v == 1) // night fog lines stay light misty blue-white over dark slate
      {
        ink = screen(pxX, pxY, 0.72f) ? W_ABS : B;
      }
      px(pxX, pxY, ink);
    }
  }
}

// Crisp nearest/feature-preserving downsample for 104x104 condition icons
static void drawIconScaled(const uint8_t *data, int srcSize, int dstSize, int x, int y)
{
  static const uint16_t INK[7] = {0, K_ABS, W_ABS, R, Y, G, B};
  if (!data)
  {
    return;
  }
  auto sampleAt = [&](int sx, int sy) -> uint8_t {
    if (sx < 0 || sy < 0 || sx >= srcSize || sy >= srcSize) return 0;
    const int32_t n = static_cast<int32_t>(sy) * srcSize + sx;
    const uint8_t b = pgm_read_byte(&data[n >> 1]);
    const uint8_t v = (n & 1) ? (b & 0x0F) : (b >> 4);
    return (v < 7) ? v : 0;
  };
  for (int dy = 0; dy < dstSize; ++dy)
  {
    const int sy0 = (dy * srcSize) / dstSize;
    const int sy1 = std::min(srcSize - 1, ((dy + 1) * srcSize - 1) / dstSize);
    for (int dx = 0; dx < dstSize; ++dx)
    {
      const int sx0 = (dx * srcSize) / dstSize;
      const int sx1 = std::min(srcSize - 1, ((dx + 1) * srcSize - 1) / dstSize);
      uint8_t best = sampleAt((sx0 + sx1) / 2, (sy0 + sy1) / 2);
      // Keep outer silhouette continuous when center hits transparent, without darkening grey cloud stippling
      if (best == 0)
      {
        for (int sy = sy0; sy <= sy1; ++sy)
        {
          for (int sx = sx0; sx <= sx1; ++sx)
          {
            const uint8_t v = sampleAt(sx, sy);
            if (v != 0)
            {
              best = v;
              if (v == 1) break;
            }
          }
          if (best == 1) break;
        }
      }
      if (best)
      {
        px(x + dx, y + dy, INK[best]);
      }
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
  ring(cx - 0.5f, cy - 0.5f, half, 2, s_skyDark ? W_ABS : K_ABS);
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
  if (std::isnan(kelvin))
  {
    return "--";
  }
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
  int rank;  // 0 warning/red, 1 watch/orange, 2 advisory/yellow
  int color; // explicit IMS/Met colour: 3 red, 2 orange, 1 yellow, 0 none
};
static std::vector<Alert709> notices;
static const owm_hourly_t *hours = nullptr;

/* Called once before the page is drawn. Keeps the hourly forecast pointer
 * for tellingId() and filters/sorts weather alerts.
 */
void layout709Begin(std::vector<owm_alerts_t> &alerts,
                    const owm_hourly_t *hourly)
{
  hours = hourly;
  notices.clear();
#if DISPLAY_ALERTS
  if (alerts.empty())
  {
    return;
  }
  int ignore[OWM_NUM_ALERTS] = {0};
  filterAlerts(alerts, ignore, OWM_NUM_ALERTS);
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
    a.color = alertColorSeverity(alerts[i]);
    if (a.color > 0)
    {
      a.rank = 3 - a.color; // red -> 0, orange/amber -> 1, yellow -> 2
    }
    else
    {
      String lower = alerts[i].event;
      lower.toLowerCase();
      a.rank = (lower.indexOf("warning") >= 0) ? 0
             : (lower.indexOf("watch") >= 0)   ? 1
                                               : 2;
    }
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
#else
  (void)alerts;
#endif
} // end layout709Begin

static void drawNotice(int x0, int y0, int x1, int y1, const Alert709 &a,
                       bool oneLine)
{
  uint16_t ink = K, sign = Y;
  if (a.rank == 0)
  { // red warning: matches RISK_RED in drawChip
    fillBox(x0, y0, x1, y1, 9, R);
    ink = W_ABS;
    sign = R;
    if (s_skyDark)
    {
      strokeBox(x0, y0, x1, y1, 9, 1, W_ABS);
    }
  }
  else if (a.color == 1)
  { // explicit yellow warning (IMS): solid yellow ink matching RISK_YELLOW
    fillBox(x0, y0, x1, y1, 9, Y);
    ink = K_ABS;
    sign = W_ABS;
    strokeBox(x0, y0, x1, y1, 9, 2, s_skyDark ? W_ABS : K_ABS);
  }
  else
  {
    // orange warning (T_ORANGE, matching RISK_AMBER) or NWS watch / advisory
    Tone t = (a.rank == 1) ? T_ORANGE : T_ADVISORY;
    if (DARK_MODE || s_skyDark)
    {
      if (t.under == W) t.under = W_ABS;
      if (t.top == W)   t.top = W_ABS;
      ink = K_ABS;
    }
    fillTone(x0, y0, x1, y1, 9, t);
    strokeBox(x0, y0, x1, y1, 9, s_skyDark ? 1 : 2, s_skyDark ? W_ABS : K_ABS);
  }
  const int cy = (y0 + y1) / 2;
  const int h = y1 - y0;
  // Crisp warning triangle sized cleanly to compact 58..74 px card height
  const int tw = (h <= 62) ? 14 : 16;
  const int th = (h <= 62) ? 12 : 14;
  const int icx = x0 + 28;
  triangle(icx - tw, cy + th, icx, cy - th - 2, icx + tw, cy + th, ink);
  triangle(icx - tw + 5, cy + th - 3, icx, cy - th + 4, icx + tw - 5, cy + th - 3, sign);
  fillBox(icx - 2, cy - 3, icx + 2, cy + 3, 0, ink);
  fillBox(icx - 2, cy + 6, icx + 2, cy + 8, 0, ink);
  const int tx = x0 + 54;
  const int room = x1 - tx - 14;
  if (oneLine)
  {
    const String s = a.until.length() ? (a.event + TXT709_DOT + a.until) : a.event;
    drawText(tx, cy, s,
             fitFont(s, room, {&F_BITTER_30, &F_BITTER_27, &F_BITTER_24, &F_BITTER_22}),
             ink, LM);
    return;
  }
  if (!a.until.length())
  {
    drawText(tx, cy, a.event,
             fitFont(a.event, room, {&F_BITTER_30, &F_BITTER_27, &F_BITTER_24, &F_BITTER_22}),
             ink, LM);
    return;
  }
  const int dyOff = (h <= 60) ? 13 : 15;
  drawText(tx, cy - dyOff, a.event,
           fitFont(a.event, room, {&F_BITTER_28, &F_BITTER_26, &F_BITTER_24, &F_BITTER_22}),
           ink, LM);
  drawText(tx, cy + dyOff + 2, a.until,
           fitFont(a.until, room, {&F_SANS_22}), ink, LM);
}

/* Variant 3C alerts: right-aligned column inside the top adaptive sky band
 * (x = 1044..1568, y = 152..328). Collapses to zero visual footprint when empty.
 */
void drawAlerts(std::vector<owm_alerts_t> &alerts, const String &city,
                const String &date)
{
  (void)alerts; (void)city; (void)date; // prepared by layout709Begin
  if (notices.empty())
  {
    return;
  }
  const int ax0 = 1044, ax1 = 1568;
  if (notices.size() == 1)
  {
    drawNotice(ax0, 158, ax1, 232, notices[0], false);
    return;
  }
  const int more = static_cast<int>(notices.size()) - 2;
  const int y0 = more ? 150 : 154;
  const int h = more ? 58 : 64;
  const int gap = 10;
  drawNotice(ax0, y0, ax1, y0 + h, notices[0], false);
  drawNotice(ax0, y0 + h + gap, ax1, y0 + 2 * h + gap, notices[1], false);
  if (more)
  {
    const int py0 = y0 + 2 * h + 2 * gap, py1 = py0 + 34;
    const String pillText = "+" + String(more) + " " + String(TXT709_MORE);
    const int pw = std::max(128, textWidth(pillText, F_SANSB_25) + 32);
    const int px0 = ax1 - pw;
    if (s_skyDark)
    {
      fillBox(px0, py0, ax1, py1, 8, K_ABS);
      strokeBox(px0, py0, ax1, py1, 8, 1, W_ABS);
      drawText((px0 + ax1) / 2, (py0 + py1) / 2, pillText, F_SANSB_25, W_ABS, MM);
    }
    else
    {
      fillBox(px0, py0, ax1, py1, 8, W_ABS);
      strokeBox(px0, py0, ax1, py1, 8, 2, K_ABS);
      drawText((px0 + ax1) / 2, (py0 + py1) / 2, pillText, F_SANSB_25, K_ABS, MM);
    }
  }
} // end drawAlerts

/* =========================================================================
 * Top Sky Right: City & Date (right-aligned in x = 1044..1568)
 * ====================================================================== */

void drawLocationDate(const String &city, const String &date)
{
  const uint16_t ink = s_skyDark ? W_ABS : K_ABS;
  const Font709 &cf = fitFont(city, 520, {&F_BITTERSB_72, &F_BITTER_52,
                                          &F_BITTER_40, &F_BITTER_32});
  drawText(1568, 20, city, cf, ink, RA);
  drawText(1568, 102, date,
           fitFont(date, 520, {&F_BITTER_32, &F_BITTER_28, &F_SANS_24}),
           ink, RA, s_skySoftSub);
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
  return textWidth(word, F_SANSB_25) + 22;
}

static void drawChip(int x, int y, const char *word, int level)
{
  const int w = chipWidth(word), h = 34;
  uint16_t ink = W_ABS;
  // In the EPA's colours as far as the inks reach, from flatbed scans of
  // test cards 1 and 9 (2026-10-25). Good is the green ink itself, High half
  // red in yellow (T_ORANGE).
  switch (level)
  {
  case RISK_GREEN:  fillBox(x, y, x + w, y + h, 7, G);              break;
  case RISK_YELLOW: fillBox(x, y, x + w, y + h, 7, Y); ink = K_ABS; break;
  case RISK_AMBER:  fillTone(x, y, x + w, y + h, 7, T_ORANGE); ink = K_ABS; break;
  case RISK_RED:    fillBox(x, y, x + w, y + h, 7, R);              break;
  // Extreme: half red over blue in 2x2 cells, a purple (hue 346) -- the
  // nearest the inks come to the EPA's, and clear of red and of maroon.
  case RISK_PURPLE: fillTone2x(x, y, x + w, y + h, 7, {R, 0.50f, B, 1}); break;
  // Hazardous: half red over black, a true maroon -- L* 26, a* +45, b* +18
  // against the EPA's L* 25, a* +48, b* +19 -- with white words.
  case RISK_MAROON: fillTone(x, y, x + w, y + h, 7, {R, 0.50f, K_ABS, 1});
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
static void drawWindArrow(float cx, float cy, int fromDeg, float scale = 1.0f,
                          uint16_t ink = K)
{
  const float b = (fromDeg + 180) * 3.14159265f / 180.0f;
  const float dx = sinf(b), dy = -cosf(b);          // screen y grows down
  const float px = -dy, py = dx;                    // across the arrow
  const float tipX = cx + 15 * scale * dx, tipY = cy + 15 * scale * dy;
  const float bx = cx - 13 * scale * dx, by = cy - 13 * scale * dy;
  const float nX = cx - 5 * scale * dx, nY = cy - 5 * scale * dy;
  triangle(tipX, tipY, bx + 10 * scale * px, by + 10 * scale * py, nX, nY, ink);
  triangle(tipX, tipY, nX, nY, bx - 10 * scale * px, by - 10 * scale * py, ink);
}

static void drawCell(int i, const Cell &c)
{
  const int colW = 214;
  const int x = 32 + (i % 4) * colW;
  const int y = 974 + (i / 4) * 102;
  const int edge = x + colW - 10; // 204 px column width
  int lx = drawText(x, y, c.label, F_SANS_22, K, LA, true);
  if (c.tag.length() && lx + 6 + textWidth(c.tag, F_SANS_22) <= edge)
  {
    drawText(lx + 6, y, c.tag, F_SANS_22, K, LA, true);
  }
  int vStart = x;
  if (c.arrowDeg >= 0)
  {
    drawWindArrow(x + 13, y + 48, c.arrowDeg);
    vStart = x + 31;
  }
  const int trendRes = (c.trend == 1 || c.trend == -1) ? 24 : 0;
  const Font709 &vf = fitFont(c.value, edge - vStart - trendRes,
                              {&F_BITTER_34, &F_BITTER_28, &F_BITTER_24});
  const int vx = drawText(vStart, y + 30, c.value, vf, K, LA);
  if (trendRes)
  {
    drawTrend(vx + 6, y + 30, c.trend);
  }
  if (!c.word)
  {
    return;
  }
  const char *word = c.word;
  if (vx + 10 + chipWidth(word) > edge && c.brief)
  {
    word = c.brief;
  }
  if (vx + 10 + chipWidth(word) <= edge)
  {
    drawChip(vx + 10, y + 30, word, c.level);
  }
  else
  {
    drawChip(vStart, y + 70, word, c.level);
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
static uint32_t hash2(uint32_t x, uint32_t y)
{
  uint32_t h = x * 73856093u ^ y * 19349663u ^ 0x9E3779B9u;
  h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13;
  return h;
}

static void drawSkySun3C(int cx, int cy, bool darkSky)
{
  const float core = 18.0f;
  for (int y = cy - 22; y <= cy + 22; ++y)
  {
    for (int x = cx - 22; x <= cx + 22; ++x)
    {
      const float d = hypotf(static_cast<float>(x - cx), static_cast<float>(y - cy));
      if (d <= core)
      {
        const float edge = powf(d / core, 1.8f);
        const uint16_t ink = darkSky ? (screen(x, y, 0.20f * edge) ? R : Y)
                                     : (screen(x, y, 0.48f * edge) ? R : Y);
        px(x, y, ink);
      }
      else if (d <= core + 2.0f)
      {
        px(x, y, darkSky ? W_ABS : R);
      }
    }
  }
}

/* Variant 3C full-bleed top sky band (y = 0..347, seam at y = 348..349).
 * Maps every OWM/NWS Kind (both day and night) to a tuned Spectra 6 sky mood
 * and sets s_skyDark so foreground ink, horizon arc, city/date, and alert
 * trims adapt automatically.
 */
static void drawSkyBand3C(const owm_current_t &current, bool day)
{
  const Kind k = kindOf(current.weather.id, current.clouds, day, 1.0f, false);
  enum {
    SKY_SUNNY, SKY_PARTLY, SKY_OVERCAST, SKY_FOG, SKY_RAIN, SKY_TSTORM, SKY_SNOW,
    SKY_NIGHT_STAR, SKY_NIGHT_PARTLY, SKY_NIGHT_CLOUD, SKY_NIGHT_FOG,
    SKY_NIGHT_RAIN, SKY_NIGHT_TSTORM, SKY_NIGHT_SNOW
  };
  if (!day)
  {
    switch (k)
    {
    case K_CLEAR: case K_MCLEAR:   s_skyMood = SKY_NIGHT_STAR;   break;
    case K_PARTLY:                 s_skyMood = SKY_NIGHT_PARTLY; break;
    case K_MCLOUDY: case K_CLOUDY: s_skyMood = SKY_NIGHT_CLOUD;  break;
    case K_FOG:                    s_skyMood = SKY_NIGHT_FOG;    break;
    case K_TSTORM:                 s_skyMood = SKY_NIGHT_TSTORM; break;
    case K_SNOW: case K_ICE:       s_skyMood = SKY_NIGHT_SNOW;   break;
    default:                       s_skyMood = SKY_NIGHT_RAIN;   break;
    }
    s_skyDark = true;
  }
  else
  {
    switch (k)
    {
    case K_SUNNY: case K_MSUNNY:   s_skyMood = SKY_SUNNY;        break;
    case K_PARTLY:                 s_skyMood = SKY_PARTLY;       break;
    case K_FOG:                    s_skyMood = SKY_FOG;          break;
    case K_SHOWERS: case K_RAIN:   s_skyMood = SKY_RAIN;         break;
    case K_TSTORM:                 s_skyMood = SKY_TSTORM;       break;
    case K_SNOW: case K_ICE:       s_skyMood = SKY_SNOW;         break;
    default:                       s_skyMood = SKY_OVERCAST;     break;
    }
    s_skyDark = (s_skyMood == SKY_RAIN || s_skyMood == SKY_TSTORM || s_skyMood == SKY_SNOW);
  }
  s_skySoftSub = (s_skyMood == SKY_SUNNY || s_skyMood == SKY_PARTLY);

  const int skyH = 348;
  for (int y = 0; y < skyH; ++y)
  {
    const float t = y / static_cast<float>(skyH - 1);
    Tone rowTone = {W_ABS, 1.0f, W_ABS, 1.0f};
    switch (s_skyMood)
    {
    case SKY_SUNNY:
      if (y < 188)
      {
        const float u = y / 187.0f;
        rowTone = {B, 0.36f * (1.0f - u), W_ABS, 1.0f};
      }
      else
      {
        const float u = (y - 188) / static_cast<float>(skyH - 1 - 188);
        rowTone = {Y, 0.42f * u, W_ABS, 1.0f};
      }
      break;
    case SKY_PARTLY:
      if (y < 236)
      {
        const float u = y / 235.0f;
        rowTone = {B, 0.04f + 0.24f * (1.0f - u), W_ABS, 1.0f};
      }
      else
      {
        const float u = (y - 236) / static_cast<float>(skyH - 1 - 236);
        rowTone = {Y, 0.16f * u, W_ABS, 1.0f};
      }
      break;
    case SKY_OVERCAST:
      rowTone = {K_ABS, 0.26f - 0.16f * t, W_ABS, 1.0f};
      break;
    case SKY_FOG:
      rowTone = {K_ABS, 0.06f + 0.22f * t, W_ABS, 1.0f};
      break;
    case SKY_RAIN:
      rowTone = {K_ABS, 0.38f - 0.26f * t, B, 1.0f};
      break;
    case SKY_TSTORM: case SKY_NIGHT_TSTORM:
      rowTone = {K_ABS, 0.64f - 0.36f * t, B, 1.0f};
      break;
    case SKY_SNOW:
      rowTone = {B, 0.94f - 0.22f * t, W_ABS, 1.0f};
      break;
    case SKY_NIGHT_PARTLY:
      rowTone = {K_ABS, 0.62f - 0.36f * t, B, 1.0f};
      break;
    case SKY_NIGHT_CLOUD: case SKY_NIGHT_FOG:
      rowTone = {K_ABS, 0.70f - 0.26f * t, B, 1.0f};
      break;
    default: // SKY_NIGHT_STAR, SKY_NIGHT_RAIN, SKY_NIGHT_SNOW
      rowTone = {K_ABS, 0.68f - 0.34f * t, B, 1.0f};
      break;
    }
#if defined(EPD709_HOST)
    g_hostTone = hostKey(rowTone, false);
#endif
    for (int x = 0; x < DISP_WIDTH; ++x)
    {
      px(x, y, toneAt(rowTone, x, y));
    }
  }
#if defined(EPD709_HOST)
  g_hostTone = 0;
#endif
  // Crisp 2 px horizon seam separating atmospheric sky from paper-white data deck
  fillBox(0, skyH, DISP_WIDTH - 1, skyH + 1, 0, K_ABS);

  auto skyOpen = [&](int x, int y) -> bool {
    if (x < 8 || x >= DISP_WIDTH - 8 || y < 6 || y >= skyH - 6) return false;
    if (x <= 576 && y >= 266) return false;
    if (x >= 596 && x <= 1012 && y >= 152) return false;
    if (x >= 1024) return false;
    return true;
  };

  if (s_skyMood == SKY_RAIN || s_skyMood == SKY_TSTORM
      || s_skyMood == SKY_NIGHT_RAIN || s_skyMood == SKY_NIGHT_TSTORM)
  { // Diagonal broken rain streaks across open sky; stormier for T-storm
    const int cols = (s_skyMood == SKY_TSTORM || s_skyMood == SKY_NIGHT_TSTORM) ? 40 : 30;
    const int stepX = (DISP_WIDTH - 32) / cols;
    for (int col = 0; col < cols; ++col)
    {
      for (int band = 0; band < 4; ++band)
      {
        const uint32_t h = hash2(col, band + 17);
        const int xStart = 20 + col * stepX + static_cast<int>(h % std::max(12, stepX - 6));
        const int yStart = 8 + band * 84 + static_cast<int>((h >> 8) % 36);
        const int len = 26 + static_cast<int>((h >> 16) % 28);
        for (int dy = 0; dy < len; ++dy)
        {
          if ((dy % 5) == 4) continue;
          const int yy = yStart + dy;
          const int xx = xStart - (dy / 2);
          if (skyOpen(xx, yy))
          {
            px(xx, yy, W_ABS);
          }
        }
      }
    }
    if (s_skyMood == SKY_TSTORM || s_skyMood == SKY_NIGHT_TSTORM)
    { // Subtle distant yellow lightning glint in open upper sky between hero temp and sun arc
      stroke(574, 24, 560, 56, 2, Y);
      stroke(560, 56, 576, 56, 2, Y);
      stroke(576, 56, 558, 94, 2, Y);
    }
  }
  else if (s_skyMood == SKY_SNOW || s_skyMood == SKY_NIGHT_SNOW)
  { // Delicate 282 ppi crystal flakes & crisp falling snow specks
    for (int gy = 0; gy < 8; ++gy)
    {
      for (int gx = 0; gx < 32; ++gx)
      {
        const uint32_t h = hash2(gx + 51, gy + 211);
        if ((h % 3u) == 2u) continue;
        const int cx = 24 + gx * 48 + static_cast<int>((h >> 4) % 32);
        const int cy = 14 + gy * 40 + static_cast<int>((h >> 12) % 26);
        if (!skyOpen(cx, cy)) continue;
        if ((h >> 18) % 3u == 0u)
        { // 6-point crystal star (r = 5 px)
          for (int d = -4; d <= 4; ++d)
          {
            if (skyOpen(cx + d, cy)) px(cx + d, cy, W_ABS);
            const int dy = (d * 3) / 4;
            if (skyOpen(cx + d / 2, cy + dy)) px(cx + d / 2, cy + dy, W_ABS);
            if (skyOpen(cx + d / 2, cy - dy)) px(cx + d / 2, cy - dy, W_ABS);
          }
        }
        else
        { // Crisp round snow dot (2 px)
          for (int dy = 0; dy <= 1; ++dy)
            for (int dx = 0; dx <= 1; ++dx)
              if (skyOpen(cx + dx, cy + dy)) px(cx + dx, cy + dy, W_ABS);
        }
      }
    }
  }
  else if (s_skyMood == SKY_FOG || s_skyMood == SKY_NIGHT_FOG)
  { // Soft horizontal pale mist filaments drifting near lower horizon
    for (int band = 0; band < 4; ++band)
    {
      const uint32_t h = hash2(band + 7, 91);
      const int y0 = 216 + band * 28 + static_cast<int>(h % 10);
      for (int seg = 0; seg < 6; ++seg)
      {
        const uint32_t sh = hash2(seg + 13, band + 41);
        const int xs = 36 + seg * 168 + static_cast<int>(sh % 48);
        const int len = 76 + static_cast<int>((sh >> 8) % 58);
        for (int dx = 0; dx < len; ++dx)
        {
          const int xx = xs + dx;
          for (int dy = 0; dy <= 1; ++dy)
          {
            if (((xx + dy * 3) & 3) != 0 && skyOpen(xx, y0 + dy))
            {
              px(xx, y0 + dy, W_ABS);
            }
          }
        }
      }
    }
  }
  else if (s_skyMood == SKY_NIGHT_STAR || s_skyMood == SKY_NIGHT_PARTLY)
  { // Scattered midnight starfield (sparser on partly cloudy nights; never behind text)
    const uint32_t mask = (s_skyMood == SKY_NIGHT_PARTLY) ? 7u : 3u;
    for (int gy = 0; gy < 9; ++gy)
    {
      for (int gx = 0; gx < 38; ++gx)
      {
        const uint32_t h = hash2(gx + 101, gy + 313);
        if ((h & mask) != 0u) continue;
        const int sx = 18 + gx * 42 + static_cast<int>((h >> 4) % 30);
        const int sy = 10 + gy * 33 + static_cast<int>((h >> 12) % 24);
        if (!skyOpen(sx, sy)) continue;
        const uint16_t ink = ((h >> 20) % 7 == 0) ? Y : W_ABS;
        px(sx, sy, ink);
        if ((h >> 24) % 5 == 0)
        {
          px(sx + 1, sy, ink);
          px(sx, sy + 1, ink);
          px(sx + 1, sy + 1, ink);
        }
      }
    }
  }
}

/* Center celestial horizon arc (x = 604..994, inside the sky band). */
static void drawSky(const owm_current_t &current)
{
  const int cx = 800, xL = 612, xR = 988, hy = 204, rx = 164, ry = 116;
  const time_t now = nowUtc();
  const uint16_t fgInk = s_skyDark ? W_ABS : K_ABS;
  auto onArc = [&](float t, float &x, float &y)
  {
    x = cx - rx * cosf(static_cast<float>(M_PI) * t);
    y = hy - ry * sinf(static_cast<float>(M_PI) * t);
  };
  auto dotted = [&](float fromFrac, float toFrac, float ox, float oy, float keep)
  {
    const int i0 = static_cast<int>(ceilf(fromFrac * 100.0f));
    const int i1 = static_cast<int>(floorf(toFrac * 100.0f));
    for (int i = i0; i <= i1; i += 3)
    {
      float x, y;
      onArc(i / 100.0f, x, y);
      if (hypotf(x - ox, y - oy) > keep)
      {
        fillBox(static_cast<int>(x) - 1, static_cast<int>(y) - 1,
                static_cast<int>(x) + 1, static_cast<int>(y) + 1, 0, fgInk);
      }
    }
  };

  fillBox(xL, hy, xR, hy + 1, 0, fgInk);

  int lit = 0;
  const int phase16 = calcMoonPhase(static_cast<int64_t>(now), &lit);
  const double age = fmod((static_cast<double>(now) - 947182440.0) / 86400.0,
                          29.530588853);
  const double mfrac = (age < 0 ? age + 29.530588853 : age) / 29.530588853;
  const int phase = static_cast<int>(lround(mfrac * MOON709_STEPS)) % MOON709_STEPS;
  const String moonDesc = String(lit) + "% " + label(getMoonPhaseDesc(phase16));

  int64_t rise = current.sunrise, set = current.sunset;
  if (rise > 0 && set > rise)
  {
    const bool night = now < rise || now >= set;
    if (!night)
    {
      const float f = std::max(0.02f, std::min(0.98f,
                        static_cast<float>(now - rise) / (set - rise)));
      float sx, sy;
      onArc(f, sx, sy);
      float px0 = 0, py0 = 0;
      const int steps = std::max(1, static_cast<int>(f * 180));
      for (int i = 0; i <= steps; ++i)
      {
        float x, y;
        onArc(f * i / steps, x, y);
        if (i && hypotf(x - sx, y - sy) > 23.0f && hypotf(px0 - sx, py0 - sy) > 23.0f)
        {
          if (s_skyDark)
          {
            stroke(px0, py0, x, y, 4, Y);
          }
          else
          {
            // Deep orange-red arc for sunlit readability over warm gold horizon
            stroke(px0, py0, x, y, 4, (i & 1) ? R : Y);
          }
        }
        px0 = x;
        py0 = y;
      }
      dotted(f + 0.01f, 1.0f, sx, sy, 25.0f);
      drawSkySun3C(static_cast<int>(sx), static_cast<int>(sy), s_skyDark);

      const String daySpan = span(static_cast<long>(set - rise));
      drawText(cx, hy - 36, daySpan, F_SANS_25, fgInk, MA, s_skySoftSub);
      drawText(xL, hy + 10, clock(static_cast<time_t>(rise), TIME_FORMAT), F_SANS_24, fgInk, LA);
      drawText(xR, hy + 10, clock(static_cast<time_t>(set), TIME_FORMAT), F_SANS_24, fgInk, RA);

      // Center moon + label cleanly below horizon (y = 260..332)
      const int totalW = MOON709_PX + 12 + textWidth(moonDesc, F_SANS_24);
      const int mx = cx - totalW / 2 + MOON709_PX / 2;
      drawMoon(mx, 294, phase);
      drawText(mx + MOON709_PX / 2 + 12, 294, moonDesc, F_SANS_24, fgInk, LM, s_skySoftSub);
    }
    else
    {
      int64_t prevSet = set;
      if (now >= set)
      {
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
      else
      {
        prevSet = set - 86400;
      }
      const float nf = (rise > prevSet)
          ? std::max(0.18f, std::min(0.82f, static_cast<float>(now - prevSet) / (rise - prevSet)))
          : 0.50f;
      float mx, my;
      onArc(nf, mx, my);
      dotted(0.0f, 1.0f, mx, my, 42.0f);
      drawMoon(static_cast<int>(mx), static_cast<int>(my), phase);

      drawText(cx, hy - 34, moonDesc, F_SANS_24, fgInk, MA);
      drawText(xL, hy + 10, String("Set ") + clockShort(static_cast<time_t>(prevSet)),
               F_SANS_24, fgInk, LA);
      drawText(xR, hy + 10, String("Rise ") + clockShort(static_cast<time_t>(rise)),
               F_SANS_24, fgInk, RA);
      drawText(cx, 286, String(TXT709_SUNRISE_IN " ") + span(static_cast<long>(rise - now)),
               F_SANS_25, fgInk, MA);
    }
  }
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

  drawSkyBand3C(current, day);

  const bool isFogIcon = (current.weather.id >= 700 && current.weather.id < 800
                          && current.weather.id != 771 && current.weather.id != 781);
  drawIconOnSky(conditionIcon(current.weather.id, day, 220), 220, 24, 30,
                s_skyDark, isFogIcon);
  const String temp = tempText(current.temp);
  const uint16_t heroInk = s_skyDark ? W_ABS : K_ABS;
  drawText(256, 40, temp,
           fitFont(temp, 326, {&F_BITTER_190D, &F_BITTER_150D}),
           heroInk, LA);
  const String kind = TXT709_KIND[kindOf(current.weather.id, current.clouds, day, 1.0f, false)];
  const String sub = label(TXT_FEELS_LIKE) + " " + tempText(current.feels_like)
                   + "  \xB7  " + kind;
  drawText(34, 284, sub,
           fitFont(sub, 540, {&F_SANS_32, &F_SANS_28, &F_SANS_24}),
           heroInk, LA, s_skySoftSub);

  drawSky(current);

  // Lower deck structural dividers; 4x2 readings grid sits cleanly below enlarged 24h matrix
  dashV(904, 368, 1136, 1, 2, 6, K);
  dashH(32, 884, 956, 1, 2, 6, K);

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

  // air quality: AirNow's official figure when there is one, Israel's
  // Ministry index when configured/available, otherwise Open-Meteo's model
  const bool epa = (air.us_aqi >= 0);
  const bool useIL = !epa && air.il_valid;
  const bool airKnown = epa || useIL || air.valid;
  if (useIL)
  {
    const int il = air.il_index;
    const int band = (il > 50) ? 0 : (il >= 0) ? 1 : (il >= -200) ? 2 : 3;
    static const int IL_BAND[4] = {RISK_GREEN, RISK_YELLOW, RISK_RED,
                                   RISK_MAROON};
    cells[CK_AIR].label = label(TXT_AIR_QUALITY);
    cells[CK_AIR].tag   = TXT_IL_AQI_TAG;
    cells[CK_AIR].value = String(il);
    cells[CK_AIR].word  = TXT_IL_AQI[band];
    cells[CK_AIR].brief = TXT_IL_AQI[band];
    cells[CK_AIR].level = IL_BAND[band];
  }
  else
  {
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
               + " \xB7 "
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
} // end drawCurrentConditions

void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft,
                   bool stale)
{
  const int maxW = 1536;
  if (stale)
  { // the weather on the page is old: when it is from, in red, and why
    const String when = String(TXT709_LAST_UPDATED " ") + refreshTimeStr;
    String s = when + TXT709_DOT + statusStr;
    if (statusStr.isEmpty() || textWidth(s, F_SANS_24) > maxW)
    {
      s = when;
    }
    drawText(1568, FOOTER_Y, s, F_SANS_24, R, RA);
    return;
  }
  if (!statusStr.isEmpty())
  { // something went wrong this wake: say so, in the place of the rest
    String s = statusStr + TXT709_DOT + TXT709_UPDATED " " + refreshTimeStr;
    if (textWidth(s, F_SANS_24) > maxW)
    {
      s = statusStr;
    }
    drawText(1568, FOOTER_Y, s, F_SANS_24, R, RA);
    return;
  }
  String s = String(TXT709_UPDATED " ") + refreshTimeStr;
#if BATTERY_MONITORING
  const uint32_t percent = calcBatPercent(batVoltage, MIN_BATTERY_VOLTAGE,
                                          MAX_BATTERY_VOLTAGE);
  const String bat = String(TXT709_DOT) + TXT709_BATTERY " " + String(percent)
                   + "%";
  const String daysLeft = (batDaysLeft >= 0)
      ? ", " + String(batDaysLeft) + " " TXT709_DAYS_LEFT : String();
  if (textWidth(s + bat + daysLeft, F_SANS_24) > maxW)
  { // too long (a refresh time with a date leaves no room for the days
    // left): the time alone, as the date is the page's own, in its header
    s = String(TXT709_UPDATED " ") + clock(nowUtc(), TIME_FORMAT);
  }
  // still too long: the days left go, never the battery itself
  if (textWidth(s + bat + daysLeft, F_SANS_24) <= maxW)
  {
    s += bat + daysLeft;
  }
  else if (textWidth(s + bat, F_SANS_24) <= maxW)
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
    if (textWidth(s + wifi, F_SANS_24) <= maxW)
    {
      s += wifi;
    }
  }
  const bool low = BATTERY_MONITORING && batVoltage < WARN_BATTERY_VOLTAGE;
  drawText(1568, FOOTER_Y, s, F_SANS_24, low ? R : K, RA, !low);
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

void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  drawText(924, 368, String(TXT709_THIS_WEEK) + " \x7F Daily breakdown",
           F_BITTER_34, K, LA);
  const int days = std::min(std::max(FORECAST_DAYS, 5), OWM_NUM_DAILY);
  float weekLo = 1e9f, weekHi = -1e9f;
  for (int i = 0; i < days; ++i)
  {
    if (daily[i].dt > 0 && !std::isnan(daily[i].temp.min)
        && !std::isnan(daily[i].temp.max))
    {
      weekLo = std::min(weekLo, toUnitsF(daily[i].temp.min));
      weekHi = std::max(weekHi, toUnitsF(daily[i].temp.max));
    }
  }
  if (weekLo > weekHi)
  {
    weekLo = 58.0f;
    weekHi = 62.0f;
  }
  else if (weekHi - weekLo < 4.0f)
  {
    const float mid = (weekLo + weekHi) / 2.0f;
    weekLo = mid - 2.0f;
    weekHi = mid + 2.0f;
  }

  const int y0 = 418;
  const int rowH = 102;
  const int trackX0 = 1266, trackX1 = 1408;

  for (int i = 0; i < days; ++i)
  {
    const int ry = y0 + i * rowH;
    if (i == 0)
    {
      fillTone(924, ry + 2, 1568, ry + rowH - 5, 8, {K, 0.06f, W, 1.0f});
    }
    else
    {
      dashH(924, 1568, ry - 1, 1, 2, 6, K);
    }

    const char *name = (i == 0) ? TXT709_TODAY
                                : LC_ABDAY[(timeInfo.tm_wday + i) % 7];
    drawText(934, ry + 18, name, F_BITTER_32, K, LA);

    if (daily[i].dt <= 0 || std::isnan(daily[i].temp.max)
        || std::isnan(daily[i].temp.min))
    {
      drawText(1150, ry + 50, "--", F_BITTER_32, K, MM, true);
      continue;
    }

    const int id = tellingId(daily[i]);
    const String kind = TXT709_KIND[kindOf(id, daily[i].clouds, true, daily[i].pop, false)];
    drawText(934, ry + 58, kind,
             fitFont(kind, 170, {&F_SANS_23, &F_SANS_22}), K, LA, true);

    drawIconScaled(conditionIcon(id, true, 104), 104, 78, 1110, ry + 11);

    // Aligned min/max temperature columns bracketing the full-week range track
    drawText(trackX0 - 10, ry + 50, tempText(daily[i].temp.min), F_BITTER_28, dewInk(), RM);
    drawText(trackX1 + 10, ry + 50, tempText(daily[i].temp.max), F_BITTER_28, tempInk(), LM);

    const float dLo = toUnitsF(daily[i].temp.min);
    const float dHi = toUnitsF(daily[i].temp.max);
    int px0 = trackX0 + static_cast<int>(lroundf((trackX1 - trackX0)
                        * (dLo - weekLo) / (weekHi - weekLo)));
    int px1 = trackX0 + static_cast<int>(lroundf((trackX1 - trackX0)
                        * (dHi - weekLo) / (weekHi - weekLo)));
    if (px1 - px0 < 20)
    {
      const int mid = (px0 + px1) / 2;
      px0 = mid - 10;
      px1 = mid + 10;
    }
    px0 = std::max(trackX0, std::min(trackX1 - 20, px0));
    px1 = std::max(px0 + 20, std::min(trackX1, px1));

    fillTone(trackX0, ry + 45, trackX1, ry + 55, 5, {K, 0.10f, W, 1.0f});
    const int py0 = ry + 43, py1 = ry + 57, pr = 7;
    for (int x = px0; x <= px1; ++x)
    {
      const float u = std::max(0.0f, std::min(1.0f,
                        (x - trackX0) / static_cast<float>(trackX1 - trackX0)));
      Tone pt;
      if (u <= 0.28f)
      {
        pt = {G, u / 0.28f, B, 1.0f};
      }
      else if (u <= 0.54f)
      {
        pt = {Y, (u - 0.28f) / 0.26f, G, 1.0f};
      }
      else
      {
        pt = {R, (u - 0.54f) / 0.46f, Y, 1.0f};
      }
#if defined(EPD709_HOST)
      g_hostTone = hostKey(pt, false);
#endif
      for (int y = py0; y <= py1; ++y)
      {
        if (inRound(x, y, px0, py0, px1, py1, pr))
        {
          px(x, y, toneAt(pt, x, y));
        }
      }
    }
#if defined(EPD709_HOST)
    g_hostTone = 0;
#endif

#if DISPLAY_DAILY_PRECIP
    if (daily[i].pop >= 0.295f)
    {
      const String popStr = String(static_cast<int>(lroundf(daily[i].pop * 100))) + "%";
      const String amount = amountText(daily[i]);
      if (amount.length())
      {
        drawText(1566, ry + 26, popStr, F_SANS_26, dewInk(), RA);
        drawText(1566, ry + 60, amount, F_SANS_22, dewInk(), RA);
      }
      else
      {
        drawText(1566, ry + 50, popStr, F_SANS_26, dewInk(), RA);
      }
    }
#endif
  }
} // end drawForecast

/* =========================================================================
 * Lower-left: 24-hour editorial matrix
 * ====================================================================== */

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

#if defined(UNITS_DAILY_PRECIP_INCHES) \
    || (defined(UNITS_DAILY_PRECIP_POP) && defined(UNITS_TEMP_FAHRENHEIT))
static float amountOf(float mm) { return millimeters_to_inches(mm); }
static const int AMOUNT_DECIMALS = 2;
static const float AMOUNT_MIN = 0.004f;
static String formatHourlyAmount(float v)
{
  String s = String(v, AMOUNT_DECIMALS);
  if (s.startsWith("0.")) s = s.substring(1);
  return s + "\"";
}
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
static float amountOf(float mm) { return millimeters_to_centimeters(mm); }
static const int AMOUNT_DECIMALS = 1;
static const float AMOUNT_MIN = 0.01f;
static String formatHourlyAmount(float v)
{
  String s = String(v, AMOUNT_DECIMALS);
  if (s.startsWith("0.")) s = s.substring(1);
  return s + " " + TXT_UNITS_PRECIP_CENTIMETERS;
}
#else
static float amountOf(float mm) { return mm; }
static const int AMOUNT_DECIMALS = 0;
static const float AMOUNT_MIN = 0.1f;
static String formatHourlyAmount(float v)
{
  return String(v, AMOUNT_DECIMALS) + " " + TXT_UNITS_PRECIP_MILLIMETERS;
}
#endif

struct MatrixFrame
{
  int x0, x1, y0, y1, n;
  time_t start;
  float lo, hi;
  float Y(float v) const
  {
    const float span = (hi > lo) ? (hi - lo) : 1.0f;
    return y1 - (y1 - y0) * (v - lo) / span;
  }
};

// Pick the finest clean degree interval fitting 2..5 Y-axis steps across spanPx
static int niceTempAxis(float tLo, float tHi, float spanPx, int &axisLo, int &axisHi)
{
#if !defined(UNITS_TEMP_CELSIUS) && !defined(UNITS_TEMP_KELVIN)
  static const int kSteps[] = {2, 5, 10, 15, 20};
#else
  static const int kSteps[] = {1, 2, 5, 10};
#endif
  const int nSteps = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
  for (int i = 0; i < nSteps; ++i)
  {
    const int s = kSteps[i];
    const int lo = static_cast<int>(floorf((tLo + 1e-4f) / s)) * s;
    const int hi = static_cast<int>(ceilf((tHi - 1e-4f) / s)) * s;
    const int intervals = std::max(1, (hi - lo) / s);
    if (intervals >= 2 && intervals <= 5 && (spanPx / intervals) >= 30.0f)
    {
      axisLo = lo;
      axisHi = hi;
      return s;
    }
  }
  int s = kSteps[nSteps - 1];
  int lo = static_cast<int>(floorf((tLo + 1e-4f) / s)) * s;
  int hi = static_cast<int>(ceilf((tHi - 1e-4f) / s)) * s;
  while ((hi - lo) / s > 5)
  {
    s *= 2;
    lo = static_cast<int>(floorf((tLo + 1e-4f) / s)) * s;
    hi = static_cast<int>(ceilf((tHi - 1e-4f) / s)) * s;
  }
  if (hi <= lo) hi = lo + s;
  axisLo = lo;
  axisHi = hi;
  return s;
}

static void drawHourlyMatrix(const owm_hourly_t *hourly, int n, int gy)
{
  char heading[32];
  snprintf(heading, sizeof(heading), TXT709_NEXT_HOURS, n);
  drawText(32, gy, heading, F_BITTER_34, K, LA);

  const time_t start = static_cast<time_t>(hourly[0].dt);
  const int cols = 8;
  const int colW = 94;
  const int xStart = 134;
  const int stepH = std::max(1, n / cols);
  const int pts = std::min(n, (cols - 1) * stepH + 1);
  const int cm = std::max(0, std::min(2, GRAPH_CURVE)); // 0=Dew, 1=Feels, 2=Temp only

  int idxs[8];
  float vt[OWM_NUM_HOURLY], vd[OWM_NUM_HOURLY], vf[OWM_NUM_HOURLY];
  float tLo = 1e9f, tHi = -1e9f;
  for (int i = 0; i < pts; ++i)
  {
    vt[i] = std::isnan(hourly[i].temp) ? (i > 0 ? vt[i - 1] : 60.0f) : toUnitsF(hourly[i].temp);
    vd[i] = std::isnan(hourly[i].dew_point) ? vt[i] : toUnitsF(hourly[i].dew_point);
    vf[i] = std::isnan(hourly[i].feels_like) ? vt[i] : toUnitsF(hourly[i].feels_like);
    tLo = std::min(tLo, (cm == 0) ? std::min(vt[i], vd[i])
                      : (cm == 1) ? std::min(vt[i], vf[i])
                                  : vt[i]);
    tHi = std::max(tHi, (cm == 0) ? std::max(vt[i], vd[i])
                      : (cm == 1) ? std::max(vt[i], vf[i])
                                  : vt[i]);
  }
  if (tLo > tHi) { tLo = 58.0f; tHi = 62.0f; }
  if (tHi - tLo < 4.0f) { const float m = (tHi + tLo) / 2.0f; tLo = m - 2.0f; tHi = m + 2.0f; }

  // Right-aligned inline legend matching heading; omitted cleanly for Temp only
  const int ly = gy + 18;
  const int gRight = xStart + cols * colW;
  if (cm != 2)
  {
    const char *lbl1 = "Temp";
    const char *lbl2 = (cm == 1) ? "Feels" : "Dew";
    const int sw = 24, gapSw = 8, gapMid = 22;
    const int totalW = sw + gapSw + textWidth(lbl1, F_SANS_22)
                     + gapMid + sw + gapSw + textWidth(lbl2, F_SANS_22);
    int lx = gRight - totalW;
    fillBox(lx, ly - 2, lx + sw, ly + 2, 0, tempInk());
    lx += sw + gapSw;
    lx = drawText(lx, ly, lbl1, F_SANS_22, K, LM, true) + gapMid;
    if (cm == 1)
    {
      fillBox(lx, ly - 2, lx + 9, ly + 2, 0, tempInk());
      fillBox(lx + 14, ly - 2, lx + 23, ly + 2, 0, tempInk());
    }
    else
    {
      fillBox(lx, ly - 2, lx + sw, ly + 2, 0, dewInk());
    }
    drawText(lx + sw + gapSw, ly, lbl2, F_SANS_22, K, LM, true);
  }

  const int washBot = 948;
  int midCol = -1;
  String midDayName;
  for (int c = 0; c < cols; ++c)
  {
    const int idx = std::min(n - 1, c * stepH);
    idxs[c] = idx;
    const time_t tCur = static_cast<time_t>(hourly[idx].dt);
    tm tmCur;
    localtime_r(&tCur, &tmCur);
    if (c > 0 && midCol < 0)
    {
      const time_t tPrev = static_cast<time_t>(hourly[idxs[c - 1]].dt);
      tm tmPrev;
      localtime_r(&tPrev, &tmPrev);
      if (tmCur.tm_yday != tmPrev.tm_yday)
      {
        midCol = c;
        midDayName = LC_ABDAY[tmCur.tm_wday];
      }
    }
    const int cx0 = xStart + c * colW;
    const int cx = cx0 + colW / 2;
    const bool day = daylightAt(hourly[idx].dt + 1800,
                                hourly[idx].weather.icon.endsWith("d"));
    if (!day)
    {
      fillTone(cx0, gy + 44, cx0 + colW - 1, washBot, 0, {B, 0.05f, W, 1.0f});
    }
    if (c == midCol)
    {
      dashV(cx0, gy + 44, washBot, 1, 3, 5, K);
    }

    String hLbl = clock(tCur, HOUR_FORMAT);
    hLbl.replace(" ", "");
    String shortH = hLbl;
    if (shortH.endsWith("m")) shortH = shortH.substring(0, shortH.length() - 1);
    if (c == midCol)
    {
      drawText(cx, gy + 48, midDayName + " " + shortH, F_BITTER_23, K, MA);
    }
    else
    {
      drawText(cx, gy + 48, hLbl, F_SANS_23, K, MA);
    }

    drawIconScaled(conditionIcon(hourly[idx].weather.id, day, 104), 104, 54,
                   cx - 27, gy + 80);
  }

  const int curveTop = 526;
  const int curveBot = 714;
  const int yDiv1    = 724;
  const int yDiv2    = 844;

  dashH(xStart, gRight, yDiv1, 1, 2, 6, K);
  dashH(xStart, gRight, yDiv2, 1, 2, 6, K);

  const int ftTop = curveTop + 28;
  const int ftBot = curveBot - 10;
  int axisLo = 0, axisHi = 0;
  const int axisStep = niceTempAxis(tLo, tHi, static_cast<float>(ftBot - ftTop), axisLo, axisHi);

  MatrixFrame ft = {xStart + colW / 2, xStart + (cols - 1) * colW + colW / 2,
                    ftTop, ftBot, (cols - 1) * stepH, start,
                    static_cast<float>(axisLo), static_cast<float>(axisHi)};

  // Neutral Silver Ribbon highlight between primary and secondary curves (or axis floor)
  {
    const float *secArr = (cm == 1) ? vf : vd;
    for (int px = ft.x0; px <= ft.x1; ++px)
    {
      const float f = std::max(0.0f, std::min(static_cast<float>(pts - 1),
                        (px - ft.x0) * static_cast<float>(stepH) / colW));
      const float yt = ft.Y(smoothAt(vt, pts, f));
      const float yb = (cm == 2) ? static_cast<float>(ft.y1)
                                 : ft.Y(smoothAt(secArr, pts, f));
      fillTone(px, static_cast<int>(std::min(yt, yb)),
               px, static_cast<int>(std::max(yt, yb)), 0, {K, 0.07f, W, 1.0f});
    }
  }

  // Dynamic Y-axis temperature ticks + subtle dotted gridlines + left row labels
  for (int tv = axisLo; tv <= axisHi; tv += axisStep)
  {
    const int yTick = static_cast<int>(lroundf(ft.Y(static_cast<float>(tv))));
    dashH(xStart, gRight, yTick, 1, 1, 7, K);
    drawText(xStart - 10, yTick, String(tv) + "\xB0", F_SANS_22, K, RM, true);
  }
  drawText(xStart - 10, 748, "Clouds",   F_SANS_22, K, RM, true);
  drawText(xStart - 10, 786, "Rain %",   F_SANS_22, K, RM, true);
  drawText(xStart - 10, 808, "Amount",   F_SANS_22, K, RA, true);
  drawText(xStart - 10, 864, "Wind dir", F_SANS_22, K, RM, true);
  drawText(xStart - 10, 888, "Speed",    F_SANS_22, K, RA, true);
  drawText(xStart - 10, 918, "Gusts",    F_SANS_22, K, RA, true);

  // Secondary curve (Dew point or Feels like when cm != 2) then primary Temp curve
  for (int pass = 0; pass < 2; ++pass)
  {
    const int wSec = pass ? 3 : 7;
    const int wPri = pass ? 5 : 10;
    const float steps = 6.0f;
    for (int k = 1; k <= (pts - 1) * static_cast<int>(steps); ++k)
    {
      const float f0 = (k - 1) / steps, f1 = k / steps;
      const float pxA = ft.x0 + (f0 / stepH) * colW;
      const float pxB = ft.x0 + (f1 / stepH) * colW;
      if (cm != 2)
      {
        const float *secArr = (cm == 1) ? vf : vd;
        if (!(cm == 1 && (k - 1) % 5 >= 3))
        {
          stroke(pxA, ft.Y(smoothAt(secArr, pts, f0)),
                 pxB, ft.Y(smoothAt(secArr, pts, f1)), wSec,
                 pass ? (cm == 1 ? tempInk() : dewInk()) : W);
        }
      }
      stroke(pxA, ft.Y(smoothAt(vt, pts, f0)),
             pxB, ft.Y(smoothAt(vt, pts, f1)), wPri, pass ? tempInk() : W);
    }
  }

  for (int c = 0; c < cols; ++c)
  {
    const int idx = std::min(pts - 1, idxs[c]);
    const float cx = static_cast<float>(xStart + c * colW + colW / 2);
    const float yt = ft.Y(vt[idx]);
    float ys = yt;
    if (cm != 2)
    {
      ys = ft.Y((cm == 1) ? vf[idx] : vd[idx]);
      disc(cx, ys, 5.5f, W);
      disc(cx, ys, 3.5f, (cm == 1) ? tempInk() : dewInk());
    }
    disc(cx, yt, 7.5f, W);
    disc(cx, yt, 5.0f, tempInk());
    const bool flipBelow = (cm != 2 && ys < yt - 4.0f && yt + 32.0f < yDiv1);
    const String tLbl = String(static_cast<int>(lroundf(vt[idx]))) + "\xB0";
    drawText(static_cast<int>(cx), static_cast<int>(yt) + (flipBelow ? 10 : -10),
             tLbl, F_SANS_24, K, flipBelow ? MA : MD);
  }

  // Option D sky/rain band (Clouds bar + % -> Rain % -> Amount) + Wind band
  for (int c = 0; c < cols; ++c)
  {
    const int idx = idxs[c];
    const int cx = xStart + c * colW + colW / 2;

    fillTone(cx - 28, 732, cx + 28, 738, 3, {K, 0.12f, W, 1.0f});
    if (hourly[idx].clouds >= 0)
    {
      const int cld = std::max(0, std::min(100, ((hourly[idx].clouds + 2) / 5) * 5));
      if (cld > 0)
      {
        const int fw = std::max(6, (56 * cld) / 100);
        fillTone(cx - 28, 732, cx - 28 + fw, 738, 3, {K, 0.45f, W, 1.0f});
      }
      drawText(cx, 742, String(cld) + "%", F_SANS_22, K, MA, true);
    }
    else
    {
      drawText(cx, 742, "\x7F", F_SANS_22, K, MA, true);
    }

    const int pct = static_cast<int>(lroundf(hourly[idx].pop * 100.0f / 5.0f)) * 5;
    const float amt = std::isnan(hourly[idx].precip) ? 0.0f : amountOf(hourly[idx].precip);
    const String amtStr = (amt > AMOUNT_MIN) ? formatHourlyAmount(amt) : String();

    const int py = 771, ph = 30, rad = 9;
    if (pct >= 50)
    {
      fillBox(cx - 33, py, cx + 33, py + ph, rad, B);
      drawText(cx, py + ph / 2, String(pct) + "%", F_SANSB_25, W_ABS, MM);
    }
    else if (pct >= 10)
    {
      fillTone(cx - 33, py, cx + 33, py + ph, rad, {B, 0.22f, W, 1.0f});
      drawText(cx, py + ph / 2, String(pct) + "%", F_SANSB_25, dewInk(), MM);
    }
    else
    {
      drawText(cx, py + ph / 2, "\x7F", F_SANS_24, K, MM, true);
    }
    if (amtStr.length())
    {
      drawText(cx, 808, amtStr, F_SANS_22, dewInk(), MA);
    }

    const int sp = static_cast<int>(lroundf(speedOf(hourly[idx].wind_speed)));
    const bool hasGust = !std::isnan(hourly[idx].wind_gust) && hourly[idx].wind_gust > 0.01f;
    const int gs = hasGust ? static_cast<int>(lroundf(speedOf(hourly[idx].wind_gust))) : 0;
    const bool highGust = hasGust
        && lroundf(meterspersecond_to_milesperhour(hourly[idx].wind_gust)) >= 30;
    const uint16_t gustInk = highGust ? R : K;
    const String gustStr = hasGust ? (String(gs) + speedUnit()) : String("\x7F");

    drawWindArrow(static_cast<float>(cx), 864.0f, hourly[idx].wind_deg, 0.72f, K);
    drawText(cx, 888, String(sp) + speedUnit(), F_SANS_22, K, MA);
    drawText(cx, 918, gustStr, F_SANS_22, gustInk, MA, !highGust);
  }
} // end drawHourlyMatrix

void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
  (void)daily; (void)timeInfo;
  const int n = std::min(std::max(HOURLY_GRAPH_MAX, 8), OWM_NUM_HOURLY);
  drawHourlyMatrix(hourly, n, 368);
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
  display.clearCustomLut();
#if !defined(EPD709_HOST) && !defined(EPD709_TEST_CARD)
  if (FAST_REFRESH)
  {
    Preferences store;
    store.begin(NVS_NAMESPACE, true);
    const uint8_t lutCount = store.getUChar("lut709n", 0);
    // BUSY time of the last stock refresh at PLL 08: which stock set the
    // chip really ran (checked against the set read back below)
    const uint32_t stockMs = store.getUInt("stk709ms", 0);
    store.end();
    if (lutCount >= 9)
    {
      Serial.println("[epd709] fast LUT scrub (10th wake), using stock OTP");
      display.setClock(Epd709::CLOCK_NORMAL);
    }
    else if (stockMs == 0)
    { // no stock refresh measured yet: do one, so the next wake can check
      Serial.println("[epd709] no stock refresh time yet, using stock OTP");
      display.setClock(Epd709::CLOCK_NORMAL);
    }
    else
    {
      static uint8_t rawSet[Epd709Lut::TABLE_LEN];
      static uint8_t fastLut[Epd709Lut::TABLE_LEN];
      uint8_t fastPwr[6];
      float tempC = 0.0f;
      int8_t setIdx = display.readTempC(tempC)
                        ? Epd709Lut::setForTempC(tempC) : -1;
      Epd709Lut::BuildInfo info{};
      const bool built = setIdx >= 0 && display.readOtpSet(setIdx, rawSet)
          && Epd709Lut::buildFast709(rawSet, fastLut, fastPwr, true, &info);
      if (built && !Epd709Lut::framesMatchStock(info.stockFrames, stockMs))
      { // the set read back is not the one the chip ran last time (a band
        // change, or a wrong set number): a stock refresh, which also
        // measures the time again for the next wake
        Serial.printf("[epd709] set %d (%u fr) does not match the last stock "
                      "refresh (%u ms), using stock OTP\n", setIdx,
                      info.stockFrames, (unsigned)stockMs);
        display.setClock(Epd709::CLOCK_NORMAL);
      }
      else if (built && display.setCustomLut(fastLut, fastPwr))
      {
        display.setClock(Epd709::CLOCK_NORMAL);
        Serial.printf("[epd709] fast G13 set %d (%.1f C, %u fr, max|q| %.3f)\n",
                      setIdx, tempC, info.fastFrames, info.maxAbsQ);
      }
      else
      {
        Serial.printf("[epd709] fast G13 unavailable (%.1f C, set %d), using PLL 0x04\n",
                      tempC, setIdx);
        display.setClock(Epd709::CLOCK_FAST);
      }
    }
  }
  else
  {
    display.setClock(Epd709::CLOCK_NORMAL);
  }
#else
  display.setClock(FAST_REFRESH ? Epd709::CLOCK_FAST : Epd709::CLOCK_NORMAL);
#endif
  display.setRotation(EPD709_ROTATION);
  display.setFullWindow();
  display.firstPage();
} // end initDisplay

void powerOffDisplay()
{
#if !defined(EPD709_HOST) && !defined(EPD709_TEST_CARD)
  if (FAST_REFRESH)
  {
    if (display.lastRefreshUsedLut())
    {
      Preferences store;
      store.begin(NVS_NAMESPACE, false);
      store.putUChar("lut709n", store.getUChar("lut709n", 0) + 1);
      store.end();
    }
    else if (display.lastRefreshWasStockNormal())
    {
      Preferences store;
      store.begin(NVS_NAMESPACE, false);
      if (store.getUChar("lut709n", 0) != 0)
      {
        store.putUChar("lut709n", 0);
      }
      const uint32_t ms = display.lastRefreshMs();
      const uint32_t was = store.getUInt("stk709ms", 0);
      if (ms > 0 && (was == 0 || ms > was + was / 100 || ms + was / 100 < was))
      {
        store.putUInt("stk709ms", ms); // only when it moved by over 1 %
      }
      store.end();
    }
  }
#endif
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
  display.clearCustomLut();
  display.setClock(Epd709::CLOCK_QUICK); // text to read once: 13 s, not 30
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
  display.clearCustomLut();
  display.setClock(Epd709::CLOCK_QUICK); // text to read once: 13 s, not 30
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
