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

/* Map a panel temperature sensor reading (0x40, in deg C) to the verified
 * stock OTP set index, or -1 if outside the tested 20.5..30.0 C window. */
int8_t setForTempC(float tempC);

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
