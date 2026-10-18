/* Stand-in for SPI.h on a PC: the SPI object lives in Arduino.h's stub; the
 * settings the 4.26in renderer passes are accepted and ignored. */
#pragma once
#include "Arduino.h"
#ifndef MSBFIRST
#define MSBFIRST 1
#endif
#ifndef SPI_MODE0
#define SPI_MODE0 0
#endif
struct SPISettings
{
  SPISettings(uint32_t = 0, uint8_t = 0, uint8_t = 0) {}
};
