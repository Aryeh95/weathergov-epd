#!/usr/bin/env python3
"""Generate 2-bit (anti-aliased) twins of the condition icons, for the 7.5in
panel's four greys (GREYS_75).

Each icon getConditionsBitmap (platformio/src/display_utils.cpp) can return
(at 196, 64 and 48 px), each alert icon (48 px), each error/setup screen's
icon (196 px), and each 32 px hourly cloud icon is rendered from its SVG
(icons/svg), 8x8 supersampled, and each dot stored as 2 bits: 0 none, 1 light
grey (35 % or more), 2 black (82 % or more), and 3 interior fill (precomputed
cloud / sun / moon fill for drawConditionIcon, saving PSRAM/CPU work at
runtime). Four dots a byte, the first in the top bits, row after row. Clear
32 px icons and smaller UI icons stay 1-bit.

The table pairs each with a hash of its 1-bit header's bytes (same name and
size), by which the display (icon2Twin / drawConditionIcon in renderer.cpp)
finds the twin of the bitmap it was handed.

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

# Rain, snow, and thunderstorm clouds extend fill down to the cloud base.
TO_BASE_CLOUDS = frozenset({
    "wi_day_thunderstorm", "wi_night_alt_thunderstorm", "wi_thunderstorm",
    "wi_day_storm_showers", "wi_night_alt_storm_showers", "wi_storm_showers",
    "wi_day_showers", "wi_night_alt_showers", "wi_showers",
    "wi_day_rain_wind", "wi_day_rain", "wi_night_alt_rain_wind",
    "wi_night_alt_rain", "wi_rain_wind", "wi_rain",
    "wi_day_rain_mix", "wi_night_alt_rain_mix", "wi_rain_mix",
    "wi_day_snow_wind", "wi_day_snow", "wi_night_alt_snow_wind",
    "wi_night_alt_snow", "wi_snow_wind", "wi_snow",
    "wi_day_sleet", "wi_night_alt_sleet", "wi_sleet",
})

# Dry clouds + clear sun/moon get interior fill (gusts and 7xx non-cloud excluded).
PLAIN_FILL_ICONS = frozenset({
    "wi_day_sunny_overcast", "wi_night_alt_partly_cloudy",
    "wi_day_cloudy", "wi_night_alt_cloudy", "wi_cloud", "wi_cloudy",
    "wi_day_sunny", "wi_night_clear",
})


def render_hr(path, res):
    """Rasterize an SVG at res x res float coverage in [0.0, 1.0]."""
    page = pymupdf.open(path)[0]
    z = res / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(z, z),
                          colorspace=pymupdf.csGRAY, alpha=False)
    im = Image.frombytes("L", (pix.width, pix.height), pix.samples)
    if (pix.width, pix.height) != (res, res):
        im = im.resize((res, res), Image.BOX)
    return 1.0 - np.array(im, dtype=np.float32) / 255.0


def coverage(path, size):
    hr = render_hr(path, size * SS)
    return hr.reshape(size, SS, size, SS).mean(axis=(1, 3))


ICON_DIR = os.path.join(ROOT, "platformio", "lib", "esp32-weather-epd-assets",
                        "icons")


def mono_info(name, size):
    """Return (FNV-1a hash, (size, size) ink1 bool mask) from the 1-bit header.
    Every source file has its own copy of the icon arrays, so the display finds
    the twin by content hash, not by address."""
    path = os.path.join(ICON_DIR, "%dx%d" % (size, size),
                        "%s_%dx%d.h" % (name, size, size))
    text = open(path, encoding="utf-8").read()
    body = text[text.index("{") + 1:text.rindex("}")]
    raw = [int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body)]
    h = 0x811C9DC5
    for b in raw:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    stride = (size + 7) // 8
    arr = np.array(raw, dtype=np.uint8).reshape(size, stride)
    bits = np.unpackbits(arr, axis=1, bitorder="big")[:, :size]
    return h, (bits == 0)


def flood_exterior(wall):
    """Flood-fill open background from the border of boolean obstacle mask."""
    h, w = wall.shape
    ext = np.zeros((h, w), dtype=bool)
    stack = []
    for i in range(w):
        if not wall[0, i]:
            ext[0, i] = True
            stack.append((0, i))
        if not wall[h - 1, i]:
            ext[h - 1, i] = True
            stack.append((h - 1, i))
    for j in range(1, h - 1):
        if not wall[j, 0]:
            ext[j, 0] = True
            stack.append((j, 0))
        if not wall[j, w - 1]:
            ext[j, w - 1] = True
            stack.append((j, w - 1))
    while stack:
        y, x = stack.pop()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            yy, xx = y + dy, x + dx
            if 0 <= yy < h and 0 <= xx < w and not wall[yy, xx] and not ext[yy, xx]:
                ext[yy, xx] = True
                stack.append((yy, xx))
    return ext


def largest_interior(wall, ext=None):
    """Return the largest 4-connected interior pocket not connected to the border."""
    if ext is None:
        ext = flood_exterior(wall)
    inside = (~wall) & (~ext)
    h, w = wall.shape
    vis = np.zeros((h, w), dtype=np.int32)
    best, best_n, cid = 0, 0, 0
    for j in range(h):
        for i in range(w):
            if inside[j, i] and vis[j, i] == 0:
                cid += 1
                stack = [(j, i)]
                vis[j, i] = cid
                n = 0
                while stack:
                    y, x = stack.pop()
                    n += 1
                    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < h and 0 <= xx < w and inside[yy, xx] and vis[yy, xx] == 0:
                            vis[yy, xx] = cid
                            stack.append((yy, xx))
                if n > best_n:
                    best_n, best = n, cid
    if best == 0:
        return np.zeros((h, w), dtype=bool)
    return vis == best


def compute_svg_fill(hr, size, to_base=False):
    """Exact high-res SVG interior fill (no morphological erosion/dilation)."""
    if size <= 64:
        mult = SS
        hr_fill = hr
    else:
        mult = 2
        hr_fill = hr.reshape(size * mult, SS // mult, size * mult, SS // mult).mean(axis=(1, 3))
    res = size * mult
    wall = hr_fill >= 0.5
    if to_base:
        y_close = int(round(20.6 / 30.0 * res))
        wall_c = wall.copy()
        wall_c[y_close, :] = True
        ext = flood_exterior(wall_c)
        inner_hr = largest_interior(wall_c, ext=ext)
        # Capture trapped corner pockets inside the cloud split off by raindrops/lightning.
        all_inside = (~wall_c) & (~ext)
        ys, xs = np.nonzero(inner_hr)
        if ys.size:
            y0, y1, x0, x1 = int(ys.min()), int(ys.max()), int(xs.min()), int(xs.max())
            h, w = wall.shape
            vis = np.zeros((h, w), dtype=bool)
            combined = inner_hr.copy()
            for j in range(y0, y1 + 1):
                for i in range(x0, x1 + 1):
                    if all_inside[j, i] and not combined[j, i] and not vis[j, i]:
                        stack = [(j, i)]
                        vis[j, i] = True
                        pts = []
                        ok = True
                        while stack:
                            yy, xx = stack.pop()
                            pts.append((yy, xx))
                            if yy < y0 or yy > y1 or xx < x0 or xx > x1:
                                ok = False
                            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                                ny, nx = yy + dy, xx + dx
                                if 0 <= ny < h and 0 <= nx < w and all_inside[ny, nx] and not vis[ny, nx]:
                                    vis[ny, nx] = True
                                    stack.append((ny, nx))
                        if ok and pts:
                            for yy, xx in pts:
                                combined[yy, xx] = True
            inner_hr = combined
    else:
        inner_hr = largest_interior(wall)
    fill_cov = inner_hr.reshape(size, mult, size, mult).mean(axis=(1, 3))
    return fill_cov >= 0.15


def pack(lv):
    flat = lv.flatten()
    flat = np.concatenate([flat, np.zeros((-len(flat)) % 4, np.uint8)])
    return flat[0::4] << 6 | flat[1::4] << 4 | flat[2::4] << 2 | flat[3::4]


def main():
    src = open(SRC, encoding="utf-8").read()
    cond = sorted(set(re.findall(r"getBitmap\((wi_[a-z0-9_]+)", src)))
    # the alerts' icons (getAlertBitmap48) and the error/setup screens'
    # icons (main.cpp drawError + renderer.cpp drawConfigPortalScreen)
    alerts = sorted(set(re.findall(r"return ([a-z0-9_]+)_48x48;", src)))
    errors = sorted(set(re.findall(r"drawError\(([a-z0-9_]+)_196x196",
                                   open(MAIN, encoding="utf-8").read())))
    if "wifi" not in errors:
        errors.append("wifi")
        errors.sort()
    clouds32 = [
        n for n in cond
        if n in TO_BASE_CLOUDS
        or (n in PLAIN_FILL_ICONS and n not in ("wi_day_sunny", "wi_night_clear"))
    ]
    jobs = [(196, n) for n in cond + [e for e in errors if e not in cond]]
    jobs += [(64, n) for n in cond]
    jobs += [(48, n) for n in cond + [a for a in alerts if a not in cond]]
    jobs += [(32, n) for n in clouds32]
    out = ["// 2-bit twins of the condition icons (tools/icons2bit.py; do not",
           "// edit): 0 none, 1 light grey, 2 black, 3 interior fill; four dots a byte.",
           "#ifndef __ICONS_2BIT_H__", "#define __ICONS_2BIT_H__", ""]
    table = []
    seen_hashes = set()
    total = 0
    for size, n in jobs:
        svg = os.path.join(SVG, n.replace("_", "-") + ".svg")
        if not os.path.exists(svg):
            svg = os.path.join(SVG, n + ".svg")
        if not os.path.exists(svg):
            raise SystemExit("no SVG for " + n)
        h, ink1 = mono_info(n, size)
        if (size, h) in seen_hashes:
            raise SystemExit("hash collision at (%d, 0x%08x) for %s" % (size, h, n))
        seen_hashes.add((size, h))
        to_base = n in TO_BASE_CLOUDS
        wants_fill = to_base or (n in PLAIN_FILL_ICONS)
        if size == 32:
            # Keep 1-bit 32px strokes crisp; only add exact SVG cloud fill.
            hr = render_hr(svg, size * SS)
            filled = compute_svg_fill(hr, size, to_base=to_base)
            assert filled.any(), n
            lv = np.where(ink1, 2, 0).astype(np.uint8)
            lv = np.where((lv != 2) & filled, 3, lv).astype(np.uint8)
        elif wants_fill:
            hr = render_hr(svg, size * SS)
            cov = hr.reshape(size, SS, size, SS).mean(axis=(1, 3))
            lv = np.where(cov >= HI, 2, np.where(cov >= LO, 1, 0)).astype(np.uint8)
            filled = compute_svg_fill(hr, size, to_base=to_base)
            assert filled.any(), n
            lv = np.where((lv != 2) & filled, 3, lv).astype(np.uint8)
        else:
            cov = coverage(svg, size)
            lv = np.where(cov >= HI, 2, np.where(cov >= LO, 1, 0)).astype(np.uint8)
        p = pack(lv)
        var = "%s_%dx%d_2bit" % (n, size, size)
        out.append("const uint8_t %s[] PROGMEM = {" % var)
        for k in range(0, len(p), 24):
            out.append("  " + ", ".join("0x%02x" % b for b in p[k:k + 24]) + ",")
        out.append("};")
        table.append("  {%d, 0x%08xu, %s}, // %s" % (size, h, var, n))
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
