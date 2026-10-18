/* Stand-in for GxEPD2_4G_4G.h on a PC: the 4.26in panel (GDEQ0426T82) in its
 * four greys. Both drawing modes, greys and black and white (GxEPD2_4G_BW.h),
 * draw into one 800x480 frame of grey levels, 0 black .. 3 white, which
 * sim_main writes out: the panel shows whatever was drawn last. */
#pragma once
#include <cstdint>
#include <vector>
#include <Adafruit_GFX.h>
#include "GxEPD2_4G.h"

struct GxEPD2_426_GDEQ0426T82
{
  static const uint16_t WIDTH = 800, HEIGHT = 480;
  GxEPD2_426_GDEQ0426T82(int16_t, int16_t, int16_t, int16_t) {}
  void hibernate() {}
};

// the frame: a grey level a dot, as the board sits (rotation is applied)
inline uint8_t *host426Frame()
{
  static std::vector<uint8_t> frame(800 * 480, 3);
  return frame.data();
}

class Host426 : public Adafruit_GFX
{
public:
  Host426(int16_t w, int16_t h, bool grey) : Adafruit_GFX(w, h), grey_(grey) {}
  void init(uint32_t = 0, bool = true, uint16_t = 10, bool = false) {}
  template <class S, class T>
  void init(uint32_t, bool, uint16_t, bool, S &, T) {}
  void setFullWindow() {}
  void firstPage()
  {
    uint8_t *f = host426Frame();
    for (int i = 0; i < 800 * 480; ++i) f[i] = 3;
  }
  bool nextPage() { return false; }
  void hibernate() {}
  void powerOff() {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override
  {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    host426Frame()[static_cast<size_t>(y) * 800 + x] = grey_ ? levelOf(color) : (color == GxEPD_WHITE ? 3 : 0);
  }
  // GxEPD2_4G_4G's own rule: black 0, white 3, anything else by brightness
  static uint8_t levelOf(uint16_t color)
  {
    if (color == 0) return 0;
    if (color == GxEPD_WHITE) return 3;
    const uint32_t b = uint32_t(color & 0xF800) + (uint32_t(color & 0x07E0) << 5) + (uint32_t(color & 0x001F) << 11);
    return static_cast<uint8_t>((b - 1) / 0xC000ul);
  }
private:
  bool grey_;
};

template <typename GxEPD2_Type, const uint16_t page_height>
class GxEPD2_4G_4G : public Host426
{
public:
  GxEPD2_Type epd2;
  GxEPD2_4G_4G(GxEPD2_Type e) : Host426(GxEPD2_Type::WIDTH, GxEPD2_Type::HEIGHT, true), epd2(e) {}
};
