/* Stand-in for GxEPD2_BW.h on a PC: the panel class names renderer.h mentions
 * and a display template drawing into HostDisplay. */
#pragma once
#include "HostDisplay.h"
#ifndef SIM480_PANELS
#define SIM480_PANELS
struct SimPanel800
{
  static const uint16_t WIDTH = 800, HEIGHT = 480;
  SimPanel800(int16_t, int16_t, int16_t, int16_t) {}
protected:
  uint32_t _busy_timeout = 0;
};
struct SimPanel640
{
  static const uint16_t WIDTH = 640, HEIGHT = 384;
  SimPanel640(int16_t, int16_t, int16_t, int16_t) {}
};
typedef SimPanel800 GxEPD2_750_GDEY075T7;
typedef SimPanel800 GxEPD2_750c_GDEY075Z08;
typedef SimPanel800 GxEPD2_730c_GDEY073D46;
typedef SimPanel800 GxEPD2_730c_GDEP073E01;
typedef SimPanel640 GxEPD2_750;
#endif
template <typename GxEPD2_Type, const uint16_t page_height>
class GxEPD2_BW : public HostDisplay
{
public:
  GxEPD2_Type epd2;
  GxEPD2_BW(GxEPD2_Type e) : HostDisplay(GxEPD2_Type::WIDTH, GxEPD2_Type::HEIGHT), epd2(e) {}
};
