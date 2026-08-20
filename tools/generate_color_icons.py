#!/usr/bin/env python3
"""Generate platformio/include/icons/icons_color.h from InkyPi's weather icons.

The multicolor e-paper panels (Spectra 6 / ACeP, see MULTICOLOR_DISPLAY in
config.h) can render full-color weather icons instead of single-color line
art. This script downloads the icon set from the InkyPi project
(https://github.com/fatihak/InkyPi, GPL-3.0, by Faith Akici), quantizes each
icon to the panel's six native inks (black, white, red, yellow, green, blue)
with Floyd-Steinberg dithering, and packs the result as 4-bit palette
indices (two pixels per byte, high nibble first; index 0 = transparent).

Three sizes are emitted per icon, matching the renderer's draw sites:
196x196 (current conditions), 64x64 (daily forecast), 32x32 (hourly graph).

Usage:  python tools/generate_color_icons.py [icon_dir]
        icon_dir: optional directory of already-downloaded PNGs; if omitted,
        icons are fetched from GitHub into ./inkypi_icons/

Requires: pip install pillow
"""
import os
import sys
import urllib.request

from PIL import Image, ImageEnhance

ICONS = ["01d", "01n", "02d", "02n", "03d", "04d",
         "09d", "10d", "10n", "11d", "13d", "50d"]
SIZES = [196, 64, 32]
SATURATION = 1.8
ALPHA_THRESHOLD = 128
# Quantization targets; palette indices are these positions + 1
# (index 0 is reserved for transparency).
PALETTE = [(0, 0, 0), (255, 255, 255), (255, 0, 0),
           (255, 255, 0), (0, 128, 0), (0, 0, 255)]
RAW_URL = ("https://raw.githubusercontent.com/fatihak/InkyPi/main/"
           "src/plugins/weather/icons/{}.png")
OUT_PATH = os.path.join(os.path.dirname(__file__), "..",
                        "platformio", "lib", "esp32-weather-epd-assets",
                        "icons", "icons_color.h")


def fetch_icons(icon_dir):
    os.makedirs(icon_dir, exist_ok=True)
    for name in ICONS:
        path = os.path.join(icon_dir, name + ".png")
        if not os.path.exists(path):
            print("downloading", name)
            urllib.request.urlretrieve(RAW_URL.format(name), path)


def quantize(path, size):
    """Returns a list of 4-bit palette indices, row-major."""
    im = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
    alpha = im.getchannel("A")
    rgb = Image.new("RGB", im.size, (255, 255, 255))
    rgb.paste(im, mask=alpha)
    rgb = ImageEnhance.Color(rgb).enhance(SATURATION)
    pal_img = Image.new("P", (1, 1))
    flat = sum([list(c) for c in PALETTE], [])
    pal_img.putpalette(flat + [0] * (768 - len(flat)))
    q = rgb.quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG)
    qp, ap = q.load(), alpha.load()
    out = []
    for y in range(size):
        for x in range(size):
            out.append(qp[x, y] + 1 if ap[x, y] >= ALPHA_THRESHOLD else 0)
    return out


def pack(indices):
    data = bytearray()
    for i in range(0, len(indices), 2):
        hi = indices[i]
        lo = indices[i + 1] if i + 1 < len(indices) else 0
        data.append((hi << 4) | lo)
    return data


def emit(f, name, data):
    f.write("static const uint8_t %s[%d] PROGMEM = {\n" % (name, len(data)))
    for i in range(0, len(data), 20):
        row = ",".join("0x%02x" % b for b in data[i:i + 20])
        f.write("  " + row + ",\n")
    f.write("};\n\n")


def main():
    icon_dir = sys.argv[1] if len(sys.argv) > 1 else "inkypi_icons"
    fetch_icons(icon_dir)
    total = 0
    with open(OUT_PATH, "w", newline="\n") as f:
        f.write(
"""/* Full-color weather condition icons for multicolor e-paper panels.
 *
 * GENERATED FILE -- do not edit by hand; regenerate with
 *   python tools/generate_color_icons.py
 *
 * Icon artwork from the InkyPi project by Faith Akici,
 * https://github.com/fatihak/InkyPi (GPL-3.0), quantized to the six native
 * inks of Spectra 6 / ACeP panels with Floyd-Steinberg dithering.
 *
 * Format: 4-bit palette indices, two pixels per byte (high nibble first),
 * row-major. Index 0 is transparent; 1..6 map to black, white, red, yellow,
 * green, blue (see CI_PALETTE in renderer.cpp).
 */

#ifndef __ICONS_COLOR_H__
#define __ICONS_COLOR_H__

#include <Arduino.h>

""")
        for name in ICONS:
            path = os.path.join(icon_dir, name + ".png")
            for size in SIZES:
                data = pack(quantize(path, size))
                total += len(data)
                emit(f, "ci_%s_%d" % (name, size), data)
                print("%s @ %dpx: %d bytes" % (name, size, len(data)))
        f.write("typedef struct {\n"
                "  const char code[4];\n"
                "  const uint8_t *px196;\n"
                "  const uint8_t *px64;\n"
                "  const uint8_t *px32;\n"
                "} color_icon_t;\n\n")
        f.write("static const color_icon_t COLOR_ICONS[] = {\n")
        for name in ICONS:
            f.write('  {"%s", ci_%s_196, ci_%s_64, ci_%s_32},\n'
                    % (name, name, name, name))
        f.write("};\n\n#endif\n")
    print("wrote %s (%d bytes of icon data)" % (OUT_PATH, total))


if __name__ == "__main__":
    main()
