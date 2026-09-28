/* GDEB0709E01 display driver declarations for esp32-weather-epd.
 *
 * Good Display GDEB0709E01: 7.09in E Ink Spectra 6, 1200x1600, 282 ppi,
 * two controllers behind two chip selects.
 *
 * The register values and the refresh sequence follow Seeed Studio's
 * Driver_GDEB0709E01 (Seeed_GFX2), used under the MIT license:
 *
 *   Copyright (c) 2026 Seeed Studio
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Everything else in this file is part of esp32-weather-epd and is free
 * software under the GNU General Public License, version 3 or later.
 */

#ifndef __EPD709_H__
#define __EPD709_H__

#include <Arduino.h>
#include <GxEPD2.h> // the GxEPD_* colour names the rest of the project uses

/* Why this is not a GxEPD2 panel class:
 *
 * GxEPD2 draws in pages, sending each strip to the controller as it goes.
 * This panel cannot take strips. Its left and right halves belong to two
 * controllers, and each wants its whole half in one transfer with its chip
 * select held low from the first row to the last -- so the complete frame
 * has to exist before anything is sent. At 4 bits per pixel that is 960,000
 * bytes, which lives in PSRAM.
 *
 * The class keeps the handful of GxEPD2 calls the project makes
 * (init / setFullWindow / firstPage / nextPage / hibernate /
 * drawInvertedBitmap), so main.cpp and portal.cpp drive it unchanged:
 * firstPage() clears the frame, the drawing loop runs once, and nextPage()
 * sends the frame, refreshes the panel and returns false.
 *
 * It draws pixels and nothing else. Lines, shapes, text and screens of dots
 * are the renderer's business (renderer709.cpp), built on drawPixel().
 */
class Epd709
{
public:
  static const uint16_t WIDTH  = 1200; // as the controllers count: portrait
  static const uint16_t HEIGHT = 1600;
  static const uint32_t FRAME_BYTES = (WIDTH / 2) * HEIGHT;

  // the panel's own codes for its inks
  enum : uint8_t { INK_BLACK = 0, INK_WHITE = 1, INK_YELLOW = 2, INK_RED = 3,
                   INK_BLUE = 5, INK_GREEN = 6 };

  Epd709(uint8_t csMaster, uint8_t csSlave, uint8_t dc, uint8_t rst,
         uint8_t busy);

  // GxEPD2's surface, as far as this project uses it. init()'s arguments
  // are GxEPD2's and are ignored.
  void init(uint32_t serialDiagBitrate = 0, bool initial = true,
            uint16_t resetDuration = 20, bool pulldownRstMode = false);
  void setFullWindow() {}
  void firstPage();
  bool nextPage();
  void powerOff() {}
  void hibernate();
  void drawInvertedBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                          int16_t w, int16_t h, uint16_t color);

  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void fillScreen(uint16_t color);
  // 0 = portrait as the panel counts, 1 and 3 = landscape, 2 = portrait
  // upside down; the same numbering as Adafruit GFX.
  void setRotation(uint8_t r);
  int16_t width() const { return _width; }
  int16_t height() const { return _height; }

  /* Half tone for whatever is drawn next: with the stipple on, only every
   * other pixel (a checker) is set. This is how grey text is made on a
   * panel whose darkest and lightest inks have nothing in between.
   */
  void setStipple(bool on) { _stipple = on; }

  bool ready() const { return _frame != nullptr; }
  const uint8_t *frame() const { return _frame; }

private:
  uint8_t *_frame = nullptr;
  uint8_t _csM, _csS, _dc, _rst, _busy;
  bool _stipple = false;
  uint8_t _rotation = 0;
  int16_t _width = WIDTH, _height = HEIGHT;
  bool _started = false;

  static uint8_t inkOf(uint16_t color);
  bool waitReady(uint32_t timeoutMs);
  void select(bool master, bool slave);
  void command(bool master, bool slave, uint8_t cmd, const uint8_t *data,
               size_t len);
  void sendFrame();
  void refresh();
};

#endif
