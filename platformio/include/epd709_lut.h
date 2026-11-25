/* S6 register LUT decoder, fast-full builder, and safety lint for GDEB0709E01.
 *
 * Implements the S6 800-byte waveform table format, DC-charge balance gate
 * (|q_sf| <= 1 S-frame), hang-pattern checks, and the fast709 F3c / G13
 * recipes documented in Jonas Niesner's "E-paper LUT research"
 * (https://github.com/jonasniesner/epaper-lut-research; code MIT, docs CC BY 4.0).
 *
 * Everything else in this file is part of esp32-weather-epd and is free
 * software under the GNU General Public License, version 3 or later.
 */

#ifndef __EPD709_LUT_H__
#define __EPD709_LUT_H__

#include <stddef.h>
#include <stdint.h>

namespace Epd709Lut
{

static const size_t TABLE_LEN = 800;

struct BuildInfo
{
  uint16_t stockFrames;
  uint16_t fastFrames;
  float maxAbsQ;
};

/* Map a panel temperature sensor reading (0x40, in deg C) to the stock OTP
 * set index the fast table is built from, or -1.
 * - Set 4 from 21.0 C (not Jonas Niesner's 20.5): on our GDEB0709E01 the
 *   stock refresh took 49.4 s (the cold set) at 20.5 C and 49 s at 20.0 C,
 *   but 30.5 s (set 4) at 21 C, so the edge lies between 20.5 and 21.
 * - Set 4 up to 25.5 C (his measured 4|5 edge, 25.5-26 C).
 * - The warm set (25.5..30.0 C) only with allowWarm: his G13 recipe is
 *   documented for set 4, and whether the warm set is number 5 or 6 is an
 *   open question in his notes. */
int8_t setForTempC(float tempC, bool allowWarm = false);

/* Does a stock set of `frames` frames match a stock refresh that took
 * `stockMs` ms at PLL 08? Stock sets run at rate code 2, 20.35 ms a frame
 * (his frame-time table; our set 4 replica: 1512 frames, 30.53 s). Within
 * 3 %: then the set we read is the one the chip runs at this temperature. */
bool framesMatchStock(uint16_t frames, uint32_t stockMs);

/* Verify that an 800-byte OTP set image decodes with consistent non-zero block
 * lengths across LUTC and active colour slots 0, 1, 2, 3, 5, 6. */
bool validateStockSet(const uint8_t in[TABLE_LEN],
                      uint16_t *framesOut = nullptr);

/* Build an 800-byte fast709 register table (g13 = true -> G13, false -> F3c)
 * and its 6-byte PWR command data from a stock set read back via 0x93. Runs
 * lintCustom() before returning true. */
bool buildFast709(const uint8_t in[TABLE_LEN], uint8_t out[TABLE_LEN],
                  uint8_t pwrOut[6], bool g13 = true,
                  BuildInfo *info = nullptr);

/* Safety gate: rejects hang patterns (8 consecutive 0x00 bytes or all-zero
 * header), LUTC VCOM swing bits, unequal active block lengths, or any active
 * slot exceeding |q_sf| <= 1.0 S-frame at nominal rails. */
bool lintCustom(const uint8_t lut[TABLE_LEN], const uint8_t pwr[6],
                float *maxAbsQOut = nullptr);

} // namespace Epd709Lut

#endif
