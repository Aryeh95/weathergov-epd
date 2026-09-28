/* Firmware target check for esp32-weather-epd.
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __OTA_TARGET_H__
#define __OTA_TARGET_H__

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Every build carries one line of text saying what it was built for
 * (FIRMWARE_TARGET in config.h): a prefix, the build target, the panel and
 * a ';'. This finds that line in a firmware file as it is uploaded, a chunk
 * at a time, so that the portal can refuse a file built for another board.
 *
 * The prefix to look for is taken from the running firmware's own line and
 * not written out a second time: a second copy in the code would itself be
 * found in every image.
 *
 * Nothing here needs Arduino, so that it can be tested on a PC
 * (tools/ota_target_test).
 */
class TargetScanner
{
public:
  // ownLine: this firmware's line. prefixLen: how much of it is the prefix.
  // The prefix must not contain its first character a second time, which
  // is what lets a failed match start over without looking back.
  TargetScanner(const char *ownLine, size_t prefixLen)
    : own(ownLine), prefix(prefixLen)
  {
    reset();
  }

  void reset()
  {
    matched = 0;
    length = 0;
    reading = false;
    done = false;
    text[0] = '\0';
  }

  void feed(const uint8_t *buf, size_t len)
  {
    for (size_t i = 0; i < len && !done; ++i)
    {
      const char c = static_cast<char>(buf[i]);
      if (reading)
      {
        if (c == ';')
        {
          done = true;
          continue;
        }
        if (c >= 0x20 && c <= 0x7E && length < sizeof(text) - 1)
        {
          text[length++] = c;
          text[length] = '\0';
          continue;
        }
        // not the line after all; this character may start the real one
        reading = false;
        matched = 0;
        length = 0;
        text[0] = '\0';
      }
      if (c == own[matched])
      {
        if (++matched == prefix)
        {
          reading = true;
        }
      }
      else
      {
        matched = (c == own[0]) ? 1 : 0;
      }
    }
  }

  // true once a whole line has gone past
  bool found() const { return done; }

  // what the file was built for: the line without its prefix and ';'
  const char *target() const { return text; }

  // what the running firmware was built for
  bool ownTarget(char *out, size_t size) const
  {
    const size_t n = strlen(own);
    if (n < prefix + 1 || n - prefix > size)
    {
      return false;
    }
    memcpy(out, own + prefix, n - prefix - 1);
    out[n - prefix - 1] = '\0';
    return true;
  }

  bool matches() const
  {
    char mine[sizeof(text)];
    return done && ownTarget(mine, sizeof(mine)) && strcmp(mine, text) == 0;
  }

private:
  const char *own;
  size_t prefix;
  size_t matched; // how much of the prefix the stream has matched
  size_t length;
  bool reading;   // prefix found, reading up to the ';'
  bool done;
  char text[81];
};

#endif
