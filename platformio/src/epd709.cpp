/* GDEB0709E01 display driver for esp32-weather-epd.
 *
 * The register values and the refresh sequence are Good Display's, from
 * their GDEB0709E01 demo (2026); the frame layout and the two-controller
 * structure follow Seeed Studio's Driver_GDEB0709E01 (Seeed_GFX2, MIT
 * license, Copyright (c) 2026 Seeed Studio); the full notice is in
 * epd709.h. Everything else in this file is part of esp32-weather-epd and
 * is free software under the GNU General Public License, version 3 or
 * later.
 */

#include "config.h"

#if defined(DISP_7C_709)

#include "epd709.h"

#ifndef EPD709_HOST
#include <SPI.h>
// 8 MHz: the clock bb_epaper (the OpenDisplay firmware's driver) uses for
// this controller pair, and the fastest any driver has been seen to run it.
// An ESPHome driver for the EE02 found only 2 MHz reliable, so if the test
// card shows noise or a blank half, lower this before touching the init
// bytes. A 960 KB frame takes ~1 s at 8 MHz, ~4 s at 2 MHz.
static const SPISettings EPD709_SPI(8000000, MSBFIRST, SPI_MODE0);
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

  // Good Display's own GDEB0709E01 demo (2026), register for register and
  // in its order. Seeed's set for the 13.3in T133A01 (the same controller
  // pair) drove this panel on 2026-10-14 but with dull inks: a grey-green
  // white and a brown black. The two differ in PSR (6B, not 69), the PLL,
  // the power-off sequences (0x03, one per controller), register 0xA4, the
  // refresh mode byte (DRF 00, not 01) and the power-off byte (POF 01,
  // not 00); EPD709_SEEED_VALUES brings Seeed's set back for comparison.
  static const uint8_t anTm[]  = {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15,
                                  0x15, 0x55};
  static const uint8_t rF0[]   = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
#ifdef EPD709_SEEED_VALUES
  static const uint8_t psr[]   = {0xDF, 0x69};
#else
  static const uint8_t psr[]   = {0xDF, 0x6B};
#endif
  static const uint8_t dcdc[]  = {0x44, 0x54, 0x00};
  static const uint8_t pll[]   = {0x08};
  static const uint8_t cdi[]   = {0x37};
  static const uint8_t tcon[]  = {0x03, 0x03};
  static const uint8_t pofsM[] = {0x00, 0xC0, 0x03, 0xA8};
  static const uint8_t pofsS[] = {0x00, 0xC0, 0x03, 0x9A};
  static const uint8_t r86[]   = {0x10};
  static const uint8_t pws[]   = {0x22};
  static const uint8_t ccset[] = {0x01};
  static const uint8_t tres[]  = {0x04, 0xB0, 0x03, 0x20};
  static const uint8_t rA4[]   = {0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00,
                                  0x00, 0x00};
  static const uint8_t pwr[]   = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
  static const uint8_t rB6[]   = {0x07};
  static const uint8_t btst[]  = {0xE0, 0x20};
  static const uint8_t rB7[]   = {0x01};
  static const uint8_t rB0[]   = {0x01};
  static const uint8_t rB1[]   = {0x02};
  // M = master only, S = slave only, B = both controllers
  enum : uint8_t { M, S, B };
  struct Step { uint8_t to; uint8_t cmd; const uint8_t *data; size_t len; };
  const Step steps[] = {
    {M, 0x74, anTm,  sizeof(anTm)},  {B, 0xF0, rF0,   sizeof(rF0)},
    {B, 0x00, psr,   sizeof(psr)},   {M, 0xA5, dcdc,  sizeof(dcdc)},
#ifndef EPD709_SEEED_VALUES
    {B, 0x30, pll,   sizeof(pll)},
#endif
    {B, 0x50, cdi,   sizeof(cdi)},   {B, 0x60, tcon,  sizeof(tcon)},
#ifndef EPD709_SEEED_VALUES
    {M, 0x03, pofsM, sizeof(pofsM)}, {S, 0x03, pofsS, sizeof(pofsS)},
#endif
    {B, 0x86, r86,   sizeof(r86)},   {B, 0xE3, pws,   sizeof(pws)},
    {B, 0xE0, ccset, sizeof(ccset)}, {B, 0x61, tres,  sizeof(tres)},
#ifndef EPD709_SEEED_VALUES
    {M, 0xA4, rA4,   sizeof(rA4)},
#endif
    {M, 0x01, pwr,   sizeof(pwr)},   {M, 0xB6, rB6,   sizeof(rB6)},
    {M, 0x06, btst,  sizeof(btst)},  {M, 0xB7, rB7,   sizeof(rB7)},
    {M, 0x05, btst,  sizeof(btst)},  {M, 0xB0, rB0,   sizeof(rB0)},
    {M, 0xB1, rB1,   sizeof(rB1)},
  };
  for (const Step &s : steps)
  {
    command(s.to != S, s.to != M, s.cmd, s.data, s.len);
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
#ifdef EPD709_SEEED_VALUES
  static const uint8_t drf = 0x01;
  static const uint8_t pof = 0x00;
#else
  static const uint8_t drf = 0x00;
  static const uint8_t pof = 0x01;
#endif
  command(true, true, 0x04, nullptr, 0); // power on
  waitReady(EPD709_BUSY_TIMEOUT_MS);
  delay(50);
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
#if defined(EPD709_TRIAL) && EPD709_TRIAL == 1
    // trial: a second pass over the same frame, from a settled state. (A
    // second trial, telling the controller it was at 15 C through the
    // UC81xx TSE/TSSET commands 0x41/0xE5 so that it would pick a harder
    // waveform, was tried on 2026-10-14: the refresh never started and BUSY
    // never cleared -- those commands do not mean that on this controller.)
    Serial.println("[epd709] trial: second refresh");
    refresh();
#endif
  }
  return false;
}

/* Called once the refresh has finished, just before the panel's supply is
 * switched off. The lines to it are let go so that nothing is driven into
 * an unpowered controller through its protection diodes.
 */
void Epd709::hibernate()
{
  if (_started)
  { // the controllers' deep sleep before the supply goes. Good Display's
    // demo sends the 0xA5 check code after 0x02 (power off), which this
    // controller family takes with 0x07 (DSLP, as GxEPD2's GDEP073E01
    // driver and the UC81xx data sheets have it); power is already off
    // from refresh(), so the deep sleep is what is meant here.
    static const uint8_t dslp = 0xA5;
    command(true, true, 0x07, &dslp, 1);
    delay(20);
  }
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
