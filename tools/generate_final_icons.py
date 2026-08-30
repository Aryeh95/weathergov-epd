#!/usr/bin/env python3
"""Generate icons_color.h: the final mixed icon set, light + dark.

Composition (chosen on-device/in-preview 2026-08):
  - Condition icons 168/64/48px: Google weather set-2 artwork
    (https://github.com/mrdarrengriffin/google-weather-icons). The PNGs
    are Google's property and are downloaded at generation time into
    tools/google_icons/ -- they must NOT be committed to the repo.
  - Condition icons 32px (hourly graph): native outline art drawn by
    generate_native_icons.py (crisper at small sizes).
  - Widget icons 48/40px: InkyPi set (as before).

Every icon is emitted twice: quantized for the paper-white background
(light) and for a black background (dark mode) -- Google's pale artwork
needs opposite tone treatments on the two grounds, and the 32px outline
art swaps black/white. Same 4-bit format as before; COLOR_ICONS /
COLOR_ICONS_DARK and the widget tables are selected at runtime via
DARK_MODE (see display_utils.cpp).

Usage: python tools/generate_final_icons.py
"""
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy        # noqa: E402
import generate_native_icons as native       # noqa: E402
import generate_google_preview as gpre       # noqa: E402
import preview_full_display as pfd           # noqa: E402

OUT_PATH = legacy.OUT_PATH
POLLEN_OUT_PATH = OUT_PATH.replace("icons_color.h", "icons_pollen.h")
GOOGLE_SIZES = [168, 64, 48]
# Widget list including the pollen flower (drawn by this script -- no
# InkyPi counterpart).
WIDGETS = legacy.WIDGET_ICONS + ["pollen"]


def draw_pollen_icons(icon_dir):
    """Flower art for the pollen widget: color version (red petals,
    yellow center, green stem) for light and dark grounds, plus a
    black line-art version for the single-color panels (emitted as a
    dithered-format bitmap in icons_pollen.h)."""
    import math, os
    from PIL import Image, ImageDraw
    RED = (255, 0, 0, 255)
    YEL = (255, 255, 0, 255)
    GRN = (0, 128, 0, 255)
    BLK = (0, 0, 0, 255)
    LTG = (205, 205, 205, 255)   # dithers near-white: dark-mode outline

    def flower(outline, petal, center, stem, path):
        im = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        cx, cy = 256, 210
        # stem + leaf
        d.line([(cx, cy + 60), (cx, 480)], fill=stem, width=30)
        d.ellipse([cx + 10, 360, cx + 130, 430], fill=stem)
        # petals: 6 ellipses radiating from the center
        for i in range(6):
            a = math.pi * 2 * i / 6
            px_, py_ = cx + math.cos(a) * 95, cy + math.sin(a) * 95
            d.ellipse([px_ - 62, py_ - 62, px_ + 62, py_ + 62],
                      fill=petal, outline=outline, width=12)
        d.ellipse([cx - 60, cy - 60, cx + 60, cy + 60],
                  fill=center, outline=outline, width=12)
        im.save(path)

    flower(BLK, RED, YEL, GRN, os.path.join(icon_dir, "pollen.png"))
    flower(LTG, RED, YEL, GRN, os.path.join(icon_dir, "pollen_dark.png"))
    # line art: unfilled petals, black strokes
    im = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, cy = 256, 210
    d.line([(cx, cy + 60), (cx, 480)], fill=BLK, width=22)
    d.arc([cx + 4, 350, cx + 140, 440], 100, 340, fill=BLK, width=18)
    for i in range(6):
        a = math.pi * 2 * i / 6
        px_, py_ = cx + math.cos(a) * 95, cy + math.sin(a) * 95
        d.ellipse([px_ - 62, py_ - 62, px_ + 62, py_ + 62],
                  outline=BLK, width=20)
    d.ellipse([cx - 60, cy - 60, cx + 60, cy + 60], fill=BLK)
    im.save(os.path.join(icon_dir, "pollen_line.png"))


def invert_bw(indices):
    return [2 if i == 1 else 1 if i == 2 else i for i in indices]


def main():
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    legacy.draw_custom_icons(icon_dir)
    pfd.draw_custom_icons_dark(icon_dir)
    draw_pollen_icons(icon_dir)
    total = 0
    with open(OUT_PATH, "w", newline="\n") as f:
        f.write(
"""/* Full-color weather icons for multicolor e-paper panels, light + dark.
 *
 * GENERATED FILE -- do not edit by hand; regenerate with
 *   python tools/generate_final_icons.py
 *
 * Condition icons at 168/64/48px are quantized from the Google weather
 * icon set (github.com/mrdarrengriffin/google-weather-icons, set-2).
 * The source PNGs are the property of Google and are downloaded at
 * generation time; they are not distributed with this repository.
 * 32px condition icons (hourly graph) are original outline art drawn by
 * tools/generate_native_icons.py. Widget icons are from the InkyPi
 * project by Faith Akici (github.com/fatihak/InkyPi, GPL-3.0).
 *
 * Each icon has light-background and dark-background (DARK_MODE)
 * variants. Format: 4-bit palette indices, two pixels per byte (high
 * nibble first), row-major. Index 0 is transparent; 1..6 map to black,
 * white, red, yellow, green, blue (see CI_PALETTE in renderer.cpp).
 */

#ifndef __ICONS_COLOR_H__
#define __ICONS_COLOR_H__

#include <Arduino.h>

""")
        # ---- condition icons
        for code in legacy.ICONS:
            gpath = gpre.flatten(gpre.GOOGLE_MAP[code])
            rawpath = os.path.join(TOOLS_DIR, "google_icons",
                                   gpre.GOOGLE_MAP[code] + ".png")
            for size in GOOGLE_SIZES:
                data = legacy.pack(legacy.quantize_condition(gpath, size))
                total += len(data)
                legacy.emit(f, "ci_%s_%d" % (code, size), data)
                data = legacy.pack(pfd.quantize_on_black(rawpath, size))
                total += len(data)
                legacy.emit(f, "cid_%s_%d" % (code, size), data)
            n32 = native.quantize_native(native.draw_condition(code), 32)
            for name, arr in (("ci_%s_32" % code, n32),
                              ("cid_%s_32" % code, invert_bw(n32))):
                data = legacy.pack(arr)
                total += len(data)
                legacy.emit(f, name, data)
            print(code, "done")
        # ---- widget icons (InkyPi light, on-black dark)
        for name in WIDGETS:
            path = os.path.join(icon_dir, name + ".png")
            dark_path = path
            if name in ("intemp", "inhumidity", "pollen"):
                dark_path = os.path.join(icon_dir, name + "_dark.png")
            sat = None if name in legacy.SOFT_WIDGET_ICONS else legacy.SATURATION
            for wsize in (48, 40):
                data = legacy.pack(legacy.quantize(path, wsize, dither=True,
                                                   saturation=sat))
                total += len(data)
                legacy.emit(f, "ci_w_%s_%d" % (name, wsize), data)
                data = legacy.pack(pfd.quantize_on_black(dark_path, wsize,
                                                         sat=1.8))
                total += len(data)
                legacy.emit(f, "cid_w_%s_%d" % (name, wsize), data)
            print(name, "done")
        # ---- tables
        f.write("typedef struct {\n"
                "  const char code[8];\n"
                "  const uint8_t *px168;\n"
                "  const uint8_t *px64;\n"
                "  const uint8_t *px48;\n"
                "  const uint8_t *px32;\n"
                "} color_icon_t;\n\n")
        for tbl, pfx in (("COLOR_ICONS", "ci"), ("COLOR_ICONS_DARK", "cid")):
            f.write("static const color_icon_t %s[] = {\n" % tbl)
            for code in legacy.ICONS:
                f.write('  {"%s", %s_%s_168, %s_%s_64, %s_%s_48, %s_%s_32},\n'
                        % (code, pfx, code, pfx, code, pfx, code, pfx, code))
            f.write("};\n\n")
        f.write("typedef struct {\n"
                "  const char name[12];\n"
                "  const uint8_t *px48;\n"
                "  const uint8_t *px40;\n"
                "} color_widget_t;\n\n")
        for tbl, pfx in (("COLOR_WIDGETS", "ci_w"),
                         ("COLOR_WIDGETS_DARK", "cid_w")):
            f.write("static const color_widget_t %s[] = {\n" % tbl)
            for name in WIDGETS:
                f.write('  {"%s", %s_%s_48, %s_%s_40},\n'
                        % (name, pfx, name, pfx, name))
            f.write("};\n\n")
        f.write("#endif\n")
    with open(POLLEN_OUT_PATH, "w", newline="\n") as f:
        f.write("/* Line-art pollen (flower) widget icon in the dithered\n"
                " * 4-bit format (0 transparent, 1 black, 2 white) --\n"
                " * compiled into EVERY panel build, like icons_moon.h.\n"
                " * GENERATED FILE -- do not edit by hand; regenerate with\n"
                " *   python tools/generate_final_icons.py\n"
                " */\n\n"
                "#ifndef __ICONS_POLLEN_H__\n"
                "#define __ICONS_POLLEN_H__\n\n"
                "#include <Arduino.h>\n\n")
        lpath = os.path.join(icon_dir, "pollen_line.png")
        for wsize in (48, 40):
            data = legacy.pack(legacy.quantize(lpath, wsize, dither=False,
                                               allowed=[0]))
            legacy.emit(f, "pollen_line_%d" % wsize, data)
        f.write("#endif\n")
    print("wrote %s" % POLLEN_OUT_PATH)
    print("wrote %s (%d bytes of icon data)" % (OUT_PATH, total))


if __name__ == "__main__":
    main()
