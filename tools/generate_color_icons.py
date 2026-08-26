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
168x168 (current conditions, centered in the 196px slot), 64x64 and
48x48 (daily forecast, size depends on forecast_days), 32x32 (hourly graph).

Usage:  python tools/generate_color_icons.py [icon_dir]
        icon_dir: optional directory of source PNGs; defaults to the
        tools/inkypi_icons/ folder vendored in this repository (see its
        README). Any icon missing from the directory is fetched from the
        InkyPi GitHub repo as a fallback.

Requires: pip install pillow
"""
import colorsys
import os
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageEnhance

ICONS = ["01d", "01n", "02d", "02n", "022d", "022n", "03d", "04d",
         "09d", "10d", "10n", "11d", "13d", "50d"]
# 168 (not the full 196 slot): the InkyPi artwork fills its canvas
# edge-to-edge, unlike the padded line art, so the current-conditions icon is
# rendered slightly smaller and drawn centered in the 196px slot.
SIZES = [168, 64, 48, 32]
# Left-panel widget icons, emitted at the 48px widget slot size. The last
# three have no InkyPi counterpart and are drawn by this script in the same
# flat style (see draw_custom_icons).
WIDGET_ICONS = ["sunrise", "sunset", "wind", "humidity", "pressure",
                "uvi", "visibility", "aqi",
                "dewpoint", "intemp", "inhumidity"]
# The moons are emitted into their own header (icons_moon.h): being pure
# black/white dither they render identically on every panel, so they are
# compiled into single-color builds too, unlike the color set.
MOON_NAMES = ["newmoon", "waxingcrescent", "firstquarter", "waxinggibbous",
              "fullmoon", "waninggibbous", "lastquarter", "waningcrescent"]
CUSTOM_ICONS = ["dewpoint", "intemp", "inhumidity",
                "newmoon", "waxingcrescent", "firstquarter", "waxinggibbous",
                "fullmoon", "waninggibbous", "lastquarter", "waningcrescent"]
# Moon phase icons are drawn by this script (draw_moons): a grayscale moon
# -- light lit side with subtle craters, dark shadow side, per-phase
# terminator -- quantized against black/white only, so the grays become
# clean B/W dither on the panel (the same treatment that makes the clouds
# read gray). Real-panel iterations ruled out InkyPi's yellow moon and a
# plain white-with-outline disk.
MOON_ICONS = {"newmoon", "waxingcrescent", "firstquarter", "waxinggibbous",
              "fullmoon", "waninggibbous", "lastquarter", "waningcrescent"}
MOON_PALETTE = [0, 1]  # black, white
# Widget icons whose pale blues read too blue on real ink at the full boost
# (like the condition icons' clouds); they get CONDITION_SATURATION instead.
SOFT_WIDGET_ICONS = {"wind"}
# Saturation boost before quantization for widget icons (their colors are
# saturated flat fills).
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
MOON_OUT_PATH = os.path.join(os.path.dirname(__file__), "..",
                             "platformio", "lib", "esp32-weather-epd-assets",
                             "icons", "icons_moon.h")


def fetch_icons(icon_dir):
    os.makedirs(icon_dir, exist_ok=True)
    for name in ICONS + WIDGET_ICONS:
        if name in CUSTOM_ICONS:
            continue
        path = os.path.join(icon_dir, name + ".png")
        if not os.path.exists(path):
            print("downloading", name)
            urllib.request.urlretrieve(RAW_URL.format(name), path)


def _droplet(d, cx, cy, r, fill):
    d.polygon([(cx, cy - int(r * 1.55)), (cx - r, cy - int(r * 0.1)),
               (cx + r, cy - int(r * 0.1))], fill=fill)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)


def _thermometer(d, cx, top, bot, w, bulb_r, tube, mercury, lvl=0.55):
    d.rounded_rectangle([cx - w, top, cx + w, bot], radius=w, fill=tube)
    d.ellipse([cx - bulb_r, bot - bulb_r, cx + bulb_r, bot + bulb_r],
              fill=tube)
    iw = int(w * 0.45)
    ml = int(top + (bot - top) * (1 - lvl))
    d.rounded_rectangle([cx - iw, ml, cx + iw, bot], radius=iw, fill=mercury)
    ir = int(bulb_r * 0.62)
    d.ellipse([cx - ir, bot - ir, cx + ir, bot + ir], fill=mercury)


def draw_custom_icons(icon_dir):
    """Draws the widget icons the InkyPi set lacks, in its flat style."""
    BLUE, RED = (30, 110, 220, 255), (230, 40, 40, 255)
    BLACK, WHITE = (0, 0, 0, 255), (255, 255, 255, 255)

    im = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    _droplet(d, 256, 300, 190, BLUE)
    _thermometer(d, 256, 190, 380, 34, 62, WHITE, RED)
    im.save(os.path.join(icon_dir, "dewpoint.png"))

    for name, inner in [("intemp", "temp"), ("inhumidity", "hum")]:
        im = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.polygon([(256, 40), (30, 235), (95, 235), (95, 470), (417, 470),
                   (417, 235), (482, 235)], fill=BLACK)
        if inner == "temp":
            _thermometer(d, 256, 240, 400, 30, 56, WHITE, RED)
        else:
            _droplet(d, 256, 330, 100, WHITE)
            _droplet(d, 256, 330, 74, BLUE)
        im.save(os.path.join(icon_dir, name + ".png"))


def draw_moons(icon_dir):
    """Draws the 8 grayscale moon-phase icons. Phase geometry: the
    terminator is the ellipse x = cos(theta)*sqrt(r^2-y^2) for phase angle
    theta; waxing phases are lit from the right, waning from the left."""
    import math
    S, R = 512, 220
    CX = CY = 256
    LIT, SHADOW, EDGE = 208, 58, 40
    CRATERS = [(-70, -60, 46), (60, 30, 34), (-20, 90, 28), (95, -95, 24),
               (-115, 55, 20)]
    names = ["newmoon", "waxingcrescent", "firstquarter", "waxinggibbous",
             "fullmoon", "waninggibbous", "lastquarter", "waningcrescent"]
    for pidx, name in enumerate(names):
        theta = math.pi * 2 * pidx / 8  # 0 = new, pi = full
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        px = im.load()
        for y in range(S):
            for x in range(S):
                dx, dy = x - CX, y - CY
                d2 = dx * dx + dy * dy
                if d2 > R * R:
                    continue
                half = math.sqrt(R * R - dy * dy)
                xt = math.cos(theta) * half
                if pidx == 0:
                    lit = False
                elif pidx == 4:
                    lit = True
                elif pidx < 4:  # waxing, lit from the right
                    lit = dx >= xt
                else:           # waning, lit from the left
                    lit = dx <= -xt
                g = LIT if lit else SHADOW
                if lit:
                    for cx2, cy2, cr in CRATERS:
                        if (dx - cx2) ** 2 + (dy - cy2) ** 2 <= cr * cr:
                            g = LIT - 40
                            break
                if d2 > (R - 7) * (R - 7):
                    g = EDGE  # outline ring keeps the shape on white
                px[x, y] = (g, g, g, 255)
        im.save(os.path.join(icon_dir, name + ".png"))


def condition_recolor(rgb):
    """Per-pixel recolor for condition icons, tuned on the real panel.
    The InkyPi raindrops share their exact blue with the clouds' shading,
    so color alone cannot separate them -- geometry can: drops, snowflakes
    and fog strokes are small disconnected blobs, cloud bodies are large
    regions. Blue-family pixels are grouped into connected components;
    small components keep a saturated blue ink, large ones (clouds) turn
    neutral gray so they dither gray on the panel. Warm colors (sun,
    lightning) get a saturation boost so the yellows stay strong."""
    px = rgb.load()
    w, h = rgb.size
    blue = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            hh, ss, vv = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if 170 <= hh * 360 <= 260 and ss >= 0.08:
                blue[y][x] = True
            elif ss >= 0.08:  # warm detail: boost saturation
                r2, g2, b2 = colorsys.hsv_to_rgb(hh, min(1.0, ss * 1.8), vv)
                px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255))
    # label 4-connected blue components with BFS
    seen = [[False] * w for _ in range(h)]
    small_cutoff = (w * h) // 60  # blobs under ~1.7% of the icon are "drops"
    for y0 in range(h):
        for x0 in range(w):
            if not blue[y0][x0] or seen[y0][x0]:
                continue
            comp = [(x0, y0)]
            seen[y0][x0] = True
            head = 0
            while head < len(comp):
                cx, cy = comp[head]
                head += 1
                for nx, ny in ((cx-1,cy),(cx+1,cy),(cx,cy-1),(cx,cy+1)):
                    if 0 <= nx < w and 0 <= ny < h and blue[ny][nx]                        and not seen[ny][nx]:
                        seen[ny][nx] = True
                        comp.append((nx, ny))
            drop = len(comp) <= small_cutoff
            for cx, cy in comp:
                if drop:
                    px[cx, cy] = (0, 0, 255)
                else:
                    r, g, b = px[cx, cy]
                    _, ss, vv = colorsys.rgb_to_hsv(r/255, g/255, b/255)
                    lum = int(255 * vv * (1 - 0.35 * ss))
                    px[cx, cy] = (lum, lum, lum)
    return rgb


def quantize(path, size, dither=True, allowed=None, saturation=SATURATION):
    """Returns a list of 4-bit palette indices, row-major. `allowed` limits
    quantization to a subset of PALETTE (list of PALETTE indices)."""
    im = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
    alpha = im.getchannel("A")
    rgb = Image.new("RGB", im.size, (255, 255, 255))
    rgb.paste(im, mask=alpha)
    if saturation is None:  # condition icons: classify, don't boost
        rgb = condition_recolor(rgb)
    else:
        rgb = ImageEnhance.Color(rgb).enhance(saturation)
    subset = allowed if allowed is not None else list(range(len(PALETTE)))
    pal_img = Image.new("P", (1, 1))
    flat = sum([list(PALETTE[i]) for i in subset], [])
    pal_img.putpalette(flat + [0] * (768 - len(flat)))
    q = rgb.quantize(palette=pal_img,
                     dither=Image.FLOYDSTEINBERG if dither else Image.NONE)
    qp, ap = q.load(), alpha.load()
    out = []
    for y in range(size):
        for x in range(size):
            out.append(subset[qp[x, y]] + 1
                       if ap[x, y] >= ALPHA_THRESHOLD else 0)
    return out


def quantize_condition(path, size):
    """Condition-icon quantization: recolor (see condition_recolor), then
    dither the neutral cloud regions against black/white ONLY -- diffusing
    their error through the full palette speckles the gray with colored
    inks -- and the colored regions (rain, sun) against the full palette.
    Returns 4-bit palette indices like quantize()."""
    im = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
    alpha = im.getchannel("A")
    rgb = Image.new("RGB", im.size, (255, 255, 255))
    rgb.paste(im, mask=alpha)
    rgb = condition_recolor(rgb)
    # neutral mask: pixels the recolor left gray
    px = rgb.load()
    neutral = [[px[x, y][0] == px[x, y][1] == px[x, y][2]
                for x in range(size)] for y in range(size)]
    # layer A: grayscale 1-bit dither for the neutral regions
    gray = rgb.convert("L").convert("1")  # PIL defaults to FS dithering
    ga = gray.load()
    # layer B: full-palette dither for the colored regions
    pal_img = Image.new("P", (1, 1))
    flat = sum([list(c) for c in PALETTE], [])
    pal_img.putpalette(flat + [0] * (768 - len(flat)))
    q = rgb.quantize(palette=pal_img, dither=Image.FLOYDSTEINBERG)
    qp, ap = q.load(), alpha.load()
    out = []
    for y in range(size):
        for x in range(size):
            if ap[x, y] < ALPHA_THRESHOLD:
                out.append(0)
            elif neutral[y][x]:
                out.append(2 if ga[x, y] else 1)  # white : black
            else:
                out.append(qp[x, y] + 1)
    return out


def moon_outline(indices, size):
    """White-on-white is invisible: trace a black outline around every lit
    (white) moon pixel that borders transparency."""
    def at(x, y):
        return indices[y * size + x] if 0 <= x < size and 0 <= y < size else 0
    out = list(indices)
    for y in range(size):
        for x in range(size):
            if at(x, y) == 2:  # white
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        if at(x + dx, y + dy) == 0:
                            out[y * size + x] = 1  # black
    # thicken to 2px: repeat once against the new outline
    out2 = list(out)
    for y in range(size):
        for x in range(size):
            if out[y * size + x] == 2:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < size and 0 <= yy < size                            and out[yy * size + xx] == 1                            and any(at(xx + ex, yy + ey) == 0
                                   for ex in (-1, 0, 1) for ey in (-1, 0, 1)):
                            out2[y * size + x] = out2[y * size + x]
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
    icon_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "inkypi_icons")
    fetch_icons(icon_dir)
    draw_custom_icons(icon_dir)
    draw_moons(icon_dir)
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
                data = pack(quantize_condition(path, size))
                total += len(data)
                emit(f, "ci_%s_%d" % (name, size), data)
                print("%s @ %dpx: %d bytes" % (name, size, len(data)))
        for name in WIDGET_ICONS:
            path = os.path.join(icon_dir, name + ".png")
            sat = None if name in SOFT_WIDGET_ICONS else SATURATION
            # 48px for the 5-row widget layout, 40px for the 6-row one
            for wsize in (48, 40):
                data = pack(quantize(path, wsize, dither=True,
                                     saturation=sat))
                total += len(data)
                emit(f, "ci_w_%s_%d" % (name, wsize), data)
                print("%s @ %dpx: %d bytes" % (name, wsize, len(data)))
        f.write("typedef struct {\n"
                "  const char code[8];\n"
                "  const uint8_t *px168;\n"
                "  const uint8_t *px64;\n"
                "  const uint8_t *px48;\n"
                "  const uint8_t *px32;\n"
                "} color_icon_t;\n\n")
        f.write("static const color_icon_t COLOR_ICONS[] = {\n")
        for name in ICONS:
            f.write('  {"%s", ci_%s_168, ci_%s_64, ci_%s_48, ci_%s_32},\n'
                    % (name, name, name, name, name))
        f.write("};\n\n#endif\n")
    with open(MOON_OUT_PATH, 'w', newline='\n') as f:
        f.write('/* Dithered grayscale moon-phase widget icons, drawn by\n'
                ' * tools/generate_color_icons.py (see draw_moons). Pure\n'
                ' * black/white, so unlike icons_color.h these are compiled\n'
                ' * into EVERY panel build. Same 4-bit index format:\n'
                ' * 0 transparent, 1 black, 2 white.\n'
                ' *\n'
                ' * GENERATED FILE -- do not edit by hand.\n'
                ' */\n\n'
                '#ifndef __ICONS_MOON_H__\n'
                '#define __ICONS_MOON_H__\n\n'
                '#include <Arduino.h>\n\n')
        for name in MOON_NAMES:
            path = os.path.join(icon_dir, name + '.png')
            for wsize in (48, 40):
                data = pack(quantize(path, wsize, dither=True,
                                     allowed=MOON_PALETTE))
                total += len(data)
                emit(f, 'moon_%s_%d' % (name, wsize), data)
        f.write('static const uint8_t * const MOON_DITHER_48[8] = {\n')
        for name in MOON_NAMES:
            f.write('  moon_%s_48,\n' % name)
        f.write('};\nstatic const uint8_t * const MOON_DITHER_40[8] = {\n')
        for name in MOON_NAMES:
            f.write('  moon_%s_40,\n' % name)
        f.write('};\n\n#endif\n')
    print("wrote %s (%d bytes of icon data)" % (OUT_PATH, total))



if __name__ == "__main__":
    main()
