/* Renderer for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
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

#include "config.h"
// The GDEB0709E01 (1600x1200) and the EE05's 4.26in in four greys have a
// layout and a renderer of their own, in renderer709.cpp and
// renderer426.cpp; nothing in this file is built for them.
#if !defined(DISP_7C_709) && !defined(DISP_4G_426)

#include <algorithm>
#include "_locale.h"
#include "_strftime.h"
#include "renderer.h"
#include "history.h"
#include "api_response.h"
#include "config.h"
#include "conversions.h"
#include "display_utils.h"
#include "roundrect.h"
#include "rtl.h"
#include "hebcal.h"
#include "sun.h"
#include "smooth.h"

// fonts: every compiled-in family, and the FONT_*pt8b names resolved at
// runtime through FONT_FAMILY_INDEX
#include "fonts/font_table.h"

// icon header files
#include "icons/icons_pollen.h" // line-art pollen flower, all panel types
#include "icons/icons_16x16.h"
#include "icons/icons_24x24.h"
#include "icons/icons_32x32.h"
#include "icons/icons_48x48.h"
#include "icons/icons_64x64.h"
#include "icons/icons_96x96.h"
#include "icons/icons_128x128.h"
#include "icons/icons_160x160.h"
#include "icons/icons_196x196.h"
#include "icons/icons_40x40.h" // 6-row widget layout (widget_rows = 6)
#if defined(GREYS_75)
#include "assets426/moon426.h"   // the moon's near side, for its photo
#endif

#if defined(DISP_BW_V2) && defined(GREYS_75)
#if defined(ESP32) && !defined(PANEL_GDEQ0426T82)
  void GxEPD2_750_GDEY075T7_Patient::busySleepCb(const void *param)
  {
    auto *self = const_cast<GxEPD2_750_GDEY075T7_Patient *>(
        static_cast<const GxEPD2_750_GDEY075T7_Patient *>(param));
    if (!self || !self->_power_is_on || !self->_canLightSleep)
    {
      delay(1);
      return;
    }
    self->_canLightSleep = false;
    const int16_t holdPins[] = {
      static_cast<int16_t>(PIN_EPD_PWR),
      self->_cs,
      self->_dc,
      self->_rst,
      static_cast<int16_t>(PIN_SD_EN),
      static_cast<int16_t>(PIN_SD_CS),
    };
    waitBusyLightSleep(self->_busy, static_cast<uint8_t>(self->_busy_level),
                       self->_busy_timeout / 1000U, holdPins,
                       sizeof(holdPins) / sizeof(holdPins[0]));
  }

  void GxEPD2_750_GDEY075T7_Patient::refresh(bool partial_update_mode)
  {
    const uint32_t savedMhz = getCpuFrequencyMhz();
    if (savedMhz > 80) setCpuFrequencyMhz(80);
    _canLightSleep = true;
    setBusyCallback(&busySleepCb, this);
    GxEPD2_750_GDEY075T7::refresh(partial_update_mode);
    setBusyCallback(nullptr);
    _canLightSleep = false;
    if (getCpuFrequencyMhz() != savedMhz) setCpuFrequencyMhz(savedMhz);
  }

  void GxEPD2_750_GDEY075T7_Patient::refresh(int16_t x, int16_t y,
                                               int16_t w, int16_t h)
  {
    const uint32_t savedMhz = getCpuFrequencyMhz();
    if (savedMhz > 80) setCpuFrequencyMhz(80);
    _canLightSleep = true;
    setBusyCallback(&busySleepCb, this);
    GxEPD2_750_GDEY075T7::refresh(x, y, w, h);
    setBusyCallback(nullptr);
    _canLightSleep = false;
    if (getCpuFrequencyMhz() != savedMhz) setCpuFrequencyMhz(savedMhz);
  }
#endif

  void Grey75Display::drawPixel(int16_t x, int16_t y, uint16_t color)
  {
    switch (color)
    {
    case G75_KDG:
      color = ((x + y) & 1) ? GxEPD_BLACK : GxEPD_DARKGREY;
      break;
    case G75_LGW:
      color = ((x + y) & 1) ? GxEPD_LIGHTGREY : GxEPD_WHITE;
      break;
    case G75_DGLG:
      color = ((x + y) & 1) ? GxEPD_DARKGREY : GxEPD_LIGHTGREY;
      break;
    case G75_LGW4:
      color = ((x & 1) == 0 && ((y + (x >> 1)) & 1) == 0)
              ? GxEPD_LIGHTGREY : GxEPD_WHITE;
      break;
    default:
      break;
    }
    Grey75Base::drawPixel(x, y, color);
  }
  void Grey75Display::fillScreen(uint16_t color)
  {
    // GxEPD2_4G_4G::fillScreen(0x0000) underflows `(brightness - 1) / 0xC000ul`
    // to 0x55 (dark grey); delegate only white/light/dark grey natively and
    // fill black / G75_* patterns through drawPixel().
    if (color == GxEPD_WHITE || color == GxEPD_LIGHTGREY
        || color == GxEPD_DARKGREY)
    {
      Grey75Base::fillScreen(color);
      return;
    }
    fillRect(0, bandTop(), width(), bandBottom() - bandTop(), color);
  }
  Grey75Display display(
    GxEPD2_BW_V2_PANEL(PIN_EPD_CS,
                       PIN_EPD_DC,
                       PIN_EPD_RST,
                       PIN_EPD_BUSY));
  void Grey75Display::firstPage()
  {
    bandY0 = 0;
    bandY1 = static_cast<int16_t>(std::min<uint16_t>(pageHeight(), DISP_HEIGHT));
    Grey75Base::firstPage();
  }
  bool Grey75Display::nextPage()
  {
    const bool more = Grey75Base::nextPage();
    if (more)
    {
      bandY0 = bandY1;
      bandY1 = static_cast<int16_t>(std::min<int>(bandY0 + pageHeight(), DISP_HEIGHT));
    }
    else
    {
      bandY0 = 0;
      bandY1 = static_cast<int16_t>(std::min<uint16_t>(pageHeight(), DISP_HEIGHT));
    }
    return more;
  }
  int16_t Grey75Display::bandTop() const
  {
    return (getRotation() == 0) ? bandY0 : 0;
  }
  int16_t Grey75Display::bandBottom() const
  {
    return (getRotation() == 0) ? bandY1 : DISP_HEIGHT;
  }
  bool Grey75Display::inBand(int16_t y, int16_t h) const
  {
    if (getRotation() != 0) return true;
    return (y + h) > bandY0 && y < bandY1;
  }
#elif defined(DISP_BW_V2)
  GxEPD2_BW<GxEPD2_BW_V2_PANEL,
            GxEPD2_BW_V2_PANEL::HEIGHT> display(
    GxEPD2_BW_V2_PANEL(PIN_EPD_CS,
                       PIN_EPD_DC,
                       PIN_EPD_RST,
                       PIN_EPD_BUSY));
#endif
#ifdef DISP_3C_B
  GxEPD2_3C<GxEPD2_750c_GDEY075Z08,
            GxEPD2_750c_GDEY075Z08::HEIGHT / 2> display(
    GxEPD2_750c_GDEY075Z08(PIN_EPD_CS,
                           PIN_EPD_DC,
                           PIN_EPD_RST,
                           PIN_EPD_BUSY));
#endif
#ifdef DISP_7C_F
  GxEPD2_7C<GxEPD2_730c_GDEY073D46,
            GxEPD2_730c_GDEY073D46::HEIGHT / 4> display(
    GxEPD2_730c_GDEY073D46(PIN_EPD_CS,
                           PIN_EPD_DC,
                           PIN_EPD_RST,
                           PIN_EPD_BUSY));
#endif
#ifdef DISP_BW_V1
  GxEPD2_BW<GxEPD2_750,
            GxEPD2_750::HEIGHT> display(
    GxEPD2_750(PIN_EPD_CS,
               PIN_EPD_DC,
               PIN_EPD_RST,
               PIN_EPD_BUSY));
#endif

#ifdef DISP_7C_E6
  void GxEPD2_730c_GDEP073E01_Patient::busySleepCb(const void *param)
  {
    auto *self = const_cast<GxEPD2_730c_GDEP073E01_Patient *>(
        static_cast<const GxEPD2_730c_GDEP073E01_Patient *>(param));
    if (!self || !self->_power_is_on || !self->_canLightSleep)
    {
      delay(1);
      return;
    }
    self->_canLightSleep = false;
    const int16_t holdPins[] = {
      static_cast<int16_t>(PIN_EPD_PWR),
      self->_cs,
      self->_dc,
      self->_rst,
      static_cast<int16_t>(PIN_SD_EN),
      static_cast<int16_t>(PIN_SD_CS),
    };
    waitBusyLightSleep(self->_busy, static_cast<uint8_t>(self->_busy_level),
                       self->_busy_timeout / 1000U, holdPins,
                       sizeof(holdPins) / sizeof(holdPins[0]));
  }

  void GxEPD2_730c_GDEP073E01_Patient::refresh(bool partial_update_mode)
  {
    _canLightSleep = true;
    setBusyCallback(&busySleepCb, this);
    GxEPD2_730c_GDEP073E01::refresh(partial_update_mode);
    setBusyCallback(nullptr);
    _canLightSleep = false;
  }

  void GxEPD2_730c_GDEP073E01_Patient::refresh(int16_t x, int16_t y,
                                               int16_t w, int16_t h)
  {
    _canLightSleep = true;
    setBusyCallback(&busySleepCb, this);
    GxEPD2_730c_GDEP073E01::refresh(x, y, w, h);
    setBusyCallback(nullptr);
    _canLightSleep = false;
  }

  GxEPD2_7C<GxEPD2_730c_GDEP073E01_Patient,
            GxEPD2_730c_GDEP073E01_Patient::HEIGHT / 4> display(
    GxEPD2_730c_GDEP073E01_Patient(PIN_EPD_CS,
                                   PIN_EPD_DC,
                                   PIN_EPD_RST,
                                   PIN_EPD_BUSY));
#endif
#ifndef ACCENT_COLOR
  #define ACCENT_COLOR GxEPD_BLACK
#endif

#if defined(GREYS_75)
/* GREYS_75: the 7.5in black-and-white panel in its four greys. The layout
 * is the same; the greys go where they make the page easier to read: the
 * rain bars filled, the dew point a grey curve, the widget labels in grey
 * under their black values, the lows grey, every other forecast day
 * shaded, clouds filled and the moon drawn as it looks. Two greys dot by dot make the tones
 * between them (G75_* in renderer.h). In dark mode the page stays black
 * and white.
 */
static const uint16_t G_DG = GxEPD_DARKGREY;
static const uint16_t G_LG = GxEPD_LIGHTGREY;
bool ALERT_75 = false;
// greys on this page: not in dark mode
static inline bool greys() {return !DARK_MODE;}
static inline bool alertRibbon75() {return greys() && ALERT_75;}
#else
static inline bool alertRibbon75() {return false;}
#endif

#if defined(GREYS_75)
/* 2-bit text (tools/fontconvert2.py): each 1-bit face that has a twin in 2
 * bits is known by its signature (line height and the metrics of H a g 0),
 * so whichever family table it came from, the twin is found. The twin keeps
 * the 1-bit face's advances: text measures (getTextBounds) and lines up as
 * before; only the ink differs.
 */
#if defined(FONT_INCLUDE_Bitter) && FONT_INCLUDE_Bitter
#include "fonts/Bitter_2bit.h"

static bool font2Matches(const GFXfont *f, const int16_t *sig)
{
  if (pgm_read_byte(&f->yAdvance) != sig[0]) return false;
  static const char SIG_CHARS[] = "Hag0";
  const uint16_t first = pgm_read_word(&f->first);
  for (int k = 0; k < 4; ++k)
  {
    const uint8_t c = static_cast<uint8_t>(SIG_CHARS[k]);
    if (c < first || c > pgm_read_word(&f->last)) return false;
    const GFXglyph *g = &f->glyph[c - first];
    const int16_t *e = sig + 1 + 5 * k;
    if (pgm_read_byte(&g->xAdvance) != e[0]
        || pgm_read_byte(&g->width) != e[1]
        || pgm_read_byte(&g->height) != e[2]
        || static_cast<int8_t>(pgm_read_byte(&g->xOffset)) != e[3]
        || static_cast<int8_t>(pgm_read_byte(&g->yOffset)) != e[4])
    {
      return false;
    }
  }
  return true;
}

static const GFXfont *font2For(const GFXfont *f)
{
  static const GFXfont *lastMono = nullptr, *lastGrey = nullptr;
  if (f == lastMono) return lastGrey;
  lastMono = f;
  lastGrey = nullptr;
  for (const Font2Face &t : FONT2)
  {
    if (t.sig && font2Matches(f, t.sig))
    {
      lastGrey = t.grey;
      break;
    }
  }
  return lastGrey;
}
#else
static inline const GFXfont *font2For(const GFXfont *) {return nullptr;}
#endif

#include "icons/icons_2bit.h"
/* The 2-bit twin of a 1-bit icon (tools/icons2bit.py), or null. Every source
 * file has its own copy of the icon arrays, so a twin is found by a hash of
 * the 1-bit bytes (FNV-1a, as the tool computes it), and remembered by
 * address.
 */
static const uint8_t *icon2Twin(const uint8_t *mono, int16_t size)
{
  if (size != 196 && size != 64 && size != 48 && size != 32) return nullptr;
  struct Seen { const uint8_t *mono; int16_t size; const uint8_t *twin; };
  static Seen seen[32] = {};
  static int next = 0;
  for (const Seen &e : seen)
  {
    if (e.mono == mono && e.size == size) return e.twin;
  }
  uint32_t h = 0x811C9DC5u;
  const int n = ((size + 7) / 8) * size;
  for (int i = 0; i < n; ++i)
  {
    h = (h ^ pgm_read_byte(&mono[i])) * 0x01000193u;
  }
  const uint8_t *twin = nullptr;
  for (const Icon2Twin &t : ICON2_TWINS)
  {
    if (t.size == size && t.hash == h)
    {
      twin = t.dots;
      break;
    }
  }
  seen[next] = {mono, size, twin};
  next = (next + 1) % 32;
  return twin;
}

static inline uint8_t icon2At(const uint8_t *dots, int i)
{
  return (pgm_read_byte(&dots[i >> 2]) >> (6 - 2 * (i & 3))) & 3;
}

void Grey75Display::drawInvertedBitmap(int16_t x, int16_t y,
                                       const uint8_t bitmap[], int16_t w,
                                       int16_t h, uint16_t color)
{
  if (!inBand(y, h)) return;
  const uint8_t *twin = (color == GxEPD_BLACK && w == h && !DARK_MODE)
                        ? icon2Twin(bitmap, w) : nullptr;
  if (!twin)
  {
    Grey75Base::drawInvertedBitmap(x, y, bitmap, w, h, color);
    return;
  }
  const int j0 = std::max<int>(0, bandTop() - y);
  const int j1 = std::min<int>(h, bandBottom() - y);
  for (int j = j0; j < j1; ++j)
  {
    for (int i = 0; i < w; ++i)
    {
      const uint8_t v = icon2At(twin, j * w + i);
      if (v == 2) drawPixel(x + i, y + j, GxEPD_BLACK);
      else if (v == 1) drawPixel(x + i, y + j, GxEPD_LIGHTGREY);
    }
  }
}

// the 2-bit twin of the face in use, when this page is in greys
const GFXfont *Grey75Display::twin2()
{
  return (gfxFont && textsize_x == 1 && textsize_y == 1 && !DARK_MODE)
         ? font2For(gfxFont) : nullptr;
}

// measured with the twin's advances and ink, as write() draws it
void Grey75Display::getTextBounds(const char *s, int16_t x, int16_t y,
                                  int16_t *x1, int16_t *y1, uint16_t *w,
                                  uint16_t *h)
{
  const GFXfont *t = twin2();
  GFXfont *mono = gfxFont;
  if (t) gfxFont = const_cast<GFXfont *>(t);
  Grey75Base::getTextBounds(s, x, y, x1, y1, w, h);
  gfxFont = mono;
}

void Grey75Display::getTextBounds(const String &s, int16_t x, int16_t y,
                                  int16_t *x1, int16_t *y1, uint16_t *w,
                                  uint16_t *h)
{
  getTextBounds(s.c_str(), x, y, x1, y1, w, h);
}

size_t Grey75Display::write(uint8_t c)
{
  const GFXfont *grey = twin2();
  if (!grey || c == '\n' || c == '\r')
  {
    return Grey75Base::write(c);
  }
  const uint16_t first = pgm_read_word(&grey->first);
  if (c < first || c > pgm_read_word(&grey->last))
  {
    return 1;
  }
  // black text: black core, light-grey edge. The grey labels (G75_DGLG, a
  // dark/light-grey mix): that mix for the core, light grey and white mixed
  // for the edge. White text (warning banners, dark tabs): white core with
  // light-grey anti-aliased edge. Any other colour: one ink, core and edge
  // alike.
  // A point, comma or colon in the grey mix is a dot or two, which the
  // mix's checker can leave all light grey (the rain amounts' decimal
  // point vanished): those are drawn black.
  const bool dot = c == '.' || c == ',' || c == ':';
  const bool mid = textcolor == G75_DGLG && !dot;
  const bool black = textcolor == GxEPD_BLACK || (textcolor == G75_DGLG && dot);
  const bool dg = textcolor == GxEPD_DARKGREY;
  const GFXglyph *g = &grey->glyph[c - first];
  const uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
  if (w > 0 && h > 0)
  {
    const uint8_t *dots = grey->bitmap + pgm_read_word(&g->bitmapOffset);
    const int16_t x0 = cursor_x + static_cast<int8_t>(pgm_read_byte(&g->xOffset));
    const int16_t y0 = cursor_y + static_cast<int8_t>(pgm_read_byte(&g->yOffset));
    const int16_t yTop = bandTop(), yBot = bandBottom();
    if (y0 + h > yTop && y0 < yBot)
    {
      const int j0 = std::max<int>(0, yTop - y0);
      const int j1 = std::min<int>(h, yBot - y0);
      for (int y = j0; y < j1; ++y)
      {
        int i = y * w;
        for (int x = 0; x < w; ++x, ++i)
        {
          const uint8_t v = (pgm_read_byte(&dots[i >> 2]) >> (6 - 2 * (i & 3))) & 3;
          if (!v) continue;
          uint16_t ink = textcolor;
          if (black)
          {
            ink = (v == 3) ? GxEPD_BLACK : (v == 2) ? GxEPD_DARKGREY : GxEPD_LIGHTGREY;
          }
          else if (mid)
          {
            ink = (v == 3) ? G75_DGLG : (v == 2) ? GxEPD_LIGHTGREY : G75_LGW;
          }
          else if (dg)
          {
            ink = (v == 3) ? GxEPD_DARKGREY : (v == 2) ? G75_DGLG : GxEPD_LIGHTGREY;
          }
          else if (textcolor == GxEPD_WHITE)
          {
            ink = (v == 3) ? GxEPD_WHITE : GxEPD_LIGHTGREY;
          }
          drawPixel(x0 + x, y0 + y, ink);
        }
      }
    }
  }
  cursor_x += pgm_read_byte(&g->xAdvance);
  return 1;
}
#endif

#if defined(GREYS_75)
/* An icon (a 1-bit outline, GxEPD2's inverted bitmap) in `ink`, with the
 * largest area its outline closes in filled with `fill` -- or, `every`,
 * each closed area of some size (a house split by the thermometer in it).
 * Gaps in the outline up to about twice R dots wide count as closed.
 */
static std::vector<uint8_t> fillMask(const uint8_t *bmp, int16_t size,
                                     int R, bool every);

static const uint8_t *icon2Twin(const uint8_t *mono, int16_t size);
static inline uint8_t icon2At(const uint8_t *dots, int i);

static void drawFilledIcon(int16_t x, int16_t y, const uint8_t *bmp,
                           int16_t size, uint16_t ink, uint16_t fill,
                           int R, bool every)
{
  if (!display.inBand(y, size)) return;
  display.drawInvertedBitmap(x, y, bmp, size, size, ink);
  // Worked out once a wake: the page is drawn a band at a time, so each
  // small icon (32/40/48 px) stays cached in SRAM without rebuilding.
  struct Cached
  {
    const uint8_t *bmp;
    int16_t size;
    int R;
    bool every;
    std::vector<uint8_t> mask;
  };
  static std::vector<Cached> cache;
  const std::vector<uint8_t> *mask = nullptr;
  for (const Cached &c : cache)
  {
    if (c.bmp == bmp && c.size == size && c.R == R && c.every == every)
    {
      mask = &c.mask;
      break;
    }
  }
  if (!mask)
  {
    cache.push_back({bmp, size, R, every, fillMask(bmp, size, R, every)});
    mask = &cache.back().mask;
  }
  if (mask->empty())
  {
    return;
  }
  const int j0 = std::max<int>(0, display.bandTop() - y);
  const int j1 = std::min<int>(size, display.bandBottom() - y);
  for (int j = j0; j < j1; ++j)
  {
    for (int i = 0; i < size; ++i)
    {
      if ((*mask)[j * size + i]) display.drawPixel(x + i, y + j, fill);
    }
  }
}

/* The dots of `bmp` its fill covers (empty: none), see drawFilledIcon. */
static std::vector<uint8_t> fillMask(const uint8_t *bmp, int16_t size,
                                     int R, bool every)
{
  const int w = size, h = size, stride = (size + 7) / 8;
  auto inkAt = [&](int i, int j) {
    return !(pgm_read_byte(&bmp[j * stride + i / 8]) & (0x80 >> (i & 7)));
  };
  // The outline thickened by R dots closes the small gaps where rain or
  // snow leaves a cloud's bottom open; the fill grows back after.
  std::vector<uint8_t> wall(w * h, 0);
  for (int j = 0; j < h; ++j)
  {
    for (int i = 0; i < w; ++i)
    {
      if (!inkAt(i, j)) continue;
      for (int v = std::max(0, j - R); v <= std::min(h - 1, j + R); ++v)
        for (int u = std::max(0, i - R); u <= std::min(w - 1, i + R); ++u)
          wall[v * w + u] = 1;
    }
  }
  auto isInk = [&](int i, int j) {return wall[j * w + i] != 0;};
  // each blank dot's area, by four-way neighbours (a diagonal step between
  // two dots of ink is a wall): -1 open to the icon's edge, 1.. closed
  std::vector<int16_t> area(w * h, 0);
  std::vector<int32_t> stack;
  auto flood = [&](int start, int16_t label) {
    int n = 0;
    area[start] = label;
    stack.push_back(start);
    while (!stack.empty())
    {
      const int p = stack.back();
      stack.pop_back();
      ++n;
      const int i = p % w, j = p / w;
      const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto &d : nb)
      {
        const int ii = i + d[0], jj = j + d[1];
        if (ii < 0 || jj < 0 || ii >= w || jj >= h) continue;
        const int q = jj * w + ii;
        if (area[q] || isInk(ii, jj)) continue;
        area[q] = label;
        stack.push_back(q);
      }
    }
    return n;
  };
  for (int j = 0; j < h; ++j)
  {
    for (int i = 0; i < w; ++i)
    {
      const bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1;
      if (edge && !area[j * w + i] && !isInk(i, j)) flood(j * w + i, -1);
    }
  }
  int16_t label = 0, best = 0;
  int bestN = 0;
  std::vector<int> sizes(1, 0);
  for (int p = 0; p < w * h; ++p)
  {
    if (area[p] || isInk(p % w, p / w)) continue;
    const int n = flood(p, ++label);
    sizes.push_back(n);
    if (n > bestN) {bestN = n; best = label;}
  }
  if (bestN * 60 < w * h)
  {
    return {};   // nothing big enough to be a cloud
  }
  std::vector<uint8_t> in(w * h, 0);
  for (int p = 0; p < w * h; ++p)
  {
    in[p] = area[p] > 0
            && (every ? sizes[area[p]] * 60 >= w * h : area[p] == best);
  }
  // back out to the outline: all eight neighbours (the thickening was a
  // square, so a cross would leave a ring of white along every curve), and
  // a step more than it took away
  for (int step = 0; step <= R; ++step)
  {
    std::vector<uint8_t> grown(in);
    for (int p = 0; p < w * h; ++p)
    {
      const int i = p % w, j = p / w;
      if (in[p] || inkAt(i, j)) continue;
      bool next = false;
      for (int dj = -1; dj <= 1 && !next; ++dj)
      {
        for (int di = -1; di <= 1 && !next; ++di)
        {
          const int ii = i + di, jj = j + dj;
          next = ii >= 0 && jj >= 0 && ii < w && jj < h && in[jj * w + ii];
        }
      }
      if (next) grown[p] = 1;
    }
    in.swap(grown);
  }
  return in;
}
#endif

/* A condition icon in `ink`. In greys its cloud is filled: dark grey for a
 * thunderstorm, light grey for drizzle, rain and snow, and light grey dotted
 * with white for any other cloud. A clear sky is left white: its largest
 * closed area is the sun.
 */
static void drawConditionIcon(int16_t x, int16_t y, const uint8_t *bmp,
                              int16_t size, uint16_t ink, int id)
{
#if defined(GREYS_75)
  if (!display.inBand(y, size)) return;
  if (greys() && id != 0)
  {
    bool hasFill = false;
    uint16_t fill = G75_LGW;
    if (id >= 200 && id < 300) {fill = G_DG; hasFill = true;}
    else if (id >= 300 && id < 700) {fill = G_LG; hasFill = true;}
    else if (id >= 800 && id < 900) {fill = G75_LGW; hasFill = true;}

    const uint8_t *twin = (ink == GxEPD_BLACK) ? icon2Twin(bmp, size) : nullptr;
    if (twin)
    {
      const int j0 = std::max<int>(0, display.bandTop() - y);
      const int j1 = std::min<int>(size, display.bandBottom() - y);
      for (int j = j0; j < j1; ++j)
      {
        for (int i = 0; i < size; ++i)
        {
          const uint8_t v = icon2At(twin, j * size + i);
          uint16_t c;
          if (v == 2) c = GxEPD_BLACK;
          else if (v == 3) {if (!hasFill) continue; c = fill;}
          else if (v == 1) c = GxEPD_LIGHTGREY;
          else continue;
          display.drawPixel(x + i, y + j, c);
        }
      }
      return;
    }
    if (hasFill && id != 800)
    { // 32px icons without a 2-bit twin
      drawFilledIcon(x, y, bmp, size, ink, fill, 1, false);
      return;
    }
  }
#endif
  (void)id;
  display.drawInvertedBitmap(x, y, bmp, size, size, ink);
}

#if defined(GREYS_75)
/* The moon as it is tonight, `N` dots across: NASA's map of the near
 * side (moon426.h, as on the 4.26in page), lit for the phase and
 * error-diffused into the four greys. Cached once per wake so subsequent
 * display bands blit only the visible rows with zero heap recomputation and
 * zero permanent .bss bloat.
 */
static void drawPhotoMoon75(int16_t x0, int16_t y0, int N, double frac,
                            bool south)
{
  if (!display.inBand(y0, N) || N <= 0) return;
  static const uint16_t INK[4] = {GxEPD_BLACK, G_DG, G_LG, GxEPD_WHITE};
  struct MoonCache
  {
    int N = -1;
    double frac = -1.0;
    bool south = false;
    std::vector<uint8_t> px; // 0..3 -> INK index, 255 -> outside
  };
  static MoonCache cache;
  if (cache.N != N || cache.frac != frac || cache.south != south)
  {
    cache.N = N;
    cache.frac = frac;
    cache.south = south;
    cache.px.assign(N * N, 255);
    const int M = MOON426_PX;
    // the levels' brightness relative to white (the 7.5in's, measured:
    // tools/sim480/measured_75.json)
    static const float REFL[4] = {0.028f, 0.078f, 0.367f, 1.0f};
    std::vector<float> tone(N * N, -1000.0f);
    const float OUTSIDE = -1000.0f;
    const float NIGHT = -500.0f;   // the night side, in earthshine
    const float e = static_cast<float>(2.0 * M_PI * frac);
    const float se = std::sin(e), ce = std::cos(e);
    for (int j = 0; j < N; ++j)
    {
      for (int i = 0; i < N; ++i)
      {
        const float x = (i + 0.5f) / N * 2 - 1, y = (j + 0.5f) / N * 2 - 1;
        const float r2 = x * x + y * y;
        if (r2 > 1.0f)
        {
          tone[j * N + i] = OUTSIDE;
          continue;
        }
        const float z = std::sqrt(1.0f - r2);
        const float ndl = x * se - z * ce;
        if (ndl <= 0.0f)
        {
          tone[j * N + i] = NIGHT;
          continue;
        }
        const float lit = std::max(0.0f, std::min(1.0f, ndl * 6))
                        * (0.72f + 0.28f * std::max(0.0f, ndl));
        int sum = 0, cnt = 0;
        const int v0 = j * M / N, v1 = std::max(v0 + 1, (j + 1) * M / N);
        const int u0 = i * M / N, u1 = std::max(u0 + 1, (i + 1) * M / N);
        for (int v = v0; v < v1; ++v)
        {
          for (int u = u0; u < u1; ++u)
          {
            sum += pgm_read_byte(&MOON426_ALBEDO[v * M + u]);
            ++cnt;
          }
        }
        const float alb = sum / (255.0f * cnt);
        tone[j * N + i] = std::pow(0.12f + 0.88f * alb, 1.6f) * lit;
      }
    }
    const float limb = (N / 2.0f - 1.0f) * (N / 2.0f - 1.0f);
    for (int j = 0; j < N; ++j)
    {
      for (int i = 0; i < N; ++i)
      {
        const float v = tone[j * N + i];
        if (v <= OUTSIDE) continue;
        const int px_x = south ? N - 1 - i : i, py = south ? N - 1 - j : j;
        const float ex = i + 0.5f - N / 2.0f, ey = j + 0.5f - N / 2.0f;
        if (ex * ex + ey * ey > limb)
        { // a black rim all round, as the other widget icons are outlined
          cache.px[py * N + px_x] = 0;
          continue;
        }
        if (v <= NIGHT)
        { // earthshine: a dark grey, so the whole disc shows
          cache.px[py * N + px_x] = 1;
          continue;
        }
        int k = 0;
        for (int l = 1; l < 4; ++l)
        {
          if (std::fabs(REFL[l] - v) < std::fabs(REFL[k] - v)) {k = l;}
        }
        const float err = v - REFL[k];
        auto spread = [&](int ii, int jj, float wgt) {
          if (ii >= 0 && ii < N && jj < N && tone[jj * N + ii] > NIGHT)
          {
            tone[jj * N + ii] += err * wgt;
          }
        };
        spread(i + 1, j, 7 / 16.0f);
        spread(i - 1, j + 1, 3 / 16.0f);
        spread(i, j + 1, 5 / 16.0f);
        spread(i + 1, j + 1, 1 / 16.0f);
        cache.px[py * N + px_x] = static_cast<uint8_t>(k);
      }
    }
  }
  const int j0 = std::max<int>(0, display.bandTop() - y0);
  const int j1 = std::min<int>(N, display.bandBottom() - y0);
  for (int j = j0; j < j1; ++j)
  {
    for (int i = 0; i < N; ++i)
    {
      const uint8_t idx = cache.px[j * N + i];
      if (idx < 4) display.drawPixel(x0 + i, y0 + j, INK[idx]);
    }
  }
}
  #define WGT_LABEL_INK DM_FG   // black: in greys, smoothed (2-bit text)
  // the lows in grey beside the black highs, as the colour panels draw
  // them in blue beside red
  // (the panel's solid dark grey, L* 34 against black's 19: the dark/light
  // mix, L* 54, read too faint)
  #define LO_INK (greys() ? G_DG : DM_TEXT(COLOR_TEMP_LO))
#else
  #define WGT_LABEL_INK DM_FG
  #define LO_INK DM_TEXT(COLOR_TEMP_LO)
#endif

/* Fills the page buffer with the background color. Must be called at the
 * top of every paged-drawing loop iteration: firstPage()/nextPage() clear
 * the buffer to white, so dark mode repaints it black before drawing.
 */
void fillDisplayBackground()
{
  if (DARK_MODE)
  {
    display.fillScreen(GxEPD_BLACK);
  }
} // end fillDisplayBackground

#ifdef MULTICOLOR_DISPLAY
// Palette for the full-color condition icons (icons/icons_color.h): 4-bit
// indices, 0 = transparent (pixel skipped), 1..6 = the panel's native inks.
static const uint16_t CI_PALETTE[7] = {
  0, GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED, GxEPD_YELLOW, GxEPD_GREEN,
  GxEPD_BLUE};

/* Draws a packed 4-bit paletted icon (two pixels per byte, high nibble
 * first, row-major) at the given position. Transparent pixels are skipped so
 * anything behind the icon (gridlines, graph fills) survives.
 */
static void drawColorIcon(int16_t x, int16_t y, const uint8_t *data,
                          int16_t size)
{
  for (int32_t n = 0; n < static_cast<int32_t>(size) * size; ++n)
  {
    uint8_t b = pgm_read_byte(&data[n >> 1]);
    uint8_t v = (n & 1) ? (b & 0x0F) : (b >> 4);
    if (v)
    {
      display.drawPixel(x + (n % size), y + (n / size), CI_PALETTE[v]);
    }
  }
} // end drawColorIcon
#endif // MULTICOLOR_DISPLAY

/* Draws a packed 4-bit dithered black/white icon (icons_moon.h format).
 * Both inks are drawn explicitly -- black AND white -- so the icon keeps
 * its own contrast on either background: on paper the white pixels are
 * invisible, in dark mode the black ones are, and the artwork (e.g. the
 * moon's lit/shadow sides) reads correctly both ways. Valid on every
 * panel type, unlike the multicolor-only drawColorIcon.
 */
static void drawDitheredIcon(int16_t x, int16_t y, const uint8_t *data,
                             int16_t size)
{
  for (int32_t n = 0; n < static_cast<int32_t>(size) * size; ++n)
  {
    uint8_t b = pgm_read_byte(&data[n >> 1]);
    uint8_t v = (n & 1) ? (b & 0x0F) : (b >> 4);
    if (v == 1 || v == 2)
    {
      display.drawPixel(x + (n % size), y + (n / size),
                        (v == 1) ? GxEPD_BLACK : GxEPD_WHITE);
    }
  }
} // end drawDitheredIcon

/* Left-panel widget grid geometry. WIDGET_ROWS (config.json, 5 or 6) sets
 * the density: 5 rows of 48px icons on a 56px pitch, or 6 rows of 40px
 * icons on a 46px pitch (12 slots -- enough for every widget).
 */
static inline int wgtIconSize()
{
  return (WIDGET_ROWS > 5) ? 40 : 48;
}
static inline int wgtY(int PosY)
{
  return 204 + (wgtIconSize() + ((WIDGET_ROWS > 5) ? 6 : 8)) * PosY;
}
// The 6-row layout's smaller icons pull the value line up toward the label;
// nudge the label up and the value down so the label/value gap matches the
// 5-row layout.
/* Rightmost x a small source tag after a widget label may reach. The right
 * column's cell ends at 324, but the outlook graph's temperature axis labels
 * (right-aligned at 342) begin a few pixels before that.
 */
static inline int wgtTagRight(int PosX)
{
  return (PosX == 0) ? 162 : 318;
}

static inline int wgtLabelY(int PosY)
{
  return wgtY(PosY) + ((WIDGET_ROWS > 5) ? 8 : 10);
}
static inline int wgtValueY(int PosY)
{
  return wgtY(PosY) + 17 / 2 + wgtIconSize() / 2
         + ((WIDGET_ROWS > 5) ? 4 : 0);
}

#if defined(GREYS_75)
#include "icons/icons_widgets_grey.h"

static bool drawGreyWidgetIcon(int16_t x, int16_t y, const char *name,
                               int16_t size)
{
  const uint8_t *data = getGreyWidgetIcon(name, size);
  if (!data) return false;
  if (!display.inBand(y, size)) return true;
  static const uint16_t PAL[7] = {
    0, GxEPD_BLACK, G_DG, G_LG, G75_LGW, G75_LGW4, GxEPD_WHITE
  };
  const int j0 = std::max<int>(0, display.bandTop() - y);
  const int j1 = std::min<int>(size, display.bandBottom() - y);
  for (int j = j0; j < j1; ++j)
  {
    const int rowOff = (j * size) >> 1;
    for (int i = 0; i < size; i += 2)
    {
      const uint8_t b = pgm_read_byte(&data[rowOff + (i >> 1)]);
      const uint8_t hi = b >> 4;
      const uint8_t lo = b & 0x0F;
      if (hi >= 1 && hi <= 6) display.drawPixel(x + i,     y + j, PAL[hi]);
      if (lo >= 1 && lo <= 6) display.drawPixel(x + i + 1, y + j, PAL[lo]);
    }
  }
  return true;
}
#endif

/* Draws a left-panel widget icon at the size the current layout calls for:
 * the full-color version when the panel and icon set provide one, otherwise
 * the given line art in lineColor.
 */
static void drawWidgetIcon(int16_t x, int16_t y, const uint8_t *lineArt48,
                           const uint8_t *lineArt40, const char *colorName,
                           uint16_t lineColor)
{
  const int size = wgtIconSize();
#ifdef MULTICOLOR_DISPLAY
  const uint8_t *ci = getColorWidgetIcon(colorName, size);
  if (ci)
  {
    drawColorIcon(x, y, ci, size);
    return;
  }
#elif defined(GREYS_75)
  if (greys() && drawGreyWidgetIcon(x, y, colorName, size))
  {
    return;
  }
#else
  (void)colorName;
#endif
#if defined(GREYS_75)
  if (greys())
  { // remaining line-art widgets (sunrise, sunset, wind, uvi)
    drawFilledIcon(x, y, (size == 40) ? lineArt40 : lineArt48, size,
                   lineColor, G75_LGW, 1, true);
    return;
  }
#endif
  display.drawInvertedBitmap(x, y, (size == 40) ? lineArt40 : lineArt48,
                             size, size, lineColor);
} // end drawWidgetIcon

// Risk levels for drawRiskChip.
#define RISK_PLAIN  0
#define RISK_GREEN  1 // low/good: white text on green
#define RISK_YELLOW 2 // moderate: black text on yellow (EPA yellow band)
#define RISK_AMBER  3 // elevated: black text on a yellow/red dither (orange)
#define RISK_RED    4 // high: white text on red
#define RISK_PURPLE 5 // very high: white text on a red/blue dither (plum)
#define RISK_MAROON 6 // hazardous: white text on a red/black dither

/* Which United States AQI band a value falls in: an index into
 * UNITED_STATES_AQI_TXT, its short-form twin, and AQI_BAND_RISK below.
 * Kept in one place so the color and the word can never disagree.
 */
static int usScaleAqiBand(int aqi)
{
  return (aqi <= 50)  ? 0
       : (aqi <= 100) ? 1
       : (aqi <= 150) ? 2
       : (aqi <= 200) ? 3
       : (aqi <= 300) ? 4
                      : 5;
}
static const int AQI_BAND_RISK[6] = { RISK_GREEN, RISK_YELLOW, RISK_AMBER,
                                      RISK_RED,   RISK_PURPLE, RISK_MAROON };

#ifdef MULTICOLOR_DISPLAY
/* Vertical run painted as a two-ink checker -- the dithered analogue of
 * GFX's writeFastVLine. Parity is keyed to ABSOLUTE screen coordinates, so
 * adjacent spans continue one checkerboard instead of each starting its own
 * phase (which would show as a seam down the badge).
 */
static void ditherVLine(int16_t x, int16_t y, int16_t h,
                        uint16_t inkA, uint16_t inkB)
{
  for (int16_t i = 0; i < h; ++i)
  {
    const int16_t yy = static_cast<int16_t>(y + i);
    display.drawPixel(x, yy, ((x + yy) & 1) ? inkA : inkB);
  }
}

/* fillRoundRect for a color the panel has no ink for: the same spans GFX
 * would fill (roundrect.h), each painted as a two-ink checker.
 */
static void fillRoundRectDithered(int16_t x, int16_t y, int16_t w, int16_t h,
                                  int16_t r, uint16_t inkA, uint16_t inkB)
{
  roundRectSpans(x, y, w, h, r,
                 [&](int16_t sx, int16_t sy, int16_t sh)
                 { ditherVLine(sx, sy, sh, inkA, inkB); });
}
#endif

/* Draws a risk-category word. On multicolor panels, elevated levels render
 * as a colored badge -- the ink as AREA with solid text knocked out on top,
 * which stays legible where colored glyphs do not (amber/red are too dim as
 * 7pt text on this ink; verified on the E1002). Purple has no native ink,
 * but a red/blue checker at area scale reads as a dark plum. Uses the
 * currently selected font; x is the left edge, y the text baseline.
 */
#ifdef GREYS_75
static uint16_t fillRiskChipGrey(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                 int level)
{
  const int16_t w = x1 - x0 + 1, h = y1 - y0 + 1;
  if (level >= RISK_RED)
  {
    display.fillRoundRect(x0, y0, w, h, 3, GxEPD_BLACK);
    return GxEPD_WHITE;
  }
  display.fillRoundRect(x0, y0, w, h, 3, G_LG);
  if (level == RISK_AMBER)
  {
    display.drawRoundRect(x0, y0, w, h, 3, G_DG);
  }
  return GxEPD_BLACK;
}
#endif

static void drawRiskChip(int16_t x, int16_t y, const String &text, int level)
{
#ifdef MULTICOLOR_DISPLAY
  if (level == RISK_PLAIN || text.isEmpty())
  {
    drawString(x, y, text, LEFT);
    return;
  }
  uint16_t w = getStringWidth(text);
  int16_t x0 = x - 2, y0 = y - 12, x1 = x + w + 4, y1 = y + 3;
  if (level == RISK_PURPLE || level == RISK_MAROON || level == RISK_AMBER)
  { // no usable native ink: an area dither reads as plum / maroon / orange
    // (the panel's own "orange" code renders as a blotchy brown gradient)
    uint16_t alt = (level == RISK_PURPLE) ? GxEPD_BLUE
                 : (level == RISK_AMBER)  ? GxEPD_YELLOW
                                          : GxEPD_BLACK;
    // Rounded like its solid neighbours: this used to be a nested loop over
    // the whole rectangle, which had no notion of the corner radius and so
    // gave every dithered badge -- UV High and Extreme, AQI 101-150 /
    // 201-300 / 300+, pollen 3 -- square corners beside rounded ones.
    fillRoundRectDithered(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, GxEPD_RED, alt);
  }
  else
  {
    uint16_t fill = GxEPD_RED;
    if (level == RISK_YELLOW) {fill = GxEPD_YELLOW;}
    if (level == RISK_GREEN)  {fill = GxEPD_GREEN;}
    display.fillRoundRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, fill);
  }
  const bool darkText = (level == RISK_YELLOW || level == RISK_AMBER);
  drawString(x + 1, y, text,
             (alignment_t)LEFT, darkText ? GxEPD_BLACK : GxEPD_WHITE);
#elif defined(GREYS_75)
  if (!greys() || level == RISK_PLAIN || level == RISK_GREEN || text.isEmpty())
  {
    drawString(x, y, text, LEFT);
    return;
  }
  const uint16_t w = getStringWidth(text);
  const int16_t x0 = x - 2, y0 = y - 12, x1 = x + w + 4, y1 = y + 3;
  const uint16_t ink = fillRiskChipGrey(x0, y0, x1, y1, level);
  drawString(x + 1, y, text, LEFT, ink);
#else
  (void)level;
  drawString(x, y, text, LEFT);
#endif
} // end drawRiskChip

/* drawRiskChip carrying two lines instead of one, for a name too wide for
 * the column in one piece. Same horizontal geometry -- 2px of bleed left, 4
 * right -- and the same 10px line spacing drawMultiLnString uses.
 *
 * Vertically it does NOT sit on the value's baseline the way the one-line
 * chip does: two 5pt lines need 23px against the one-line chip's 16, and
 * hanging that off the baseline puts the chip's top hard against the
 * widget's own label (measured: zero clear rows under "Air Quality"). So
 * the block straddles the baseline instead -- one line above, one just
 * below -- putting the chip's top on the row the 12pt value's digits start
 * at, so the two line up and clear the label by the same 2px, and spending
 * the slack downward where the widget row is empty anyway.
 */
static void drawRiskChipWrapped(int16_t x, int16_t y, const String &lineA,
                                const String &lineB, int level)
{
  // No chip to make room for: plain text keeps drawMultiLnString's placement
#ifdef MULTICOLOR_DISPLAY
  if (level == RISK_PLAIN)
  {
    drawString(x, y - 10, lineA, LEFT);
    drawString(x, y, lineB, LEFT);
    return;
  }
  const int16_t baseA = y - 7, baseB = y + 3; // 10px apart, as wrapped text
  const uint16_t w = std::max(getStringWidth(lineA), getStringWidth(lineB));
  const int16_t x0 = x - 2, y0 = y - 16, x1 = x + w + 4, y1 = y + 6;
  if (level == RISK_PURPLE || level == RISK_MAROON || level == RISK_AMBER)
  {
    uint16_t alt = (level == RISK_PURPLE) ? GxEPD_BLUE
                 : (level == RISK_AMBER)  ? GxEPD_YELLOW
                                          : GxEPD_BLACK;
    fillRoundRectDithered(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, GxEPD_RED, alt);
  }
  else
  {
    uint16_t fill = GxEPD_RED;
    if (level == RISK_YELLOW) {fill = GxEPD_YELLOW;}
    if (level == RISK_GREEN)  {fill = GxEPD_GREEN;}
    display.fillRoundRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, fill);
  }
  const bool darkText = (level == RISK_YELLOW || level == RISK_AMBER);
  const uint16_t ink = darkText ? GxEPD_BLACK : GxEPD_WHITE;
  drawString(x + 1, baseA, lineA, (alignment_t)LEFT, ink);
  drawString(x + 1, baseB, lineB, (alignment_t)LEFT, ink);
#elif defined(GREYS_75)
  if (!greys() || level == RISK_PLAIN || level == RISK_GREEN)
  {
    drawString(x, y - 10, lineA, LEFT);
    drawString(x, y, lineB, LEFT);
    return;
  }
  const int16_t baseA = y - 7, baseB = y + 3;
  const uint16_t w = std::max(getStringWidth(lineA), getStringWidth(lineB));
  const int16_t x0 = x - 2, y0 = y - 16, x1 = x + w + 4, y1 = y + 6;
  const uint16_t ink = fillRiskChipGrey(x0, y0, x1, y1, level);
  drawString(x + 1, baseA, lineA, LEFT, ink);
  drawString(x + 1, baseB, lineB, LEFT, ink);
#else
  (void)level;
  drawString(x, y - 10, lineA, LEFT);
  drawString(x, y, lineB, LEFT);
#endif
} // end drawRiskChipWrapped

/* Breaks text onto two lines at a space, both within max_w, keeping the
 * first line as long as it can (drawMultiLnString's rule). False if two
 * lines cannot do it -- one word has nothing to break on, and three-word
 * names like "Unhealthy for Sensitive Groups" need three.
 * Measured in the CURRENT font.
 */
static bool wrapTwoLines(const String &text, int max_w,
                         String &lineA, String &lineB)
{
  for (int i = text.length() - 1; i > 0; --i)
  {
    if (text.charAt(i) != ' ') {continue;}
    lineA = text.substring(0, i);
    lineB = text.substring(i + 1);
    if (getStringWidth(lineA) <= max_w && getStringWidth(lineB) <= max_w)
    {
      return true;
    }
  }
  return false;
} // end wrapTwoLines

/* Draws a risk badge in the fullest form that fits max_w, trying in order:
 * the full name at 7pt, at 5pt, wrapped onto two 5pt lines, then shortForm
 * at 7pt and 5pt. A smaller font is preferred over a clipped word, and two
 * lines over an abbreviation. When nothing fits, the full name wraps as
 * plain text with no badge, as it did before there were badges.
 *
 * shortForm may be NULL (the UV and pollen widgets have no abbreviations).
 * Leaves the font at 7pt.
 */
static void drawFittedRiskChip(int16_t x, int16_t y, const String &full,
                               const char *shortForm, int level, int max_w)
{
  const GFXfont *const fonts[2] = {&FONT_7pt8b, &FONT_5pt8b};
  String lineA, lineB;
  for (int f = 0; f < 2; ++f)
  {
    display.setFont(fonts[f]);
    if (getStringWidth(full) <= max_w)
    {
      drawRiskChip(x, y, full, level);
      display.setFont(&FONT_7pt8b);
      return;
    }
  }
  // still at 5pt: two lines before giving up the words
  if (wrapTwoLines(full, max_w, lineA, lineB))
  {
    drawRiskChipWrapped(x, y, lineA, lineB, level);
    display.setFont(&FONT_7pt8b);
    return;
  }
  if (shortForm && full != shortForm)
  {
    for (int f = 0; f < 2; ++f)
    {
      display.setFont(fonts[f]);
      if (getStringWidth(shortForm) <= max_w)
      {
        drawRiskChip(x, y, shortForm, level);
        display.setFont(&FONT_7pt8b);
        return;
      }
    }
  }
  // draw higher to allow room for a 2nd line
  display.setFont(&FONT_5pt8b);
  drawMultiLnString(x, y - 10, full, LEFT, max_w, 2, 10);
  display.setFont(&FONT_7pt8b);
} // end drawFittedRiskChip



/* Returns the string width in pixels
 */
uint16_t getStringWidth(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(shapeText(text), 0, 0, &x1, &y1, &w, &h);
  return w;
}

/* Returns the string height in pixels
 */
uint16_t getStringHeight(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(shapeText(text), 0, 0, &x1, &y1, &w, &h);
  return h;
}

/* Draws a string with alignment
 */
void drawString(int16_t x, int16_t y, const String &text, alignment_t alignment,
                uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextColor(color);
  const String shaped = shapeText(text);
  display.getTextBounds(shaped, x, y, &x1, &y1, &w, &h);
  // x1 is where the ink starts, which for a glyph with a side bearing is a
  // pixel or two right of the cursor; align the ink, not the cursor, so a
  // RIGHT-aligned string ends where it was asked to (Hebrew faces bear
  // more than FreeSans, and at the panel's edge a pixel is the margin).
  const int16_t bearing = x1 - x;
  if (alignment == RIGHT)
  {
    x = x - w - bearing;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2 - bearing;
  }
  display.setCursor(x, y);
  display.print(shaped);
  return;
} // end drawString

/* Draws a string that will flow into the next line when max_width is reached.
 * If a string exceeds max_lines an ellipsis (...) will terminate the last word.
 * Lines will break at spaces(' ') and dashes('-').
 *
 * Note: max_width should be big enough to accommodate the largest word that
 *       will be displayed. If an unbroken string of characters longer than
 *       max_width exist in text, then the string will be printed beyond
 *       max_width.
 */
void drawMultiLnString(int16_t x, int16_t y, const String &text,
                       alignment_t alignment, uint16_t max_width,
                       uint16_t max_lines, int16_t line_spacing,
                       uint16_t color)
{
  uint16_t current_line = 0;
  String textRemaining = text;
  // print until we reach max_lines or no more text remains
  while (current_line < max_lines && !textRemaining.isEmpty())
  {
    int16_t  x1, y1;
    uint16_t w, h;

    display.getTextBounds(shapeText(textRemaining), 0, 0, &x1, &y1, &w, &h);

    int endIndex = textRemaining.length();
    // check if remaining text is to wide, if it is then print what we can
    String subStr = textRemaining;
    int splitAt = 0;
    int keepLastChar = 0;
    while (w > max_width && splitAt != -1)
    {
      if (keepLastChar)
      {
        // if we kept the last character during the last iteration of this while
        // loop, remove it now so we don't get stuck in an infinite loop.
        subStr.remove(subStr.length() - 1);
      }

      // find the last place in the string that we can break it.
      if (current_line < max_lines - 1)
      {
        splitAt = std::max(subStr.lastIndexOf(" "),
                           subStr.lastIndexOf("-"));
      }
      else
      {
        // this is the last line, only break at spaces so we can add ellipsis
        splitAt = subStr.lastIndexOf(" ");
      }

      // if splitAt == -1 then there is an unbroken set of characters that is
      // longer than max_width. Otherwise if splitAt != -1 then we can continue
      // the loop until the string is <= max_width
      if (splitAt != -1)
      {
        endIndex = splitAt;
        subStr = subStr.substring(0, endIndex + 1);

        char lastChar = subStr.charAt(endIndex);
        if (lastChar == ' ')
        {
          // remove this char now so it is not counted towards line width
          keepLastChar = 0;
          subStr.remove(endIndex);
          --endIndex;
        }
        else if (lastChar == '-')
        {
          // this char will be printed on this line and removed next iteration
          keepLastChar = 1;
        }

        if (current_line < max_lines - 1)
        {
          // this is not the last line
          display.getTextBounds(shapeText(subStr), 0, 0, &x1, &y1, &w, &h);
        }
        else
        {
          // this is the last line, we need to make sure there is space for
          // ellipsis
          display.getTextBounds(shapeText(subStr + "..."), 0, 0, &x1, &y1, &w, &h);
          if (w <= max_width)
          {
            // ellipsis fit, add them to subStr
            subStr = subStr + "...";
          }
        }

      } // end if (splitAt != -1)
    } // end inner while

    drawString(x, y + (current_line * line_spacing), subStr, alignment, color);

    // update textRemaining to no longer include what was printed
    // +1 for exclusive bounds, +1 to get passed space/dash
    textRemaining = textRemaining.substring(endIndex + 2 - keepLastChar);

    ++current_line;
  } // end outer while

  return;
} // end drawMultiLnString

/* Initialize e-paper display
 */
void initDisplay()
{
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    delay(20); // let the switched panel rail settle before _reset()
  }
#ifdef DRIVER_WAVESHARE
  display.init(115200, true, 2, false);
#endif
#if defined(DRIVER_DESPI_C02)     && (defined(PANEL_GDEQ0426T82) || defined(GREYS_75))
  // not "initial": GxEPD2 would first wipe the panel white with a full
  // refresh of its own, and then draw the page with a second -- on every
  // wake, since the panel loses its power while the board sleeps. A full
  // refresh does not depend on what the panel showed before.
  display.init(115200, false, 10, false);
#elif defined(DRIVER_DESPI_C02)
  display.init(115200, true, 10, false);
#endif
  // remap spi
  SPI.end();
  SPI.begin(PIN_EPD_SCK,
            PIN_EPD_MISO,
            PIN_EPD_MOSI,
            PIN_EPD_CS);

#if defined(BOARD_XIAO_EE05)
  // the EE05's 24-pin connector puts the 4.26in glass's first row at the
  // bottom of the page as the board sits (seen on the panel, 2026-10-17)
  display.setRotation(2);
#else
  display.setRotation(0);
#endif
  display.setTextSize(1);
  display.setTextColor(DM_FG);
  display.setTextWrap(false);
  // display.fillScreen(GxEPD_WHITE);
  display.setFullWindow();
  display.firstPage(); // use paged drawing mode, sets fillScreen(GxEPD_WHITE)
  return;
} // end initDisplay

/* Power-off e-paper display
 */
void powerOffDisplay()
{
  display.hibernate(); // turns powerOff() and sets controller to deep sleep for
                       // minimum power use
  if (PIN_EPD_PWR != PIN_UNUSED)
  {
    SPI.end();
    pinMode(PIN_EPD_CS, INPUT);
    pinMode(PIN_EPD_DC, INPUT);
    pinMode(PIN_EPD_RST, INPUT);
    pinMode(PIN_EPD_SCK, INPUT);
    pinMode(PIN_EPD_MOSI, INPUT);
    digitalWrite(PIN_EPD_PWR, LOW);
  }
  return;
} // end powerOffDisplay


/* These functions are responsible for drawing the current conditions and
 * associated icons on the left panel.
 */

// drawCurrentSunrise
/* A widget's value with its unit in the smaller font beside it. Left to
 * right the unit follows the number ("19 km/h"); in a right-to-left locale
 * the unit stands to the number's left ("קמ"ש 19"), the way Hebrew lays out
 * "19 קמ"ש". Leaves the cursor after whichever came last, for a further
 * item (the compass point) to follow.
 */
#define G75_SUB_INK DM_FG

static void drawValueUnit(int16_t x, int16_t y, const String &value,
                          const String &unit, const GFXfont *valueFont,
                          const GFXfont *unitFont)
{
  const uint16_t unitInk = G75_SUB_INK;
  if (LC_RTL)
  {
    String u = unit;
    u.trim();
    display.setFont(unitFont);
    drawString(x, y, u, LEFT, unitInk);
    // drawString places by ink, so a trailing space would not advance the
    // cursor: the gap is the unit font's space itself
    const int16_t gap = unitFont->glyph[' ' - unitFont->first].xAdvance;
    display.setFont(valueFont);
    drawString(display.getCursorX() + gap, y, value, LEFT);
    return;
  }
  display.setFont(valueFont);
  drawString(x, y, value, LEFT);
  display.setFont(unitFont);
  drawString(display.getCursorX(), y, unit, LEFT, unitInk);
}

static int16_t shapedAdvance(const GFXfont *font, const String &shaped)
{
  int16_t adv = 0;
  for (size_t i = 0; i < shaped.length(); ++i)
  {
    const uint8_t c = static_cast<uint8_t>(shaped[i]);
    if (c >= font->first && c <= font->last)
    {
      adv += font->glyph[c - font->first].xAdvance;
    }
  }
  return adv;
}

/* Draws a widget's 7pt label at its column origin, plus an optional 5pt
 * parenthesized tag (air quality source or dominant pollen type) when it fits
 * before wgtTagRight(PosX). Left to right the tag follows the label on the
 * right ("Air Quality (EPA)"); in a right-to-left locale the tag stands to
 * the left of the label so Hebrew reads right to left as
 * "איכות אוויר (הגנ"ס)" rather than "(הגנ"ס) איכות אוויר".
 * Leaves the font at FONT_7pt8b.
 */
static void drawWidgetLabelWithTag(int PosX, int PosY, const char *label,
                                   const String &tag)
{
  const int16_t x = 48 + (162 * PosX);
  const int16_t y = wgtLabelY(PosY);
  const int16_t rightLimit = wgtTagRight(PosX);

  if (tag.isEmpty())
  {
    display.setFont(&FONT_7pt8b);
    drawString(x, y, label, LEFT, WGT_LABEL_INK);
    return;
  }

  if (LC_RTL)
  {
    const int16_t labelX = x + shapedAdvance(&FONT_5pt8b, shapeText(tag)) + 3;
    display.setFont(&FONT_7pt8b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(shapeText(label), labelX, y, &x1, &y1, &w, &h);
    if (x1 + static_cast<int16_t>(w) <= rightLimit)
    {
      display.setFont(&FONT_5pt8b);
      drawString(x, y, tag, LEFT, G75_SUB_INK);
      display.setFont(&FONT_7pt8b);
      drawString(display.getCursorX() + 3, y, label, LEFT, WGT_LABEL_INK);
      return;
    }
    drawString(x, y, label, LEFT, WGT_LABEL_INK);
    return;
  }

  display.setFont(&FONT_7pt8b);
  drawString(x, y, label, LEFT, WGT_LABEL_INK);
  display.setFont(&FONT_5pt8b);
  const int16_t tagX = display.getCursorX() + 3;
  if (tagX + getStringWidth(tag) <= rightLimit)
  {
    drawString(tagX, y, tag, LEFT, G75_SUB_INK);
  }
  display.setFont(&FONT_7pt8b);
}

void drawCurrentSunrise(const owm_current_t &current)
{
  if (POS_SUNRISE < 0 || POS_SUNRISE / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_SUNRISE % 2);
  int PosY = static_cast<int>(POS_SUNRISE / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_sunrise_48x48, wi_sunrise_40x40, "sunrise", COLOR_SUN);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_SUNRISE, LEFT, WGT_LABEL_INK);

  // sunrise
  display.setFont(&FONT_12pt8b);
  if (current.sunrise <= 0)
  {
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  char timeBuffer[32] = {};
  time_t ts = current.sunrise;
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(48 + (162 * PosX), wgtValueY(PosY), timeBuffer, LEFT);

  return;
}
// end drawCurrentSunrise

// drawCurrentSunset
void drawCurrentSunset(const owm_current_t &current)
{
  if (POS_SUNSET < 0 || POS_SUNSET / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_SUNSET % 2);
  int PosY = static_cast<int>(POS_SUNSET / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_sunset_48x48, wi_sunset_40x40, "sunset", COLOR_SUN);

  // On the eve of Shabbat or a holiday the widget turns into candle
  // lighting (sunset less the custom's minutes), on the day itself into
  // its close (sunset plus the custom's minutes); any other day it is the
  // sunset. config.json "shabbat".
  const char *label = TXT_SUNSET;
  time_t ts = current.sunset;
  const bool shabbat = (SHABBAT_TIMES < 0) ? LC_SHABBAT_TIMES : (SHABBAT_TIMES > 0);
  if (shabbat && current.sunset > 0)
  {
    time_t page = pageTime();
    tm today;
    localtime_r(&page, &today);
    bool yomTov = false;
    switch (hebcalEvening(&today, yomTov))
    {
      case HEBCAL_EVE_CANDLES:
        label = TXT_CANDLE_LIGHTING;
        ts = current.sunset - CANDLE_LIGHTING_MINUTES * 60;
        break;
      case HEBCAL_EVE_CANDLES_LATE:   // lit after the first holy day ends
        label = TXT_CANDLE_LIGHTING;
        ts = current.sunset + HAVDALAH_MINUTES * 60;
        break;
      case HEBCAL_EVE_HAVDALAH:
        label = yomTov ? TXT_HOLIDAY_ENDS : TXT_SHABBAT_ENDS;
        ts = current.sunset + HAVDALAH_MINUTES * 60;
        break;
      default:
        break;
    }
  }

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), label, LEFT, WGT_LABEL_INK);

  // sunset (or the candle lighting / close that stands in for it)
  display.setFont(&FONT_12pt8b);
  if (ts <= 0)
  {
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  char timeBuffer[32] = {};
  tm *timeInfo = localtime(&ts);
  _strftime(timeBuffer, sizeof(timeBuffer), TIME_FORMAT, timeInfo);
  drawString(48 + (162 * PosX), wgtValueY(PosY), timeBuffer, LEFT);

  return;
}
// end drawCurrentSunset

// drawCurrentWind
void drawCurrentWind(const owm_current_t &current)
{
  if (POS_WIND < 0 || POS_WIND / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_WIND % 2);
  int PosY = static_cast<int>(POS_WIND / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_strong_wind_48x48, wi_strong_wind_40x40, "wind", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_WIND, LEFT, WGT_LABEL_INK);

  // wind
  display.setFont(&FONT_12pt8b);
#ifdef WIND_INDICATOR_ARROW
  display.drawInvertedBitmap(48 + (162 * PosX), wgtY(PosY) + 24 / 2,
                             getWindBitmap24(current.wind_deg),
                             24, 24, DM_FG);
#endif
#ifdef UNITS_SPEED_METERSPERSECOND
  dataStr = String(static_cast<int>(std::round(current.wind_speed)));
  unitStr = String(" ") + TXT_UNITS_SPEED_METERSPERSECOND;
#endif
#ifdef UNITS_SPEED_FEETPERSECOND
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_feetpersecond(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_FEETPERSECOND;
#endif
#ifdef UNITS_SPEED_KILOMETERSPERHOUR
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_kilometersperhour(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_KILOMETERSPERHOUR;
#endif
#ifdef UNITS_SPEED_MILESPERHOUR
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_milesperhour(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_MILESPERHOUR;
#endif
#ifdef UNITS_SPEED_KNOTS
  dataStr = String(static_cast<int>(std::round(
                   meterspersecond_to_knots(current.wind_speed) )));
  unitStr = String(" ") + TXT_UNITS_SPEED_KNOTS;
#endif
#ifdef UNITS_SPEED_BEAUFORT
  dataStr = String(meterspersecond_to_beaufort(current.wind_speed));
  unitStr = String(" ") + TXT_UNITS_SPEED_BEAUFORT;
#endif

#ifdef WIND_INDICATOR_ARROW
  drawValueUnit((48 + 24) + (162 * PosX), wgtValueY(PosY), dataStr, unitStr,
                &FONT_12pt8b, &FONT_8pt8b);
#else
  drawValueUnit(48 + (162 * PosX), wgtValueY(PosY), dataStr, unitStr,
                &FONT_12pt8b, &FONT_8pt8b);
#endif

#if defined(WIND_INDICATOR_NUMBER)
  dataStr = String(current.wind_deg) + "\260";
  display.setFont(&FONT_12pt8b);
  drawString(display.getCursorX() + 6, wgtValueY(PosY),
             dataStr, LEFT);
#endif
#if defined(WIND_INDICATOR_CPN_CARDINAL)                \
 || defined(WIND_INDICATOR_CPN_INTERCARDINAL)           \
 || defined(WIND_INDICATOR_CPN_SECONDARY_INTERCARDINAL) \
 || defined(WIND_INDICATOR_CPN_TERTIARY_INTERCARDINAL)
  dataStr = getCompassPointNotation(current.wind_deg);
  display.setFont(&FONT_12pt8b);
  drawString(display.getCursorX() + 6, wgtValueY(PosY),
             dataStr, LEFT);
#endif

  return;
}
// end drawCurrentWind

// drawCurrentUVI
void drawCurrentUVI(const owm_current_t &current)
{
  if (POS_UVI < 0 || POS_UVI / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_UVI % 2);
  int PosY = static_cast<int>(POS_UVI / 2);

  const bool uviKnown = !std::isnan(current.uvi);
  unsigned int uvi = uviKnown ? static_cast<unsigned int>(
                                std::max(std::round(current.uvi), 0.0f))
                              : 0;

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_day_sunny_48x48, wi_day_sunny_40x40, "uvi", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_UV_INDEX, LEFT, WGT_LABEL_INK);

  // spacing between end of index value and start of descriptor text
  const int sp = 8;

  // uv index
  display.setFont(&FONT_12pt8b);
  if (!uviKnown)
  { // not fetched: no number, and no risk badge
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  dataStr = String(uvi);
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_7pt8b);
  dataStr = String(getUVIdesc(uvi));
  // EPA/WHO UV index colors: green / yellow / orange / red / violet
  const int uviRisk = (uvi <= 2)  ? RISK_GREEN
                    : (uvi <= 5)  ? RISK_YELLOW
                    : (uvi <= 7)  ? RISK_AMBER
                    : (uvi <= 10) ? RISK_RED
                                  : RISK_PURPLE;
  const int16_t chipX = display.getCursorX() + sp;
  const int max_w = (162 + (PosX * 162) - sp) - chipX;
  drawFittedRiskChip(chipX, wgtValueY(PosY), dataStr, nullptr, uviRisk, max_w);
  return;
}
// end drawCurrentUVI

// drawCurrentMoonPhase
// Computed on-device from the date (see calcMoonPhase in sun.cpp); the value
// is the illuminated percentage, the descriptor the phase name.
void drawCurrentMoonPhase()
{
  if (POS_MOON_PHASE < 0 || POS_MOON_PHASE / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr;
  int PosX = (POS_MOON_PHASE % 2);
  int PosY = static_cast<int>(POS_MOON_PHASE / 2);

  int illum = 0;
  int phase = calcMoonPhase(static_cast<int64_t>(pageTime()), &illum);

  // icons
#if defined(GREYS_75)
  if (greys())
  {
    drawPhotoMoon75(162 * PosX, wgtY(PosY), wgtIconSize(),
                    moonPhaseFraction(static_cast<int64_t>(pageTime())),
                    LAT.toFloat() < 0);
  }
  else
#endif
  drawDitheredIcon(162 * PosX, wgtY(PosY),
                   getMoonPhaseDithered(phase, wgtIconSize()),
                   wgtIconSize());

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_MOONPHASE,
             LEFT, WGT_LABEL_INK);

  // spacing between end of value and start of descriptor text
  const int sp = 8;

  // illuminated percentage, then the phase name fitted beside/below it
  display.setFont(&FONT_12pt8b);
  dataStr = String(illum) + "%";
  drawString(48 + (162 * PosX), wgtValueY(PosY),
             dataStr, LEFT);
  display.setFont(&FONT_7pt8b);
  dataStr = String(getMoonPhaseDesc(phase));
  // no badge here, but the same fitting ladder: 7pt, 5pt, then two lines
  const int16_t textX = display.getCursorX() + sp;
  drawFittedRiskChip(textX, wgtValueY(PosY), dataStr, nullptr, RISK_PLAIN,
                     (162 + (PosX * 162) - sp) - textX);
  return;
}
// end drawCurrentMoonPhase

// drawCurrentAirQuality
void drawCurrentAirQuality(const owm_resp_air_pollution_t &owm_air_pollution)
{
  if (POS_AIR_QUALITY < 0 || POS_AIR_QUALITY / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_AIR_QUALITY % 2);
  int PosY = static_cast<int>(POS_AIR_QUALITY / 2);

  // labels
  display.setFont(&FONT_7pt8b);

  // When AirNow reported (us_aqi >= 0), display the official US EPA AQI it
  // provides -- by definition on the United States AQI scale, regardless of
  // the locale's configured AQI_SCALE. Otherwise compute an AQI on the
  // locale's scale from Open-Meteo's pollutant concentrations.
  const bool useAirNow = (owm_air_pollution.us_aqi >= 0);
  // The Israel Ministry of Environmental Protection's index, when the
  // nearest of its stations reported: its own scale (100 best, negative
  // worst) and its own four bands, so it is drawn on its own path below.
  const bool useIL = !useAirNow && owm_air_pollution.il_valid;

  const char *air_quality_index_label;
  if (useAirNow || useIL || aqi_desc_type(AQI_SCALE) == AIR_QUALITY_DESC)
  {
    air_quality_index_label = TXT_AIR_QUALITY;
  }
  else // (aqi_desc_type(AQI_SCALE) == AIR_POLLUTION_DESC)
  {
    air_quality_index_label = TXT_AIR_POLLUTION;
  }
  // Source tag: "(EPA)" when the value is AirNow's, i.e. measured at the
  // EPA's monitoring stations; "(model)" when it is computed from
  // Open-Meteo's CAMS model pollutant fields -- worth telling apart at a
  // glance. Drawn in the smallest font after the label, and left out if it
  // would run into the graph's axis labels.
  const char *tag = useAirNow ? TXT_AQI_TAG_EPA
                  : useIL ? TXT_IL_AQI_TAG : TXT_AQI_TAG_MODEL;
  drawWidgetLabelWithTag(PosX, PosY, air_quality_index_label, tag);

  // spacing between end of index value and start of descriptor text
  const int sp = 8;

  // air quality index
  display.setFont(&FONT_12pt8b);
  if (useIL)
  {
    const int il = owm_air_pollution.il_index;
    const int band = (il > 50) ? 0 : (il >= 0) ? 1 : (il >= -200) ? 2 : 3;
    // the Ministry's legend: good green, medium yellow, low red, very low
    // brown (the red/black dither is the nearest ink mix to its brown)
    static const int IL_BAND_RISK[4] = { RISK_GREEN, RISK_YELLOW, RISK_RED,
                                         RISK_MAROON };
    drawWidgetIcon(162 * PosX, wgtY(PosY),
                   air_filter_48x48, air_filter_40x40, "aqi", DM_FG);
    drawString(48 + (162 * PosX), wgtValueY(PosY), String(il), LEFT);
    display.setFont(&FONT_7pt8b);
    const int16_t chipX = display.getCursorX() + sp;
    const int max_w = (162 + (PosX * 162) - sp) - chipX;
    drawFittedRiskChip(chipX, wgtValueY(PosY), String(TXT_IL_AQI[band]),
                       nullptr, IL_BAND_RISK[band], max_w);
    return;
  }
  if (!useAirNow && !owm_air_pollution.valid)
  { // neither source answered: no number, and no risk badge
    drawWidgetIcon(162 * PosX, wgtY(PosY),
                   air_filter_48x48, air_filter_40x40, "aqi", DM_FG);
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  int aqi;
  int aqi_max;
  if (useAirNow)
  {
    aqi = owm_air_pollution.us_aqi;
    aqi_max = UNITED_STATES_AQI_MAX;
  }
  else
  {
    const owm_components_t &c = owm_air_pollution.components;
    // Open-Meteo does not provide pb (lead) concentrations, so we pass NULL.
    aqi = calc_aqi(AQI_SCALE, c.co, c.nh3, c.no, c.no2, c.o3, NULL, c.so2,
                              c.pm10, c.pm2_5);
    aqi_max = aqi_scale_max(AQI_SCALE);
  }

  // icon (colored by AQI level on multicolor panels; the level thresholds
  // are only defined for the United States AQI scale)
  const bool usScale = useAirNow || (AQI_SCALE == UNITED_STATES_AQI);
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 air_filter_48x48, air_filter_40x40, "aqi", DM_FG);
  if (aqi > aqi_max)
  {
    dataStr = String(TXT_MORE_THAN) + String(aqi_max);
  }
  else
  {
    dataStr = String(aqi);
  }
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_7pt8b);
  dataStr = useAirNow ? String(united_states_aqi_desc(aqi))
                      : String(aqi_desc(AQI_SCALE, aqi));
  // Full EPA color bands (defined for the US scale only)
  const int band = usScaleAqiBand(aqi);
  const int aqiRisk = usScale ? AQI_BAND_RISK[band] : RISK_PLAIN;
  const int16_t chipX = display.getCursorX() + sp;
  const int max_w = (162 + (PosX * 162) - sp) - chipX;
  /* The band names outgrow the column at several values: a three-digit AQI
   * leaves 59px and "> 500" only 39, against 89px for "Very Unhealthy" at
   * 7pt. drawFittedRiskChip works down from the full name; the locale's
   * short form (the EPA's own "USG" for 101-150) is its last resort.
   */
  drawFittedRiskChip(chipX, wgtValueY(PosY), dataStr,
                     usScale ? UNITED_STATES_AQI_SHORT_TXT[band] : nullptr,
                     aqiRisk, max_w);

  return;
}
// end drawCurrentAirQuality

/* Draws a small trend arrow (see history.h TREND_*) after a widget value.
 * x is the cursor position right after the value text; y is the value
 * baseline. TREND_UNKNOWN draws nothing.
 */
static void drawTrendArrow(int16_t x, int16_t y, int trend)
{
  const uint8_t *bmp;
  switch (trend)
  {
  case TREND_RISING:  bmp = wi_direction_up_right_24x24;   break;
  case TREND_STEADY:  bmp = wi_direction_right_24x24;      break;
  case TREND_FALLING: bmp = wi_direction_down_right_24x24; break;
  default: return;
  }
  display.drawInvertedBitmap(x + 1, y - 19, bmp, 24, 24, DM_FG);
} // end drawTrendArrow

// drawCurrentInTemp
void drawCurrentInTemp(float inTemp, int trend)
{
  if (POS_INTEMP < 0 || POS_INTEMP / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_INTEMP % 2);
  int PosY = static_cast<int>(POS_INTEMP / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 house_thermometer_48x48, house_thermometer_40x40, "intemp", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_INDOOR_TEMPERATURE, LEFT, WGT_LABEL_INK);

  // indoor temperature
  display.setFont(&FONT_12pt8b);
  if (!std::isnan(inTemp))
  {
#ifdef UNITS_TEMP_KELVIN
    dataStr = String(std::round(celsius_to_kelvin(inTemp) * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_CELSIUS
    dataStr = String(std::round(inTemp * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    dataStr = String(static_cast<int>(
              std::round(celsius_to_fahrenheit(inTemp))));
#endif
  }
  else
  {
    dataStr = "--";
  }
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
  dataStr += "\260";
#endif
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  drawTrendArrow(display.getCursorX(), wgtValueY(PosY), trend);
  return;
}
// end drawCurrentInTemp

// drawCurrentHumidity
void drawCurrentHumidity(const owm_current_t &current)
{
  if (POS_HUMIDITY < 0 || POS_HUMIDITY / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_HUMIDITY % 2);
  int PosY = static_cast<int>(POS_HUMIDITY / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_humidity_48x48, wi_humidity_40x40, "humidity", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_HUMIDITY, LEFT, WGT_LABEL_INK);

  // humidity
  display.setFont(&FONT_12pt8b);
  dataStr = String(current.humidity);
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX(), wgtValueY(PosY),
             "%", LEFT);
  return;
}
// end drawCurrentHumidity

// drawCurrentPressure
void drawCurrentPressure(const owm_current_t &current, int trend)
{
  if (POS_PRESSURE < 0 || POS_PRESSURE / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_PRESSURE % 2);
  int PosY = static_cast<int>(POS_PRESSURE / 2);
  //  icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_barometer_48x48, wi_barometer_40x40, "pressure", DM_FG);

  //  labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_PRESSURE, LEFT, WGT_LABEL_INK);

  // 0 = not available (the "nws" current-conditions source, or a failed
  // fetch with nothing cached): show a placeholder rather than a fabricated
  // reading, and no trend arrow.
  if (current.pressure <= 0)
  {
    display.setFont(&FONT_12pt8b);
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }

  // pressure
#ifdef UNITS_PRES_HECTOPASCALS
  dataStr = String(current.pressure);
  unitStr = String(" ") + TXT_UNITS_PRES_HECTOPASCALS;
#endif
#ifdef UNITS_PRES_PASCALS
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_pascals(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_PASCALS;
#endif
#ifdef UNITS_PRES_MILLIMETERSOFMERCURY
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_millimetersofmercury(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_MILLIMETERSOFMERCURY;
#endif
#ifdef UNITS_PRES_INCHESOFMERCURY
  dataStr = String(std::round(1e1f *
                   hectopascals_to_inchesofmercury(current.pressure)
                   ) / 1e1f, 1);
  unitStr = String(" ") + TXT_UNITS_PRES_INCHESOFMERCURY;
#endif
#ifdef UNITS_PRES_MILLIBARS
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_millibars(current.pressure) )));
  unitStr = String(" ") + TXT_UNITS_PRES_MILLIBARS;
#endif
#ifdef UNITS_PRES_ATMOSPHERES
  dataStr = String(std::round(1e3f *
                   hectopascals_to_atmospheres(current.pressure) )
                   / 1e3f, 3);
  unitStr = String(" ") + TXT_UNITS_PRES_ATMOSPHERES;
#endif
#ifdef UNITS_PRES_GRAMSPERSQUARECENTIMETER
  dataStr = String(static_cast<int>(std::round(
                   hectopascals_to_gramspersquarecentimeter(current.pressure)
                   )));
  unitStr = String(" ") + TXT_UNITS_PRES_GRAMSPERSQUARECENTIMETER;
#endif
#ifdef UNITS_PRES_POUNDSPERSQUAREINCH
  dataStr = String(std::round(1e2f *
                   hectopascals_to_poundspersquareinch(current.pressure)
                   ) / 1e2f, 2);
  unitStr = String(" ") + TXT_UNITS_PRES_POUNDSPERSQUAREINCH;
#endif
  display.setFont(&FONT_12pt8b);
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX(), wgtValueY(PosY),
             unitStr, LEFT);
  drawTrendArrow(display.getCursorX(), wgtValueY(PosY), trend);

  return;
}
// end drawCurrentPressure

// drawCurrentVisibility
void drawCurrentVisibility(const owm_current_t &current)
{
  if (POS_VISIBILITY < 0 || POS_VISIBILITY / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_VISIBILITY % 2);
  int PosY = static_cast<int>(POS_VISIBILITY / 2);

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 visibility_icon_48x48, visibility_icon_40x40, "visibility", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_VISIBILITY, LEFT, WGT_LABEL_INK);

  // -1 = not available, see drawCurrentPressure
  if (current.visibility < 0)
  {
    display.setFont(&FONT_12pt8b);
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }

  // visibility
  display.setFont(&FONT_12pt8b);
#ifdef UNITS_DIST_KILOMETERS
  float vis = meters_to_kilometers(current.visibility);
  unitStr = String(" ") + TXT_UNITS_DIST_KILOMETERS;
#endif
#ifdef UNITS_DIST_MILES
  float vis = meters_to_miles(current.visibility);
  unitStr = String(" ") + TXT_UNITS_DIST_MILES;
#endif
  // if visibility is less than 1.95, round to 1 decimal place
  // else round to int
  if (vis < 1.95)
  {
    dataStr = String(std::round(10 * vis) / 10.0, 1);
  }
  else
  {
    dataStr = String(static_cast<int>(std::round(vis)));
  }
#ifdef UNITS_DIST_KILOMETERS
  if (vis >= 10)
  {
#endif
#ifdef UNITS_DIST_MILES
  if (vis >= 6)
  {
#endif
    dataStr = String(TXT_MORE_THAN) + dataStr;
  }
  drawValueUnit(48 + (162 * PosX), wgtValueY(PosY), dataStr, unitStr,
                &FONT_12pt8b, &FONT_8pt8b);

  return;
}
// end drawCurrentVisibility

// drawCurrentPollen
void drawCurrentPollen(const pollen_info_t &pollen)
{
  if (POS_POLLEN < 0 || POS_POLLEN / 2 >= WIDGET_ROWS)
  {
    return;
  }
  int PosX = (POS_POLLEN % 2);
  int PosY = static_cast<int>(POS_POLLEN / 2);

  // icons: full-color flower on multicolor panels, line art elsewhere
  const int iconSize = wgtIconSize();
#ifdef MULTICOLOR_DISPLAY
  const uint8_t *ci = getColorWidgetIcon("pollen", iconSize);
  if (ci)
  {
    drawColorIcon(162 * PosX, wgtY(PosY), ci, iconSize);
  }
#elif defined(GREYS_75)
  if (!greys() || !drawGreyWidgetIcon(162 * PosX, wgtY(PosY), "pollen", iconSize))
  {
    drawDitheredIcon(162 * PosX, wgtY(PosY),
                     (iconSize == 40) ? pollen_line_40 : pollen_line_48,
                     iconSize);
  }
#else
  drawDitheredIcon(162 * PosX, wgtY(PosY),
                   (iconSize == 40) ? pollen_line_40 : pollen_line_48,
                   iconSize);
#endif

  // labels: "Pollen", plus a small tag naming whichever type is driving
  // the index, since the value alone cannot say whether it is tree,
  // grass or weed. Types that tie for the highest are joined with "+".
  // Nothing is tagged when the fetch failed or every type reads 0.
  String pollenTag;
  if (pollen.max_upi > 0)
  {
    String types;
    const bool allTie = (pollen.tree  == pollen.max_upi)
                     && (pollen.grass == pollen.max_upi)
                     && (pollen.weed  == pollen.max_upi);
    if (allTie)
    { // "(tree+grass+weed)" overflows the cell; say it in one word
      types = TXT_POLLEN_ALL;
    }
    else
    {
      if (pollen.tree  == pollen.max_upi) {types += TXT_POLLEN_TREE;}
      if (pollen.grass == pollen.max_upi)
      {
        if (!types.isEmpty()) {types += "+";}
        types += TXT_POLLEN_GRASS;
      }
      if (pollen.weed  == pollen.max_upi)
      {
        if (!types.isEmpty()) {types += "+";}
        types += TXT_POLLEN_WEED;
      }
    }
    if (!types.isEmpty())
    {
      pollenTag = "(" + types + ")";
    }
  }
  drawWidgetLabelWithTag(PosX, PosY, TXT_POLLEN, pollenTag);

  // value: highest Universal Pollen Index (0-5) of tree/grass/weed, with
  // the UV-index category words (Google's UPI categories match: 1 very
  // low ... 5 very high). -1 = fetch failed / no key.
  display.setFont(&FONT_12pt8b);
  if (pollen.max_upi < 0)
  {
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  drawString(48 + (162 * PosX), wgtValueY(PosY),
             String(pollen.max_upi), LEFT);
  display.setFont(&FONT_7pt8b);
  const char *desc;
  switch (pollen.max_upi)
  {
  case 0:
  case 1:  desc = TXT_UV_LOW;       break;
  case 2:  desc = TXT_UV_LOW;       break;
  case 3:  desc = TXT_UV_MODERATE;  break;
  case 4:  desc = TXT_UV_HIGH;      break;
  default: desc = TXT_UV_VERY_HIGH; break;
  }
  const int pollenRisk = (pollen.max_upi <= 2) ? RISK_GREEN
                       : (pollen.max_upi == 3) ? RISK_AMBER
                                               : RISK_RED;
  const int sp = 8;
  const int16_t chipX = display.getCursorX() + sp;
  drawFittedRiskChip(chipX, wgtValueY(PosY), desc, nullptr, pollenRisk,
                     (162 + (PosX * 162) - sp) - chipX);
  return;
}
// end drawCurrentPollen

// drawCurrentInHumidit
void drawCurrentInHumidity(float inHumidity)
{
  if (POS_INHUMIDITY < 0 || POS_INHUMIDITY / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_INHUMIDITY % 2);
  int PosY = static_cast<int>(POS_INHUMIDITY / 2);

  // current weather data icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 house_humidity_48x48, house_humidity_40x40, "inhumidity", DM_FG);

  // current weather data labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_INDOOR_HUMIDITY, LEFT, WGT_LABEL_INK);

  // indoor humidity
  display.setFont(&FONT_12pt8b);
  if (!std::isnan(inHumidity))
  {
    dataStr = String(static_cast<int>(std::round(inHumidity)));
  }
  else
  {
    dataStr = "--";
  }
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX(), wgtValueY(PosY),
             "%", LEFT);
  return;
}
// end drawCurrentInHumidity


/* Heat stress: the Israel Meteorological Service's discomfort index for the
 * current temperature and humidity, with its level on a chip in the IMS
 * colours (none green ... extreme maroon). IMS's own figure when IMS is the
 * source, computed on the device otherwise; it is what Israeli forecasts
 * report where American ones give the dew point.
 */
void drawCurrentHeatStress(const owm_current_t &current)
{
  if (POS_HEAT_STRESS < 0 || POS_HEAT_STRESS / 2 >= WIDGET_ROWS)
  {
    return;
  }
  int PosX = (POS_HEAT_STRESS % 2);
  int PosY = static_cast<int>(POS_HEAT_STRESS / 2);

  // icons (no colour set entry yet: line art on every panel)
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_hot_48x48, wi_thermometer_40x40, "heat_stress", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_HEAT_STRESS, LEFT, WGT_LABEL_INK);

  // IMS reports the index and its level itself; any other source (and a
  // page held through an outage) gets them computed from temperature and
  // humidity.
  float index;
  int level;
  if (current.heat_stress > 0.f && current.heat_stress_level >= 0)
  {
    index = current.heat_stress;
    level = std::min(current.heat_stress_level, 5);
  }
  else
  {
    index = heatStressIndex(kelvin_to_celsius(current.temp), current.humidity);
    level = heatStressLevel(index);
  }

  display.setFont(&FONT_12pt8b);
  if (level < 0)
  {
    drawString(48 + (162 * PosX), wgtValueY(PosY), "--", LEFT);
    return;
  }
  String dataStr = String(static_cast<int>(std::floor(index + 0.4999f)));
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_7pt8b);
  static const int HEAT_RISK[6] = { RISK_GREEN, RISK_YELLOW, RISK_AMBER,
                                    RISK_RED,   RISK_PURPLE, RISK_MAROON };
  const int sp = 8;
  const int16_t chipX = display.getCursorX() + sp;
  const int max_w = (162 + (PosX * 162) - sp) - chipX;
  drawFittedRiskChip(chipX, wgtValueY(PosY), String(TXT_HEAT_STRESS_LEVEL[level]),
                     nullptr, HEAT_RISK[level], max_w);
} // end drawCurrentHeatStress

// drawCurrentDewpoint
void drawCurrentDewpoint(const owm_current_t &current)
{
  if (POS_DEWPOINT < 0 || POS_DEWPOINT / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_DEWPOINT % 2);
  int PosY = static_cast<int>(POS_DEWPOINT / 2);
  
  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_thermometer_48x48, wi_thermometer_40x40, "dewpoint", DM_FG);
#ifdef MULTICOLOR_DISPLAY
  if (!getColorWidgetIcon("dewpoint", (WIDGET_ROWS == 5) ? 48 : 40))
#elif defined(GREYS_75)
  if (!greys())
#endif
  display.drawInvertedBitmap(162 * PosX + 48 - 24, wgtY(PosY) + 4,
                             wi_raindrops_24x24, 24, 24, DM_FG);
  
  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_DEWPOINT, LEFT, WGT_LABEL_INK);

  // Dew point
  display.setFont(&FONT_12pt8b);
  if (!std::isnan(current.dew_point))
  {
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(std::round(current.dew_point * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(std::round(kelvin_to_celsius(current.dew_point) * 10) / 10.0f, 1);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_fahrenheit(current.dew_point))));
#endif
  }
  else
  {
    dataStr = "--";
  }
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
  dataStr += "\260";
#endif
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  return;
}
// end drawCurrentDewpoint

//End defining functions for left panel.

/* This function is responsible for drawing the current conditions and
 * associated icons.
 */
void drawCurrentConditions(const owm_current_t &current,
                           const owm_daily_t &today,
                           const owm_resp_air_pollution_t &owm_air_pollution,
                           const pollen_info_t &pollen,
                           float inTemp, float inHumidity)
{
  String dataStr, unitStr;
  // current weather icon (full-color where the panel and icon set allow,
  // otherwise single-color line art)
#ifdef MULTICOLOR_DISPLAY
  const uint8_t *colorIcon = getColorIcon168(current);
  if (colorIcon)
  {
    drawColorIcon(14, 14, colorIcon, 168); // centered in the 196px slot
  }
  else
#endif
  drawConditionIcon(0, alertRibbon75() ? 16 : 0,
                    getCurrentConditionsBitmap196(current, today),
                    196, getCurrentConditionsColor196(current),
                    current.weather.id);

  const int16_t tempDy = alertRibbon75() ? 10 : 0;

  // current temp
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(static_cast<int>(std::round(current.temp)));
  unitStr = TXT_UNITS_TEMP_KELVIN;
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_celsius(current.temp))));
  unitStr = TXT_UNITS_TEMP_CELSIUS;
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(static_cast<int>(
            std::round(kelvin_to_fahrenheit(current.temp))));
  unitStr = TXT_UNITS_TEMP_FAHRENHEIT;
#endif
  // FONT_**_temperature fonts only have the character set used for displaying
  // temperature (0123456789.-\260)
  display.setFont(&FONT_48pt8b_temperature);
#ifndef DISP_BW_V1
    drawString(196 + 164 / 2 - 20, 196 / 2 + 69 / 2 + tempDy, dataStr, CENTER);
#elif defined(DISP_BW_V1)
    drawString(156 + 164 / 2 - 20, 196 / 2 + 69 / 2 + tempDy, dataStr, CENTER);
#endif
  display.setFont(&FONT_14pt8b);
  drawString(display.getCursorX(), 196 / 2 - 69 / 2 + 20 + tempDy, unitStr, LEFT,
             G75_SUB_INK);

  // current feels like
#ifdef UNITS_TEMP_KELVIN
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(current.feels_like)));
#endif
#ifdef UNITS_TEMP_CELSIUS
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(
                     kelvin_to_celsius(current.feels_like))))
            + '\260';
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  dataStr = String(TXT_FEELS_LIKE) + ' '
            + String(static_cast<int>(std::round(
                     kelvin_to_fahrenheit(current.feels_like))))
            + '\260';
#endif
  display.setFont(&FONT_12pt8b);
#ifndef DISP_BW_V1
  drawString(196 + 164 / 2, 98 + 69 / 2 + 12 + 17 + tempDy, dataStr, CENTER,
             G75_SUB_INK);
#elif defined(DISP_BW_V1)
  drawString(156 + 164 / 2, 98 + 69 / 2 + 12 + 17 + tempDy, dataStr, CENTER,
             G75_SUB_INK);
#endif

  // condition description, when the provider supplies one (IMS gives a
  // short phrase per weather code, localized to OWM_LANG). The NWS path
  // leaves it empty: its "short forecast" is too long for this slot.
  if (!current.weather.description.isEmpty())
  {
    display.setFont(&FONT_7pt8b);
#ifndef DISP_BW_V1
    drawString(196 + 164 / 2, 98 + 69 / 2 + 12 + 17 + 17 + tempDy,
               current.weather.description, CENTER);
#elif defined(DISP_BW_V1)
    drawString(156 + 164 / 2, 98 + 69 / 2 + 12 + 17 + 17 + tempDy,
               current.weather.description, CENTER);
#endif
  }
  // line dividing top and bottom display areas
  // display.drawLine(0, 196, DISP_WIDTH - 1, 196, DM_FG);

  // draw current data of the left panel.
  // Each widget function checks its own runtime position (config.json
  // "widget_positions") and no-ops if disabled (-1).

  drawCurrentSunrise(current);
  drawCurrentSunset(current);
  drawCurrentWind(current);
  drawCurrentHumidity(current);
  drawCurrentUVI(current);
  drawCurrentPressure(current, historyPressureTrend());
  drawCurrentVisibility(current);
  drawCurrentPollen(pollen);
  drawCurrentAirQuality(owm_air_pollution);
  drawCurrentInTemp(inTemp, historyIndoorTrend());
  drawCurrentInHumidity(inHumidity);
  drawCurrentDewpoint(current);
  drawCurrentHeatStress(current);
  drawCurrentMoonPhase();

  // end drawing left panel

  return;
} // end drawCurrentConditions

/* This function is responsible for drawing the five day forecast.
 */
void drawForecast(const owm_daily_t *daily, tm timeInfo)
{
  // 5 day, forecast
  String hiStr, loStr;
  String dataStr, unitStr;
  // FORECAST_DAYS (config.json, range [5-7]) sets how many columns the row
  // is divided into. At 6+ days the narrower columns get 48px icons and a
  // smaller temperature font. The smaller DISP_BW_V1 panel is fixed at 5.
#ifndef DISP_BW_V1
  const int days = FORECAST_DAYS;
  // In the 6/7-day layouts the row also reclaims the unused strip between
  // the "Feels Like" text (worst case ends ~x365) and the first column,
  // widening every column.
  const int xStart = (days > 5) ? 372 : 398;
  const float colW = (DISP_WIDTH - xStart) / static_cast<float>(days);
#elif defined(DISP_BW_V1)
  const int days = 5;
  const float colW = 64;
  const int xStart = 318;
#endif
  const int iconSize = (colW >= 72) ? 64 : 48;
  const GFXfont *tempFont = (colW >= 72) ? &FONT_8pt8b : &FONT_7pt8b;
#ifndef MULTICOLOR_DISPLAY
  // On single-color panels the Hi|Lo temperatures are all one color, so in
  // the narrower 6/7-day layouts one day's low visually runs into the next
  // day's high. A faint dotted divider between columns keeps the days
  // separated. (Multicolor panels don't need it -- red highs and blue lows
  // alternate, which separates the pairs on their own.)
#if defined(GREYS_75)
  if (days > 5 && !greys())
#else
  if (days > 5)
#endif
  {
    for (int i = 1; i < days; ++i)
    {
      int xDiv = xStart + static_cast<int>(i * colW);
      for (int y = 66; y <= 196; y += 3)
      {
        display.drawPixel(xDiv, y, DM_FG);
      }
    }
  }
#endif
  // Day of week labels. Left to right the row shows the abbreviation (%a).
  // Hebrew abbreviates its days to a single letter (א', ב'), terse for a
  // column this wide, so a right-to-left locale uses the longest form in
  // which all seven names fit the column, decided once for the whole row:
  // at 11 pt the full name (יום ראשון), else the name without its "יום"
  // word (ראשון); then the same two at 8 pt (the sizes every panel's font
  // table has); else the letters. Size comes before length: a row of
  // "ראשון" at 11 pt reads better than one of "יום ראשון" at 8 pt.
  int rtlDayForm = 2;                  // 0 full name, 1 without "יום", 2 letters
  const GFXfont *rtlDayFont = &FONT_11pt8b;
  if (LC_RTL)
  {
    const int maxW = static_cast<int>(colW) - 6;
    const GFXfont *const fonts[2] = {&FONT_11pt8b, &FONT_8pt8b};
    for (const GFXfont *f : fonts)
    {
      display.setFont(f);
      for (int form = 0; form < 2 && rtlDayForm == 2; ++form)
      {
        int widest = 0;
        for (int d = 0; d < 7; ++d)
        {
          String name = LC_DAY[d];
          int sp = name.indexOf(' ');
          if (form == 1 && sp > 0) name = name.substring(sp + 1);
          widest = std::max<int>(widest, getStringWidth(name));
        }
        if (widest <= maxW)
        {
          rtlDayForm = form;
          rtlDayFont = f;
        }
      }
      if (rtlDayForm < 2) break;
    }
  }
  for (int i = 0; i < days; ++i)
  {
    if (daily[i].dt <= 0 || std::isnan(daily[i].temp.max))
    { // the forecast did not reach this day
      continue;
    }
    // column center; the icon's vertical center matches the old layout.
    // Right to left, today takes the rightmost column and the days run
    // leftwards, as a Hebrew calendar row reads.
    const int col = LC_RTL ? (days - 1 - i) : i;
    const int fcDy = alertRibbon75() ? 4 : 0;
    int cx = xStart + static_cast<int>(col * colW + colW / 2);
    int iconX = cx - iconSize / 2;
    int iconY = 98 + 69 / 2 - 6 - iconSize / 2 + fcDy;
#if defined(GREYS_75)
    if (greys() && (col & 1))
    { // rounded cards on alternating days
      const int x0 = xStart + static_cast<int>(col * colW);
      const int x1 = xStart + static_cast<int>((col + 1) * colW);
      const int cardY = alertRibbon75() ? 66 : 58;
      const int cardH = alertRibbon75() ? 131 : 140;
      display.fillRoundRect(x0 + 1, cardY, x1 - x0 - 2, cardH, 4, G75_LGW4);
    }
#endif
    // icons
#ifdef MULTICOLOR_DISPLAY
    const uint8_t *colorIcon = getColorIconDaily(daily[i], iconSize);
    if (colorIcon)
    {
      drawColorIcon(iconX, iconY, colorIcon, iconSize);
    }
    else
#endif
    drawConditionIcon(iconX, iconY,
                      (iconSize == 64)
                          ? getDailyForecastBitmap64(daily[i])
                          : getDailyForecastBitmap48(daily[i]),
                      iconSize, getDailyForecastColor64(daily[i]),
                      daily[i].weather.id);
    // day of week label (see above)
    display.setFont(&FONT_11pt8b);
    char dayBuffer[32] = {};
    _strftime(dayBuffer, sizeof(dayBuffer), "%a", &timeInfo); // abbrv'd day
    String dayStr = dayBuffer;
    if (LC_RTL)
    {
      if (rtlDayForm < 2)
      {
        dayStr = LC_DAY[timeInfo.tm_wday];
        int sp = dayStr.indexOf(' ');
        if (rtlDayForm == 1 && sp > 0) dayStr = dayStr.substring(sp + 1);
      }
      display.setFont(rtlDayFont);
    }
    drawString(cx - 2, 98 + 69 / 2 - 32 - 26 - 6 + 16 + fcDy, dayStr, CENTER);
    timeInfo.tm_wday = (timeInfo.tm_wday + 1) % 7; // increment to next day

    // high | low
    display.setFont(tempFont);
#if defined(GREYS_75)
    drawString(cx, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, "|", CENTER,
               greys() ? G_LG : DM_FG);
#else
    drawString(cx, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, "|", CENTER);
#endif
#ifdef UNITS_TEMP_KELVIN
    hiStr = String(static_cast<int>(std::round(daily[i].temp.max)));
    loStr = String(static_cast<int>(std::round(daily[i].temp.min)));
#endif
#ifdef UNITS_TEMP_CELSIUS
    hiStr = String(static_cast<int>(
                std::round(kelvin_to_celsius(daily[i].temp.max)))) +
            "\260";
    loStr = String(static_cast<int>(
                std::round(kelvin_to_celsius(daily[i].temp.min)))) +
            "\260";
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    hiStr = String(static_cast<int>(
                std::round(kelvin_to_fahrenheit(daily[i].temp.max)))) +
            "\260";
    loStr = String(static_cast<int>(
                std::round(kelvin_to_fahrenheit(daily[i].temp.min)))) +
            "\260";
#endif
    // In dark mode the hi/lo inks (brick red, navy) are dim on black, so
    // the digits are overstruck 1px right for a fake-bold weight.
    for (int pass = 0; pass <= (DARK_MODE ? 1 : 0); ++pass)
    {
#ifdef TEMP_ORDER_HL
      drawString(cx - 4 + pass, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, hiStr, RIGHT,
                 DM_TEXT(COLOR_TEMP_HI));
      drawString(cx + 5 + pass, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, loStr, LEFT,
                 LO_INK);
#endif
#ifdef TEMP_ORDER_LH
      drawString(cx - 4 + pass, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, loStr, RIGHT,
                 LO_INK);
      drawString(cx + 5 + pass, 98 + 69 / 2 + 38 - 6 + 12 + fcDy, hiStr, LEFT,
                 DM_TEXT(COLOR_TEMP_HI));
#endif
    }

// daily forecast precipitation
#if DISPLAY_DAILY_PRECIP
    float dailyPrecip;
    bool  showPrecip = true;
    bool  trace = false; // an amount above zero that rounds to zero
#if defined(UNITS_DAILY_PRECIP_POP)
    dailyPrecip = daily[i].pop * 100;
    dataStr = String(static_cast<int>(dailyPrecip));
    unitStr = "%";
#else
    // Liquid-equivalent amount, NaN when neither source covered the day
    // (precip.h) -- then nothing is drawn rather than "nan".
    //
    // A TRACE -- rain is forecast and the source expects some, but less than
    // the display's 0.1 in / 0.1 cm / 1 mm step -- is written as "<0.1 in"
    // rather than "0.0 in", and is never hidden by smart mode: a rain icon
    // over a blank would otherwise read as a gap where the data says
    // "a little". A true zero still shows nothing in smart mode.
    const float rawMm = daily[i].snow + daily[i].rain;
    showPrecip = !std::isnan(rawMm);
#if defined(UNITS_DAILY_PRECIP_MILLIMETERS)
    dailyPrecip = std::round(rawMm);
    trace = (rawMm > 0.0f && dailyPrecip <= 0.0f);
    dataStr = trace ? String("<1") : String(static_cast<int>(dailyPrecip));
    unitStr = String(" ") + TXT_UNITS_PRECIP_MILLIMETERS;
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
    dailyPrecip = std::round(millimeters_to_centimeters(rawMm) * 10) / 10.0f;
    trace = (rawMm > 0.0f && dailyPrecip <= 0.0f);
    dataStr = trace ? String("<0.1") : String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_CENTIMETERS;
#elif defined(UNITS_DAILY_PRECIP_INCHES)
    dailyPrecip = std::round(millimeters_to_inches(rawMm) * 10) / 10.0f;
    trace = (rawMm > 0.0f && dailyPrecip <= 0.0f);
    dataStr = trace ? String("<0.1") : String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_INCHES;
#endif
#endif
#if (DISPLAY_DAILY_PRECIP == 2) // smart: a dry day shows nothing
    showPrecip = showPrecip && (dailyPrecip > 0.0f || trace);
#endif
    if (showPrecip)
    {
      display.setFont(&FONT_6pt8b);
      drawString(cx, 98 + 69 / 2 + 38 - 6 + 26 + fcDy,
                 dataStr + unitStr, CENTER, DM_TEXT(COLOR_PRECIP));
    }
#endif // DISPLAY_DAILY_PRECIP
    }

    return;
  } // end drawForecast

#ifdef MULTICOLOR_DISPLAY
/* Draws an inverted 1bpp bitmap in two inks alternating in a checker, which
 * mixes them into a color the panel has no ink for. Dithering two INKS is
 * what makes this usable on line art: every pixel of the stroke is still
 * covered, where mixing an ink with white would punch paper holes through
 * thin strokes (verified on the E1002 -- a blue/white snowflake falls apart
 * at 32px, a blue/green one keeps its shape).
 */
static void drawDitheredBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                               int16_t w, int16_t h, uint16_t inkA,
                               uint16_t inkB)
{
  const int16_t byteWidth = (w + 7) / 8;
  for (int16_t j = 0; j < h; ++j)
  {
    for (int16_t i = 0; i < w; ++i)
    {
      const uint8_t b = pgm_read_byte(&bitmap[j * byteWidth + (i >> 3)]);
      if (!((b >> (7 - (i & 7))) & 1))
      { // inverted bitmap: a cleared bit is ink
        display.drawPixel(x + i, y + j, ((i + j) & 1) ? inkA : inkB);
      }
    }
  }
} // end drawDitheredBitmap
#endif

/* Draws an alert's icon, sized 48 or 32, in an ink chosen from its category.
 *
 * Black/white and 3-color panels keep the single ACCENT_COLOR every alert
 * has always used. On multicolor panels the color carries meaning:
 *   blue   water and cold (winter, flood, tsunami)
 *   gold   lightning, as a yellow/red checker -- flat yellow is the
 *          palette's weakest ink on white, and the bolt is a solid enough
 *          glyph to hold a dither even at 32px
 *   red    life-threatening events (tornado, hurricane, fire, heat,
 *          volcano, nuclear, biohazard...) and the four marine icons,
 *          which are red warning flags in reality
 *   black  the low-urgency haze group, left quiet beside the alert text
 */
#if defined(MULTICOLOR_DISPLAY) || defined(GREYS_75)
/* Paints a rounded badge in an alert's severity level (1 = advisory/yellow,
 * 2 = watch/orange dither, 3 = warning/red on multicolor; 1 = light tint,
 * 2 = light grey + black outline, 3 = solid black on GREYS_75). Returns the
 * text ink to draw inside the badge.
 */
static uint16_t fillAlertBadge(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int color)
{
  const int16_t w = x1 - x0 + 1;
  const int16_t h = y1 - y0 + 1;
#ifdef MULTICOLOR_DISPLAY
  if (color == 2)
  {
    fillRoundRectDithered(x0, y0, w, h, 4, GxEPD_RED, GxEPD_YELLOW);
    return GxEPD_BLACK;
  }
  display.fillRoundRect(x0, y0, w, h, 4,
                        (color == 1) ? GxEPD_YELLOW : GxEPD_RED);
  return (color == 1) ? GxEPD_BLACK : GxEPD_WHITE;
#else
  if (color == 3)
  {
    display.fillRoundRect(x0, y0, w, h, 4, GxEPD_BLACK);
    return GxEPD_WHITE;
  }
  if (color == 2)
  {
    display.fillRoundRect(x0, y0, w, h, 4, G_LG);
    display.drawRoundRect(x0, y0, w, h, 4, GxEPD_BLACK);
    return GxEPD_BLACK;
  }
  display.fillRoundRect(x0, y0, w, h, 4, G75_LGW);
  display.drawRoundRect(x0, y0, w, h, 4, G_LG);
  return GxEPD_BLACK;
#endif
}

static String clipAlertLine(const String &text, int maxW)
{
  if (getStringWidth(text) <= maxW)
  {
    return text;
  }
  String s = text;
  while (s.length() > 1 && getStringWidth(s + "...") > maxW)
  {
    const int sp = s.lastIndexOf(' ');
    if (sp > 0 && getStringWidth(s.substring(0, sp) + "...") <= maxW)
    {
      return s.substring(0, sp) + "...";
    }
    int k = static_cast<int>(s.length()) - 1;
    while (k > 0 && (static_cast<uint8_t>(s.charAt(k)) & 0xC0) == 0x80)
    {
      --k;
    }
    s.remove(k);
  }
  return s + "...";
}

static void drawAlertBadgeOneLine(int16_t x, int16_t y, const String &text,
                                  alignment_t align, int max_w, int color)
{
  const int16_t padX = 5;
  const String line = clipAlertLine(text, std::max(16, max_w - 2 * padX));
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(shapeText(line), 0, y, &bx, &by, &bw, &bh);
  const int16_t x0 = (align == RIGHT) ? (x - static_cast<int16_t>(bw) - 2 * padX)
                                      : x;
  const int16_t x1 = (align == RIGHT) ? x
                                      : (x + static_cast<int16_t>(bw) + 2 * padX);
  const int16_t y0 = by - 3;
  const int16_t y1 = by + static_cast<int16_t>(bh) + 3;
  const uint16_t ink = fillAlertBadge(x0, y0, x1, y1, color);
  const int16_t tx = (align == RIGHT) ? (x - padX) : (x + padX - bx);
  drawString(tx, y, line, align, ink);
}

static void drawAlertBadgeTwoLines(int16_t x, int16_t y, const String &text,
                                   alignment_t align, int max_w,
                                   int16_t spacing, int color)
{
  const int16_t padX = 5;
  const int innerW = std::max(16, max_w - 2 * padX);
  String lineA, lineB;
  if (!wrapTwoLines(text, innerW, lineA, lineB))
  {
    const int sp = text.lastIndexOf(' ');
    if (sp > 0)
    {
      lineA = clipAlertLine(text.substring(0, sp), innerW);
      lineB = clipAlertLine(text.substring(sp + 1), innerW);
    }
    else
    {
      drawAlertBadgeOneLine(x, y + spacing / 2, text, align, max_w, color);
      return;
    }
  }
  int16_t bxA, byA, bxB, byB;
  uint16_t bwA, bhA, bwB, bhB;
  display.getTextBounds(shapeText(lineA), 0, y, &bxA, &byA, &bwA, &bhA);
  display.getTextBounds(shapeText(lineB), 0, y + spacing, &bxB, &byB, &bwB, &bhB);
  const int16_t bw = static_cast<int16_t>(std::max(bwA, bwB));
  const int16_t x0 = (align == RIGHT) ? (x - bw - 2 * padX) : x;
  const int16_t x1 = (align == RIGHT) ? x : (x + bw + 2 * padX);
  const int16_t y0 = byA - 3;
  const int16_t y1 = byB + static_cast<int16_t>(bhB) + 3;
  const uint16_t ink = fillAlertBadge(x0, y0, x1, y1, color);
  drawString((align == RIGHT) ? (x - padX) : (x + padX - bxA), y, lineA, align, ink);
  drawString((align == RIGHT) ? (x - padX) : (x + padX - bxB), y + spacing, lineB, align, ink);
}
#endif // MULTICOLOR_DISPLAY || GREYS_75

#if defined(GREYS_75)
static int alertGreySeverity(const owm_alerts_t &alert)
{
  const int ims = alertColorSeverity(alert);
  if (ims > 0) return ims;
  String ev = alert.event;
  ev.toLowerCase();
  if (ev.indexOf("warning") >= 0 || ev.indexOf("emergency") >= 0
      || ev.indexOf("evacuation") >= 0)
  {
    return 3;
  }
  if (ev.indexOf("watch") >= 0)
  {
    return 2;
  }
  return 1;
}
#endif

static void drawAlertIcon(int16_t x, int16_t y, const owm_alerts_t &alert,
                          int size)
{
  const uint8_t *bitmap = (size == 48) ? getAlertBitmap48(alert)
                                       : getAlertBitmap32(alert);
#ifndef MULTICOLOR_DISPLAY
  display.drawInvertedBitmap(x, y, bitmap, size, size,
                             DARK_MODE ? DM_GFX(COLOR_SUN) : ACCENT_COLOR);
#else
  uint16_t ink = GxEPD_RED;
  bool dither = false;
  const int color = alertColorSeverity(alert);
  if (color == 1)
  {
    // Yellow line art on white e-paper has low contrast; draw the icon crisp
    // in foreground ink beside the yellow badge.
    ink = GxEPD_BLACK;
  }
  else if (color == 2)
  {
    dither = true;
  }
  else if (color == 3)
  {
    ink = GxEPD_RED;
  }
  else
  {
    switch (getAlertCategory(alert))
    {
    case WINTER: case FLOOD: case TSUNAMI:
      ink = GxEPD_BLUE;
      break;
    case LIGHTNING:
      dither = true;
      break;
    case SMOG: case SMOKE: case FOG: case DUST: case SANDSTORM:
    case AIR_QUALITY: case STRONG_WIND:
      ink = GxEPD_BLACK;
      break;
    default: // tornado, hurricane, fire, heat, marine flags, unknown alerts
      break;
    }
  }
  if (DARK_MODE)
  { // navy and black both vanish on a black ground; flat yellow beats the
    // checker there, since yellow is the strongest ink against black
    if (dither) {dither = false; ink = COLOR_SUN;}
    else if (ink == GxEPD_BLUE || ink == GxEPD_BLACK) {ink = GxEPD_WHITE;}
  }
  if (dither)
  {
    drawDitheredBitmap(x, y, bitmap, size, size, GxEPD_YELLOW, GxEPD_RED);
  }
  else
  {
    display.drawInvertedBitmap(x, y, bitmap, size, size, ink);
  }
#endif
} // end drawAlertIcon

#if defined(GREYS_75)
struct RibbonIconBox
{
  int16_t minX, minY, srcW, srcH, dstW, dstH;
};

/* Finds the inked bounding box of a srcSize x srcSize inverted 1bpp bitmap and
 * computes a proportionally fitted box up to targetH px tall.
 */
static RibbonIconBox measureRibbonIcon(const uint8_t *bmp, int16_t srcSize,
                                       int16_t targetH)
{
  const int16_t byteW = (srcSize + 7) / 8;
  int16_t minX = srcSize, minY = srcSize, maxX = -1, maxY = -1;
  for (int16_t sy = 0; sy < srcSize; ++sy)
  {
    for (int16_t sx = 0; sx < srcSize; ++sx)
    {
      const uint8_t b = pgm_read_byte(&bmp[sy * byteW + (sx >> 3)]);
      if (!((b >> (7 - (sx & 7))) & 1))
      {
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
      }
    }
  }
  if (maxX < minX) return {0, 0, 0, 0, 0, 0};
  const int16_t srcW = maxX - minX + 1;
  const int16_t srcH = maxY - minY + 1;
  int16_t dstW, dstH;
  if (srcH <= targetH && srcW <= targetH + 2)
  {
    dstW = srcW;
    dstH = srcH;
  }
  else
  {
    dstH = targetH;
    dstW = std::max<int16_t>(1, (srcW * dstH + srcH / 2) / srcH);
    if (dstW > targetH + 2)
    {
      dstW = targetH + 2;
      dstH = std::max<int16_t>(1, (srcH * dstW + srcW / 2) / srcW);
    }
  }
  return {minX, minY, srcW, srcH, dstW, dstH};
}

/* Renders an inverted 1bpp bitmap using precomputed RibbonIconBox bounds,
 * max-pooling each mapped source box so 1-2 px strokes stay unbroken.
 */
static void drawMeasuredRibbonIcon(int16_t x, int16_t yCenter, const uint8_t *bmp,
                                   int16_t srcSize, const RibbonIconBox &box,
                                   uint16_t ink)
{
  if (box.dstW <= 0 || box.dstH <= 0) return;
  const int16_t byteW = (srcSize + 7) / 8;
  const int16_t y0 = yCenter - box.dstH / 2;
  for (int16_t dy = 0; dy < box.dstH; ++dy)
  {
    const int16_t sy0 = box.minY + (dy * box.srcH) / box.dstH;
    const int16_t sy1 = std::max<int16_t>(
        sy0 + 1, box.minY + ((dy + 1) * box.srcH) / box.dstH);
    for (int16_t dx = 0; dx < box.dstW; ++dx)
    {
      const int16_t sx0 = box.minX + (dx * box.srcW) / box.dstW;
      const int16_t sx1 = std::max<int16_t>(
          sx0 + 1, box.minX + ((dx + 1) * box.srcW) / box.dstW);
      int16_t hits = 0;
      const int16_t area = (sy1 - sy0) * (sx1 - sx0);
      for (int16_t sy = sy0; sy < sy1; ++sy)
      {
        for (int16_t sx = sx0; sx < sx1; ++sx)
        {
          const uint8_t b = pgm_read_byte(&bmp[sy * byteW + (sx >> 3)]);
          if (!((b >> (7 - (sx & 7))) & 1)) ++hits;
        }
      }
      if (hits * 2 >= area) display.drawPixel(x + dx, y0 + dy, ink);
    }
  }
}

/* Draws the full-width top alert ticker ribbon across y = 0..23 on the 4-grey
 * 7.5in page, scaling visual urgency across the four physical grey levels.
 * Returns true if the caller (drawAlerts) should return without drawing pills.
 */
static bool drawAlertsRibbon75(std::vector<owm_alerts_t> &alerts)
{
  if (!greys()) return false;
  static const int16_t RIBBON_H = 24;
  if (!display.inBand(0, RIBBON_H)) return true;

  int ignore_list[OWM_NUM_ALERTS] = {0};
  int alert_indices[OWM_NUM_ALERTS] = {0};
  filterAlerts(alerts, ignore_list, OWM_NUM_ALERTS);

  int num_valid = 0;
  const int cap = std::min<int>(static_cast<int>(alerts.size()), OWM_NUM_ALERTS);
  for (int i = 0; i < cap; ++i)
  {
    if (!ignore_list[i])
    {
      alert_indices[num_valid++] = i;
    }
  }
  if (num_valid == 0) return true;

  const int show = std::min(num_valid, 2);
  int maxSev = 1;
  for (int k = 0; k < num_valid; ++k)
  {
    maxSev = std::max(maxSev, alertGreySeverity(alerts[alert_indices[k]]));
  }

  const uint8_t *bmps[2] = {nullptr, nullptr};
  RibbonIconBox iconBoxes[2] = {};
  String titles[2];
  for (int k = 0; k < show; ++k)
  {
    owm_alerts_t &cur = alerts[alert_indices[k]];
    bmps[k] = getAlertBitmap32(cur);
    iconBoxes[k] = measureRibbonIcon(bmps[k], 32, 15);
    toTitleCase(cur.event);
    titles[k] = cur.event;
  }

  uint16_t barFill = G75_LGW4, ruleInk = G_LG, tabFill = G_DG;
  uint16_t tabInk = GxEPD_WHITE, textInk = GxEPD_BLACK, sepInk = G_LG;
  uint16_t extraBg = G_DG, extraInk = GxEPD_WHITE;
  if (maxSev == 2)
  {
    barFill = G75_LGW;
    ruleInk = GxEPD_BLACK;
    tabFill = GxEPD_BLACK;
    sepInk = G_DG;
    extraBg = GxEPD_BLACK;
  }
  else if (maxSev == 3)
  {
    barFill = GxEPD_BLACK;
    ruleInk = GxEPD_BLACK;
    tabFill = GxEPD_BLACK;
    sepInk = G_LG;
    textInk = GxEPD_WHITE;
    extraBg = GxEPD_WHITE;
    extraInk = GxEPD_BLACK;
  }

  display.fillRect(0, 0, DISP_WIDTH, RIBBON_H - 1, barFill);
  display.drawFastHLine(0, RIBBON_H - 1, DISP_WIDTH, ruleInk);

  const bool textTab = !LC_RTL && strncmp(LOCALE_STR(LOCALE), "en_", 3) == 0;
  int16_t tabW = 30;
  if (textTab)
  {
    const String tabLabel = (num_valid == 1) ? "ALERT" : "ALERTS";
    display.setFont(&FONT_7pt8b);
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(tabLabel, 0, 0, &bx, &by, &bw, &bh);
    tabW = std::max<int16_t>(64, static_cast<int16_t>(bw) + 20);
    display.fillRect(0, 0, tabW, RIBBON_H - 1, tabFill);
    const int16_t tx = (tabW - static_cast<int16_t>(bw)) / 2 - bx;
    const int16_t ty = ((RIBBON_H - 1) - static_cast<int16_t>(bh)) / 2 - by;
    drawString(tx, ty, tabLabel, LEFT, tabInk);
    if (maxSev == 3) display.drawFastVLine(tabW - 1, 4, RIBBON_H - 9, G_LG);
  }
  else
  {
    const int16_t tabX = LC_RTL ? (DISP_WIDTH - tabW) : 0;
    display.fillRect(tabX, 0, tabW, RIBBON_H - 1, tabFill);
    RibbonIconBox tb = measureRibbonIcon(warning_icon_24x24, 24, 15);
    drawMeasuredRibbonIcon(tabX + (tabW - tb.dstW) / 2, 11,
                           warning_icon_24x24, 24, tb, tabInk);
    if (maxSev == 3)
    {
      display.drawFastVLine(LC_RTL ? tabX : (tabW - 1), 4, RIBBON_H - 9, G_LG);
    }
  }

  const int16_t padEdge = 10;
  int16_t contentL = LC_RTL ? padEdge : (tabW + padEdge);
  int16_t contentR = LC_RTL ? (DISP_WIDTH - tabW - padEdge) : (DISP_WIDTH - padEdge);

  if (num_valid > 2)
  {
    const String extraTxt = "+" + String(num_valid - 2);
    display.setFont(&FONT_7pt8b);
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(extraTxt, 0, 0, &bx, &by, &bw, &bh);
    const int16_t badgeW = static_cast<int16_t>(bw) + 10;
    const int16_t badgeH = 16;
    const int16_t badgeY = 3;
    const int16_t badgeX = LC_RTL ? contentL : (contentR - badgeW);
    display.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 3, extraBg);
    const int16_t ty = badgeY + (badgeH - static_cast<int16_t>(bh)) / 2 - by;
    drawString(badgeX + (badgeW - static_cast<int16_t>(bw)) / 2 - bx, ty,
               extraTxt, LEFT, extraInk);
    if (LC_RTL) contentL += badgeW + 8;
    else        contentR -= badgeW + 8;
  }

  const int16_t availW = std::max<int16_t>(40, contentR - contentL);
  const int16_t iconGap = 6;
  const int16_t sepW = 21; // 10 px gap + 1 px rule + 10 px gap

  auto calcWidth = [&](const GFXfont *f, int16_t twOut[2]) -> int16_t
  {
    display.setFont(f);
    int16_t tot = 0;
    for (int k = 0; k < show; ++k)
    {
      twOut[k] = static_cast<int16_t>(getStringWidth(titles[k]));
      if (iconBoxes[k].dstW > 0) tot += iconBoxes[k].dstW + iconGap;
      tot += twOut[k];
    }
    if (show == 2) tot += sepW;
    return tot;
  };

  int16_t textW[2] = {0, 0};
  const GFXfont *font = &FONT_11pt8b;
  if (calcWidth(font, textW) > availW)
  {
    font = &FONT_8pt8b;
    if (calcWidth(font, textW) > availW)
    {
      if (show == 1)
      {
        const int16_t iw = (iconBoxes[0].dstW > 0) ? (iconBoxes[0].dstW + iconGap) : 0;
        titles[0] = clipAlertLine(titles[0], std::max<int>(16, availW - iw));
      }
      else
      {
        const int16_t iw0 = (iconBoxes[0].dstW > 0) ? (iconBoxes[0].dstW + iconGap) : 0;
        const int16_t iw1 = (iconBoxes[1].dstW > 0) ? (iconBoxes[1].dstW + iconGap) : 0;
        const int16_t textAvail = std::max<int16_t>(32, availW - iw0 - iw1 - sepW);
        int16_t w0Max = static_cast<int16_t>(
            (static_cast<int32_t>(textAvail) * textW[0])
            / std::max<int>(1, textW[0] + textW[1]));
        const int16_t minEach = std::min<int16_t>(textAvail / 2, 60);
        w0Max = std::max(minEach, std::min<int16_t>(textAvail - minEach, w0Max));
        titles[0] = clipAlertLine(titles[0], w0Max);
        titles[1] = clipAlertLine(titles[1], textAvail - w0Max);
      }
      calcWidth(font, textW);
    }
  }

  display.setFont(font);
  int16_t minBy = 100, maxBot = -100;
  for (int k = 0; k < show; ++k)
  {
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(shapeText(titles[k]), 0, 0, &bx, &by, &bw, &bh);
    minBy = std::min(minBy, by);
    maxBot = std::max<int16_t>(maxBot, by + static_cast<int16_t>(bh));
  }
  const int16_t inkH = std::max<int16_t>(1, maxBot - minBy);
  const int16_t yBase = ((RIBBON_H - 1) - inkH) / 2 - minBy;

  if (!LC_RTL)
  {
    int16_t cx = contentL;
    for (int k = 0; k < show; ++k)
    {
      if (k == 1)
      {
        display.drawFastVLine(cx + 10, 5, 13, sepInk);
        cx += sepW;
      }
      if (iconBoxes[k].dstW > 0)
      {
        drawMeasuredRibbonIcon(cx, 11, bmps[k], 32, iconBoxes[k], textInk);
        cx += iconBoxes[k].dstW + iconGap;
      }
      drawString(cx, yBase, titles[k], LEFT, textInk);
      cx += textW[k];
    }
  }
  else
  {
    int16_t cx = contentR;
    for (int k = 0; k < show; ++k)
    {
      if (k == 1)
      {
        display.drawFastVLine(cx - 10, 5, 13, sepInk);
        cx -= sepW;
      }
      if (iconBoxes[k].dstW > 0)
      {
        cx -= iconBoxes[k].dstW;
        drawMeasuredRibbonIcon(cx, 11, bmps[k], 32, iconBoxes[k], textInk);
        cx -= iconGap;
      }
      drawString(cx, yBase, titles[k], RIGHT, textInk);
      cx -= textW[k];
    }
  }
  return true;
}
#endif // GREYS_75

  /* This function is responsible for drawing the current alerts if any.
   * Up to 2 alerts can be drawn.
   */
  void drawAlerts(std::vector<owm_alerts_t> & alerts,
                  const String &city, const String &date)
  {
#if DEBUG_LEVEL >= 1
  Serial.println("[debug] alerts.size()    : " + String(alerts.size()));
#endif
  if (alerts.size() == 0)
  { // no alerts to draw
    return;
  }

#if defined(GREYS_75)
  if (drawAlertsRibbon75(alerts))
  {
    return;
  }
#endif

  int ignore_list[OWM_NUM_ALERTS] = {0};
  int alert_indices[OWM_NUM_ALERTS] = {0};

  // Converts all event text and tags to lowercase, removes extra information,
  // and filters out redundant alerts of lesser urgency.
  filterAlerts(alerts, ignore_list);

  // limit alert text width so that is does not run into the location or date
  // strings
  display.setFont(&FONT_16pt8b);
  int city_w = getStringWidth(city);
  display.setFont(&FONT_12pt8b);
  int date_w = getStringWidth(date);
  int max_w = DISP_WIDTH - 2 - std::max(city_w, date_w) - (196 + 4) - 8;

  // find indices of valid alerts
  int num_valid_alerts = 0;
#if DEBUG_LEVEL >= 1
  Serial.print("[debug] ignore_list      : [ ");
#endif
  for (int i = 0; i < alerts.size(); ++i)
  {
#if DEBUG_LEVEL >= 1
    Serial.print(String(ignore_list[i]) + " ");
#endif
    if (!ignore_list[i])
    {
      alert_indices[num_valid_alerts] = i;
      ++num_valid_alerts;
    }
  }
#if DEBUG_LEVEL >= 1
  Serial.println("]\n[debug] num_valid_alerts : " + String(num_valid_alerts));
#endif

  // The block spans x = 196 .. 196 + 4 + max_w. Left to right the icon
  // stands at its left edge and the text runs rightwards from it; in a
  // right-to-left locale the block is mirrored -- icon at the right edge,
  // beside the city and date, and the text right-aligned against it,
  // running leftwards -- so a Hebrew reader meets the icon first. The icon
  // then also separates the alert from the right-aligned city and date.
  const int blockRight = 196 + 4 + max_w;
  const alignment_t textAlign = LC_RTL ? RIGHT : LEFT;

  if (num_valid_alerts == 1)
  { // 1 alert
    // adjust max width to for 48x48 icons
    max_w -= 48;

    owm_alerts_t &cur_alert = alerts[alert_indices[0]];
    const int iconX = LC_RTL ? (blockRight - 48) : 196;
    const int textX = LC_RTL ? (iconX - 4) : (196 + 48 + 4);
    drawAlertIcon(iconX, 8, cur_alert, 48);
    // must be called after getAlertBitmap
    toTitleCase(cur_alert.event);
#if defined(MULTICOLOR_DISPLAY) || defined(GREYS_75)
#ifdef MULTICOLOR_DISPLAY
    const int color = alertColorSeverity(cur_alert);
#else
    const int color = greys() ? alertGreySeverity(cur_alert) : 0;
#endif
    if (color > 0)
    {
      display.setFont(&FONT_14pt8b);
      if (getStringWidth(cur_alert.event) + 10 <= max_w)
      {
        drawAlertBadgeOneLine(textX, 24 + 8 - 12 + 20 + 1, cur_alert.event,
                              textAlign, max_w, color);
      }
      else
      {
        display.setFont(&FONT_12pt8b);
        if (getStringWidth(cur_alert.event) + 10 <= max_w)
        {
          drawAlertBadgeOneLine(textX, 24 + 8 - 12 + 17 + 1, cur_alert.event,
                                textAlign, max_w, color);
        }
        else
        {
          drawAlertBadgeTwoLines(textX, 24 + 8 - 12 + 17 - 11, cur_alert.event,
                                 textAlign, max_w, 23, color);
        }
      }
      return;
    }
#endif

    display.setFont(&FONT_14pt8b);
    if (getStringWidth(cur_alert.event) <= max_w)
    { // Fits on a single line, draw along bottom
      drawString(textX, 24 + 8 - 12 + 20 + 1, cur_alert.event, textAlign);
    }
    else
    { // use smaller font
      display.setFont(&FONT_12pt8b);
      if (getStringWidth(cur_alert.event) <= max_w)
      { // Fits on a single line with smaller font, draw along bottom
        drawString(textX, 24 + 8 - 12 + 17 + 1, cur_alert.event, textAlign);
      }
      else
      { // Does not fit on a single line, draw higher to allow room for 2nd line
        drawMultiLnString(textX, 24 + 8 - 12 + 17 - 11,
                          cur_alert.event, textAlign, max_w, 2, 23);
      }
    }
  } // end 1 alert
  else
  { // 2 alerts
    // adjust max width to for 32x32 icons
    max_w -= 32;

    display.setFont(&FONT_12pt8b);
    for (int i = 0; i < 2; ++i)
    {
      owm_alerts_t &cur_alert = alerts[alert_indices[i]];
      const int iconX = LC_RTL ? (blockRight - 32) : 196;
      const int textX = LC_RTL ? (iconX - 3) : (196 + 32 + 3);

      drawAlertIcon(iconX, (i * 32), cur_alert, 32);
      // must be called after getAlertBitmap
      toTitleCase(cur_alert.event);

#if defined(MULTICOLOR_DISPLAY) || defined(GREYS_75)
#ifdef MULTICOLOR_DISPLAY
      const int color = alertColorSeverity(cur_alert);
#else
      const int color = greys() ? alertGreySeverity(cur_alert) : 0;
#endif
      if (color > 0)
      {
        drawAlertBadgeOneLine(textX, 5 + 17 + (i * 32), cur_alert.event,
                              textAlign, max_w, color);
        continue;
      }
#endif
      drawMultiLnString(textX, 5 + 17 + (i * 32),
                        cur_alert.event, textAlign, max_w, 1, 0);
    } // end for-loop
  } // end 2 alerts

  return;
} // end drawAlerts

/* This function is responsible for drawing the city string and date
 * information in the top right corner.
 */
void drawLocationDate(const String &city, const String &date)
{
  // location, date
  const int right = DISP_WIDTH - 2;
  if (alertRibbon75())
  {
    display.setFont(&FONT_11pt8b);
    drawString(right, 44, city, RIGHT, DM_FG);
    display.setFont(&FONT_8pt8b);
    drawString(right, 60, date, RIGHT, DM_FG);
    return;
  }
  display.setFont(&FONT_16pt8b);
#ifdef MULTICOLOR_DISPLAY
  // On multicolor panels red carries meaning (temperature, alerts, warnings),
  // so a red city name reads out of place -- keep it black there. The
  // single-accent panels keep the upstream red-accent look.
  drawString(right, 23, city, RIGHT, DM_FG);
#else
  drawString(right, 23, city, RIGHT, ACCENT_COLOR);
#endif
  display.setFont(&FONT_12pt8b);
  drawString(right, 30 + 4 + 17, date, RIGHT);
  return;
} // end drawLocationDate

/* The % operator in C++ is not a true modulo operator but it instead a
 * remainder operator. The remainder operator and modulo operator are equivalent
 * for positive numbers, but not for negatives. The follow implementation of the
 * modulo operator works for +/-a and +b.
 */
inline int modulo(int a, int b)
{
  const int result = a % b;
  return result >= 0 ? result : result + b;
}

#if defined(GREYS_75)
/* Curves anti-aliased across the panel's grey levels: 4 levels for temperature
 * (G_LG / G_DG / BLACK) and a clean haloed light-grey ramp for dew point so it
 * reads smoothly across precipitation bars.
 */
static inline void aaInk75(int x, int y, float cov, bool isDew)
{
  if (isDew)
  {
    if (cov >= 0.65f) display.drawPixel(x, y, G_LG);
    else if (cov >= 0.25f) display.drawPixel(x, y, G75_LGW);
    else if (cov >= -0.6f) display.drawPixel(x, y, DM_BG);
    return;
  }
  if (cov >= 0.80f) display.drawPixel(x, y, GxEPD_BLACK);
  else if (cov >= 0.50f) display.drawPixel(x, y, G_DG);
  else if (cov >= 0.20f) display.drawPixel(x, y, G_LG);
}

static void drawRunAA75(const float *px, const float *py, int m, float halfW,
                        bool isDew);

static void drawCurveAA75(const std::vector<int> &x_t,
                          const std::vector<float> &yf, int n, float halfW,
                          bool isDew = false)
{
  if (n < 2) return;
  // one sample per pixel column; an hour with no value (NaN) breaks it, and
  // each unbroken stretch is drawn on its own
  std::vector<float> px, py;
  for (int k = 0; k < n - 1; ++k)
  {
    const int dx = x_t[k + 1] - x_t[k];
    const int steps = std::max(1, dx);
    for (int s = 0; s < steps; ++s)
    {
      const float u = s / static_cast<float>(steps);
      px.push_back(x_t[k] + dx * u - 0.5f);
      py.push_back(smoothGapsAt(yf.data(), n, k + u) + 0.5f);
    }
  }
  px.push_back(x_t[n - 1] - 0.5f);
  py.push_back(yf[n - 1] + 0.5f);
  for (size_t r0 = 0; r0 < px.size();)
  {
    while (r0 < px.size() && std::isnan(py[r0])) ++r0;
    size_t r1 = r0;
    while (r1 < px.size() && !std::isnan(py[r1])) ++r1;
    if (r1 - r0 >= 2)
    {
      drawRunAA75(px.data() + r0, py.data() + r0,
                  static_cast<int>(r1 - r0), halfW, isDew);
    }
    r0 = r1;
  }
} // end drawCurveAA75

// one unbroken stretch of drawCurveAA75's curve: px rising
static void drawRunAA75(const float *px, const float *py, int m, float halfW,
                        bool isDew)
{
  const float reach = isDew ? (halfW + 1.2f) : (halfW + 1.0f);
  const int yTop = display.bandTop(), yBot = display.bandBottom();
  int j0 = 0;
  for (int x = static_cast<int>(px[0] - reach);
       x <= static_cast<int>(px[m - 1] + reach) + 1; ++x)
  {
    while (j0 < m - 2 && px[j0 + 1] < x - reach) ++j0;
    int j1 = j0 + 1;
    while (j1 < m - 1 && px[j1] <= x + reach) ++j1;
    float ylo = 1e9f, yhi = -1e9f;
    for (int j = j0; j <= j1; ++j)
    {
      ylo = std::min(ylo, py[j]);
      yhi = std::max(yhi, py[j]);
    }
    const int ys = std::max<int>(yTop, static_cast<int>(std::floor(ylo - reach)));
    const int ye = std::min<int>(yBot - 1, static_cast<int>(std::ceil(yhi + reach)));
    for (int y = ys; y <= ye; ++y)
    {
      float d2 = 1e9f;
      for (int j = j0; j < j1; ++j)
      { // distance to the piece j .. j + 1
        const float ax = px[j], ay = py[j];
        const float bx = px[j + 1] - ax, by = py[j + 1] - ay;
        const float l2 = bx * bx + by * by;
        float t = l2 > 0 ? ((x - ax) * bx + (y - ay) * by) / l2 : 0;
        t = std::max(0.0f, std::min(1.0f, t));
        const float ex = x - (ax + t * bx), ey = y - (ay + t * by);
        d2 = std::min(d2, ex * ex + ey * ey);
      }
      aaInk75(x, y, halfW + 0.5f - std::sqrt(d2), isDew);
    }
  }
} // end drawRunAA75
#endif

/* Convert kelvin to the temperature unit the graph's axis is drawn in.
 */
static float kelvin_to_graph_units(float kelvin)
{
#ifdef UNITS_TEMP_KELVIN
  return kelvin;
#endif
#ifdef UNITS_TEMP_CELSIUS
  return kelvin_to_celsius(kelvin);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  return kelvin_to_fahrenheit(kelvin);
#endif
}

/* Convert temperature in kelvin to the display y coordinate to be plotted.
 */
int kelvin_to_plot_y(float kelvin, int tempBoundMin, float yPxPerUnit,
                     int yBoundMin)
{
#ifdef UNITS_TEMP_KELVIN
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin - tempBoundMin)) ));
#endif
#ifdef UNITS_TEMP_CELSIUS
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin_to_celsius(kelvin) - tempBoundMin)) ));
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  return static_cast<int>(std::round(
    yBoundMin - (yPxPerUnit * (kelvin_to_fahrenheit(kelvin) - tempBoundMin)) ));
#endif
}

/* This function is responsible for drawing the outlook graph for the specified
 * number of hours(up to 48).
 */
void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo)
{
  const int xPos0 = 350;
  int xPos1 = DISP_WIDTH;
  const int yPos0 = 216;
  const int yPos1 = DISP_HEIGHT - 46;

  // calculate y max/min and intervals
  int yMajorTicks = 5;
  float tempMin = INFINITY;
  float tempMax = -INFINITY;
  float precipMax = 0.0f;
  int yTempMajorTicks = 5;
  for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    const float newTemp = kelvin_to_graph_units(hourly[i].temp);
    if (!std::isnan(newTemp))
    {
      tempMin = std::min(tempMin, newTemp);
      tempMax = std::max(tempMax, newTemp);
    }
    if (GRAPH_DEWPOINT && !std::isnan(hourly[i].dew_point))
    {
      tempMin = std::min(tempMin, kelvin_to_graph_units(hourly[i].dew_point));
    }
    precipMax = std::max<float>(precipMax, hourly[i].pop);
  }
  if (!std::isfinite(tempMin) || !std::isfinite(tempMax))
  {
    tempMin = 0.0f;
    tempMax = 10.0f;
  }
  // The temperature scale shares the graph's yMajorTicks gridlines with the
  // rain scale, so it spans exactly yMajorTicks steps. It keeps to the
  // curves: the finest step whose span still covers them, labelled on
  // multiples of the step (of 5 for the larger steps), and of the bounds
  // that do, the one that centres them. 20.6..65 gets 20..70, not 0..75.
  static const int TEMP_STEPS[] = {2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30,
                                   40, 50, 100, 200};
  int tempBoundMin = 0, tempBoundMax = 0;
  for (int step : TEMP_STEPS)
  {
    yTempMajorTicks = step;
    const int align = (step % 5 == 0) ? 5 : step;
    const int span = step * yMajorTicks;
    // the aligned lower bounds that cover both ends: lo (the top just in)
    // up to hi (the bottom just in), with half a degree to spare at each
    // end, so the curve keeps off the axis and the top line
    int hi = static_cast<int>(std::floor(tempMin - 0.5f));
    hi -= modulo(hi, align);
    int lo = static_cast<int>(std::ceil(tempMax + 0.5f)) - span;
    lo += modulo(-lo, align);
    if (lo > hi)
    {
      continue;
    }
    // of the aligned bounds from lo to hi, the one nearest centring
    const float ideal = (tempMin + tempMax - span) / 2.0f;
    int best = lo;
    for (int c = lo; c <= hi; c += align)
    {
      if (std::fabs(c - ideal) < std::fabs(best - ideal))
      {
        best = c;
      }
    }
    tempBoundMin = best;
    tempBoundMax = best + span;
    break;
  }
  if (tempBoundMax <= tempBoundMin)
  { // nothing fitted (no temperatures at all): any scale
    yTempMajorTicks = 2;
    tempBoundMin = 0;
    tempBoundMax = 2 * yMajorTicks;
  }

#ifdef UNITS_HOURLY_PRECIP_POP
  xPos1 = DISP_WIDTH - 23;
  float precipBoundMax;
  if (precipMax > 0)
  {
    precipBoundMax = 100.0f;
  }
  else
  {
    precipBoundMax = 0.0f;
  }
#else
#ifdef UNITS_HOURLY_PRECIP_MILLIMETERS
  xPos1 = DISP_WIDTH - 24;
  float precipBoundMax = std::ceil(precipMax); // Round up to nearest mm
  int yPrecipMajorTickDecimals = (precipBoundMax < 10);
#endif
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
  xPos1 = DISP_WIDTH - 25;
  precipMax = millimeters_to_centimeters(precipMax);
  // Round up to nearest 0.1 cm
  float precipBoundMax = std::ceil(precipMax * 10) / 10.0f;
  int yPrecipMajorTickDecimals;
  if (precipBoundMax < 1)
  {
    yPrecipMajorTickDecimals = 2;
    if (precipBoundMax > 0)
    {
      xPos1 -= 6; // needs extra room
    }
  }
  else if (precipBoundMax < 10)
  {
    yPrecipMajorTickDecimals = 1;
  }
  else
  {
    yPrecipMajorTickDecimals = 0;
  }
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
  xPos1 = DISP_WIDTH - 25;
  precipMax = millimeters_to_inches(precipMax);
  // Round up to nearest 0.1 inch
  float precipBoundMax = std::ceil(precipMax * 10) / 10.0f;
  int yPrecipMajorTickDecimals;
  if (precipBoundMax < 1)
  {
    yPrecipMajorTickDecimals = 2;
  }
  else if (precipBoundMax < 10)
  {
    yPrecipMajorTickDecimals = 1;
  }
  else
  {
    yPrecipMajorTickDecimals = 0;
  }
#endif
  float yPrecipMajorTickValue = precipBoundMax / yMajorTicks;
  float precipRoundingMultiplier = std::pow(10.f, yPrecipMajorTickDecimals);
#endif

  if (precipBoundMax > 0)
  { // fill need extra room for labels
#if defined(GREYS_75)
    xPos1 -= 26;
#else
    xPos1 -= 23;
#endif
  }

  const float xInterval = (xPos1 - xPos0 - 1) / static_cast<float>(HOURLY_GRAPH_MAX);

#if defined(GREYS_75)
  if (greys())
  { // softly tint contiguous nighttime spans behind gridlines and plots
    // with rounded upper corners and a flat base flush on the x-axis
    int i = 0;
    while (i < HOURLY_GRAPH_MAX)
    {
      if (isDaytimeAt(hourly[i].dt, hourly[i].weather.icon.endsWith("d")))
      {
        ++i;
        continue;
      }
      const int runStart = i;
      while (i < HOURLY_GRAPH_MAX
             && !isDaytimeAt(hourly[i].dt, hourly[i].weather.icon.endsWith("d")))
      {
        ++i;
      }
      const int nx0 = static_cast<int>(std::round(xPos0 + 1 + (runStart * xInterval)));
      const int nx1 = static_cast<int>(std::round(xPos0 + 1 + (i * xInterval)));
      const int nw = nx1 - nx0;
      const int nh = yPos1 - 1 - yPos0;
      if (nw > 0 && nh > 0)
      {
        const int r = std::min(10, std::min(nw / 2, nh / 2));
        if (r >= 2)
        {
          display.fillRoundRect(nx0, yPos0, nw, nh, r, G75_LGW4);
          display.fillRect(nx0, yPos0 + nh - r, nw, r, G75_LGW4);
        }
        else
        {
          display.fillRect(nx0, yPos0, nw, nh, G75_LGW4);
        }
      }
    }
  }
#endif

  // draw x axis
  display.drawLine(xPos0, yPos1    , xPos1, yPos1    , DM_FG);
  display.drawLine(xPos0, yPos1 - 1, xPos1, yPos1 - 1, DM_FG);

  // draw y axis
  float yInterval = (yPos1 - yPos0) / static_cast<float>(yMajorTicks);
  for (int i = 0; i <= yMajorTicks; ++i)
  {
    String dataStr;
    int yTick = static_cast<int>(yPos0 + (i * yInterval));
    display.setFont(&FONT_8pt8b);
    // Temperature
    dataStr = String(tempBoundMax - (i * yTempMajorTicks));
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
    dataStr += "\260";
#endif
#if defined(GREYS_75)
    const uint16_t axisTempInk = greys() ? DM_FG : DM_TEXT(ACCENT_COLOR);
#else
    const uint16_t axisTempInk = DM_TEXT(ACCENT_COLOR);
#endif
    drawString(xPos0 - 8, yTick + 4, dataStr, RIGHT, axisTempInk);

    if (precipBoundMax > 0)
    { // don't labels if precip is 0
#ifdef UNITS_HOURLY_PRECIP_POP
      // PoP
      dataStr = String(100 - (i * 20));
      String precipUnit = "%";
#else
      // Precipitation volume
      float precipTick = precipBoundMax - (i * yPrecipMajorTickValue);
      precipTick = std::round(precipTick * precipRoundingMultiplier)
                              / precipRoundingMultiplier;
      dataStr = String(precipTick, yPrecipMajorTickDecimals);
#ifdef UNITS_HOURLY_PRECIP_MILLIMETERS
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_MILLIMETERS;
#endif
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_CENTIMETERS;
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
      String precipUnit = String(" ") + TXT_UNITS_PRECIP_INCHES;
#endif
#endif

#if defined(GREYS_75)
      const uint16_t precipInk = greys() ? DM_FG : DM_TEXT(COLOR_PRECIP);
#else
      const uint16_t precipInk = DM_TEXT(COLOR_PRECIP);
#endif
      drawString(xPos1 + 8, yTick + 4, dataStr, LEFT, precipInk);
      display.setFont(&FONT_5pt8b);
      drawString(display.getCursorX(), yTick + 4, precipUnit, LEFT,
                 precipInk);
    } // end draw labels if precip is >0

    // draw dotted line
    if (i < yMajorTicks)
    {
      for (int x = xPos0; x <= xPos1 + 1; x += 3)
      {
#if defined(GREYS_75)
        display.drawPixel(x, yTick + (yTick % 2), greys() ? G_LG : DM_FG);
#else
        display.drawPixel(x, yTick + (yTick % 2), DM_FG);
#endif
      }
    }
  }

  if (GRAPH_DEWPOINT)
  {
    // Legend -- only when there are two curves to tell apart; the default
    // graph keeps its unlabelled look. It sits in the gap between the daily
    // forecast row (separators end at y=196) and the first y-axis label
    // (8 pt, baseline 220, so from ~212): 5 pt text on baseline yPos0-8,
    // right-aligned to the plot edge, which keeps it clear of the y-axis
    // numbers on both sides and of the weather icons, which stack above the
    // temperature curve inside the plot. Each label is drawn in its curve's
    // colour beside a swatch of the same thickness as the line it names: on
    // a black/white panel -- where both curves are black -- the dew point is
    // the thinner one and the thickness alone identifies them; on a colour
    // panel the colour does, and both curves share one stroke.
    display.setFont(&FONT_5pt8b);
    const int legendY = yPos0 - 8;
    int legendX = xPos1;
    auto legendItem = [&](const char *label, uint16_t color, int thickness)
    {
      uint16_t bw = getStringWidth(label);
      legendX -= bw;
#if defined(GREYS_75)
      // in greys the dew point's name is in the temperature's ink; only
      // its swatch is light grey, as its curve
      drawString(legendX, legendY, label, LEFT,
                 (greys() && color == G_LG) ? DM_TEXT(ACCENT_COLOR)
                                            : DM_TEXT(color));
#else
      drawString(legendX, legendY, label, LEFT, DM_TEXT(color));
#endif
      legendX -= 4 + 14; // gap, then the swatch
      for (int t = 0; t < thickness; ++t)
      {
        display.drawLine(legendX, legendY - 4 + t, legendX + 14, legendY - 4 + t,
                         DM_GFX(color));
      }
      legendX -= 12; // gap before the next item to the left
    };
#if defined(MULTICOLOR_DISPLAY)
    legendItem(TXT_DEWPOINT, COLOR_DEWPOINT, 3);
#elif defined(GREYS_75)
    legendItem(TXT_DEWPOINT, greys() ? G_LG : COLOR_DEWPOINT, greys() ? 3 : 2);
#else
    legendItem(TXT_DEWPOINT, COLOR_DEWPOINT, 2);
#endif
    legendItem(TXT_TEMPERATURE, ACCENT_COLOR, 3);
  }

  int xMaxTicks = 8;
  int hourInterval = static_cast<int>(ceil(HOURLY_GRAPH_MAX
                                           / static_cast<float>(xMaxTicks)));
  display.setFont(&FONT_8pt8b);

  // precalculate all x and y coordinates for temperature values
  float yPxPerUnit = (yPos1 - yPos0)
                     / static_cast<float>(tempBoundMax - tempBoundMin);
  // sized, not merely reserved: they are written by index below
  std::vector<int> x_t(HOURLY_GRAPH_MAX);
  std::vector<int> y_t(HOURLY_GRAPH_MAX);
  // Dew point y per hour; INT_MIN marks an hour NWS gave no value for, and
  // the segment on either side of it is skipped rather than drawn to a zero.
  std::vector<int> y_d(HOURLY_GRAPH_MAX, INT_MIN);
    for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    y_t[i] = std::isnan(hourly[i].temp)
             ? INT_MIN
             : kelvin_to_plot_y(hourly[i].temp, tempBoundMin, yPxPerUnit, yPos1);
    x_t[i] = static_cast<int>(std::round(xPos0 + (i * xInterval)
                                          + (0.5 * xInterval) ));
    if (GRAPH_DEWPOINT && !std::isnan(hourly[i].dew_point))
    {
      y_d[i] = kelvin_to_plot_y(hourly[i].dew_point, tempBoundMin, yPxPerUnit,
                                yPos1);
    }
  }
  // the same heights as floats (NaN: no dew point) for the smooth curves
  // drawn between the hours (smooth.h)
  std::vector<float> yf_t(HOURLY_GRAPH_MAX), yf_d(HOURLY_GRAPH_MAX);
  for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    yf_t[i] = (y_t[i] == INT_MIN) ? NAN : static_cast<float>(y_t[i]);
    yf_d[i] = (y_d[i] == INT_MIN) ? NAN : static_cast<float>(y_d[i]);
  }
  // pieces per hour: short enough that the curve shows no corners
  const int SMOOTH_STEPS = 6;

#if DISPLAY_HOURLY_ICONS
  int day_idx = 0;
#endif
  display.setFont(&FONT_8pt8b);
  for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    int xTick = static_cast<int>(xPos0 + (i * xInterval));
    int x0_t, x1_t, y0_t, y1_t;

    // Precipitation bars go down FIRST, so both curves paint over them. The
    // dew point curve is the same blue as the bars and on a humid day sits
    // right in their band (67-74 F dew point under 20-40 % bars, seen live),
    // so it must be drawn on top of the dither, not under it.
#ifdef UNITS_HOURLY_PRECIP_POP
    float precipVal = hourly[i].pop * 100;
#else
    float precipVal = hourly[i].rain_1h + hourly[i].snow_1h;
#ifdef UNITS_HOURLY_PRECIP_CENTIMETERS
    precipVal = millimeters_to_centimeters(precipVal);
#endif
#ifdef UNITS_HOURLY_PRECIP_INCHES
    precipVal = millimeters_to_inches(precipVal);
#endif
#endif

    x0_t = static_cast<int>(std::round( xPos0 + 1 + (i * xInterval)));
    x1_t = static_cast<int>(std::round( xPos0 + 1 + ((i + 1) * xInterval) ));
    // precipBoundMax is 0 when no precipitation is forecast over the whole
    // graph (a dry 24 hours is common). Dividing by it would make yPxPerUnit
    // infinite and y0_t NaN, and casting that NaN to int is undefined --
    // which at best draws a bogus full-height bar and at worst spins the
    // fill loop below for billions of iterations.
    if (precipBoundMax > 0)
    {
      yPxPerUnit = (yPos1 - yPos0) / precipBoundMax;
      y0_t = static_cast<int>(std::round( yPos1 - (yPxPerUnit * (precipVal)) ));
    }
    else
    {
      y0_t = yPos1; // nothing to fill
    }
    y1_t = yPos1;

    // graph Precipitation
#if defined(GREYS_75)
    if (greys())
    { // pale fill for <50% chance, darker G_LG + G_DG cap for >=50%; guard
      // strictly above row yPos1 - 1 so tiny precip never paints over the axis
      if (y0_t < y1_t - 2 && x1_t - x0_t > 1)
      {
#ifdef UNITS_HOURLY_PRECIP_POP
        const bool highConf = (precipVal >= 50.0f);
#else
        const bool highConf = (hourly[i].pop >= 0.50f)
                              || (precipBoundMax > 0
                                  && precipVal >= 0.5f * precipBoundMax);
#endif
        const uint16_t barFill = highConf ? G_LG : G75_LGW;
        const uint16_t barCap  = highConf ? G_DG : G_LG;
        const int16_t barW = static_cast<int16_t>(x1_t - x0_t - 1);
        const int16_t barH = static_cast<int16_t>(y1_t - 1 - (y0_t + 1));
        display.fillRect(x0_t, y0_t + 1, barW, barH, barFill);
        display.drawFastHLine(x0_t, y0_t + 1, barW, barCap);
      }
    }
    else
#endif
    for (int y = y1_t - 1; y > y0_t; y -= 2)
    {
      for (int x = x0_t + (x0_t % 2); x < x1_t; x += 2)
      {
        display.drawPixel(x, y, DM_GFX(COLOR_PRECIP));
      }
    }

    // both curves, as SMOOTH_STEPS short pieces of a smooth curve per hour
    // (skipped when GREYS_75 draws whole AA curves after the hour loop)
    if (i > 0) for (int s = 0; s < SMOOTH_STEPS; ++s)
    {
#if defined(GREYS_75)
      if (greys()) break;
#endif
      const float f0 = (i - 1) + s / static_cast<float>(SMOOTH_STEPS);
      const float f1 = (i - 1) + (s + 1) / static_cast<float>(SMOOTH_STEPS);
      const int dx = x_t[i] - x_t[i - 1];
      x0_t = x_t[i - 1] + (dx * s) / SMOOTH_STEPS;
      x1_t = x_t[i - 1] + (dx * (s + 1)) / SMOOTH_STEPS;
      const float t0 = smoothGapsAt(yf_t.data(), HOURLY_GRAPH_MAX, f0);
      const float t1 = smoothGapsAt(yf_t.data(), HOURLY_GRAPH_MAX, f1);
      y0_t = std::isnan(t0) ? INT_MIN : static_cast<int>(std::lround(t0));
      y1_t = std::isnan(t1) ? INT_MIN : static_cast<int>(std::lround(t1));
      const float d0 = smoothGapsAt(yf_d.data(), HOURLY_GRAPH_MAX, f0);
      const float d1 = smoothGapsAt(yf_d.data(), HOURLY_GRAPH_MAX, f1);
      // graph dew point -- over the bars, under the temperature: where the
      // two curves meet (saturated air: fog, drizzle) the temperature paints
      // on top. On a black/white panel the dew point stays the thinner
      // curve, which is all that tells the two apart; on a colour panel the
      // blue does that, so it gets the temperature's stroke. It is the same
      // blue as the precipitation bars, so it gets a one-pixel halo in the
      // background colour on each side; without it a 2 px blue stroke
      // through 50 % blue dither reads as slightly denser dither, not a line.
      if (!std::isnan(d0) && !std::isnan(d1))
      {
        const int y0_d = static_cast<int>(std::lround(d0));
        const int y1_d = static_cast<int>(std::lround(d1));
        // dark mode: colour ink is dim on a black ground, so the line is 3 px
        // (as the temperature's is thickened) and the halo sits one further out
        const int below = DARK_MODE ? 2 : 1;
        display.drawLine(x0_t, y0_d - below, x1_t, y1_d - below, DM_BG);
        display.drawLine(x0_t, y0_d + 2    , x1_t, y1_d + 2    , DM_BG);
#if defined(GREYS_75)
        const uint16_t dewInk = greys() ? G_LG : DM_GFX(COLOR_DEWPOINT);
#else
        const uint16_t dewInk = DM_GFX(COLOR_DEWPOINT);
#endif
        display.drawLine(x0_t, y0_d        , x1_t, y1_d        , dewInk);
        display.drawLine(x0_t, y0_d + 1    , x1_t, y1_d + 1    , dewInk);
#if defined(GREYS_75)
        if (greys())
        { // as thick as the temperature: the grey tells them apart
          display.drawLine(x0_t - 1, y0_d, x1_t - 1, y1_d, dewInk);
        }
#endif
#ifdef MULTICOLOR_DISPLAY
        display.drawLine(x0_t - 1, y0_d    , x1_t - 1, y1_d    , DM_GFX(COLOR_DEWPOINT));
#endif
        if (DARK_MODE)
        {
          display.drawLine(x0_t, y0_d - 1, x1_t, y1_d - 1, DM_GFX(COLOR_DEWPOINT));
        }
      }
      // graph temperature (in greys: drawn whole, anti-aliased, below)
#if defined(GREYS_75)
      if (greys()) continue;
#endif
      if (y0_t != INT_MIN && y1_t != INT_MIN)
      {
        display.drawLine(x0_t    , y0_t    , x1_t    , y1_t    , DM_GFX(ACCENT_COLOR));
        display.drawLine(x0_t    , y0_t + 1, x1_t    , y1_t + 1, DM_GFX(ACCENT_COLOR));
        display.drawLine(x0_t - 1, y0_t    , x1_t - 1, y1_t    , DM_GFX(ACCENT_COLOR));
        if (DARK_MODE)
        { // thicken the line: the red ink is dim against a black ground
          display.drawLine(x0_t    , y0_t - 1, x1_t    , y1_t - 1, DM_GFX(ACCENT_COLOR));
          display.drawLine(x0_t    , y0_t + 2, x1_t    , y1_t + 2, DM_GFX(ACCENT_COLOR));
        }
      }
    } // the curves' pieces

    if (i > 0)
    {
      // draw hourly bitmap
#if DISPLAY_HOURLY_ICONS
      if (day_idx + 1 < OWM_NUM_DAILY && daily[day_idx].dt > 0
          && daily[day_idx + 1].dt > 0
          && daily[day_idx].dt + 86400 <= hourly[i].dt)
      {
        ++day_idx;
      }
      if ((i % hourInterval) == 0) // skip first and last tick
      {
        int y_b = INT_MAX;
        // find the highest (lowest in coordinate value) temperature point that
        // exists within the width of the icon.
        // find closest point above the temperature line where the icon won't
        // interect the temperature line.
        // y = mx + b
        int span = static_cast<int>(std::round(16 / xInterval));
        int l_idx = std::max(i - 1 - span, 0);
        int r_idx = std::min(i + span, HOURLY_GRAPH_MAX - 1);
        // left intersecting slope
        float m_l = (y_t[l_idx + 1] - y_t[l_idx]) / xInterval;
        int x_l = xTick - 16 - x_t[l_idx];
        int y_l = static_cast<int>(std::round(m_l * x_l + y_t[l_idx]));
        y_b = std::min(y_l, y_b);
        // right intersecting slope
        float m_r = (y_t[r_idx] - y_t[r_idx - 1]) / xInterval;
        int x_r = xTick + 16 - x_t[r_idx - 1];
        int y_r = static_cast<int>(std::round(m_r * x_r + y_t[r_idx - 1]));
        y_b = std::min(y_r, y_b);
        // any peaks in between
        for (int idx = l_idx + 1; idx < r_idx; ++idx)
        {
          y_b = std::min(y_t[idx], y_b);
        }
#ifdef MULTICOLOR_DISPLAY
        const uint8_t *colorIcon = getColorIcon32(hourly[i]);
        if (colorIcon)
        {
          drawColorIcon(xTick - 16, y_b - 32, colorIcon, 32);
        }
        else
#endif
        {
        const uint8_t *bitmap = getHourlyForecastBitmap32(hourly[i],
                                                          daily[day_idx]);
        drawConditionIcon(xTick - 16, y_b - 32, bitmap, 32,
                          getHourlyForecastColor32(hourly[i]),
                          hourly[i].weather.id);
        }
      }
#endif
    }

    if ((i % hourInterval) == 0)
    {
      // draw x tick marks
      display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, DM_FG);
      display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, DM_FG);
      // draw x axis labels
      char timeBuffer[32] = {};
      time_t ts = hourly[i].dt;
      tm *timeInfo = localtime(&ts);
      _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
      drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER, G75_SUB_INK);
    }

  }

#if defined(GREYS_75)
  if (greys())
  {
    if (GRAPH_DEWPOINT)
    {
      drawCurveAA75(x_t, yf_d, HOURLY_GRAPH_MAX, 1.0f, true);
    }
    drawCurveAA75(x_t, yf_t, HOURLY_GRAPH_MAX, 1.1f, false);
  }
#endif

  // draw the last tick mark
  if ((HOURLY_GRAPH_MAX % hourInterval) == 0)
  {
    int xTick = static_cast<int>(
                std::round(xPos0 + (HOURLY_GRAPH_MAX * xInterval)));
    // draw x tick marks
    display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, DM_FG);
    display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, DM_FG);
    // draw x axis labels
    char timeBuffer[32] = {};
    time_t ts = hourly[HOURLY_GRAPH_MAX - 1].dt + 3600;
    tm *timeInfo = localtime(&ts);
    _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
    drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER, G75_SUB_INK);
  }

#if defined(GREYS_75)
  if (greys())
  { // min/max peak callouts directly on the temperature curve
    int iMin = -1, iMax = -1;
    float minVal = INFINITY, maxVal = -INFINITY;
    for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
    {
      if (y_t[i] == INT_MIN || std::isnan(hourly[i].temp)) continue;
      const float v = kelvin_to_graph_units(hourly[i].temp);
      if (v < minVal) {minVal = v; iMin = i;}
      if (v > maxVal) {maxVal = v; iMax = i;}
    }
    if (iMin >= 0 && iMax >= 0 && iMin != iMax && (maxVal - minVal) >= 1.0f)
    {
      display.setFont(&FONT_5pt8b);
      auto drawPeak = [&](int idx, float val, bool preferAbove) {
        const int px = x_t[idx], py = y_t[idx];
        display.fillCircle(px, py, 4, GxEPD_WHITE);
        display.fillCircle(px, py, 2, GxEPD_BLACK);
        String lbl = String(static_cast<int>(std::lround(val)));
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
        lbl += "\260";
#endif
        const int lx = std::max(xPos0 + 16, std::min(xPos1 - 16, px));
#if DISPLAY_HOURLY_ICONS
        if (preferAbove && (py + 15) < yPos1)
        { // flip below the dot when an adjacent 32x32 hourly icon sits nearby
          for (int k = hourInterval; k < HOURLY_GRAPH_MAX; k += hourInterval)
          {
            const int hx = static_cast<int>(xPos0 + (k * xInterval));
            if (std::abs(px - hx) < 26)
            {
              preferAbove = false;
              break;
            }
          }
        }
#endif
        int ly = preferAbove ? (py - 7) : (py + 13);
        if (preferAbove && ly < yPos0 + 9) ly = py + 13;
        if (!preferAbove && ly > yPos1 - 4) ly = py - 7;
        drawString(lx, ly, lbl, CENTER, GxEPD_BLACK);
      };
      drawPeak(iMax, maxVal, true);
      drawPeak(iMin, minVal, false);
    }
  }
#endif

  return;
} // end drawOutlookGraph


#if defined(GREYS_75)
/* ------------------------------------------- the pages KEY3 turns to */

/* The page KEY3 turns to: weather.gov's forecast in words (forecastPage75),
 * rendered in the panel's four greys from the cached weather snapshot.
 */
// a rule or a quiet word: grey on a grey page, black on dark/mono
static inline uint16_t pageInk75(uint16_t tone) {return greys() ? tone : DM_FG;}

static String fitted75(const String &text, int maxW)
{
  if (text.isEmpty() || getStringWidth(text) <= maxW)
  {
    return text;
  }
  String s = text;
  while (s.length() > 1 && getStringWidth(s + "..") > maxW)
  {
    int k = static_cast<int>(s.length()) - 1;
    while (k > 0 && (static_cast<uint8_t>(s.charAt(k)) & 0xC0) == 0x80)
    {
      --k;
    }
    s.remove(k);
  }
  s.trim();
  return s + "..";
}

static String deg75(float kelvin)
{
  if (std::isnan(kelvin))
  {
    return "--";
  }
  String s = String(static_cast<int>(std::lround(kelvin_to_graph_units(kelvin))));
#if defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT)
  s += "\260";
#endif
  return s;
}

/* When weather.gov's detailedForecast would overflow `maxLines` at the
 * current font, prefer ending cleanly at the last complete sentence that
 * fits (when it covers at least 25% of the fitted area) instead of clipping
 * mid-clause with "...".
 */
static String fitForecastText75(const String &text, int maxW, int maxLines)
{
  if (text.isEmpty() || maxLines <= 0) return text;
  int start = 0;
  const int len = static_cast<int>(text.length());
  int lastSentEnd = -1;
  int fittedEnd = 0;
  for (int line = 0; line < maxLines && start < len; ++line)
  {
    while (start < len && text.charAt(start) == ' ') ++start;
    if (start >= len) break;
    const String rem = text.substring(start);
    if (getStringWidth(rem) <= maxW)
    {
      return text;
    }
    int cut = rem.length();
    int endIdx = start;
    String piece;
    while (cut > 0)
    {
      int sp = rem.lastIndexOf(' ', cut - 1);
      if (line < maxLines - 1)
      {
        const int dash = rem.lastIndexOf('-', cut - 1);
        if (dash > sp) sp = dash;
      }
      if (sp <= 0) break;
      const String cand = (rem.charAt(sp) == '-')
                          ? rem.substring(0, sp + 1)
                          : rem.substring(0, sp);
      if (getStringWidth(cand) <= maxW)
      {
        piece = cand;
        endIdx = start + static_cast<int>(cand.length());
        break;
      }
      cut = sp;
    }
    if (piece.isEmpty())
    {
      int nextSp = text.indexOf(' ', start);
      endIdx = (nextSp > start) ? nextSp : len;
    }
    for (int pos = start; pos < endIdx; ++pos)
    {
      const char ch = text.charAt(pos);
      if ((ch == '.' || ch == '!' || ch == '?')
          && (pos + 1 == len || text.charAt(pos + 1) == ' '))
      {
        if (line < maxLines - 1
            || getStringWidth(text.substring(start, pos + 1)) <= maxW)
        {
          lastSentEnd = pos + 1;
        }
      }
    }
    fittedEnd = endIdx;
    start = endIdx;
    if (start < len && text.charAt(start) == ' ') ++start;
  }
  while (start < len && text.charAt(start) == ' ') ++start;
  if (start < len && lastSentEnd > 0 && lastSentEnd * 4 >= fittedEnd)
  {
    return text.substring(0, lastSentEnd);
  }
  return text;
}

/* The top of the forecast page: two tabs, "Now" (the home page) and
 * "Forecast", this page's underlined; on the right the city and date, and
 * under them when the weather was updated.
 */
static void tabs75(const String &city, const String &date,
                   const String &refreshTime, int page)
{
  const uint16_t K = DM_FG;
  if (greys())
  {
    display.fillRect(0, 0, DISP_WIDTH, 49, G75_LGW4);
  }
  const char *const TABS[2] = {TXT_TAB_NOW, TXT_TAB_FORECAST};
  display.setFont(&FONT_14pt8b);
  int x = 14;
  for (int i = 0; i < 2; ++i)
  {
    drawString(x, 34, TABS[i], LEFT, (i == page) ? K : pageInk75(G75_DGLG));
    const int w = getStringWidth(TABS[i]);
    if (i == page)
    {
      display.fillRect(x, 42, w, 4, K);
    }
    x += w + 30;
  }
  display.setFont(&FONT_9pt8b);
  drawString(DISP_WIDTH - 14, 22, fitted75(city + "  \267  " + date, 420),
             RIGHT, K);
  display.setFont(&FONT_7pt8b);
  drawString(DISP_WIDTH - 14, 41, String(TXT_UPDATED) + " " + refreshTime,
             RIGHT, K);
  if (!greys())
  {
    display.fillRect(0, 49, DISP_WIDTH, 2, pageInk75(G_LG));
  }
}

/* The forecast page: weather.gov's forecast in words, its next six 12-hour
 * periods in two columns of three: the 48x48 sky icon, the period's name,
 * its high or low, and what it says (four lines at most). Another forecast
 * source has no words: the page says so.
 */
static void forecastPage75()
{
  const uint16_t K = DM_FG;
  if (NWS_PERIODS.empty())
  {
    display.setFont(&FONT_12pt8b);
    drawMultiLnString(60, 220, TXT_FORECAST_WORDS_NWS_ONLY, LEFT, 680, 3, 32,
                      K);
    return;
  }
  const int top = 62, colW = 372;
  const int xs[2] = {14, 414};
  const int slotH = (DISP_HEIGHT - top) / 3;
  const int n = std::min<int>(NWS_PERIODS.size(), NWS_NUM_PERIODS);
  for (int k = 0; k < n; ++k)
  {
    const nws_period_t &p = NWS_PERIODS[k];
    const int x0 = xs[k / 3], y0 = top + (k % 3) * slotH;
    if ((k % 3) && !greys())
    {
      display.drawFastHLine(x0, y0 - 9, colW, pageInk75(G_LG));
      display.drawFastHLine(x0, y0 - 8, colW, pageInk75(G_LG));
    }
    if (!p.day && greys())
    { // subtly distinguish overnight forecast periods at a glance
      display.fillRoundRect(x0 - 6, y0 - 8, colW + 12, slotH - 6, 10, G75_LGW4);
    }
    drawConditionIcon(x0, y0 - 6, getPeriodBitmap48(p), 48, K, p.weather.id);
    const String tempVal = deg75(p.temp);
    display.setFont(&FONT_12pt8b);
    const int tempValW = getStringWidth(tempVal);
    drawString(x0 + colW, y0 + 24, tempVal, RIGHT, K);
    const char *hlLbl = p.day ? TXT_PERIOD_HIGH : TXT_PERIOD_LOW;
    display.setFont(&FONT_8pt8b);
    const int lblW = getStringWidth(hlLbl);
    drawString(x0 + colW - tempValW - 5, y0 + 24, hlLbl, RIGHT, K);
    display.setFont(&FONT_11pt8b);
    const int nameMaxW = std::max(40, colW - 54 - 8 - tempValW - 5 - lblW);
    drawString(x0 + 54, y0 + 24, fitted75(p.name, nameMaxW), LEFT, K);
    display.setFont(&FONT_9pt8b);
    drawMultiLnString(x0, y0 + 58, fitForecastText75(p.text, colW, 4), LEFT,
                      colW, 4, 21, K);
  }
  if (!greys())
  {
    display.drawFastVLine(400, top, DISP_HEIGHT - top - 10, pageInk75(G_LG));
  }
}

void drawPage426(int page, const owm_current_t &current,
                 const owm_hourly_t *hourly, const owm_daily_t *daily,
                 const owm_resp_air_pollution_t &air,
                 const pollen_info_t &pollen, float inTemp,
                 float inHumidity, const String &city,
                 const String &date, const String &refreshTime,
                 uint32_t batVoltage)
{
  (void)current;
  (void)hourly;
  (void)daily;
  (void)air;
  (void)pollen;
  (void)inTemp;
  (void)inHumidity;
  (void)batVoltage;
  fillDisplayBackground();
  tabs75(city, date, refreshTime, page);
  forecastPage75();
}
#endif

#if defined(GREYS_75)
/* A test card for the 7.5in panel in its four greys (-D GREY75_TEST_CARD,
 * the seeed_xiao_ee04_grey_testcard build): the levels, the six checkers
 * of two levels, and text and lines on each level, laid out as the 4.26in
 * card (renderer426.cpp drawTestCard426) so the same measuring works. For
 * a flatbed scan or a raw photo beside white paper, to set the simulator's
 * greys for this panel.
 */
void drawTestCard75()
{
  static const uint16_t LEVEL[4] = {GxEPD_BLACK, G_DG, G_LG, GxEPD_WHITE};
  static const char *const NAME[4] = {"black", "dark grey", "light grey",
                                      "white"};
  const uint16_t K = GxEPD_BLACK, W = GxEPD_WHITE;
  display.setFont(&FONT_9pt8b);
  drawString(8, 18, "top left", LEFT, K);
  drawString(DISP_WIDTH - 8, 18, "top right", RIGHT, K);
  drawString(8, DISP_HEIGHT - 6, "bottom left", LEFT, K);
  drawString(DISP_WIDTH - 8, DISP_HEIGHT - 6, "bottom right", RIGHT, K);
  display.setFont(&FONT_11pt8b);
  drawString(DISP_WIDTH / 2, 20, "7.5in GDEY075T7: the four greys", CENTER, K);

  // the levels: 4 patches 180 x 130, white with an outline
  for (int i = 0; i < 4; ++i)
  {
    const int x = 20 + i * 192, y = 30;
    display.fillRect(x, y, 180, 130, LEVEL[i]);
    if (LEVEL[i] == W)
    {
      display.drawRect(x - 1, y - 1, 182, 132, K);
    }
    display.setFont(&FONT_9pt8b);
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
        display.drawPixel(x + i, y + j, LEVEL[PAIR[p][(i + j) & 1]]);
      }
    }
    display.setFont(&FONT_8pt8b);
    drawString(x + 58, y + 106, String(PAIR[p][0]) + " + " + String(PAIR[p][1]),
               CENTER, K);
  }

  // text and lines on each level
  for (int i = 0; i < 4; ++i)
  {
    const int x = 20 + i * 192, y = 322;
    display.fillRect(x, y, 180, 128, LEVEL[i]);
    display.drawRect(x - 1, y - 1, 182, 130, K);
    display.setFont(&FONT_14pt8b);
    drawString(x + 8, y + 30, "Rain 72\260", LEFT, K);
    drawString(x + 8, y + 62, "Rain 72\260", LEFT, W);
    display.setFont(&FONT_8pt8b);
    drawString(x + 8, y + 84, "small 8pt black", LEFT, K);
    drawString(x + 8, y + 102, "small 8pt white", LEFT, W);
    for (int k = 0; k < 4; ++k)
    {
      display.drawFastVLine(x + 140 + k * 4, y + 72, 46, K);
      display.drawFastVLine(x + 142 + k * 4, y + 72, 46, W);
    }
    display.drawFastHLine(x + 8, y + 116, 120, K);
    display.drawFastHLine(x + 8, y + 120, 120, W);
  }
}
#endif

#if defined(DISP_7C_E6)
/* A test card for the 7.3in Spectra 6 panel (-D E6_TEST_CARD, the
 * seeed_reterminal_e1002_testcard build): the six inks and the dot mixes
 * the 800x480 layout draws, in large patches for a flatbed scan or a raw
 * photo beside white paper, to set tools/sim480's ink colours.
 */
static uint16_t e6PatternInk(int x, int y, uint16_t a, uint16_t b, int level)
{
  // level: 0 all a, 1 a on a quarter of the dots (2x2 ordered), 2 checker,
  // 3 a on three quarters
  static const uint8_t B2[2][2] = {{0, 2}, {3, 1}};
  const int v = B2[y & 1][x & 1];
  switch (level)
  {
  case 1: return (v < 1) ? a : b;
  case 2: return ((x + y) & 1) ? a : b;
  case 3: return (v < 3) ? a : b;
  default: return a;
  }
}

void drawTestCardE6()
{
  display.setFont(&FONT_12pt8b);
  drawString(DISP_WIDTH / 2, 26, "Spectra 6 inks and mixes (scan beside white paper)",
             CENTER, GxEPD_BLACK);
  struct P { uint16_t a, b; int level; const char *name; };
  const uint16_t K = GxEPD_BLACK, W = GxEPD_WHITE, R = GxEPD_RED,
                 Y = GxEPD_YELLOW, B = GxEPD_BLUE, G = GxEPD_GREEN;
  const P rows[3][6] = {
    {{K, W, 0, "black"}, {W, W, 0, "white"}, {Y, W, 0, "yellow"},
     {R, W, 0, "red"}, {B, W, 0, "blue"}, {G, W, 0, "green"}},
    {{R, Y, 2, "red/yellow"}, {R, B, 2, "red/blue"}, {R, K, 2, "red/black"},
     {K, W, 2, "black/white"}, {Y, W, 2, "yellow/white"}, {B, W, 2, "blue/white"}},
    {{K, W, 1, "25% black"}, {K, W, 3, "75% black"}, {G, W, 2, "green/white"},
     {G, Y, 2, "green/yellow"}, {K, B, 2, "black/blue"}, {W, W, 0, "white"}}};
  display.setFont(&FONT_7pt8b);
  for (int r = 0; r < 3; ++r)
  {
    for (int c = 0; c < 6; ++c)
    {
      const P &p = rows[r][c];
      const int x0 = 24 + c * 128, y0 = 44 + r * 146;
      for (int y = y0; y < y0 + 116; ++y)
      {
        for (int x = x0; x < x0 + 116; ++x)
        {
          display.drawPixel(x, y, e6PatternInk(x, y, p.a, p.b, p.level));
        }
      }
      display.drawRect(x0 - 1, y0 - 1, 118, 118, GxEPD_BLACK);
      drawString(x0 + 58, y0 + 134, p.name, CENTER, GxEPD_BLACK);
    }
  }
} // end drawTestCardE6
#endif

/* This function is responsible for drawing the status bar along the bottom of
 * the display.
 */
void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft,
                   bool stale)
{
  String dataStr;
  uint16_t dataColor = DM_FG;
  display.setFont(&FONT_6pt8b);
  int pos = DISP_WIDTH - 2;
  const int sp = 2;

#if BATTERY_MONITORING
  // battery - (expecting 3.7v LiPo)
  uint32_t batPercent = calcBatPercent(batVoltage,
                                       MIN_BATTERY_VOLTAGE,
                                       MAX_BATTERY_VOLTAGE);
#if defined(DISP_3C_B) || defined(DISP_7C_F) || defined(DISP_7C_E6)
  if (batVoltage < WARN_BATTERY_VOLTAGE)
  {
    dataColor = DM_TEXT(ACCENT_COLOR);
  }
#endif
#if STATUS_BAR_EXTRAS_BAT_PERCENTAGE || STATUS_BAR_EXTRAS_BAT_VOLTAGE
  dataStr = "";
#if STATUS_BAR_EXTRAS_BAT_PERCENTAGE
  dataStr += String(batPercent) + "%";
#endif
  if (batDaysLeft >= 0)
  { // estimated runtime from the multi-day discharge slope (history.cpp)
    dataStr += " ~" + String(batDaysLeft) + "d";
  }
#if STATUS_BAR_EXTRAS_BAT_VOLTAGE
  dataStr += " (" + String( std::round(batVoltage / 10.f) / 100.f, 2 ) + "v)";
#endif
  drawString(pos, DISP_HEIGHT - 1 - 2, dataStr, RIGHT, dataColor);
  pos -= getStringWidth(dataStr) + 1;
#endif
  pos -= 24;
#if defined(GREYS_75)
  if (greys())
  { // a thin outline, filled in light grey as far as the charge goes, where
    // the bitmap's body and terminal are
    const int bx = pos + 4, by = DISP_HEIGHT - 1 - 17 + 7;
    display.drawRect(bx, by, 18, 10, dataColor);
    display.fillRect(bx + 18, by + 3, 2, 4, dataColor);
    const int fillW = static_cast<int>(std::round(14 * std::min<uint32_t>(batPercent, 100) / 100.0f));
    if (fillW > 0)
    {
      display.fillRect(bx + 2, by + 2, fillW, 6, G_LG);
    }
  }
  else
#endif
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 17,
                             getBatBitmap24(batPercent), 24, 24, dataColor);
  pos -= sp + 9;
#endif

  // WiFi
  dataColor = rssi >= -70 ? DM_FG : DM_TEXT(ACCENT_COLOR);
#if STATUS_BAR_EXTRAS_WIFI_STRENGTH || STATUS_BAR_EXTRAS_WIFI_RSSI
  dataStr = "";
#if STATUS_BAR_EXTRAS_WIFI_STRENGTH
  dataStr += String(getWiFidesc(rssi));
#endif
#if STATUS_BAR_EXTRAS_WIFI_RSSI
  if (rssi != 0)
  {
    dataStr += " (" + String(rssi) + "dBm)";
  }
#endif
  drawString(pos, DISP_HEIGHT - 1 - 2, dataStr, RIGHT, dataColor);
  pos -= getStringWidth(dataStr) + 1;
#endif
  pos -= 18;
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 13, getWiFiBitmap16(rssi),
                             16, 16, dataColor);
  pos -= sp + 8;

  // last refresh
  // When the weather on the page is old, the time says so: in the accent
  // colour where the panel has one and it reads at this size, otherwise
  // (black and white, or dark mode, where small red text is lost) with a
  // warning sign in the place of the refresh arrows.
  dataColor = DM_FG;
  bool staleSign = stale;
#if defined(DISP_3C_B) || defined(DISP_7C_F) || defined(DISP_7C_E6)
  if (stale && !DARK_MODE)
  {
    dataColor = ACCENT_COLOR;
    staleSign = false;
  }
#endif
  drawString(pos, DISP_HEIGHT - 1 - 2, refreshTimeStr, RIGHT, dataColor);
  if (staleSign)
  {
    pos -= getStringWidth(refreshTimeStr) + 24;
    display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 18,
                               warning_icon_24x24, 24, 24, dataColor);
  }
  else
  {
    pos -= getStringWidth(refreshTimeStr) + 25;
    display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 21, wi_refresh_32x32,
                               32, 32, dataColor);
  }
  pos -= sp;

  // status
  dataColor = DM_TEXT(ACCENT_COLOR);
  if (!statusStr.isEmpty())
  {
    drawString(pos, DISP_HEIGHT - 1 - 2, statusStr, RIGHT, dataColor);
    pos -= getStringWidth(statusStr) + 24;
    display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 18, error_icon_24x24,
                               24, 24, dataColor);
  }

  return;
} // end drawStatusBar

/* Draws the configuration portal info screen: a wifi icon, a "Setup Mode"
 * title, and up to 3 centered lines telling the user how to reach the
 * portal. Lines that would overflow the panel width are drawn in a smaller
 * font instead of clipping.
 */
void drawConfigPortalScreen(const String &line1, const String &line2,
                            const String &line3)
{
#ifndef DISP_BW_V1
  const int iconTop = 20;
  const int titleY = iconTop + 196 + 60;
  const int lineStart = titleY + 66;
  const int lineSpacing = 47;
#else
  const int iconTop = 8;
  const int titleY = iconTop + 196 + 48;
  const int lineStart = titleY + 50;
  const int lineSpacing = 36;
#endif

  display.drawInvertedBitmap((DISP_WIDTH - 196) / 2, iconTop, wifi_196x196,
                             196, 196, DM_GFX(ACCENT_COLOR));
#ifndef DISP_BW_V1
  display.setFont(&FONT_26pt8b);
#else
  display.setFont(&FONT_22pt8b);
#endif
  drawString(DISP_WIDTH / 2, titleY, LC_RTL ? "מצב הגדרות" : "Setup Mode", CENTER);

  const String lines[3] = {line1, line2, line3};
  for (int i = 0; i < 3; ++i)
  {
    if (lines[i].isEmpty())
    {
      continue;
    }
#ifndef DISP_BW_V1
    display.setFont(&FONT_16pt8b);
    if (getStringWidth(lines[i]) > DISP_WIDTH - 16)
    {
      display.setFont(&FONT_12pt8b);
    }
#else
    display.setFont(&FONT_12pt8b);
    if (getStringWidth(lines[i]) > DISP_WIDTH - 16)
    {
      display.setFont(&FONT_9pt8b);
    }
#endif
    drawString(DISP_WIDTH / 2, lineStart + i * lineSpacing, lines[i], CENTER);
  }
  return;
} // end drawConfigPortalScreen

/* This function is responsible for drawing prominent error messages to the
 * screen.
 *
 * If error message line 2 (errMsgLn2) is empty, line 1 will be automatically
 * wrapped.
 */
void drawError(const uint8_t *bitmap_196x196,
               const String &errMsgLn1, const String &errMsgLn2, bool mono)
{
  display.setFont(&FONT_26pt8b);
  if (!errMsgLn2.isEmpty())
  {
    drawString(DISP_WIDTH / 2,
               DISP_HEIGHT / 2 + 196 / 2 + 21,
               errMsgLn1, CENTER);
    drawString(DISP_WIDTH / 2,
               DISP_HEIGHT / 2 + 196 / 2 + 21 + 55,
               errMsgLn2, CENTER);
  }
  else
  {
    drawMultiLnString(DISP_WIDTH / 2,
                      DISP_HEIGHT / 2 + 196 / 2 + 21,
                      errMsgLn1, CENTER, DISP_WIDTH - 200, 2, 55);
  }
  display.drawInvertedBitmap(DISP_WIDTH / 2 - 196 / 2,
                             DISP_HEIGHT / 2 - 196 / 2 - 21,
                             bitmap_196x196, 196, 196,
                             mono ? GxEPD_BLACK : DM_GFX(ACCENT_COLOR));
  return;
} // end drawError

#endif // !DISP_7C_709 && !DISP_4G_426

