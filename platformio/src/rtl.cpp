/* Right-to-left text shaping for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
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

#include <vector>
#include <utility>
#include "_locale.h"
#include "rtl.h"

/* Text shaping for a right-to-left locale (LC_RTL, the Hebrew locale).
 *
 * The locale files and the IMS replies are UTF-8; the panel fonts are
 * 8-bit Adafruit GFX faces. For a Hebrew build the font family is one
 * fontconvert.py generated with --encoding=iso-8859-8, which puts the
 * Hebrew alphabet at 0xE0-0xFA (and leaves the degree sign at 0xB0), so
 * each UTF-8 string is transcoded to that page here. GFX also only prints
 * left to right, so the line is reordered into visual order first: the
 * runs of a line are laid out right to left and the characters inside each
 * Hebrew run reversed, while runs of digits and Latin letters keep their
 * own left-to-right order (a simplified Unicode bidi: numbers take their
 * attached % . : - / signs with them, other neutrals follow the paragraph
 * direction, and brackets inside Hebrew runs are mirrored).
 *
 * Positions and alignment are untouched: a LEFT-aligned label still starts
 * at its x, it just reads right to left. drawMultiLnString wraps the
 * logical text and shapes each line through drawString, so wrapped text
 * breaks in reading order.
 *
 * Latin-1 bytes that are not valid UTF-8 (the "\260C" degree escapes the
 * other locales use) pass through unchanged, so an unshaped string still
 * draws what it always drew.
 */
static inline bool isHebrewCp(uint32_t c)
{
  return c >= 0x0590 && c <= 0x05FF;
}

static inline bool isLatinLetterCp(uint32_t c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
      || (c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7);
}

static inline bool isDigitCp(uint32_t c)
{
  return c >= '0' && c <= '9';
}

// Signs that stay with an adjacent number ("24°", "91%", "10:30", "3.5").
static inline bool isNumberSignCp(uint32_t c)
{
  return c == '%' || c == 0x00B0 || c == '+' || c == '-' || c == '.'
      || c == ',' || c == ':' || c == '/';
}

static uint32_t mirrorCp(uint32_t c)
{
  switch (c)
  {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    default:  return c;
  }
}

// One code point to the ISO-8859-8 byte the Hebrew families carry.
static uint8_t toHebrewPage(uint32_t c)
{
  if (c >= 0x05D0 && c <= 0x05EA)
  {
    return static_cast<uint8_t>(0xE0 + (c - 0x05D0));
  }
  switch (c)
  {
    case 0x05F3: return '\'';   // geresh
    case 0x05F4: return '"';    // gershayim
    case 0x05BE: return '-';    // maqaf
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
      return '-';
    case 0x2018: case 0x2019: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2026: return '.';    // ellipsis (drawMultiLnString adds "...")
    case 0x00A0: return ' ';
    default: break;
  }
  if (c < 0x80)
  {
    return static_cast<uint8_t>(c);
  }
  // ISO-8859-8 keeps most Latin-1 signs (0xA2-0xBE: ¢ £ ¤ ¥ § ¨ © × « ¬ ® ¯
  // ° ± ² ³ ´ µ ¶ · ¸ ¹ ÷ » ¼ ½ ¾); its letters are gone.
  if (c == 0x00D7) return 0xAA;
  if (c == 0x00F7) return 0xBA;
  if (c >= 0x00A2 && c <= 0x00BE && c != 0x00AA && c != 0x00BA)
  {
    return static_cast<uint8_t>(c);
  }
  if (c >= 0x05B0 && c <= 0x05C7)
  {
    return 0;                   // vowel points: no glyph, drop them
  }
  return '?';
}

// Decodes UTF-8; a byte that does not start a valid sequence is taken as
// the Latin-1 character it is (so the "\260" escapes keep working).
static void decodeUtf8(const String &text, std::vector<uint32_t> &out)
{
  const uint8_t *p = reinterpret_cast<const uint8_t *>(text.c_str());
  const size_t n = text.length();
  out.clear();
  out.reserve(n);
  for (size_t i = 0; i < n;)
  {
    uint8_t b = p[i];
    uint32_t cp = b;
    size_t len = 1;
    if ((b & 0xE0) == 0xC0)      { cp = b & 0x1F; len = 2; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; len = 3; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; len = 4; }
    bool ok = (len == 1) || (i + len <= n);
    for (size_t k = 1; ok && k < len; ++k)
    {
      if ((p[i + k] & 0xC0) != 0x80)
      {
        ok = false;
      }
      else
      {
        cp = (cp << 6) | (p[i + k] & 0x3F);
      }
    }
    if (!ok || (len == 2 && cp < 0x80))
    {
      out.push_back(b);         // a lone Latin-1 byte
      i += 1;
    }
    else
    {
      out.push_back(cp);
      i += len;
    }
  }
}

String shapeText(const String &text)
{
  if (!LC_RTL || text.isEmpty())
  {
    return text;
  }
  std::vector<uint32_t> cps;
  decodeUtf8(text, cps);
  const size_t n = cps.size();

  // 1. direction of each character: 'R' Hebrew, 'L' Latin letter or digit,
  //    'N' neutral (resolved below)
  std::vector<char> dir(n, 'N');
  bool anyHebrew = false;
  for (size_t i = 0; i < n; ++i)
  {
    if (isHebrewCp(cps[i]))                      { dir[i] = 'R'; anyHebrew = true; }
    else if (isDigitCp(cps[i]) || isLatinLetterCp(cps[i])) { dir[i] = 'L'; }
  }
  // a number's signs go with it: "24°" "91%" "10:30" "-3" "3.5"
  for (size_t i = 0; i < n; ++i)
  {
    if (dir[i] == 'N' && isNumberSignCp(cps[i]))
    {
      bool before = i > 0 && isDigitCp(cps[i - 1]);
      bool after  = i + 1 < n && isDigitCp(cps[i + 1]);
      if (before || after)
      {
        dir[i] = 'L';
      }
    }
  }
  // remaining neutrals: between two L characters they are L, else they
  // follow the paragraph direction (R)
  for (size_t i = 0; i < n;)
  {
    if (dir[i] != 'N') { ++i; continue; }
    size_t j = i;
    while (j < n && dir[j] == 'N') ++j;
    bool leftL  = i > 0 && dir[i - 1] == 'L';
    bool rightL = j < n && dir[j] == 'L';
    char d = (leftL && rightL) ? 'L' : 'R';
    for (size_t k = i; k < j; ++k) dir[k] = d;
    i = j;
  }

  // 2. visual order: runs laid right to left, Hebrew runs reversed
  std::vector<uint32_t> visual;
  visual.reserve(n);
  if (!anyHebrew)
  {
    // all-Latin text (a status line, a unit) keeps its order even in a
    // Hebrew locale; only the code page changes
    visual = cps;
  }
  else
  {
    std::vector<std::pair<size_t, size_t>> runs;  // [start, end)
    for (size_t i = 0; i < n;)
    {
      size_t j = i;
      while (j < n && dir[j] == dir[i]) ++j;
      runs.push_back(std::make_pair(i, j));
      i = j;
    }
    for (int r = static_cast<int>(runs.size()) - 1; r >= 0; --r)
    {
      size_t a = runs[r].first, b = runs[r].second;
      if (dir[a] == 'L')
      {
        for (size_t k = a; k < b; ++k) visual.push_back(cps[k]);
      }
      else
      {
        for (size_t k = b; k-- > a;) visual.push_back(mirrorCp(cps[k]));
      }
    }
  }

  // 3. to the font's code page
  String out;
  out.reserve(visual.size());
  for (uint32_t c : visual)
  {
    uint8_t b = toHebrewPage(c);
    if (b != 0)
    {
      out += static_cast<char>(b);
    }
  }
  return out;
} // end shapeText
