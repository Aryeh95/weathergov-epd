/* GDEB0709E01 display driver for esp32-weather-epd.
 *
 * The register values and the refresh sequence follow Seeed Studio's
 * Driver_GDEB0709E01 (Seeed_GFX2, MIT license, Copyright (c) 2026 Seeed
 * Studio); the full notice is in epd709.h. Everything else in this file is
 * part of esp32-weather-epd and is free software under the GNU General
 * Public License, version 3 or later.
 */

#include "config.h"

#if defined(DISP_7C_709)

#include "epd709.h"

#ifndef EPD709_HOST
#include <SPI.h>
static const SPISettings EPD709_SPI(10000000, MSBFIRST, SPI_MODE0);
#endif

// A full refresh takes about 27 s at 25 C and longer in the cold.
static const uint32_t EPD709_BUSY_TIMEOUT_MS = 60000;

Epd709::Epd709(uint8_t csMaster, uint8_t csSlave, uint8_t dc, uint8_t rst,
               uint8_t busy)
    : _csM(csMaster), _csS(csSlave), _dc(dc), _rst(rst), _busy(busy)
{
}

void Epd709::setRotation(uint8_t r)
{
  _rotation = r & 3;
  _width  = (_rotation & 1) ? HEIGHT : WIDTH;
  _height = (_rotation & 1) ? WIDTH : HEIGHT;
}

uint8_t Epd709::inkOf(uint16_t color)
{
  switch (color)
  {
  case GxEPD_BLACK:  return INK_BLACK;
  case GxEPD_WHITE:  return INK_WHITE;
  case GxEPD_RED:    return INK_RED;
  case GxEPD_YELLOW: return INK_YELLOW;
  case GxEPD_GREEN:  return INK_GREEN;
  case GxEPD_BLUE:   return INK_BLUE;
  default:
    // Anything else is not an ink. The panel does answer to an "orange"
    // code, but it is an uncalibrated state that renders as a blotchy brown
    // (measured on the E1002's Spectra 6), so it is never sent.
    return INK_BLACK;
  }
}

void Epd709::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  if (!_frame || x < 0 || y < 0 || x >= _width || y >= _height)
  {
    return;
  }
  if (_stipple && ((x + y) & 1))
  {
    return;
  }
  int16_t nx, ny; // where that is on the panel as the controllers count it
  switch (_rotation)
  {
  case 1:  nx = WIDTH - 1 - y; ny = x;              break;
  case 2:  nx = WIDTH - 1 - x; ny = HEIGHT - 1 - y; break;
  case 3:  nx = y;             ny = HEIGHT - 1 - x; break;
  default: nx = x;             ny = y;              break;
  }
  uint8_t *p = _frame + static_cast<uint32_t>(ny) * (WIDTH / 2) + (nx >> 1);
  const uint8_t ink = inkOf(color);
  *p = (nx & 1) ? ((*p & 0xF0) | ink) : ((*p & 0x0F) | (ink << 4));
}

void Epd709::fillScreen(uint16_t color)
{
  if (_frame)
  {
    const uint8_t ink = inkOf(color);
    memset(_frame, (ink << 4) | ink, FRAME_BYTES);
  }
}

void Epd709::drawInvertedBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                                int16_t w, int16_t h, uint16_t color)
{
  const int16_t byteWidth = (w + 7) / 8;
  for (int16_t j = 0; j < h; ++j)
  {
    for (int16_t i = 0; i < w; ++i)
    {
      const uint8_t b = pgm_read_byte(&bitmap[j * byteWidth + (i >> 3)]);
      if (!((b >> (7 - (i & 7))) & 1))
      { // inverted: a cleared bit is ink
        drawPixel(x + i, y + j, color);
      }
    }
  }
}

void Epd709::firstPage()
{
  fillScreen(GxEPD_WHITE);
}

#ifdef EPD709_HOST
// ---- on a PC there is no panel: the frame is all there is
void Epd709::init(uint32_t, bool, uint16_t, bool)
{
  if (!_frame)
  {
    _frame = static_cast<uint8_t *>(malloc(FRAME_BYTES));
  }
  fillScreen(GxEPD_WHITE);
}
bool Epd709::nextPage() { return false; }
void Epd709::hibernate() {}

#else
// ---- the panel

bool Epd709::waitReady(uint32_t timeoutMs)
{
  // The controller needs a moment to pull BUSY low after a command; sampled
  // too early, the idle-high level looks like "already done".
  delay(10);
  const uint32_t start = millis();
  while (digitalRead(_busy) == LOW) // HIGH = ready
  {
    if (millis() - start > timeoutMs)
    {
      Serial.println("[epd709] busy timeout");
      return false;
    }
    delay(5);
  }
  return true;
}

void Epd709::select(bool master, bool slave)
{
  digitalWrite(_csM, master ? LOW : HIGH);
  digitalWrite(_csS, slave ? LOW : HIGH);
}

void Epd709::command(bool master, bool slave, uint8_t cmd,
                     const uint8_t *data, size_t len)
{
  SPI.beginTransaction(EPD709_SPI);
  select(master, slave);
  digitalWrite(_dc, LOW);
  SPI.transfer(cmd);
  digitalWrite(_dc, HIGH);
  if (data && len)
  {
    SPI.writeBytes(data, len);
  }
  select(false, false);
  SPI.endTransaction();
}

void Epd709::init(uint32_t, bool, uint16_t, bool)
{
  if (!_frame)
  {
    _frame = static_cast<uint8_t *>(ps_malloc(FRAME_BYTES));
    if (!_frame)
    {
      Serial.println("[epd709] no PSRAM for the frame (960000 bytes); "
                     "is BOARD_HAS_PSRAM set and the memory type qio_opi?");
      return;
    }
  }
  fillScreen(GxEPD_WHITE);

  pinMode(_csM, OUTPUT);
  pinMode(_csS, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT_PULLUP);
  select(false, false);
  digitalWrite(_dc, HIGH);
  SPI.end();
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1); // write-only, chip selects by hand

  digitalWrite(_rst, LOW);
  delay(20);
  digitalWrite(_rst, HIGH);
  delay(20);
  waitReady(EPD709_BUSY_TIMEOUT_MS);

  // Seeed's value set, which is the 13.3in T133A01's byte for byte and the
  // one verified on Seeed's boards with this panel. Good Display's own
  // example differs in four places, measured only on their 5 V carrier
  // board; if the panel stays blank they are the first thing to try:
  //   0x74 AN_TM  C0 1E 1E CE CE CE 15 15 55
  //   0x50 CDI    F7
  //   0x06 / 0x05 BTST  E8 28
  //   0xA5        not sent at all
  static const uint8_t anTm[] = {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15,
                                 0x15, 0x55};
  static const uint8_t rF0[]  = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
  static const uint8_t psr[]  = {0xDF, 0x69};
  static const uint8_t dcdc[] = {0x44, 0x54, 0x00};
  static const uint8_t cdi[]  = {0x37};
  static const uint8_t tcon[] = {0x03, 0x03};
  static const uint8_t r86[]  = {0x10};
  static const uint8_t pws[]  = {0x22};
  static const uint8_t tres[] = {0x04, 0xB0, 0x03, 0x20};
  static const uint8_t pwr[]  = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
  static const uint8_t rB6[]  = {0x07};
  static const uint8_t btst[] = {0xE0, 0x20};
  static const uint8_t rB7[]  = {0x01};
  static const uint8_t rB0[]  = {0x01};
  static const uint8_t rB1[]  = {0x02};
  struct Step { bool both; uint8_t cmd; const uint8_t *data; size_t len; };
  const Step steps[] = {
    {false, 0x74, anTm, sizeof(anTm)}, {true,  0xF0, rF0,  sizeof(rF0)},
    {true,  0x00, psr,  sizeof(psr)},  {false, 0xA5, dcdc, sizeof(dcdc)},
    {true,  0x50, cdi,  sizeof(cdi)},  {true,  0x60, tcon, sizeof(tcon)},
    {true,  0x86, r86,  sizeof(r86)},  {true,  0xE3, pws,  sizeof(pws)},
    {true,  0x61, tres, sizeof(tres)}, {false, 0x01, pwr,  sizeof(pwr)},
    {false, 0xB6, rB6,  sizeof(rB6)},  {false, 0x06, btst, sizeof(btst)},
    {false, 0xB7, rB7,  sizeof(rB7)},  {false, 0x05, btst, sizeof(btst)},
    {false, 0xB0, rB0,  sizeof(rB0)},  {false, 0xB1, rB1,  sizeof(rB1)},
  };
  for (const Step &s : steps)
  {
    command(true, s.both, s.cmd, s.data, s.len);
    delay(10);
  }
  _started = true;
}

void Epd709::sendFrame()
{
  // CCSET picks the waveform group stored in the controllers and has to go
  // out before every frame, not once at start-up.
  static const uint8_t ccset = 0x01;
  command(true, true, 0xE0, &ccset, 1);
  waitReady(EPD709_BUSY_TIMEOUT_MS);

  // Master takes the left 600 columns, slave the right 600. Each gets its
  // half row by row, chip select low for the whole of it.
  const uint32_t half = WIDTH / 4; // bytes of one half row
  for (uint8_t chip = 0; chip < 2; ++chip)
  {
    SPI.beginTransaction(EPD709_SPI);
    select(chip == 0, chip == 1);
    digitalWrite(_dc, LOW);
    SPI.transfer(0x10);
    digitalWrite(_dc, HIGH);
    for (uint32_t row = 0; row < HEIGHT; ++row)
    {
      SPI.writeBytes(_frame + row * (WIDTH / 2) + chip * half, half);
    }
    select(false, false);
    SPI.endTransaction();
  }
}

void Epd709::refresh()
{
  static const uint8_t drf = 0x01;
  static const uint8_t pof = 0x00;
  command(true, true, 0x04, nullptr, 0); // power on
  waitReady(EPD709_BUSY_TIMEOUT_MS);
  delay(30);
  const uint32_t start = millis();
  command(true, true, 0x12, &drf, 1);    // refresh
  waitReady(EPD709_BUSY_TIMEOUT_MS);
  Serial.println("[epd709] refresh took "
                 + String((millis() - start) / 1000.0, 1) + " s");
  delay(30);
  command(true, true, 0x02, &pof, 1);    // power off
  waitReady(EPD709_BUSY_TIMEOUT_MS);
  delay(30);
}

bool Epd709::nextPage()
{
  if (_frame && _started)
  {
    sendFrame();
    refresh();
  }
  return false;
}

/* Called once the refresh has finished, just before the panel's supply is
 * switched off. The lines to it are let go so that nothing is driven into
 * an unpowered controller through its protection diodes.
 */
void Epd709::hibernate()
{
  SPI.end();
  pinMode(_csM, INPUT);
  pinMode(_csS, INPUT);
  pinMode(_dc, INPUT);
  pinMode(_rst, INPUT);
  pinMode(PIN_EPD_SCK, INPUT);
  pinMode(PIN_EPD_MOSI, INPUT);
  _started = false;
}
#endif // EPD709_HOST

#endif // DISP_7C_709
