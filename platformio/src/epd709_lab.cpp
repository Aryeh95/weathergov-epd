/* Bench firmware for the GDEB0709E01 (-D EPD709_LAB): pixel codes, partial
 * windows and cut-short refreshes, driven by commands over USB serial.
 *
 * Coordinates are the controllers' (portrait, 1200 x 1600): x 0..599 is the
 * master's half (the bottom half in landscape), 600..1199 the slave's. Only
 * the master's half is used for experiments; the slave's gets white.
 *
 * As each controller counts it, its half is 1200 x 800: one controller row
 * is two of our rows side by side. A partial window here always spans the
 * full width of a half and an even run of our rows.
 *
 * Experimental: cutting a refresh short leaves the pixels' drive unbalanced.
 * For a panel that is already written off.
 *
 * This file is part of esp32-weather-epd, GPL-3.0-or-later.
 */

#include "config.h"

#if defined(DISP_7C_709) && defined(EPD709_LAB) && !defined(EPD709_HOST)

#include <SPI.h>
#include "epd709.h"
#include "epd709_lut.h"
#include <Wire.h>

extern Epd709 display;

static uint8_t s_pll = 0x08;
static uint8_t s_drf = 0x00;
static uint8_t s_ccset = 0x01;
// clock for the second (cut) passes of pcard / ppass; 0 = the same as pll
static uint8_t s_ppll = 0;
// power-off data byte (refresh ends with 0x02 and this): 00. 01 left VGH
// at 28.8 V for 1 s and then floating (epd709.cpp)
static uint8_t s_pof = 0x00;
/* A register waveform table (LUT), after "E-paper LUT research" by Jonas
 * Niesner (https://github.com/jonasniesner/epaper-lut-research, docs CC BY
 * 4.0): 800 B written to 0x20 on both controllers, used when PSR byte 2
 * bit 7 is set; the rails then come from PWR (0x01, master only). Volatile:
 * a reset brings the stored (OTP) waveform back. */
static uint8_t s_lut[800];
static bool s_lutOn = false;
static uint8_t s_pwr[6] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static uint8_t s_cdi = 0x37;
// counter codes: white to blue, blue to white, unchanged
static uint8_t s_wb = 1, s_bw = 2, s_hold = 4;

static const uint32_t HALF_BYTES = Epd709::WIDTH / 4; // one half row

static void setPx(uint8_t *f, int x, int y, uint8_t c)
{
  uint8_t *p = f + y * (Epd709::WIDTH / 2) + (x >> 1);
  *p = (x & 1) ? ((*p & 0xF0) | c) : ((*p & 0x0F) | (c << 4));
}

static void fillRect(uint8_t *f, int x0, int y0, int w, int h, uint8_t c)
{
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x)
      setPx(f, x, y, c);
}

// Seven-segment digit d (or blank for d < 0) in landscape coordinates
// (EPD709_ROTATION 1): is (X, Y) on a lit segment of a digit at (x0, y0)?
static bool segOn(int d, int x0, int y0, int X, int Y)
{
  static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                  0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // gfedcba
  if (d < 0 || d > 9) return false;
  const int w = 260, h = 460, t = 50;
  X -= x0; Y -= y0;
  if (X < 0 || Y < 0 || X >= w || Y >= h) return false;
  const uint8_t s = SEG[d];
  const bool top = Y < h / 2;
  if ((s & 0x01) && Y < t) return true;                              // a
  if ((s & 0x02) && X >= w - t && top) return true;                  // b
  if ((s & 0x04) && X >= w - t && !top) return true;                 // c
  if ((s & 0x08) && Y >= h - t) return true;                         // d
  if ((s & 0x10) && X < t && !top) return true;                      // e
  if ((s & 0x20) && X < t && top) return true;                       // f
  if ((s & 0x40) && Y >= h / 2 - t / 2 && Y < h / 2 + t / 2) return true; // g
  return false;
}

// one column of digit d (column xr of 260) as 460 bytes, 1 = lit: the same
// shapes as segOn, built from ranges instead of pixel by pixel
static void segCol(int d, int xr, uint8_t *m)
{
  static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                  0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  const int w = 260, h = 460, t = 50;
  memset(m, 0, h);
  if (d < 0 || d > 9) return;
  const uint8_t s = SEG[d];
  if (s & 0x01) memset(m, 1, t);                     // a
  if (s & 0x40) memset(m + h / 2 - t / 2, 1, t);     // g
  if (s & 0x08) memset(m + h - t, 1, t);             // d
  if (xr < t)
  {
    if (s & 0x20) memset(m, 1, h / 2);               // f
    if (s & 0x10) memset(m + h / 2, 1, h / 2);       // e
  }
  if (xr >= w - t)
  {
    if (s & 0x02) memset(m, 1, h / 2);               // b
    if (s & 0x04) memset(m + h / 2, 1, h / 2);       // c
  }
}

// a two-digit number (or blank for n < 0) lit at landscape (X, Y)
static bool numOn(int n, int X, int Y)
{
  if (n < 0) return false;
  return segOn(n / 10, 520, 670, X, Y) || segOn(n % 10, 820, 670, X, Y);
}

void epd709Lab()
{
  Epd709 &d = display;
  uint8_t *f = d._frame;

  auto cmd = [&](bool m, bool s, uint8_t c, const uint8_t *data, size_t n) {
    d.command(m, s, c, data, n);
  };
  auto cmd1 = [&](bool m, bool s, uint8_t c, uint8_t v) { cmd(m, s, c, &v, 1); };

  auto reconfigure = [&]() {
    d.resetAndConfigure();
    cmd1(true, true, 0x30, s_pll);
    cmd1(true, true, 0x50, s_cdi);
  };

  // all of the frame to both controllers
  // the uploaded table, when switched on: PSR with the select bit, its
  // rails, the table to both controllers (no TSC read may follow: it
  // reloads 0x20 from OTP)
  auto applyLut = [&]() {
    if (!s_lutOn) return;
    const uint8_t psr[] = {0xDF, 0xEB}; // the driver's DF 6B + bit 7
    cmd(true, true, 0x00, psr, 2);
    cmd(true, false, 0x01, s_pwr, 6);
    cmd(true, true, 0x20, s_lut, 800);
  };

  auto sendAll = [&]() {
    applyLut();
    cmd1(true, true, 0xE0, s_ccset);
    d.waitReady(60000);
    for (uint8_t chip = 0; chip < 2; ++chip)
    {
      SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
      d.select(chip == 0, chip == 1);
      digitalWrite(d._dc, LOW);
      SPI.transfer(0x10);
      digitalWrite(d._dc, HIGH);
      for (uint32_t row = 0; row < Epd709::HEIGHT; ++row)
        SPI.writeBytes(f + row * (Epd709::WIDTH / 2) + chip * HALF_BYTES,
                       HALF_BYTES);
      d.select(false, false);
      SPI.endTransaction();
    }
  };

  // rows y0..y1 (y0 even, y1 odd) as a partial window on both controllers
  auto sendWindow = [&](int y0, int y1) {
    cmd1(true, true, 0xE0, s_ccset);
    d.waitReady(60000);
    const uint16_t v0 = y0 / 2, v1 = y1 / 2, he = 1199;
    const uint8_t ptl[] = {0, 0, uint8_t(he >> 8), uint8_t(he & 0xFF),
                           uint8_t(v0 >> 8), uint8_t(v0 & 0xFF),
                           uint8_t(v1 >> 8), uint8_t(v1 & 0xFF), 0x01};
    // PTLW as OSPTEK's EPD0709A02 example sends it (CC BY 4.0, OSPTEK):
    // HRST = 2 x, HRED = 2 (x + w) - 1, VRST = y / 2, VRED = (y + h) / 2 - 1,
    // HRST a multiple of 8, at least 32 wide; no partial-in/out commands.
    cmd(true, true, 0x83, ptl, sizeof(ptl));
    for (uint8_t chip = 0; chip < 2; ++chip)
    {
      SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
      d.select(chip == 0, chip == 1);
      digitalWrite(d._dc, LOW);
      SPI.transfer(0x10);
      digitalWrite(d._dc, HIGH);
      for (int row = y0; row <= y1; ++row)
        SPI.writeBytes(f + row * (Epd709::WIDTH / 2) + chip * HALF_BYTES,
                       HALF_BYTES);
      d.select(false, false);
      SPI.endTransaction();
    }
  };

  // refresh; cutMs > 0 ends it with a hardware reset that long after BUSY
  // goes active. Returns the time BUSY was active, in ms.
  auto refresh = [&](uint32_t cutMs, bool partial) -> uint32_t {
    cmd(true, true, 0x04, nullptr, 0);
    d.waitReady(60000);
    delay(50);
    cmd1(true, true, 0x12, s_drf);
    uint32_t t = millis();
    while (digitalRead(d._busy) == HIGH && millis() - t < 500) {}
    const uint32_t active = millis();
    if (cutMs)
    {
      while (millis() - active < cutMs) {}
      digitalWrite(d._rst, LOW);
      const uint32_t took = millis() - active;
      delay(10);
      reconfigure();
      return took;
    }
    d.waitReady(60000);
    const uint32_t took = millis() - active;
    delay(30);
    cmd1(true, true, 0x02, s_pof);
    d.waitReady(60000);
    if (partial)
    { // window off again (PTLW with the enable byte 0)
      static const uint8_t off[9] = {0};
      cmd(true, true, 0x83, off, sizeof(off));
    }
    return took;
  };

  auto white = [&]() { memset(f, 0x11, Epd709::FRAME_BYTES); };

  // the 16 codes in a 4 x 4 grid of 150 x 400 patches over the master's half
  auto card = [&]() {
    white();
    for (int k = 0; k < 16; ++k)
      fillRect(f, (k % 4) * 150 + 10, (k / 4) * 400 + 10, 130, 380, k);
  };

  Serial.println("\n[lab] GDEB0709E01 bench. Commands:\n"
                 "  fill C            whole panel code C (hex), full refresh\n"
                 "  card              codes 0-F, 4x4 on the master half, full refresh\n"
                 "  win Y0 Y1 C MS N  rows Y0..Y1 of the master half to code C, partial,\n"
                 "                    cut MS ms after BUSY (0 = whole waveform), N times\n"
                 "  bars Y0 Y1 MS N   the same with 16 stripes of codes 0-F\n"
                 "  count FROM TO [MS] [N]  blue digits on white, one cut pulse each,\n"
                 "                    a full refresh every N steps\n"
                 "  codes WB BW HOLD  the counter's codes (default 1 2 4)\n"
                 "  pll HH | drf HH | ccset HH | cdi HH   set and keep\n"
                 "  ppll HH           clock of pcard's cut passes (00 = pll's)\n"
                 "  img [lab]         a frame over USB; lab = at pll's clock\n"
                 "  pof HH | pwr B0..B5 | lut | lutmode 0/1   register waveform table\n"
                 "  g13 [SET] | f3c [SET]  build & arm on-device fast709 LUT from 0x93\n"
                 "  rd CMD N m/s [busy]  read N bytes after CMD (hex dump)\n"
                 "  reset             hardware reset + register table");
  reconfigure();

  String line;
  for (;;)
  {
    if (!Serial.available())
    {
      delay(5);
      continue;
    }
    const char ch = Serial.read();
    if (ch != '\n' && ch != '\r')
    {
      line += ch;
      continue;
    }
    line.trim();
    if (!line.length())
      continue;
    Serial.println("[lab] > " + line);
    String tok[8];
    int nt = 0;
    for (int p = 0; p < (int)line.length() && nt < 8;)
    {
      int q = line.indexOf(' ', p);
      if (q < 0) q = line.length();
      if (q > p) tok[nt++] = line.substring(p, q);
      p = q + 1;
    }
    line = "";
    const String &o = tok[0];
    auto hex = [&](int i) { return (unsigned)strtoul(tok[i].c_str(), nullptr, 16); };
    auto dec = [&](int i) { return (unsigned)strtoul(tok[i].c_str(), nullptr, 10); };

    if (o == "fill" && nt >= 2)
    {
      const uint8_t k = hex(1) & 15;
      memset(f, (k << 4) | k, Epd709::FRAME_BYTES);
      sendAll();
      Serial.printf("[lab] fill %X: %u ms\n", k, refresh(0, false));
    }
    else if (o == "card")
    {
      card();
      sendAll();
      Serial.printf("[lab] card: %u ms\n", refresh(0, false));
    }
    else if ((o == "win" && nt >= 4) || (o == "bars" && nt >= 3))
    {
      const bool bars = o == "bars";
      int y0 = dec(1) & ~1, y1 = dec(2) | 1;
      if (y1 >= Epd709::HEIGHT) y1 = Epd709::HEIGHT - 1;
      const uint8_t k = bars ? 0 : (hex(3) & 15);
      const int a = bars ? 3 : 4;
      const uint32_t ms = nt > a ? dec(a) : 0;
      const unsigned reps = nt > a + 1 ? dec(a + 1) : 1;
      // the window's rows: the master's part as asked, the slave's white
      for (int y = y0; y <= y1; ++y)
      {
        memset(f + y * (Epd709::WIDTH / 2) + HALF_BYTES, 0x11, HALF_BYTES);
        if (bars)
          for (int x = 0; x < 600; ++x) setPx(f, x, y, (x * 16) / 600);
        else
          memset(f + y * (Epd709::WIDTH / 2), (k << 4) | k, HALF_BYTES);
      }
      for (unsigned r = 0; r < reps; ++r)
      {
        sendWindow(y0, y1);
        const uint32_t took = refresh(ms, true);
        Serial.printf("[lab] %s rows %d-%d %s#%u: busy %u ms\n", o.c_str(),
                      y0, y1, ms ? "cut " : "", r + 1, took);
      }
    }
    else if (o == "count" && nt >= 3)
    { // count FROM TO [MS]: blue digits on white, one cut pulse per step,
      // the master controller only. Assumes the digit area starts white.
      const int from = dec(1), to = dec(2);
      const uint32_t ms = nt > 3 ? dec(3) : 100;
      const int clean = nt > 4 ? dec(4) : 0; // full refresh every N steps
      for (int v = from; v <= to; ++v)
      {
        const uint32_t t0 = millis();
        const int old = v == from ? -1 : v - 1;
        // only the digits that change (landscape X = raw rows): the ones at
        // X 820..1079, the tens at 520..779
        const bool tens = old < 0 || old / 10 != v / 10;
        const int y0 = tens ? 520 : 820, y1 = 1079;
        // each row (landscape X) is built in internal RAM and copied once;
        // only the boxes of the changing digits are looked at
        const int dx[2] = {520, 820};
        const int dOld[2] = {old < 0 ? -1 : old / 10, old < 0 ? -1 : old % 10};
        const int dNew[2] = {v / 10, v % 10};
        uint8_t rowBuf[HALF_BYTES];
        for (int X = y0; X <= y1; ++X)
        {
          memset(rowBuf, s_hold * 0x11, HALF_BYTES);
          for (int p = 0; p < 2; ++p)
          {
            if (X < dx[p] || X >= dx[p] + 260 || dOld[p] == dNew[p]) continue;
            uint8_t mOld[460], mNew[460];
            segCol(dOld[p], X - dx[p], mOld);
            segCol(dNew[p], X - dx[p], mNew);
            for (int j = 0; j < 460; ++j)
            {
              const bool was = mOld[j], now = mNew[j];
              if (was == now) continue;
              const int rx = 1199 - (670 + j);
              const uint8_t c = now ? s_wb : s_bw;
              uint8_t &b = rowBuf[rx >> 1];
              b = (rx & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
            }
          }
          memcpy(f + X * (Epd709::WIDTH / 2), rowBuf, HALF_BYTES);
        }
        const uint32_t tDraw = millis() - t0;
        cmd1(true, false, 0xE0, s_ccset);
        d.waitReady(60000);
        const uint16_t v0 = y0 / 2, v1 = y1 / 2, he = 1199;
        const uint8_t ptl[] = {0, 0, uint8_t(he >> 8), uint8_t(he & 0xFF),
                               uint8_t(v0 >> 8), uint8_t(v0 & 0xFF),
                               uint8_t(v1 >> 8), uint8_t(v1 & 0xFF), 0x01};
        cmd(true, false, 0x83, ptl, sizeof(ptl));
        SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
        d.select(true, false);
        digitalWrite(d._dc, LOW);
        SPI.transfer(0x10);
        digitalWrite(d._dc, HIGH);
        for (int row = y0; row <= y1; ++row)
          SPI.writeBytes(f + row * (Epd709::WIDTH / 2), HALF_BYTES);
        d.select(false, false);
        SPI.endTransaction();
        const uint32_t t1 = millis();
        cmd(true, false, 0x04, nullptr, 0);
        d.waitReady(60000);
        delay(50);
        cmd1(true, false, 0x12, s_drf);
        const uint32_t t2 = millis();
        while (digitalRead(d._busy) == HIGH && millis() - t2 < 500) {}
        const uint32_t act = millis();
        while (millis() - act < ms) {}
        digitalWrite(d._rst, LOW);
        const uint32_t t3 = millis();
        delay(10);
        reconfigure();
        Serial.printf("[lab] count %d: draw %u, send %u, power-on %u, pulse %u, "
                      "reset %u, total %u ms\n", v, tDraw, t1 - t0 - tDraw, act - t1,
                      ms, millis() - t3, millis() - t0);
        if (clean && (v - from + 1) % clean == 0 && v < to)
        { // a full refresh of the same number with the real inks (blue 5)
          white();
          for (int X = 500; X <= 1099; ++X)
            for (int Y = 600; Y < 1200; ++Y)
              if (numOn(v, X, Y)) setPx(f, 1199 - Y, X, 5);
          sendAll();
          Serial.printf("[lab] clean at %d: %u ms\n", v, refresh(0, false));
        }
      }
      Serial.println("[lab] count done");
    }
    else if (o == "cols")
    { // first pass: six columns of the inks 0 1 2 3 5 6 across the master
      // half (raw x 100 each), full refresh
      static const uint8_t INK[6] = {0, 1, 2, 3, 5, 6};
      white();
      for (int j = 0; j < 6; ++j) fillRect(f, j * 100, 0, 100, Epd709::HEIGHT, INK[j]);
      sendAll();
      Serial.printf("[lab] cols: %u ms\n", refresh(0, false));
    }
    else if (o == "pcard")
    { // two-pass test card: 20 patches (4 x 5, 150 x 320 raw each). A full
      // refresh lays down each patch's base ink, then one cut pass per time
      // (22, 3, 0.3, 0.1 s) on the master only; a patch changes in its own
      // pass and holds (code 4) in the others.
      struct P { uint8_t base; uint16_t ms; uint8_t code; };
      static const P PATCH[20] = {
        {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {5, 0, 0}, {6, 0, 0},
        {2, 100, 1}, {3, 100, 1}, {5, 100, 1}, {6, 100, 1},
        {1, 22000, 6}, {1, 22000, 3}, {6, 300, 2},
        {3, 300, 2}, {0, 300, 2}, {2, 300, 5}, {3, 300, 5},
        {1, 3000, 5}, {0, 3000, 5}, {1, 3000, 1}};
      // "pcard g": a green card -- the green ink taken lighter by very short
      // passes (20-90 ms) of the white-going codes 2, 3 and 0, beside white,
      // green, yellow and the 300 ms lime for reference (100-250 ms all
      // give the lime already)
      static const P GREEN[20] = {
        {1, 0, 0}, {6, 0, 0}, {2, 0, 0}, {6, 300, 2},
        {6, 20, 2}, {6, 30, 2}, {6, 40, 2}, {6, 50, 2},
        {6, 60, 2}, {6, 70, 2}, {6, 80, 2}, {6, 90, 2},
        {6, 20, 0}, {6, 40, 0}, {6, 60, 0}, {6, 80, 0},
        {6, 30, 3}, {6, 50, 3}, {6, 70, 3}, {6, 90, 3}};
      const bool green = nt >= 2 && tok[1] == "g";
      const P *TAB = green ? GREEN : PATCH;
      auto patchRect = [&](int k, uint8_t c) {
        fillRect(f, (k % 4) * 150 + 10, (k / 4) * 320 + 10, 130, 300, c);
      };
      white();
      for (int k = 0; k < 20; ++k) patchRect(k, TAB[k].base);
      sendAll();
      Serial.printf("[lab] pcard base: %u ms\n", refresh(0, false));
      static const uint16_t PASS[4] = {22000, 3000, 300, 100}; // long first
      static const uint16_t GPASS[9] = {300, 90, 80, 70, 60, 50, 40, 30, 20};
      const uint16_t *passes = green ? GPASS : PASS;
      const int npass = green ? 9 : 4;
      // a reset before the first pass: straight after a full refresh a cut
      // pass misbehaves (2026-11-22: green + code 2 went dark slate)
      reconfigure();
      for (int pi = 0; pi < npass; ++pi)
      {
        const uint16_t pms = passes[pi];
        if (s_ppll) cmd1(true, true, 0x30, s_ppll); // reconfigure set pll's
        // master half: hold everywhere, the pass's patches get their code
        for (int y = 0; y < Epd709::HEIGHT; ++y)
          memset(f + y * (Epd709::WIDTH / 2), 0x44, HALF_BYTES);
        for (int k = 0; k < 20; ++k)
          if (TAB[k].ms == pms) patchRect(k, TAB[k].code);
        cmd1(true, false, 0xE0, s_ccset);
        d.waitReady(60000);
        static const uint8_t ptl[] = {0, 0, 0x04, 0xAF, 0, 0, 0x03, 0x1F, 0x01};
        cmd(true, false, 0x83, ptl, sizeof(ptl)); // the whole of the half
        SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        d.select(true, false);
        digitalWrite(d._dc, LOW);
        SPI.transfer(0x10);
        digitalWrite(d._dc, HIGH);
        for (int row = 0; row < Epd709::HEIGHT; ++row)
          SPI.writeBytes(f + row * (Epd709::WIDTH / 2), HALF_BYTES);
        d.select(false, false);
        SPI.endTransaction();
        cmd(true, false, 0x04, nullptr, 0);
        d.waitReady(60000);
        delay(50);
        cmd1(true, false, 0x12, 0x01);
        const uint32_t t = millis();
        while (digitalRead(d._busy) == HIGH && millis() - t < 500) {}
        const uint32_t act = millis();
        while (millis() - act < pms) {}
        digitalWrite(d._rst, LOW);
        delay(10);
        reconfigure();
        Serial.printf("[lab] pcard pass %u ms done\n", pms);
      }
      Serial.println("[lab] pcard done");
    }
    else if (o == "codes" && nt >= 4)
    {
      s_wb = hex(1) & 15; s_bw = hex(2) & 15; s_hold = hex(3) & 15;
      Serial.printf("[lab] codes WB %X BW %X hold %X\n", s_wb, s_bw, s_hold);
    }
    else if (o == "pof" && nt >= 2) { s_pof = hex(1); Serial.printf("[lab] pof %02X\n", s_pof); }
    else if (o == "pwr" && nt >= 7)
    {
      for (int i = 0; i < 6; ++i) s_pwr[i] = hex(1 + i);
      Serial.printf("[lab] pwr %02X %02X %02X %02X %02X %02X\n", s_pwr[0], s_pwr[1],
                    s_pwr[2], s_pwr[3], s_pwr[4], s_pwr[5]);
    }
    else if (o == "lutmode" && nt >= 2)
    {
      s_lutOn = tok[1] == "1";
      reconfigure(); // the stored waveform's registers; sendAll adds the table
      Serial.printf("[lab] lutmode %d\n", s_lutOn ? 1 : 0);
    }
    else if (o == "lut")
    { // 800 bytes over USB, answered with '#'
      Serial.setRxBufferSize(2048); // default 256 can drop bursts
      Serial.println("[lab] lut ready");
      Serial.setTimeout(5000);
      const size_t got = Serial.readBytes(s_lut, sizeof(s_lut));
      Serial.write('#');
      uint32_t sum = 0;
      for (uint8_t b : s_lut) sum += b;
      Serial.printf("[lab] lut got %u bytes, sum %u\n", (unsigned)got, (unsigned)sum);
      if (got != sizeof(s_lut)) s_lutOn = false;
    }
    else if (o == "g13" || o == "f3c")
    {
      const bool isG13 = (o == "g13");
      float tempC = 0.0f;
      int8_t setIdx = (nt >= 2) ? static_cast<int8_t>(dec(1)) : -1;
      if (setIdx < 0)
      {
        if (!d.readTempC(tempC))
        {
          Serial.println("[lab] temp read failed");
          continue;
        }
        setIdx = Epd709Lut::setForTempC(tempC);
        if (setIdx < 0)
        {
          Serial.printf("[lab] %.1f C outside 21.0..25.5 C (set 4); pass set index (e.g. %s 5)\n",
                        tempC, o.c_str());
          continue;
        }
      }
      static uint8_t rawSet[Epd709Lut::TABLE_LEN];
      Epd709Lut::BuildInfo info{};
      if (!d.readOtpSet(static_cast<uint8_t>(setIdx), rawSet))
      {
        Serial.printf("[lab] 0x93 read/validate failed for set %d\n", setIdx);
        continue;
      }
      if (!Epd709Lut::buildFast709(rawSet, s_lut, s_pwr, isG13, &info))
      {
        Serial.printf("[lab] %s build/lint failed for set %d\n", o.c_str(), setIdx);
        continue;
      }
      s_pll = 0x08;
      s_lutOn = true;
      reconfigure();
      if (nt >= 2)
        Serial.printf("[lab] %s set %d (manual): %u fr (from %u), "
                      "PWR %02X %02X %02X %02X %02X %02X, max|q| %.3f, ready\n",
                      o.c_str(), setIdx, info.fastFrames, info.stockFrames,
                      s_pwr[0], s_pwr[1], s_pwr[2], s_pwr[3], s_pwr[4], s_pwr[5],
                      info.maxAbsQ);
      else
        Serial.printf("[lab] %s set %d (%.1f C): %u fr (from %u), "
                      "PWR %02X %02X %02X %02X %02X %02X, max|q| %.3f, ready\n",
                      o.c_str(), setIdx, tempC, info.fastFrames, info.stockFrames,
                      s_pwr[0], s_pwr[1], s_pwr[2], s_pwr[3], s_pwr[4], s_pwr[5],
                      info.maxAbsQ);
    }
    else if (o == "rd" && nt >= 4)
    { // rd CMD N CHIP(m/s) [busy]: send CMD, optionally wait for BUSY, clock
      // in N bytes on SI1 (as the temp read), print them as hex. Reads only.
      const uint8_t c = hex(1);
      const uint32_t n = dec(2);
      const bool master = tok[3] != "s", waitBusy = nt >= 5 && tok[4] == "busy";
      const uint8_t SCK = PIN_EPD_SCK, MOSI_ = PIN_EPD_MOSI, SI1 = 8;
      SPI.end();
      pinMode(SCK, OUTPUT); pinMode(MOSI_, OUTPUT); pinMode(SI1, INPUT);
      digitalWrite(SCK, LOW);
      d.select(master, !master);
      digitalWrite(d._dc, LOW);
      for (int b = 7; b >= 0; --b)
      {
        digitalWrite(MOSI_, (c >> b) & 1);
        delayMicroseconds(2); digitalWrite(SCK, HIGH);
        delayMicroseconds(2); digitalWrite(SCK, LOW);
      }
      delay(1);
      if (waitBusy)
      {
        const uint32_t t0 = millis();
        while (digitalRead(d._busy) == LOW && millis() - t0 < 2000) {}
      }
      digitalWrite(d._dc, HIGH);
      char line[80];
      int pos = 0;
      for (uint32_t i = 0; i < n; ++i)
      {
        uint8_t v = 0;
        for (int b = 7; b >= 0; --b)
        {
          delayMicroseconds(1); digitalWrite(SCK, HIGH);
          delayMicroseconds(1); v |= digitalRead(SI1) << b;
          digitalWrite(SCK, LOW);
        }
        pos += sprintf(line + pos, "%02X", v);
        if ((i % 32) == 31 || i == n - 1)
        {
          Serial.printf("[rd] %05u %s\n", (unsigned)(i - (i % 32)), line);
          pos = 0;
        }
      }
      d.select(false, false);
      SPI.begin(SCK, -1, MOSI_, -1);
      Serial.printf("[lab] rd %02X %u done\n", c, (unsigned)n);
    }
    else if (o == "ppll" && nt >= 2) { s_ppll = hex(1); Serial.printf("[lab] ppll %02X\n", s_ppll); }
    else if (o == "pll" && nt >= 2) { s_pll = hex(1); reconfigure(); Serial.printf("[lab] pll %02X\n", s_pll); }
    else if (o == "drf" && nt >= 2) { s_drf = hex(1); Serial.printf("[lab] drf %02X\n", s_drf); }
    else if (o == "ccset" && nt >= 2) { s_ccset = hex(1); Serial.printf("[lab] ccset %02X\n", s_ccset); }
    else if (o == "cdi" && nt >= 2) { s_cdi = hex(1); reconfigure(); Serial.printf("[lab] cdi %02X\n", s_cdi); }
    else if (o == "reset") { reconfigure(); Serial.println("[lab] reset done"); }
    else if (o == "img")
    {
      // a whole frame in controller order (960000 bytes, as tools/sim709
      // writes with SIM_RAW), then shown as the weather firmware does: a
      // full refresh and, in EPD709_EXTRA_INKS builds, the extra-ink pass
      // 4 KB at a time, each answered with '#' before the next is sent: the
      // USB serial drops bytes when it is simply streamed into
      Serial.setRxBufferSize(16384); // the default 256 drops bytes
      Serial.println("[lab] img ready");
      Serial.setTimeout(5000);
      size_t n = 0;
      while (n < Epd709::FRAME_BYTES)
      {
        const size_t want = std::min<size_t>(4096, Epd709::FRAME_BYTES - n);
        const size_t got = Serial.readBytes(d._frame + n, want);
        n += got;
        if (got != want) break;
        Serial.write('#');
        Serial.flush();
      }
      Serial.printf("[lab] img got %u bytes\n", (unsigned)n);
      if (n == Epd709::FRAME_BYTES && nt >= 2 && tok[1] == "lab")
      { // the bench's own path: the clock set with pll, no extra-ink pass
        sendAll();
        Serial.printf("[lab] img done %u ms (pll %02X)\n", refresh(0, false), s_pll);
      }
      else if (n == Epd709::FRAME_BYTES)
      {
        const uint32_t t0 = millis();
        d.sendFrame();
        d.refresh();
#if defined(EPD709_EXTRA_INKS)
        if (d.hasExtraInks()) d.extraInkPass();
#endif
        Serial.printf("[lab] img done %u ms\n", (unsigned)(millis() - t0));
      }
    }
    else if (o == "xpass" && nt >= 2)
    {
      // the extra-ink pass again over the frame from img, the bench's way:
      // code 2 where an extra ink goes, hold elsewhere, cut after MS;
      // "both" drives the slave too (as the weather firmware does)
      const uint32_t pms = dec(1);
      const bool both = nt >= 3 && tok[2] == "both";
      static const uint8_t CODE[16] = {4, 4, 4, 4, 4, 4, 4, 4,
                                       4, 2, 2, 2, 4, 4, 4, 4};
      uint8_t lut[256], rowBuf[HALF_BYTES];
      for (int b = 0; b < 256; ++b) lut[b] = (CODE[b >> 4] << 4) | CODE[b & 15];
      cmd1(true, both, 0xE0, s_ccset);
      d.waitReady(60000);
      static const uint8_t ptl[] = {0, 0, 0x04, 0xAF, 0, 0, 0x03, 0x1F, 0x01};
      cmd(true, both, 0x83, ptl, sizeof(ptl));
      for (uint8_t chip = 0; chip < (both ? 2 : 1); ++chip)
      {
        SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        d.select(chip == 0, chip == 1);
        digitalWrite(d._dc, LOW);
        SPI.transfer(0x10);
        digitalWrite(d._dc, HIGH);
        for (int row = 0; row < Epd709::HEIGHT; ++row)
        {
          const uint8_t *src = f + row * (Epd709::WIDTH / 2) + chip * HALF_BYTES;
          for (uint32_t i = 0; i < HALF_BYTES; ++i) rowBuf[i] = lut[src[i]];
          SPI.writeBytes(rowBuf, HALF_BYTES);
        }
        d.select(false, false);
        SPI.endTransaction();
      }
      cmd(true, both, 0x04, nullptr, 0);
      d.waitReady(60000);
      delay(50);
      cmd1(true, both, 0x12, 0x01);
      const uint32_t t = millis();
      while (digitalRead(d._busy) == HIGH && millis() - t < 500) {}
      const uint32_t act = millis();
      const uint32_t waited = act - t;
      while (millis() - act < pms) {}
      digitalWrite(d._rst, LOW);
      delay(10);
      reconfigure();
      Serial.printf("[lab] xpass %u ms %s done (busy went low after %u ms)\n",
                    (unsigned)pms, both ? "both" : "master", (unsigned)waited);
    }
    else if (o == "sht")
    {
      // SHT4x on the U6 pads (SDA 13, SCL 11, powered with the panel):
      // one high-precision measurement (0xFD), CRC not checked.
      pinMode(PIN_EPD_PWR, OUTPUT);
      digitalWrite(PIN_EPD_PWR, HIGH);
      delay(5);
      Wire.begin(PIN_BME_SDA, PIN_BME_SCL, 100000);
      Wire.beginTransmission(0x44);
      Wire.write(0xFD);
      const int e = Wire.endTransmission();
      delay(12);
      uint8_t b[6] = {0};
      const int n = Wire.requestFrom(0x44, 6);
      for (int i = 0; i < n && i < 6; ++i) b[i] = Wire.read();
      Wire.end();
      if (e || n != 6)
        Serial.printf("[lab] sht no reply (err %d, %d bytes)\n", e, n);
      else
        Serial.printf("[lab] sht %.2f C %.1f %%RH\n",
                      -45 + 175 * ((b[0] << 8) | b[1]) / 65535.0,
                      -6 + 125 * ((b[3] << 8) | b[4]) / 65535.0);
    }
    else if (o == "temp")
    {
      // TSC 0x40 read-back, bit-banged, master only. Sequence after OSPTEK's
      // EPD0709A02 bring-up example (CC BY 4.0, gitee.com/osptek): CS held
      // low, send 0x40, wait for BUSY high (the measurement), then clock in
      // two bytes with DC high. The controller answers on SI1, which the
      // EE02 wires to GPIO8. SPI is detached for the read and re-attached
      // after, then both controllers are reset and configured.
      const uint8_t SCK = PIN_EPD_SCK, MOSI_ = PIN_EPD_MOSI, SI1 = 8;
      SPI.end();
      pinMode(SCK, OUTPUT); pinMode(MOSI_, OUTPUT); pinMode(SI1, INPUT);
      digitalWrite(SCK, LOW);
      d.select(true, false);
      digitalWrite(d._dc, LOW);
      for (int b = 7; b >= 0; --b)
      {
        digitalWrite(MOSI_, (0x40 >> b) & 1);
        delayMicroseconds(2); digitalWrite(SCK, HIGH);
        delayMicroseconds(2); digitalWrite(SCK, LOW);
      }
      delay(1);
      const uint32_t t0 = millis();
      while (digitalRead(d._busy) == LOW && millis() - t0 < 2000) {}
      const uint32_t tw = millis() - t0;
      digitalWrite(d._dc, HIGH);
      uint8_t r[2] = {0, 0};
      for (int k = 0; k < 2; ++k)
        for (int b = 7; b >= 0; --b)
        {
          delayMicroseconds(2); digitalWrite(SCK, HIGH);
          delayMicroseconds(2); r[k] |= digitalRead(SI1) << b;
          digitalWrite(SCK, LOW);
        }
      d.select(false, false);
      Serial.printf("[lab] temp master %02X %02X = %.1f C (busy %u ms)\n", r[0], r[1],
                    (int8_t)r[0] + ((r[1] & 0x80) ? 0.5f : 0.0f), (unsigned)tw);
      SPI.begin(SCK, -1, MOSI_, -1);
      reconfigure();
      Serial.println("[lab] temp done");
    }
    else Serial.println("[lab] ?");
  }
}

#endif
