#!/usr/bin/env python3
"""Generate 2-bit (anti-aliased) twins of the condition icons, for the 7.5in
panel's four greys (GREYS_75).

Each icon getConditionsBitmap (platformio/src/display_utils.cpp) can return
(at 196, 64 and 48 px), each alert icon (48 px) and each error screen's icon
(196 px, main.cpp) is rendered from its SVG (icons/svg), 8x8 supersampled,
and each dot stored by how much of it the icon covers: 0 none, 1 light grey
(35 % or more), 2 black (82 % or more). Four dots a byte, the first in the top
bits, row after row. The smaller sizes stay 1-bit: at 32 px and under the
icons are mostly one-dot lines, and shading them showed no difference on the
panel.

The table pairs each with a hash of its 1-bit header's bytes (same name and
size), by which the display (Grey75Display::drawInvertedBitmap) finds the twin
of the bitmap it was handed.

Writes platformio/lib/esp32-weather-epd-assets/icons/icons_2bit.h.

    pip install pymupdf pillow numpy
"""
import os
import re

import numpy as np
import pymupdf
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SVG = os.path.join(ROOT, "icons", "svg")
SRC = os.path.join(ROOT, "platformio", "src", "display_utils.cpp")
MAIN = os.path.join(ROOT, "platformio", "src", "main.cpp")
OUT = os.path.join(ROOT, "platformio", "lib", "esp32-weather-epd-assets",
                   "icons", "icons_2bit.h")
SS = 8
LO, HI = 0.35, 0.82


def coverage(path, size):
    page = pymupdf.open(path)[0]
    z = size * SS / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(z, z),
                          colorspace=pymupdf.csGRAY, alpha=False)
    im = Image.frombytes("L", (pix.width, pix.height), pix.samples)
    return 1.0 - np.array(im.resize((size, size), Image.BOX)) / 255.0


ICON_DIR = os.path.join(ROOT, "platformio", "lib", "esp32-weather-epd-assets",
                        "icons")


def mono_hash(name, size):
    """FNV-1a of the 1-bit header's bytes: every source file has its own copy
    of the icon arrays, so the display finds the twin by content, not by
    address (icon2Twin in renderer.cpp hashes the same way)."""
    path = os.path.join(ICON_DIR, "%dx%d" % (size, size),
                        "%s_%dx%d.h" % (name, size, size))
    text = open(path, encoding="utf-8").read()
    body = text[text.index("{") + 1:text.rindex("}")]
    h = 0x811C9DC5
    for b in re.findall(r"0x([0-9a-fA-F]{2})", body):
        h = ((h ^ int(b, 16)) * 0x01000193) & 0xFFFFFFFF
    return h


def pack(lv):
    flat = lv.flatten()
    flat = np.concatenate([flat, np.zeros((-len(flat)) % 4, np.uint8)])
    return flat[0::4] << 6 | flat[1::4] << 4 | flat[2::4] << 2 | flat[3::4]


def main():
    src = open(SRC, encoding="utf-8").read()
    cond = sorted(set(re.findall(r"getBitmap\((wi_[a-z0-9_]+)", src)))
    # the alerts' icons (getAlertBitmap48) and the error screens' (main.cpp,
    # drawError)
    alerts = sorted(set(re.findall(r"return ([a-z0-9_]+)_48x48;", src)))
    errors = sorted(set(re.findall(r"drawError\(([a-z0-9_]+)_196x196",
                                   open(MAIN, encoding="utf-8").read())))
    jobs = [(196, n) for n in cond + [e for e in errors if e not in cond]]
    jobs += [(64, n) for n in cond]
    jobs += [(48, n) for n in cond + [a for a in alerts if a not in cond]]
    out = ["// 2-bit twins of the condition icons (tools/icons2bit.py; do not",
           "// edit): 0 none, 1 light grey, 2 black, four dots a byte.",
           "#ifndef __ICONS_2BIT_H__", "#define __ICONS_2BIT_H__", ""]
    table = []
    total = 0
    for size, n in jobs:
        if True:
            svg = os.path.join(SVG, n.replace("_", "-") + ".svg")
            if not os.path.exists(svg):
                svg = os.path.join(SVG, n + ".svg")
            if not os.path.exists(svg):
                raise SystemExit("no SVG for " + n)
            cov = coverage(svg, size)
            lv = np.where(cov >= HI, 2, np.where(cov >= LO, 1, 0)).astype(np.uint8)
            p = pack(lv)
            var = "%s_%dx%d_2bit" % (n, size, size)
            out.append("const uint8_t %s[] PROGMEM = {" % var)
            for k in range(0, len(p), 24):
                out.append("  " + ", ".join("0x%02x" % b for b in p[k:k + 24]) + ",")
            out.append("};")
            table.append("  {%d, 0x%08xu, %s}, // %s" % (size, mono_hash(n, size),
                                                       var, n))
            total += len(p)
    out.append("")
    out.append("struct Icon2Twin { int16_t size; uint32_t hash; const uint8_t *dots; };")
    out.append("const Icon2Twin ICON2_TWINS[] PROGMEM = {")
    out += table
    out.append("};")
    out.append("#endif")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("%d icons, %d bytes (%.0f KB)" % (len(jobs), total, total / 1024))


if __name__ == "__main__":
    main()
