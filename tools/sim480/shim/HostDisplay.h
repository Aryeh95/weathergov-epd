/* The panel on a PC: an Adafruit_GFX canvas of ink indices, with the few
 * GxEPD2 calls the renderer makes (init, paging, hibernate). */
#pragma once
#include <algorithm>
#include <vector>
#include <Adafruit_GFX.h>
#include "GxEPD2.h"

class HostDisplay : public Adafruit_GFX
{
public:
  HostDisplay(int16_t w, int16_t h) : Adafruit_GFX(w, h), buf_(static_cast<size_t>(w) * h, 2) {}
  void init(uint32_t = 0, bool = true, uint16_t = 10, bool = false) { clear(); }
  void firstPage() { clear(); }
  bool nextPage() { return false; }
  void hibernate() {}
  void powerOff() {}
  void setFullWindow() {}
  // GxEPD2's: the bitmap's clear bits are drawn in `color`, set bits left alone
  void drawInvertedBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color)
  {
    const int16_t byteWidth = (w + 7) / 8;
    for (int16_t j = 0; j < h; ++j)
    {
      for (int16_t i = 0; i < w; ++i)
      {
        if (!(bitmap[j * byteWidth + i / 8] & (0x80 >> (i & 7))))
        {
          drawPixel(x + i, y + j, color);
        }
      }
    }
  }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override
  {
    if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
    buf_[static_cast<size_t>(y) * WIDTH + x] = inkOf(color);
  }
  const uint8_t *frame() const { return buf_.data(); }
  // 1 black 2 white 3 red 4 yellow 5 green 6 blue 7 orange
  static uint8_t inkOf(uint16_t c)
  {
    switch (c)
    {
      case GxEPD_BLACK:  return 1;
      case GxEPD_WHITE:  return 2;
      case GxEPD_RED:    return 3;
      case GxEPD_YELLOW: return 4;
      case GxEPD_GREEN:  return 5;
      case GxEPD_BLUE:   return 6;
      case GxEPD_ORANGE: return 7;
      default:           return ((c >> 11) + ((c >> 5) & 0x3F) / 2 + (c & 0x1F)) > 48 ? 2 : 1;
    }
  }
private:
  std::vector<uint8_t> buf_;
  void clear() { std::fill(buf_.begin(), buf_.end(), 2); }
};
