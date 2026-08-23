#!/usr/bin/env python3
"""Generate 40x40 line-art icons by downsampling the 196x196 set.

The pre-generated line-art library (platformio/lib/esp32-weather-epd-assets)
ships fixed sizes (16..196) without 40px, which the 6-row widget layout
(widget_rows = 6) needs. Rather than re-rendering from the original fonts,
each 196px 1-bit icon is downscaled with Lanczos antialiasing and
re-thresholded, which preserves stroke weight cleanly at this scale.

Only the widget-slot icons are emitted (conditions keep their existing
sizes). Output: icons/icons_40x40.h in the assets library, same inverted
1-bit format as the other sizes (bit set = white, 5-byte row stride).

Usage: python tools/generate_40px_icons.py     (requires: pip install pillow)
"""
import os
import re

from PIL import Image

SIZE = 40
THRESHOLD = 140  # grayscale cutoff after Lanczos downscale; lower = thinner
ASSETS = os.path.join(os.path.dirname(__file__), "..", "platformio", "lib",
                      "esp32-weather-epd-assets", "icons")
ICONS = [
    "wi_sunrise", "wi_sunset", "wi_strong_wind", "wi_humidity",
    "wi_thermometer", "wi_day_sunny", "wi_barometer", "air_filter",
    "visibility_icon", "house_thermometer", "house_humidity",
    "wi_moon_alt_new", "wi_moon_alt_waxing_crescent_4",
    "wi_moon_alt_first_quarter", "wi_moon_alt_waxing_gibbous_4",
    "wi_moon_alt_full", "wi_moon_alt_waning_gibbous_4",
    "wi_moon_alt_third_quarter", "wi_moon_alt_waning_crescent_4",
]


def load196(base):
    path = os.path.join(ASSETS, "196x196", base + "_196x196.h")
    vals = [int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})",
                                           open(path).read())]
    img = Image.new("L", (196, 196), 255)
    px = img.load()
    stride = 25  # (196+7)//8
    for y in range(196):
        for x in range(196):
            if not (vals[y * stride + x // 8] >> (7 - (x % 8))) & 1:
                px[x, y] = 0
    return img


def to40(img):
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    return small.point(lambda v: 0 if v < THRESHOLD else 255)


def emit(f, base, img):
    stride = (SIZE + 7) // 8
    data = bytearray([0xFF] * (stride * SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            if px[x, y] < 128:  # black pixel -> clear the bit (inverted fmt)
                data[y * stride + x // 8] &= ~(1 << (7 - (x % 8)))
    f.write("// %d x %d\n" % (SIZE, SIZE))
    f.write("const unsigned char %s_40x40[] PROGMEM = {\n" % base)
    for i in range(0, len(data), 12):
        f.write("  " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",\n")
    f.write("};\n\n")


def main():
    out = os.path.join(ASSETS, "icons_40x40.h")
    with open(out, "w", newline="\n") as f:
        f.write(
"""/* 40x40 line-art widget icons for the 6-row widget layout.
 *
 * GENERATED FILE -- do not edit by hand; regenerate with
 *   python tools/generate_40px_icons.py
 * (downsampled from the 196x196 icons in this library; same artwork and
 * license as the rest of the icon set)
 */

#ifndef __ICONS_40X40_H__
#define __ICONS_40X40_H__

#include <Arduino.h>

""")
        for base in ICONS:
            emit(f, base, to40(load196(base)))
            print(base)
        f.write("#endif\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
