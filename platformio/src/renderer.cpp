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

#include "_locale.h"
#include "_strftime.h"
#include "renderer.h"
#include "history.h"
#include "api_response.h"
#include "config.h"
#include "conversions.h"
#include "display_utils.h"
#include "sun.h"

// fonts
#include FONT_HEADER

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

#ifdef DISP_BW_V2
  GxEPD2_BW<GxEPD2_750_GDEY075T7,
            GxEPD2_750_GDEY075T7::HEIGHT> display(
    GxEPD2_750_GDEY075T7(PIN_EPD_CS,
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
static inline int wgtLabelY(int PosY)
{
  return wgtY(PosY) + ((WIDGET_ROWS > 5) ? 8 : 10);
}
static inline int wgtValueY(int PosY)
{
  return wgtY(PosY) + 17 / 2 + wgtIconSize() / 2
         + ((WIDGET_ROWS > 5) ? 4 : 0);
}

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
#else
  (void)colorName;
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

/* Draws a risk-category word. On multicolor panels, elevated levels render
 * as a colored badge -- the ink as AREA with solid text knocked out on top,
 * which stays legible where colored glyphs do not (amber/red are too dim as
 * 7pt text on this ink; verified on the E1002). Purple has no native ink,
 * but a red/blue checker at area scale reads as a dark plum. Uses the
 * currently selected font; x is the left edge, y the text baseline.
 */
static void drawRiskChip(int16_t x, int16_t y, const String &text, int level)
{
#ifndef MULTICOLOR_DISPLAY
  (void)level;
  drawString(x, y, text, LEFT);
#else
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
    for (int16_t yy = y0; yy <= y1; ++yy)
    {
      for (int16_t xx = x0; xx <= x1; ++xx)
      {
        display.drawPixel(xx, yy, ((xx + yy) & 1) ? GxEPD_RED : alt);
      }
    }
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
#endif
} // end drawRiskChip

/* Returns the string width in pixels
 */
uint16_t getStringWidth(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

/* Returns the string height in pixels
 */
uint16_t getStringHeight(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
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
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT)
  {
    x = x - w;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2;
  }
  display.setCursor(x, y);
  display.print(text);
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

    display.getTextBounds(textRemaining, 0, 0, &x1, &y1, &w, &h);

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
          display.getTextBounds(subStr, 0, 0, &x1, &y1, &w, &h);
        }
        else
        {
          // this is the last line, we need to make sure there is space for
          // ellipsis
          display.getTextBounds(subStr + "...", 0, 0, &x1, &y1, &w, &h);
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
  }
#ifdef DRIVER_WAVESHARE
  display.init(115200, true, 2, false);
#endif
#ifdef DRIVER_DESPI_C02
  display.init(115200, true, 10, false);
#endif
  // remap spi
  SPI.end();
  SPI.begin(PIN_EPD_SCK,
            PIN_EPD_MISO,
            PIN_EPD_MOSI,
            PIN_EPD_CS);

  display.setRotation(0);
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
    digitalWrite(PIN_EPD_PWR, LOW);
  }
  return;
} // end initDisplay


/* These functions are responsible for drawing the current conditions and
 * associated icons on the left panel.
 */

// drawCurrentSunrise
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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_SUNRISE, LEFT);

  // sunrise
  display.setFont(&FONT_12pt8b);
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
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

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_SUNSET, LEFT);

  // sunset
  display.setFont(&FONT_12pt8b);
  char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
  time_t ts = current.sunset;
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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_WIND, LEFT);

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
  drawString( (48 + 24)+ (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
#else
  drawString(48    + (162 * PosX) , wgtValueY(PosY), dataStr, LEFT);
#endif
  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX(), wgtValueY(PosY),
             unitStr, LEFT);

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

  unsigned int uvi = static_cast<unsigned int>(
                                std::max(std::round(current.uvi), 0.0f));

  // icons
  drawWidgetIcon(162 * PosX, wgtY(PosY),
                 wi_day_sunny_48x48, wi_day_sunny_40x40, "uvi", DM_FG);

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_UV_INDEX, LEFT);

  // spacing between end of index value and start of descriptor text
  const int sp = 8;

  // uv index
  display.setFont(&FONT_12pt8b);
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
  int max_w = (162 + (PosX * 162) - sp) - (display.getCursorX() + sp);
  if (getStringWidth(dataStr) <= max_w)
  { // Fits on a single line, draw along bottom
    drawRiskChip(display.getCursorX() + sp, wgtValueY(PosY),
                 dataStr, uviRisk);
  }
  else
  { // use smaller font
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    { // Fits on a single line with smaller font, draw along bottom
      drawRiskChip(display.getCursorX() + sp,
                   wgtValueY(PosY),
                   dataStr, uviRisk);
    }
    else
    { // Does not fit on a single line, draw higher to allow room for 2nd line
      drawMultiLnString(display.getCursorX() + sp,
                        wgtValueY(PosY) - 10,
                        dataStr, LEFT, max_w, 2, 10);
    }
  }
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
  int phase = calcMoonPhase(static_cast<int64_t>(time(nullptr)), &illum);

  // icons
  drawDitheredIcon(162 * PosX, wgtY(PosY),
                   getMoonPhaseDithered(phase, wgtIconSize()),
                   wgtIconSize());

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_MOONPHASE,
             LEFT);

  // spacing between end of value and start of descriptor text
  const int sp = 8;

  // illuminated percentage, then the phase name fitted beside/below it
  display.setFont(&FONT_12pt8b);
  dataStr = String(illum) + "%";
  drawString(48 + (162 * PosX), wgtValueY(PosY),
             dataStr, LEFT);
  display.setFont(&FONT_7pt8b);
  dataStr = String(getMoonPhaseDesc(phase));
  int max_w = (162 + (PosX * 162) - sp) - (display.getCursorX() + sp);
  if (getStringWidth(dataStr) <= max_w)
  {
    drawString(display.getCursorX() + sp,
               wgtValueY(PosY), dataStr, LEFT);
  }
  else
  {
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    {
      drawString(display.getCursorX() + sp,
                 wgtValueY(PosY), dataStr, LEFT);
    }
    else
    {
      drawMultiLnString(display.getCursorX() + sp,
                        wgtValueY(PosY) - 10,
                        dataStr, LEFT, max_w, 2, 10);
    }
  }
  return;
}
// end drawCurrentMoonPhase

// drawCurrentAirQuality
void drawCurrentAirQuality(const owm_resp_air_pollution_t &owm_air_pollution)
{
  if (POS_AIR_QULITY < 0 || POS_AIR_QULITY / 2 >= WIDGET_ROWS)
  {
    return;
  }
  String dataStr, unitStr;
  int PosX = (POS_AIR_QULITY % 2);
  int PosY = static_cast<int>(POS_AIR_QULITY / 2);

  // labels
  display.setFont(&FONT_7pt8b);

  // When AirNow reported (us_aqi >= 0), display the official US EPA AQI it
  // provides -- by definition on the United States AQI scale, regardless of
  // the locale's configured AQI_SCALE. Otherwise compute an AQI on the
  // locale's scale from Open-Meteo's pollutant concentrations.
  const bool useAirNow = (owm_air_pollution.us_aqi >= 0);

  const char *air_quality_index_label;
  if (useAirNow || aqi_desc_type(AQI_SCALE) == AIR_QUALITY_DESC)
  {
    air_quality_index_label = TXT_AIR_QUALITY;
  }
  else // (aqi_desc_type(AQI_SCALE) == AIR_POLLUTION_DESC)
  {
    air_quality_index_label = TXT_AIR_POLLUTION;
  }
  // Source tag: AirNow = measured at EPA monitoring stations; otherwise the
  // AQI is computed from Open-Meteo's CAMS *model* pollutant fields -- worth
  // telling apart at a glance. Drawn in the smallest font so it fits after
  // the label in the widget column.
  drawString(48 + (162 * PosX), wgtLabelY(PosY), air_quality_index_label,
             LEFT);
  display.setFont(&FONT_5pt8b);
  drawString(display.getCursorX() + 3, wgtLabelY(PosY),
             useAirNow ? "(AirNow)" : "(model)", LEFT);
  display.setFont(&FONT_7pt8b);

  // spacing between end of index value and start of descriptor text
  const int sp = 8;

  // air quality index
  display.setFont(&FONT_12pt8b);
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
    dataStr = "> " + String(aqi_max);
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
  const int aqiRisk = !usScale     ? RISK_PLAIN
                    : (aqi <= 50)  ? RISK_GREEN
                    : (aqi <= 100) ? RISK_YELLOW
                    : (aqi <= 150) ? RISK_AMBER
                    : (aqi <= 200) ? RISK_RED
                    : (aqi <= 300) ? RISK_PURPLE
                                   : RISK_MAROON;
  int max_w = (162 + (PosX * 162) - sp) - (display.getCursorX() + sp);
  if (usScale && aqi > 100 && aqi <= 150 && getStringWidth(dataStr) > max_w)
  { // "Unhealthy for Sensitive Groups" can't fit a one-line chip; the EPA's
    // own legend abbreviates the band as "USG".
    dataStr = "USG";
  }
  if (getStringWidth(dataStr) <= max_w)
  { // Fits on a single line, draw along bottom
    drawRiskChip(display.getCursorX() + sp, wgtValueY(PosY),
                 dataStr, aqiRisk);
  }
  else
  { // use smaller font
    display.setFont(&FONT_5pt8b);
    if (getStringWidth(dataStr) <= max_w)
    { // Fits on a single line with smaller font, draw along bottom
      drawRiskChip(display.getCursorX() + sp,
                   wgtValueY(PosY),
                   dataStr, aqiRisk);
    }
    else
    { // Does not fit on a single line, draw higher to allow room for 2nd line
      drawMultiLnString(display.getCursorX() + sp,
                        wgtValueY(PosY) - 10,
                        dataStr, LEFT, max_w, 2, 10);
    }
  }

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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_INDOOR_TEMPERATURE, LEFT);

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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_HUMIDITY, LEFT);

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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_PRESSURE, LEFT);

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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_VISIBILITY, LEFT);

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
    dataStr = "> " + dataStr;
  }
  drawString(48 + (162 * PosX), wgtValueY(PosY), dataStr, LEFT);
  display.setFont(&FONT_8pt8b);
  drawString(display.getCursorX(), wgtValueY(PosY),
             unitStr, LEFT);

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
#else
  drawDitheredIcon(162 * PosX, wgtY(PosY),
                   (iconSize == 40) ? pollen_line_40 : pollen_line_48,
                   iconSize);
#endif

  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_POLLEN, LEFT);

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
  drawRiskChip(display.getCursorX() + 8, wgtValueY(PosY), desc, pollenRisk);
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
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_INDOOR_HUMIDITY, LEFT);

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
  display.drawInvertedBitmap(162 * PosX + 48 - 24, wgtY(PosY) + 4,
                             wi_raindrops_24x24, 24, 24, DM_FG);
  
  // labels
  display.setFont(&FONT_7pt8b);
  drawString(48 + (162 * PosX), wgtLabelY(PosY), TXT_DEWPOINT, LEFT);

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
  display.drawInvertedBitmap(0, 0,
                             getCurrentConditionsBitmap196(current, today),
                             196, 196, getCurrentConditionsColor196(current));

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
    drawString(196 + 164 / 2 - 20, 196 / 2 + 69 / 2, dataStr, CENTER);
#elif defined(DISP_BW_V1)
    drawString(156 + 164 / 2 - 20, 196 / 2 + 69 / 2, dataStr, CENTER);
#endif
  display.setFont(&FONT_14pt8b);
  drawString(display.getCursorX(), 196 / 2 - 69 / 2 + 20, unitStr, LEFT);

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
  drawString(196 + 164 / 2, 98 + 69 / 2 + 12 + 17, dataStr, CENTER);
#elif defined(DISP_BW_V1)
  drawString(156 + 164 / 2, 98 + 69 / 2 + 12 + 17, dataStr, CENTER);
#endif
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
  if (days > 5)
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
  for (int i = 0; i < days; ++i)
  {
    // column center; the icon's vertical center matches the old layout
    int cx = xStart + static_cast<int>(i * colW + colW / 2);
    int iconX = cx - iconSize / 2;
    int iconY = 98 + 69 / 2 - 6 - iconSize / 2;
    // icons
#ifdef MULTICOLOR_DISPLAY
    const uint8_t *colorIcon = getColorIconDaily(daily[i], iconSize);
    if (colorIcon)
    {
      drawColorIcon(iconX, iconY, colorIcon, iconSize);
    }
    else
#endif
    display.drawInvertedBitmap(iconX, iconY,
                               (iconSize == 64)
                                   ? getDailyForecastBitmap64(daily[i])
                                   : getDailyForecastBitmap48(daily[i]),
                               iconSize, iconSize,
                               getDailyForecastColor64(daily[i]));
    // day of week label
    display.setFont(&FONT_11pt8b);
    char dayBuffer[8] = {};
    _strftime(dayBuffer, sizeof(dayBuffer), "%a", &timeInfo); // abbrv'd day
    drawString(cx - 2, 98 + 69 / 2 - 32 - 26 - 6 + 16, dayBuffer, CENTER);
    timeInfo.tm_wday = (timeInfo.tm_wday + 1) % 7; // increment to next day

    // high | low
    display.setFont(tempFont);
    drawString(cx, 98 + 69 / 2 + 38 - 6 + 12, "|", CENTER);
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
      drawString(cx - 4 + pass, 98 + 69 / 2 + 38 - 6 + 12, hiStr, RIGHT,
                 DM_TEXT(COLOR_TEMP_HI));
      drawString(cx + 5 + pass, 98 + 69 / 2 + 38 - 6 + 12, loStr, LEFT,
                 DM_TEXT(COLOR_TEMP_LO));
#endif
#ifdef TEMP_ORDER_LH
      drawString(cx - 4 + pass, 98 + 69 / 2 + 38 - 6 + 12, loStr, RIGHT,
                 DM_TEXT(COLOR_TEMP_LO));
      drawString(cx + 5 + pass, 98 + 69 / 2 + 38 - 6 + 12, hiStr, LEFT,
                 DM_TEXT(COLOR_TEMP_HI));
#endif
    }

// daily forecast precipitation
#if DISPLAY_DAILY_PRECIP
    float dailyPrecip;
#if defined(UNITS_DAILY_PRECIP_POP)
    dailyPrecip = daily[i].pop * 100;
    dataStr = String(static_cast<int>(dailyPrecip));
    unitStr = "%";
#else
    dailyPrecip = daily[i].snow + daily[i].rain;
#if defined(UNITS_DAILY_PRECIP_MILLIMETERS)
    // Round up to nearest mm
    dailyPrecip = std::round(dailyPrecip);
    dataStr = String(static_cast<int>(dailyPrecip));
    unitStr = String(" ") + TXT_UNITS_PRECIP_MILLIMETERS;
#elif defined(UNITS_DAILY_PRECIP_CENTIMETERS)
    // Round up to nearest 0.1 cm
    dailyPrecip = millimeters_to_centimeters(dailyPrecip);
    dailyPrecip = std::round(dailyPrecip * 10) / 10.0f;
    dataStr = String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_CENTIMETERS;
#elif defined(UNITS_DAILY_PRECIP_INCHES)
    // Round up to nearest 0.1 inch
    dailyPrecip = millimeters_to_inches(dailyPrecip);
    dailyPrecip = std::round(dailyPrecip * 10) / 10.0f;
    dataStr = String(dailyPrecip, 1);
    unitStr = String(" ") + TXT_UNITS_PRECIP_INCHES;
#endif
#endif
#if (DISPLAY_DAILY_PRECIP == 2) // smart
      if (dailyPrecip > 0.0f)
      {
#endif
        display.setFont(&FONT_6pt8b);
        drawString(cx, 98 + 69 / 2 + 38 - 6 + 26,
                   dataStr + unitStr, CENTER, DM_TEXT(COLOR_PRECIP));
#if (DISPLAY_DAILY_PRECIP == 2) // smart
      }
#endif
#endif // DISPLAY_DAILY_PRECIP
    }

    return;
  } // end drawForecast

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

  int *ignore_list = (int *) calloc(alerts.size(), sizeof(*ignore_list));
  int *alert_indices = (int *) calloc(alerts.size(), sizeof(*alert_indices));
  if (!ignore_list || !alert_indices)
  {
    Serial.println("Error: Failed to allocate memory while handling alerts.");
    free(ignore_list);
    free(alert_indices);
    return;
  }

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

  if (num_valid_alerts == 1)
  { // 1 alert
    // adjust max width to for 48x48 icons
    max_w -= 48;

    owm_alerts_t &cur_alert = alerts[alert_indices[0]];
    display.drawInvertedBitmap(196, 8, getAlertBitmap48(cur_alert), 48, 48,
                               DARK_MODE ? DM_GFX(COLOR_SUN)
                                         : ACCENT_COLOR);
    // must be called after getAlertBitmap
    toTitleCase(cur_alert.event);

    display.setFont(&FONT_14pt8b);
    if (getStringWidth(cur_alert.event) <= max_w)
    { // Fits on a single line, draw along bottom
      drawString(196 + 48 + 4, 24 + 8 - 12 + 20 + 1, cur_alert.event, LEFT);
    }
    else
    { // use smaller font
      display.setFont(&FONT_12pt8b);
      if (getStringWidth(cur_alert.event) <= max_w)
      { // Fits on a single line with smaller font, draw along bottom
        drawString(196 + 48 + 4, 24 + 8 - 12 + 17 + 1, cur_alert.event, LEFT);
      }
      else
      { // Does not fit on a single line, draw higher to allow room for 2nd line
        drawMultiLnString(196 + 48 + 4, 24 + 8 - 12 + 17 - 11,
                          cur_alert.event, LEFT, max_w, 2, 23);
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

      display.drawInvertedBitmap(196, (i * 32), getAlertBitmap32(cur_alert),
                                 32, 32, DARK_MODE ? DM_GFX(COLOR_SUN)
                                                   : ACCENT_COLOR);
      // must be called after getAlertBitmap
      toTitleCase(cur_alert.event);

      drawMultiLnString(196 + 32 + 3, 5 + 17 + (i * 32),
                        cur_alert.event, LEFT, max_w, 1, 0);
    } // end for-loop
  } // end 2 alerts

  free(ignore_list);
  free(alert_indices);

  return;
} // end drawAlerts

/* This function is responsible for drawing the city string and date
 * information in the top right corner.
 */
void drawLocationDate(const String &city, const String &date)
{
  // location, date
  display.setFont(&FONT_16pt8b);
#ifdef MULTICOLOR_DISPLAY
  // On multicolor panels red carries meaning (temperature, alerts, warnings),
  // so a red city name reads out of place -- keep it black there. The
  // single-accent panels keep the upstream red-accent look.
  drawString(DISP_WIDTH - 2, 23, city, RIGHT, DM_FG);
#else
  drawString(DISP_WIDTH - 2, 23, city, RIGHT, ACCENT_COLOR);
#endif
  display.setFont(&FONT_12pt8b);
  drawString(DISP_WIDTH - 2, 30 + 4 + 17, date, RIGHT);
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
#ifdef UNITS_TEMP_KELVIN
  float tempMin = hourly[0].temp;
#endif
#ifdef UNITS_TEMP_CELSIUS
  float tempMin = kelvin_to_celsius(hourly[0].temp);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
  float tempMin = kelvin_to_fahrenheit(hourly[0].temp);
#endif
  float tempMax = tempMin;
#ifdef UNITS_HOURLY_PRECIP_POP
  float precipMax = hourly[0].pop;
#else
  float precipMax = hourly[0].rain_1h + hourly[0].snow_1h;
#endif
  int yTempMajorTicks = 5;
  float newTemp = 0;
  for (int i = 1; i < HOURLY_GRAPH_MAX; ++i)
  {
#ifdef UNITS_TEMP_KELVIN
    newTemp = hourly[i].temp;
#endif
#ifdef UNITS_TEMP_CELSIUS
    newTemp = kelvin_to_celsius(hourly[i].temp);
#endif
#ifdef UNITS_TEMP_FAHRENHEIT
    newTemp = kelvin_to_fahrenheit(hourly[i].temp);
#endif
    tempMin = std::min(tempMin, newTemp);
    tempMax = std::max(tempMax, newTemp);
#ifdef UNITS_HOURLY_PRECIP_POP
    precipMax = std::max<float>(precipMax, hourly[i].pop);
#else
    precipMax = std::max<float>(
                precipMax, hourly[i].rain_1h + hourly[i].snow_1h);
#endif
  }
  int tempBoundMin = static_cast<int>(tempMin - 1)
                      - modulo(static_cast<int>(tempMin - 1), yTempMajorTicks);
  int tempBoundMax = static_cast<int>(tempMax + 1)
   + (yTempMajorTicks - modulo(static_cast<int>(tempMax + 1), yTempMajorTicks));

  // while we have to many major ticks then increase the step
  while ((tempBoundMax - tempBoundMin) / yTempMajorTicks > yMajorTicks)
  {
    yTempMajorTicks += 5;
    tempBoundMin = static_cast<int>(tempMin - 1)
                      - modulo(static_cast<int>(tempMin - 1), yTempMajorTicks);
    tempBoundMax = static_cast<int>(tempMax + 1) + (yTempMajorTicks
                      - modulo(static_cast<int>(tempMax + 1), yTempMajorTicks));
  }
  // while we have not enough major ticks, add to either bound
  while ((tempBoundMax - tempBoundMin) / yTempMajorTicks < yMajorTicks)
  {
    // add to whatever bound is closer to the actual min/max
    if (tempMin - tempBoundMin <= tempBoundMax - tempMax)
    {
      tempBoundMin -= yTempMajorTicks;
    }
    else
    {
      tempBoundMax += yTempMajorTicks;
    }
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
    xPos1 -= 23;
  }

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
    drawString(xPos0 - 8, yTick + 4, dataStr, RIGHT, DM_TEXT(ACCENT_COLOR));

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

      drawString(xPos1 + 8, yTick + 4, dataStr, LEFT, DM_TEXT(COLOR_PRECIP));
      display.setFont(&FONT_5pt8b);
      drawString(display.getCursorX(), yTick + 4, precipUnit, LEFT,
                 DM_TEXT(COLOR_PRECIP));
    } // end draw labels if precip is >0

    // draw dotted line
    if (i < yMajorTicks)
    {
      for (int x = xPos0; x <= xPos1 + 1; x += 3)
      {
        display.drawPixel(x, yTick + (yTick % 2), DM_FG);
      }
    }
  }

  int xMaxTicks = 8;
  int hourInterval = static_cast<int>(ceil(HOURLY_GRAPH_MAX
                                           / static_cast<float>(xMaxTicks)));
  float xInterval = (xPos1 - xPos0 - 1) / static_cast<float>(HOURLY_GRAPH_MAX);
  display.setFont(&FONT_8pt8b);

  // precalculate all x and y coordinates for temperature values
  float yPxPerUnit = (yPos1 - yPos0)
                     / static_cast<float>(tempBoundMax - tempBoundMin);
  std::vector<int> x_t;
  std::vector<int> y_t;
  x_t.reserve(HOURLY_GRAPH_MAX);
  y_t.reserve(HOURLY_GRAPH_MAX);
    for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    y_t[i] = kelvin_to_plot_y(hourly[i].temp, tempBoundMin, yPxPerUnit, yPos1);
    x_t[i] = static_cast<int>(std::round(xPos0 + (i * xInterval)
                                          + (0.5 * xInterval) ));
  }

#if DISPLAY_HOURLY_ICONS
  int day_idx = 0;
#endif
  display.setFont(&FONT_8pt8b);
  for (int i = 0; i < HOURLY_GRAPH_MAX; ++i)
  {
    int xTick = static_cast<int>(xPos0 + (i * xInterval));
    int x0_t, x1_t, y0_t, y1_t;

    if (i > 0)
    {
      // temperature
      x0_t = x_t[i - 1];
      x1_t = x_t[i    ];
      y0_t = y_t[i - 1];
      y1_t = y_t[i    ];
      // graph temperature
      display.drawLine(x0_t    , y0_t    , x1_t    , y1_t    , DM_GFX(ACCENT_COLOR));
      display.drawLine(x0_t    , y0_t + 1, x1_t    , y1_t + 1, DM_GFX(ACCENT_COLOR));
      display.drawLine(x0_t - 1, y0_t    , x1_t - 1, y1_t    , DM_GFX(ACCENT_COLOR));
      if (DARK_MODE)
      { // thicken the line: the red ink is dim against a black ground
        display.drawLine(x0_t    , y0_t - 1, x1_t    , y1_t - 1, DM_GFX(ACCENT_COLOR));
        display.drawLine(x0_t    , y0_t + 2, x1_t    , y1_t + 2, DM_GFX(ACCENT_COLOR));
      }

      // draw hourly bitmap
#if DISPLAY_HOURLY_ICONS
      if (daily[day_idx].dt + 86400 <= hourly[i].dt) {
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
        display.drawInvertedBitmap(xTick - 16, y_b - 32, bitmap, 32, 32,
                                   getHourlyForecastColor32(hourly[i]));
        }
      }
#endif
    }

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
    for (int y = y1_t - 1; y > y0_t; y -= 2)
    {
      for (int x = x0_t + (x0_t % 2); x < x1_t; x += 2)
      {
        display.drawPixel(x, y, DM_GFX(COLOR_PRECIP));
      }
    }

    if ((i % hourInterval) == 0)
    {
      // draw x tick marks
      display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, DM_FG);
      display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, DM_FG);
      // draw x axis labels
      char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
      time_t ts = hourly[i].dt;
      tm *timeInfo = localtime(&ts);
      _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
      drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER);
    }

  }

  // draw the last tick mark
  if ((HOURLY_GRAPH_MAX % hourInterval) == 0)
  {
    int xTick = static_cast<int>(
                std::round(xPos0 + (HOURLY_GRAPH_MAX * xInterval)));
    // draw x tick marks
    display.drawLine(xTick    , yPos1 + 1, xTick    , yPos1 + 4, DM_FG);
    display.drawLine(xTick + 1, yPos1 + 1, xTick + 1, yPos1 + 4, DM_FG);
    // draw x axis labels
    char timeBuffer[12] = {}; // big enough to accommodate "hh:mm:ss am"
    time_t ts = hourly[HOURLY_GRAPH_MAX - 1].dt + 3600;
    tm *timeInfo = localtime(&ts);
    _strftime(timeBuffer, sizeof(timeBuffer), HOUR_FORMAT, timeInfo);
    drawString(xTick, yPos1 + 1 + 12 + 4 + 3, timeBuffer, CENTER);
  }

  return;
} // end drawOutlookGraph

/* This function is responsible for drawing the status bar along the bottom of
 * the display.
 */
void drawStatusBar(const String &statusStr, const String &refreshTimeStr,
                   int rssi, uint32_t batVoltage, int batDaysLeft)
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
#if defined(DISP_3C_B) || defined(DISP_7C_F)
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
  dataColor = DM_FG;
  drawString(pos, DISP_HEIGHT - 1 - 2, refreshTimeStr, RIGHT, dataColor);
  pos -= getStringWidth(refreshTimeStr) + 25;
  display.drawInvertedBitmap(pos, DISP_HEIGHT - 1 - 21, wi_refresh_32x32,
                             32, 32, dataColor);
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
  drawString(DISP_WIDTH / 2, titleY, "Setup Mode", CENTER);

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
               const String &errMsgLn1, const String &errMsgLn2)
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
                             bitmap_196x196, 196, 196, DM_GFX(ACCENT_COLOR));
  return;
} // end drawError

