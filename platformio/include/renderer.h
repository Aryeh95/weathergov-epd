/* Renderer declarations for esp32-weather-epd.
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

#ifndef __RENDERER_H__
#define __RENDERER_H__

#include <vector>
#include <Arduino.h>
#include <time.h>
#include "api_response.h"
#include "config.h"
#include "rtl.h" // shapeText

#ifdef DISP_BW_V2
  #define DISP_WIDTH  800
  #define DISP_HEIGHT 480
#if defined(GREYS_75)
  // GREYS_75: the GDEY075T7 in its four greys, through GxEPD2_4G (the
  // classic layout with grey fills; see renderer.cpp)
  #include <GxEPD2_4G_4G.h>
  #include <GxEPD2_4G_BW.h>
#else
  #include <GxEPD2_BW.h>
#endif
  // The same 800x480 page on another black-and-white panel: Seeed's 4.26in
  // (XIAO EE05) is Good Display's GDEQ0426T82 glass, an SSD1677.
  #if defined(PANEL_GDEQ0426T82)
    #define GxEPD2_BW_V2_PANEL GxEPD2_426_GDEQ0426T82
  #else
    #define GxEPD2_BW_V2_PANEL GxEPD2_750_GDEY075T7
  #endif
#if defined(GREYS_75)
  // Tones between the four greys, drawn as two levels dot by dot: stand-in
  // colours nothing else uses (black is 0), turned into their dots by
  // Grey75Display::drawPixel.
  #define G75_KDG  0x0001  // black + dark grey, a real dark grey
  #define G75_LGW  0x0002  // light grey + white
  #define G75_LGW4 0x0003  // light grey on one dot in four, the faintest
  #define G75_DGLG 0x0004  // dark + light grey, a mid grey (L* 54 measured)
  // a quarter of the page per band: 24000 bytes of buffer
  typedef GxEPD2_4G_4G<GxEPD2_BW_V2_PANEL,
                       GxEPD2_BW_V2_PANEL::HEIGHT / 4> Grey75Base;
  class Grey75Display : public Grey75Base
  {
  public:
    using Grey75Base::Grey75Base;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    // Text in a face that has a 2-bit twin (tools/fontconvert2.py) is drawn
    // and measured with the twin: black text with its edges in light grey.
    size_t write(uint8_t c) override;
    void getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1,
                       int16_t *y1, uint16_t *w, uint16_t *h);
    void getTextBounds(const String &s, int16_t x, int16_t y, int16_t *x1,
                       int16_t *y1, uint16_t *w, uint16_t *h);
    const GFXfont *twin2();
    // A condition, alert or error icon at 196, 64 or 48 dots in black is
    // drawn from its 2-bit twin (icons_2bit.h), its edges in light grey.
    using Grey75Base::drawInvertedBitmap;
    void drawInvertedBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                            int16_t w, int16_t h, uint16_t color);
    // Black and white instead of the greys, on the same panel driver: the
    // panel's quicker refresh, for the pages KEY3 turns to. Set it (with
    // setGreyMode426) before initDisplay(); these then go to the black-
    // and-white buffer.
    bool bw = false;
    void init(uint32_t serial_diag_bitrate, bool initial,
              uint16_t reset_duration = 10, bool pulldown_rst_mode = false);
    void setRotation(uint8_t r) override;
    void setFullWindow();
    void firstPage();
    bool nextPage();
  };
  extern Grey75Display display;
#else
  extern GxEPD2_BW<GxEPD2_BW_V2_PANEL,
                   GxEPD2_BW_V2_PANEL::HEIGHT> display;
#endif
#endif
#ifdef DISP_3C_B
  #define DISP_WIDTH  800
  #define DISP_HEIGHT 480
  #include <GxEPD2_3C.h>
  extern GxEPD2_3C<GxEPD2_750c_GDEY075Z08,
                   GxEPD2_750c_GDEY075Z08::HEIGHT / 2> display;
#endif
#ifdef DISP_7C_F
  #define DISP_WIDTH  800
  #define DISP_HEIGHT 480
  #include <GxEPD2_7C.h>
  extern GxEPD2_7C<GxEPD2_730c_GDEY073D46, 
                   GxEPD2_730c_GDEY073D46::HEIGHT / 4> display;
#endif
#ifdef DISP_BW_V1
  #define DISP_WIDTH  640
  #define DISP_HEIGHT 384
  #include <GxEPD2_BW.h>
  extern GxEPD2_BW<GxEPD2_750,
                   GxEPD2_750::HEIGHT> display;
#endif
#ifdef DISP_7C_E6
  #define DISP_WIDTH  800
  #define DISP_HEIGHT 480
  #include <GxEPD2_7C.h>
  // Some GDEP073E01 panel variants (the reTerminal E1002's, for one) take
  // longer than the stock driver's 20s busy timeout to complete a full
  // refresh, which aborts the wait mid-update ("Busy Timeout!"). Raise the
  // limit; it only matters when exceeded, so faster panels are unaffected.
  class GxEPD2_730c_GDEP073E01_Patient : public GxEPD2_730c_GDEP073E01
  {
  public:
    GxEPD2_730c_GDEP073E01_Patient(int16_t cs, int16_t dc, int16_t rst,
                                   int16_t busy)
        : GxEPD2_730c_GDEP073E01(cs, dc, rst, busy)
    {
      _busy_timeout = 45000000; // us
    }
    void refresh(bool partial_update_mode = false) override;
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override;

  private:
    bool _canLightSleep = false;
    static void busySleepCb(const void *param);
  };
  extern GxEPD2_7C<GxEPD2_730c_GDEP073E01_Patient,
            GxEPD2_730c_GDEP073E01_Patient::HEIGHT / 4> display;
#endif

#ifdef DISP_7C_E6
  void drawTestCardE6();
#endif
#if defined(DISP_BW_V2) && defined(GREYS_75)
  // The four greys, their mixes and text on each: the _testcard build.
  void drawTestCard75();
  // The pages KEY3 turns to (main.cpp, turnPage): 0 the home page, 1 the
  // forecast in words.
  #define PAGES_426 2
  void setGreyMode426(bool grey);
  void drawPage426(int page, const owm_current_t &current,
                   const owm_hourly_t *hourly, const owm_daily_t *daily,
                   const owm_resp_air_pollution_t &air,
                   const pollen_info_t &pollen, float inTemp,
                   float inHumidity, const String &city,
                   const String &date, const String &refreshTime,
                   uint32_t batVoltage);
#endif
#ifdef DISP_7C_709
  // Landscape, as the layout sees it; the panel itself counts 1200x1600.
  #define DISP_WIDTH  1600
  #define DISP_HEIGHT 1200
  #include "epd709.h"
  extern Epd709 display;
  // This layout's alert strip pushes the rest of the page down, so the
  // alerts have to be known before anything is drawn. Call once, before
  // the drawing loop.
  void layout709Begin(std::vector<owm_alerts_t> &alerts,
                      const owm_hourly_t *hourly);
  // Inks, screens, type sizes and orientation marks, for a first look at
  // a real panel. Drawn instead of the weather by the _testcard build.
  void drawTestCard709();
  void drawTestCard709InkScan();
#endif

#ifdef DISP_4G_426
  // The EE05's 4.26in (GDEQ0426T82), through GxEPD2_4G: the home page in the
  // panel's four greys, the other pages in black and white -- a quicker
  // refresh, for pages turned by hand. `display` stands for whichever of
  // the two drawing modes initDisplay() set up; drawing goes through the
  // renderer's own functions.
  #define DISP_WIDTH  800
  #define DISP_HEIGHT 480
  #if defined(DISP_426_ON_E6)
    #include <GxEPD2.h>   // the E1002's colour panel, through GxEPD2
  #else
    #include <GxEPD2_4G.h>
  #endif
  class Display426
  {
  public:
    bool nextPage();
  };
  extern Display426 display;
  // Pages of this layout: 0 is the home page, in greys; the rest are turned
  // to with KEY3 and drawn in black and white.
  #define PAGES_426 3
  // The mode the next initDisplay() sets up: greys (the default) or black
  // and white.
  void setGreyMode426(bool grey);
  // The home page's tones (renderer426.cpp PALETTES_426); 0 the default.
  extern int PALETTE_426;
  // The indoor reading's trend for the home page (TREND_* in history.h).
  extern int INDOOR_TREND_426;
  // Whether the page has an alert (drawAlerts): set before it is drawn.
  extern bool ALERT_426;
  // The hours page's layout: 0 the cells, 1 tables, 2 graphs, 3 graph + cells.
  extern int HOURS_LAYOUT_426;
  // The four greys, their mixes and text on each: the _testcard build.
  void drawTestCard426();
  // One of the black-and-white pages (1 .. PAGES_426 - 1), drawn whole.
  void drawPage426(int page, const owm_current_t &current,
                   const owm_hourly_t *hourly, const owm_daily_t *daily,
                   const owm_resp_air_pollution_t &air,
                   const pollen_info_t &pollen, float inTemp,
                   float inHumidity, const String &city,
                   const String &date, const String &refreshTime,
                   uint32_t batVoltage);
#endif

typedef enum alignment
{
  LEFT,
  RIGHT,
  CENTER
} alignment_t;

// Dark mode (config.json "dark_mode") swaps ground and text colors at
// runtime. Default text color arguments use DM_FG so every caller follows
// the mode without changes (default args are evaluated at each call site).
#define DM_FG (DARK_MODE ? GxEPD_WHITE : GxEPD_BLACK)
#define DM_BG (DARK_MODE ? GxEPD_BLACK : GxEPD_WHITE)
// Small text can't carry the dim red/blue inks on a black ground (verified
// on real Spectra 6 ink), so accent-colored SMALL text falls back to white
// in dark mode; the color semantics stay on large elements (graph line,
// precip bars, icons) where the ink has enough area to read.
#define DM_TEXT(c) (DARK_MODE ? GxEPD_WHITE : (c))
// Graphics (lines, bars, bitmaps) keep their ink in dark mode -- except
// when the panel's macro collapsed it to GxEPD_BLACK (single-color panels,
// where every accent is black): black graphics flip to white so they stay
// visible on the black ground.
#define DM_GFX(c) ((DARK_MODE && (c) == GxEPD_BLACK) ? GxEPD_WHITE : (c))

uint16_t getStringWidth(const String &text);
uint16_t getStringHeight(const String &text);
void drawString(int16_t x, int16_t y, const String &text, alignment_t alignment,
                uint16_t color=DM_FG);
void drawMultiLnString(int16_t x, int16_t y, const String &text,
                       alignment_t alignment, uint16_t max_width,
                       uint16_t max_lines, int16_t line_spacing,
                       uint16_t color=DM_FG);
void fillDisplayBackground();
void initDisplay();
void powerOffDisplay();
void drawCurrentConditions(const owm_current_t &current,
                           const owm_daily_t &today,
                           const owm_resp_air_pollution_t &owm_air_pollution,
                           const pollen_info_t &pollen,
                           float inTemp, float inHumidity);
void drawForecast(const owm_daily_t *daily, tm timeInfo);
void drawAlerts(std::vector<owm_alerts_t> &alerts,
                const String &city, const String &date);
void drawLocationDate(const String &city, const String &date);
void drawOutlookGraph(const owm_hourly_t *hourly, const owm_daily_t *daily,
                      tm timeInfo);
// stale: the weather on the page is old (see markStale() in main.cpp)
void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft = -1,
                   bool stale = false);
// mono: the icon in black instead of the accent colour (the low-battery
// page, which may stay on the panel for days)
void drawError(const uint8_t *bitmap_196x196,
               const String &errMsgLn1, const String &errMsgLn2="",
               bool mono = false);
void drawConfigPortalScreen(const String &line1, const String &line2,
                            const String &line3);
void drawCurrentSunrise(const owm_current_t &current);
void drawCurrentSunset(const owm_current_t &current);
void drawCurrentInTemp(float inTemp, int trend);
void drawCurrentInHumidity(float inHumidity);
void drawCurrentWind(const owm_current_t &current);
void drawCurrentHumidity(const owm_current_t &current);
void drawCurrentUVI(const owm_current_t &current);
void drawCurrentPressure(const owm_current_t &current, int trend);
void drawCurrentVisibility(const owm_current_t &current);
void drawCurrentPollen(const pollen_info_t &pollen);
void drawCurrentAirQuality(const owm_resp_air_pollution_t &owm_air_pollution);
void drawCurrentDewpoint(const owm_current_t &current);
void drawCurrentHeatStress(const owm_current_t &current);

#endif
