/* Renders every risk badge the firmware can draw, at the panel's own pixels.
 *
 * drawRiskChip has two fill paths -- fillRoundRect for the inks the Spectra 6
 * panel has (green, yellow, red) and a two-ink checkerboard for the ones it
 * does not (amber, plum, maroon). The checkerboard path is the one that used
 * to ignore the corner radius. A preview is the cheapest way to see that the
 * two paths now agree, and to see what the dithers actually look like at
 * badge scale rather than as a color name.
 *
 * The chip geometry, the level->ink mapping and the text placement below are
 * copied from renderer.cpp's drawRiskChip / drawString; the glyph rasteriser
 * is copied from Adafruit_GFX 1.12.1 (drawChar + charBounds custom-font
 * paths) so the metrics match the firmware rather than approximating it.
 *
 * Build and run (writes a PPM to stdout's neighbour, see main):
 *   g++ -std=gnu++17 -I platformio/include \
 *       -I platformio/lib/esp32-weather-epd-assets/fonts \
 *       tools/badge_preview/badge_preview.cpp -o /tmp/badge_preview
 */
#include "roundrect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- the little Arduino surface the font headers expect -------------------
#define PROGMEM
typedef struct { uint16_t bitmapOffset; uint8_t width, height, xAdvance;
                 int8_t xOffset, yOffset; } GFXglyph;
typedef struct { uint8_t *bitmap; GFXglyph *glyph;
                 uint16_t first, last; uint8_t yAdvance; } GFXfont;
#include "FreeSans/FreeSans_5pt8b.h"
#include "FreeSans/FreeSans_7pt8b.h"
#include "FreeSans/FreeSans_12pt8b.h"

// ---- panel inks -----------------------------------------------------------
enum : uint16_t { GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED, GxEPD_YELLOW,
                  GxEPD_GREEN, GxEPD_BLUE };
// tools/generate_color_icons.py's quantization targets, i.e. what the icon
// pipeline already assumes these inks look like.
static const uint8_t RGB[6][3] = {
  {0,0,0}, {255,255,255}, {255,0,0}, {255,255,0}, {0,128,0}, {0,0,255} };

static const int W = 162, H = 30; // one left-panel widget column
struct Canvas
{
  std::vector<uint8_t> px = std::vector<uint8_t>(W * H, GxEPD_WHITE);
  void drawPixel(int16_t x, int16_t y, uint16_t c)
  { if (x >= 0 && x < W && y >= 0 && y < H) { px[y * W + x] = (uint8_t)c; } }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c)
  { for (int16_t j = 0; j < h; ++j) for (int16_t i = 0; i < w; ++i)
      drawPixel((int16_t)(x + i), (int16_t)(y + j), c); }
};
static Canvas display;

// ---- Adafruit_GFX 1.12.1, custom-font paths only --------------------------
static const GFXfont *gfxFont = &FreeSans_7pt8b;
static void setFont(const GFXfont *f) { gfxFont = f; }

static void gfxDrawChar(int16_t x, int16_t y, unsigned char c, uint16_t color)
{
  c -= (uint8_t)gfxFont->first;
  GFXglyph *glyph = gfxFont->glyph + c;
  uint8_t *bitmap = gfxFont->bitmap;
  uint16_t bo = glyph->bitmapOffset;
  uint8_t w = glyph->width, h = glyph->height;
  int8_t xo = glyph->xOffset, yo = glyph->yOffset;
  uint8_t bits = 0, bit = 0;
  for (uint8_t yy = 0; yy < h; yy++)
    for (uint8_t xx = 0; xx < w; xx++)
    {
      if (!(bit++ & 7)) { bits = bitmap[bo++]; }
      if (bits & 0x80) { display.drawPixel(x + xo + xx, y + yo + yy, color); }
      bits <<= 1;
    }
}

static void gfxPrint(int16_t x, int16_t y, const std::string &s, uint16_t color)
{
  int16_t cx = x;
  for (unsigned char c : s)
  {
    if (c < gfxFont->first || c > gfxFont->last) { continue; }
    GFXglyph *g = gfxFont->glyph + (c - gfxFont->first);
    if (g->width && g->height) { gfxDrawChar(cx, y, c, color); }
    cx += g->xAdvance;
  }
}

static uint16_t advanceWidth(const std::string &s)
{ // what display.getCursorX() has advanced by after print()
  uint16_t x = 0;
  for (unsigned char c : s)
  {
    if (c < gfxFont->first || c > gfxFont->last) { continue; }
    x += gfxFont->glyph[c - gfxFont->first].xAdvance;
  }
  return x;
}

static uint16_t getStringWidth(const std::string &s)
{ // getTextBounds' width, via charBounds
  int16_t x = 0, minx = 0x7FFF, maxx = -1;
  for (unsigned char c : s)
  {
    if (c < gfxFont->first || c > gfxFont->last) { continue; }
    GFXglyph *g = gfxFont->glyph + (c - gfxFont->first);
    int16_t x1 = x + g->xOffset, x2 = x1 + g->width - 1;
    if (x1 < minx) { minx = x1; }
    if (x2 > maxx) { maxx = x2; }
    x += g->xAdvance;
  }
  return (maxx >= minx) ? (uint16_t)(maxx - minx + 1) : 0;
}

// ---- Adafruit_GFX fillRoundRect (the solid path drawRiskChip calls) -------
static void fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                             uint8_t corners, int16_t delta, uint16_t color)
{
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
  delta++;
  while (x < y)
  {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    if (x < (y + 1))
    {
      if (corners & 1) display.fillRect(x0 + x, y0 - y, 1, 2 * y + delta, color);
      if (corners & 2) display.fillRect(x0 - x, y0 - y, 1, 2 * y + delta, color);
    }
    if (y != py)
    {
      if (corners & 1) display.fillRect(x0 + py, y0 - px, 1, 2 * px + delta, color);
      if (corners & 2) display.fillRect(x0 - py, y0 - px, 1, 2 * px + delta, color);
      py = y;
    }
    px = x;
  }
}
static void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          int16_t r, uint16_t color)
{
  int16_t max_radius = ((w < h) ? w : h) / 2;
  if (r > max_radius) r = max_radius;
  display.fillRect(x + r, y, w - 2 * r, h, color);
  fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
  fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
}

// ---- renderer.cpp, verbatim ----------------------------------------------
#define RISK_PLAIN  0
#define RISK_GREEN  1
#define RISK_YELLOW 2
#define RISK_AMBER  3
#define RISK_RED    4
#define RISK_PURPLE 5
#define RISK_MAROON 6

static void ditherVLine(int16_t x, int16_t y, int16_t h,
                        uint16_t inkA, uint16_t inkB)
{
  for (int16_t i = 0; i < h; ++i)
  {
    const int16_t yy = static_cast<int16_t>(y + i);
    display.drawPixel(x, yy, ((x + yy) & 1) ? inkA : inkB);
  }
}
static void fillRoundRectDithered(int16_t x, int16_t y, int16_t w, int16_t h,
                                  int16_t r, uint16_t inkA, uint16_t inkB)
{
  roundRectSpans(x, y, w, h, r,
                 [&](int16_t sx, int16_t sy, int16_t sh)
                 { ditherVLine(sx, sy, sh, inkA, inkB); });
}

static bool g_squareDither = false; // --old: reproduce the pre-fix badges
static int16_t g_cx0, g_cy0, g_cx1, g_cy1; // last chip rect, for the contact sheet

static void drawRiskChip(int16_t x, int16_t y, const std::string &text,
                         int level)
{
  if (level == RISK_PLAIN || text.empty()) { gfxPrint(x, y, text, GxEPD_BLACK); return; }
  uint16_t w = getStringWidth(text);
  int16_t x0 = x - 2, y0 = y - 12, x1 = x + w + 4, y1 = y + 3;
  g_cx0 = x0; g_cy0 = y0; g_cx1 = x1; g_cy1 = y1;
  if (level == RISK_PURPLE || level == RISK_MAROON || level == RISK_AMBER)
  {
    uint16_t alt = (level == RISK_PURPLE) ? GxEPD_BLUE
                 : (level == RISK_AMBER)  ? GxEPD_YELLOW
                                          : GxEPD_BLACK;
    if (g_squareDither)
    { // what the dithered badges looked like before roundrect.h: the checker
      // painted over the whole bounding box, with no notion of the radius
      for (int16_t yy = y0; yy <= y1; ++yy)
        for (int16_t xx = x0; xx <= x1; ++xx)
          display.drawPixel(xx, yy, ((xx + yy) & 1) ? GxEPD_RED : alt);
    }
    else
    {
      fillRoundRectDithered(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, GxEPD_RED, alt);
    }
  }
  else
  {
    uint16_t fill = GxEPD_RED;
    if (level == RISK_YELLOW) { fill = GxEPD_YELLOW; }
    if (level == RISK_GREEN)  { fill = GxEPD_GREEN; }
    fillRoundRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 3, fill);
  }
  const bool darkText = (level == RISK_YELLOW || level == RISK_AMBER);
  gfxPrint(x + 1, y, text, darkText ? GxEPD_BLACK : GxEPD_WHITE);
}

// ---- the catalogue --------------------------------------------------------
/* Each row is the value line of one left-panel widget: the big number the
 * widget prints first, then the badge, laid out exactly as drawCurrentUVI /
 * drawCurrentAirQuality / drawCurrentPollen lay them out -- including the
 * "does it fit?" fallback to the 5pt font, which is why "Very Unhealthy"
 * is smaller than its neighbours on a real panel.
 */
struct Badge { const char *widget, *cond, *value, *label, *shortLabel;
               int level; bool fits; const char *drawn; };
static Badge BADGES[] = {
  {"UV index", "0-2",     "1",     "Low",            nullptr,          RISK_GREEN,  true, ""},
  {"UV index", "3-5",     "4",     "Moderate",       nullptr,          RISK_YELLOW, true, ""},
  {"UV index", "6-7",     "7",     "High",           nullptr,          RISK_AMBER,  true, ""},
  {"UV index", "8-10",    "9",     "Very High",      nullptr,          RISK_RED,    true, ""},
  {"UV index", "11+",     "11",    "Extreme",        nullptr,          RISK_PURPLE, true, ""},
  {"AQI (US)", "0-50",    "32",    "Good",           "Good",           RISK_GREEN,  true, ""},
  {"AQI (US)", "51-100",  "78",    "Moderate",       "Moderate",       RISK_YELLOW, true, ""},
  {"AQI (US)", "101-150", "126",   "Unhealthy for Sensitive Groups",
                                                     "USG",            RISK_AMBER,  true, ""},
  {"AQI (US)", "151-200", "172",   "Unhealthy",      "Unhealthy",      RISK_RED,    true, ""},
  {"AQI (US)", "201-300", "255",   "Very Unhealthy", "V. Unhealthy",   RISK_PURPLE, true, ""},
  {"AQI (US)", "301-500", "350",   "Hazardous",      "Hazard",         RISK_MAROON, true, ""},
  {"AQI (US)", "> 500",   "> 500", "Hazardous",      "Hazard",         RISK_MAROON, true, ""},
  {"Pollen",   "UPI 0-2", "2",     "Low",            nullptr,          RISK_GREEN,  true, ""},
  {"Pollen",   "UPI 3",   "3",     "Moderate",       nullptr,          RISK_AMBER,  true, ""},
  {"Pollen",   "UPI 4",   "4",     "High",           nullptr,          RISK_RED,    true, ""},
  {"Pollen",   "UPI 5",   "5",     "Very High",      nullptr,          RISK_RED,    true, ""},
  {"AQI (other scale)", "any", "78", "Moderate",     nullptr,          RISK_PLAIN,  true, ""},
};

/* The widget's own layout: number in 12pt at x=48, badge sp px later, then
 * drawCurrentAirQuality's fit loop -- the full name at 7pt, at 5pt, then the
 * locale's short form the same way. Nothing fits: no badge, the full name
 * wraps as plain text.
 */
static const char *drawWidgetRow(Badge &b, int16_t baseline)
{
  const int sp = 8;
  setFont(&FreeSans_12pt8b);
  gfxPrint(48, baseline, b.value, GxEPD_BLACK);
  const int16_t chipX = 48 + (int16_t)advanceWidth(b.value) + sp;
  const int max_w = (162 - sp) - chipX;

  const char *names[2] = { b.label, b.shortLabel };
  const GFXfont *fonts[2] = { &FreeSans_7pt8b, &FreeSans_5pt8b };
  static const char *WHICH[2][2] = { {"full 7pt", "full 5pt"},
                                     {"short 7pt", "short 5pt"} };
  for (int n = 0; n < 2; ++n)
  {
    if (n && (!names[n] || !strcmp(names[n], names[0]))) { continue; }
    for (int f = 0; f < 2; ++f)
    {
      setFont(fonts[f]);
      if ((int)getStringWidth(names[n]) <= max_w)
      {
        drawRiskChip(chipX, baseline, names[n], b.level);
        setFont(&FreeSans_7pt8b);
        return WHICH[n][f];
      }
    }
  }
  b.fits = false;
  setFont(&FreeSans_5pt8b);
  gfxPrint(chipX, baseline - 10, b.label, GxEPD_BLACK); // stand-in for the wrap
  setFont(&FreeSans_7pt8b);
  return "wraps";
}

int main(int argc, char **argv)
{
  const char *out = (argc > 1) ? argv[1] : "badges.ppm";
  for (int i = 2; i < argc; ++i)
    if (!strcmp(argv[i], "--old")) { g_squareDither = true; }
  const int n = sizeof(BADGES) / sizeof(BADGES[0]);
  std::vector<std::vector<uint8_t>> cells;
  for (Badge &b : BADGES)
  {
    display.px.assign(W * H, GxEPD_WHITE);
    g_cx0 = g_cy0 = g_cx1 = g_cy1 = -1;
    const char *how = drawWidgetRow(b, 21);
    cells.push_back(display.px);
    printf("%s\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%d\t%d\t%d\n", b.widget, b.cond,
           b.value, b.label,
           strstr(how, "short") ? b.shortLabel : b.label, b.level, how,
           b.fits ? g_cx0 : -1, b.fits ? g_cy0 : -1,
           b.fits ? g_cx1 : -1, b.fits ? g_cy1 : -1);
  }
  FILE *f = fopen(out, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H * n);
  for (auto &c : cells)
    for (uint8_t v : c) fwrite(RGB[v], 1, 3, f);
  fclose(f);
  fprintf(stderr, "wrote %s (%d x %d)\n", out, W, H * n);
  return 0;
}
