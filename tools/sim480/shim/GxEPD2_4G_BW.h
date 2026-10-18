/* Stand-in for GxEPD2_4G_BW.h on a PC: black and white on the same driver;
 * anything not white is black. See GxEPD2_4G_4G.h. */
#pragma once
#include "GxEPD2_4G_4G.h"

template <typename GxEPD2_Type, const uint16_t page_height>
class GxEPD2_4G_BW_R : public Host426
{
public:
  GxEPD2_Type &epd2;
  GxEPD2_4G_BW_R(GxEPD2_Type &e) : Host426(GxEPD2_Type::WIDTH, GxEPD2_Type::HEIGHT, false), epd2(e) {}
};
