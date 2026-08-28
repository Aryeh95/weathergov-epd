#!/usr/bin/env python3
"""Draw a native icon set for the Spectra 6 palette from scratch.

Unlike generate_color_icons.py (which quantizes InkyPi's full-color PNG
artwork down to six inks), this script DRAWS every condition and widget
icon directly in the panel's native inks: flat regions of pure black,
white, red, yellow, green and blue with bold black outlines, plus
grayscale regions that become clean black/white dither. Because the art
never contains a color the panel can't print, quantization is exact --
no per-pixel classification heuristics, no color speckle.

The compositions are designed against the MEASURED ink appearance
(calibrated palette from the epdoptimize project,
https://github.com/paperlesspaper/epdoptimize): real Spectra 6 yellow is
olive, red is brick, blue is navy. The preview sheet this script emits
simulates the icons in those measured colors so it approximates the real
panel, side by side with the current InkyPi-derived set.

Usage:  python tools/generate_native_icons.py [--header]
        default: writes preview PNGs to tools/native_preview/
        --header: also emits icons_native.h in the icons_color.h format
                  (drop-in replacement data for A/B testing on-device)

Requires: pip install pillow
"""
import math
import os
import sys

from PIL import Image, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy  # noqa: E402  (old set, for preview)

S = 512          # master canvas, downsampled with Lanczos
OUTLINE = 14     # master-canvas outline width (about 4px at 168)

# Drawing colors = native device inks (what the panel is told to print).
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
RED = (255, 0, 0, 255)
YELLOW = (255, 255, 0, 255)
GREEN = (0, 128, 0, 255)
BLUE = (0, 0, 255, 255)
# Grays (r==g==b) mark "dither me black/white" regions.
GRAY_LT = (200, 200, 200, 255)   # light cloud shading
GRAY_MD = (150, 150, 150, 255)   # gray cloud body
GRAY_DK = (95, 95, 95, 255)      # dark cloud body / fog

# Measured appearance of the six inks (epdoptimize calibrated Spectra 6
# palette), used ONLY for the preview simulation.
MEASURED = {
    1: (31, 34, 38),      # black  #1F2226
    2: (185, 199, 201),   # white  #B9C7C9
    3: (98, 32, 30),      # red    #62201E
    4: (193, 187, 30),    # yellow #C1BB1E
    5: (53, 86, 58),      # green  #35563A
    6: (35, 63, 142),     # blue   #233F8E
}
PAPER = MEASURED[2]

ICONS = list(legacy.ICONS)
WIDGET_ICONS = list(legacy.WIDGET_ICONS)
SIZES = list(legacy.SIZES)
PREVIEW_DIR = os.path.join(TOOLS_DIR, "native_preview")
NATIVE_DIR = os.path.join(TOOLS_DIR, "native_icons")
HEADER_PATH = os.path.join(TOOLS_DIR, "..", "platformio", "lib",
                           "esp32-weather-epd-assets", "icons",
                           "icons_native.h")


# ---------------------------------------------------------------- shapes

def new_canvas():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def sun(d, cx, cy, r, rays=True, ray_len=None, fill=YELLOW):
    """Disc with triangular rays, black outlined."""
    if rays:
        rl = ray_len if ray_len else int(r * 0.55)
        gap = int(r * 0.22)
        for i in range(8):
            a = math.pi * 2 * i / 8 + math.pi / 8
            bx, by = cx + math.cos(a) * (r + gap), cy + math.sin(a) * (r + gap)
            tx, ty = cx + math.cos(a) * (r + gap + rl), cy + math.sin(a) * (r + gap + rl)
            wx, wy = -math.sin(a) * r * 0.22, math.cos(a) * r * 0.22
            d.polygon([(bx + wx, by + wy), (bx - wx, by - wy), (tx, ty)],
                      fill=YELLOW, outline=BLACK, width=OUTLINE // 2)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill,
              outline=BLACK, width=OUTLINE)


def moon(d, cx, cy, r):
    """Crescent: gray disc minus offset disc; dithers gray on panel."""
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    off = int(r * 0.62)
    md.ellipse([cx - r + off, cy - r - off // 2,
                cx + r + off, cy + r - off // 2], fill=0)
    d._image.paste(Image.new("RGBA", (S, S), GRAY_LT), (0, 0), mask)
    # outline the crescent by stroking the mask edge
    edge = mask.point(lambda v: 255 if v else 0)
    from PIL import ImageFilter
    er = edge.filter(ImageFilter.MaxFilter(OUTLINE | 1))
    ring = Image.composite(Image.new("L", (S, S), 255),
                           Image.new("L", (S, S), 0), er)
    inner = edge.filter(ImageFilter.MinFilter(OUTLINE | 1))
    outline_mask = Image.new("L", (S, S), 0)
    op = outline_mask.load()
    ep, ip = er.load(), inner.load()
    for y in range(S):
        for x in range(S):
            if ep[x, y] and not ip[x, y]:
                op[x, y] = 255
    d._image.paste(Image.new("RGBA", (S, S), BLACK), (0, 0), outline_mask)


CLOUD_LOBES = [(-0.52, 0.10, 0.46), (0.05, -0.18, 0.60),
               (0.55, 0.14, 0.42)]


def cloud(d, cx, cy, sc, body=GRAY_LT, shade=True):
    """Puffy cloud from three lobes on a flat base, black outline.
    Default body is light gray -> a soft B/W dither on the panel (real-ink
    feedback: clouds should read gray, not white)."""
    base_y = cy + int(0.42 * sc)
    # outline pass: draw everything OUTLINE bigger in black
    for pass_fill, grow in ((BLACK, OUTLINE), (body, 0)):
        for lx, ly, lr in CLOUD_LOBES:
            r = int(lr * sc) + grow
            x, y = cx + int(lx * sc), cy + int(ly * sc)
            d.ellipse([x - r, y - r, x + r, y + r], fill=pass_fill)
        d.rounded_rectangle(
            [cx - int(0.86 * sc) - grow, base_y - int(0.30 * sc) - grow,
             cx + int(0.86 * sc) + grow, base_y + grow],
            radius=int(0.16 * sc) + grow, fill=pass_fill)
    if shade and body != GRAY_MD:
        # denser gray under-shade strip along the base
        d.rounded_rectangle(
            [cx - int(0.70 * sc), base_y - int(0.20 * sc),
             cx + int(0.70 * sc), base_y - OUTLINE],
            radius=int(0.10 * sc), fill=GRAY_MD)


def drop(d, cx, cy, r, fill=BLUE):
    d.polygon([(cx, cy - int(r * 1.6)), (cx - r, cy),
               (cx + r, cy)], fill=fill)
    d.ellipse([cx - r, cy - r // 2, cx + r, cy + int(r * 1.4)], fill=fill)


def bolt(d, cx, cy, sc):
    pts = [(cx + int(0.16 * sc), cy - int(0.55 * sc)),
           (cx - int(0.28 * sc), cy + int(0.08 * sc)),
           (cx - int(0.02 * sc), cy + int(0.08 * sc)),
           (cx - int(0.16 * sc), cy + int(0.55 * sc)),
           (cx + int(0.30 * sc), cy - int(0.10 * sc)),
           (cx + int(0.04 * sc), cy - int(0.10 * sc))]
    d.polygon(pts, fill=YELLOW, outline=BLACK, width=OUTLINE // 2)


def flake(d, cx, cy, r):
    for i in range(3):
        a = math.pi * i / 3
        dx, dy = math.cos(a) * r, math.sin(a) * r
        d.line([(cx - dx, cy - dy), (cx + dx, cy + dy)],
               fill=BLUE, width=max(OUTLINE // 2, int(r * 0.28)))
    d.ellipse([cx - r * 0.22, cy - r * 0.22, cx + r * 0.22, cy + r * 0.22],
              fill=BLUE)


# ------------------------------------------------------- condition icons

def draw_condition(name):
    im, d = new_canvas()
    night = name.endswith("n")
    code = name[:-1]

    def celestial(cx, cy, r, rays=True):
        if night:
            moon(d, cx, cy, r)
        else:
            sun(d, cx, cy, r, rays=rays)

    if code == "01":            # clear
        celestial(256, 256, 150)
    elif code == "022":         # mostly sunny: big sun, small cloud
        celestial(240, 230, 140)
        cloud(d, 350, 390, 105)
    elif code == "02":          # partly cloudy: sun behind larger cloud
        celestial(190, 185, 105)
        cloud(d, 290, 315, 165)
    elif code == "03":          # cloudy
        cloud(d, 256, 265, 195)
    elif code == "04":          # overcast: gray cloud behind white cloud
        cloud(d, 320, 195, 140, body=GRAY_MD, shade=False)
        cloud(d, 230, 315, 165)
    elif code == "09":          # showers
        cloud(d, 256, 195, 160)
        for i, (dx, dy) in enumerate([(-120, 0), (-40, 55), (40, 0),
                                      (120, 55)]):
            drop(d, 256 + dx, 380 + dy, 26)
    elif code == "10":          # rain (sun/moon + cloud + drops)
        celestial(170, 165, 92, rays=True)
        cloud(d, 290, 235, 150)
        for dx, dy in [(-80, 0), (10, 45), (100, 0)]:
            drop(d, 266 + dx, 415 + dy, 26)
    elif code == "11":          # thunderstorm
        cloud(d, 256, 165, 155, body=GRAY_MD, shade=False)
        bolt(d, 256, 360, 290)
    elif code == "13":          # snow
        cloud(d, 256, 185, 160)
        for dx, dy in [(-115, 10), (0, 60), (115, 10)]:
            flake(d, 256 + dx, 390 + dy, 52)
    elif code == "50":          # fog
        for i, w in enumerate([0.78, 0.92, 0.84, 0.70]):
            y = 150 + i * 78
            g = GRAY_DK if i % 2 else GRAY_MD
            d.rounded_rectangle([256 - int(220 * w), y,
                                 256 + int(220 * w), y + 44],
                                radius=22, fill=g)
    return im


# ---------------------------------------------------------- widget icons

def draw_widget(name):
    im, d = new_canvas()
    if name in ("sunrise", "sunset"):
        # half sun rising over a horizon line, big arrow above it
        cx, hy, r = 230, 356, 120
        # upward rays only
        for i in range(5):
            a = math.pi * (1 + i / 4)
            bx, by = cx + math.cos(a) * (r + 34), hy + math.sin(a) * (r + 34)
            tx, ty = cx + math.cos(a) * (r + 100), hy + math.sin(a) * (r + 100)
            wx, wy = -math.sin(a) * 26, math.cos(a) * 26
            d.polygon([(bx + wx, by + wy), (bx - wx, by - wy), (tx, ty)],
                      fill=YELLOW, outline=BLACK, width=OUTLINE // 2)
        d.pieslice([cx - r, hy - r, cx + r, hy + r], 180, 360,
                   fill=YELLOW, outline=BLACK, width=OUTLINE)
        d.line([(30, hy), (400, hy)], fill=BLACK, width=OUTLINE + 6)
        up = name == "sunrise"
        ax = 448
        shaft_top, shaft_bot = 120, 330
        d.line([(ax, shaft_top + (60 if up else 0)),
                (ax, shaft_bot - (0 if up else 60))],
               fill=BLACK, width=OUTLINE + 8)
        if up:
            d.polygon([(ax, 90), (ax - 58, 190), (ax + 58, 190)], fill=BLACK)
        else:
            d.polygon([(ax, 330), (ax - 58, 230), (ax + 58, 230)],
                      fill=BLACK)
    elif name == "wind":
        w = OUTLINE + 16
        for y, ln, curl, flip in [(180, 290, 78, False), (280, 380, 92, False),
                                  (380, 250, 66, True)]:
            x0 = 50
            d.line([(x0, y), (x0 + ln, y)], fill=BLACK, width=w)
            bx = x0 + ln
            if flip:  # curl downward
                d.arc([bx - curl, y - w // 2 + 1, bx + curl, y + 2 * curl],
                      start=180, end=450, fill=BLACK, width=w)
            else:     # curl upward
                d.arc([bx - curl, y - 2 * curl, bx + curl, y + w // 2 - 1],
                      start=270, end=180, fill=BLACK, width=w)
    elif name == "humidity":
        drop(d, 256, 240, 150)
        # highlight
        d.ellipse([200, 300, 250, 370], fill=WHITE)
        _outline_alpha(im)
    elif name == "pressure":
        d.ellipse([56, 56, 456, 456], fill=WHITE, outline=BLACK,
                  width=OUTLINE + 14)
        for i in range(7):
            a = math.pi * (0.75 + 1.5 * i / 6)
            x0 = 256 + math.cos(a) * 128
            y0 = 256 + math.sin(a) * 128
            x1 = 256 + math.cos(a) * 172
            y1 = 256 + math.sin(a) * 172
            d.line([(x0, y0), (x1, y1)], fill=BLACK, width=OUTLINE + 8)
        a = math.pi * 1.30
        d.line([(256, 256), (256 + math.cos(a) * 135,
                             256 + math.sin(a) * 135)],
               fill=RED, width=OUTLINE + 16)
        d.ellipse([256 - 40, 256 - 40, 256 + 40, 256 + 40], fill=BLACK)
    elif name == "uvi":
        sun(d, 256, 180, 105, ray_len=52)
        # rising severity bars (index scale) under the sun
        for i, h in enumerate([70, 110, 150]):
            x = 118 + i * 100
            col = [GREEN, YELLOW, RED][i]
            d.rectangle([x, 460 - h, x + 80, 460], fill=col,
                        outline=BLACK, width=OUTLINE - 2)
    elif name == "visibility":
        d.ellipse([56, 156, 456, 356], fill=WHITE)  # placeholder lens
        # eye: two arcs forming a lens shape
        lens = Image.new("L", (S, S), 0)
        ld = ImageDraw.Draw(lens)
        ld.polygon([(40, 256)] + [(256 + int(216 * math.cos(t)),
                                   256 - int(120 * math.sin(t)))
                                  for t in [math.pi * i / 24
                                            for i in range(25)]], fill=255)
        ld.polygon([(40, 256)] + [(256 + int(216 * math.cos(t)),
                                   256 + int(120 * math.sin(t)))
                                  for t in [math.pi * i / 24
                                            for i in range(25)]], fill=255)
        im.paste(Image.new("RGBA", (S, S), WHITE), (0, 0), lens)
        _stroke_mask(im, lens)
        d.ellipse([256 - 92, 256 - 92, 256 + 92, 256 + 92], fill=BLUE,
                  outline=BLACK, width=OUTLINE)
        d.ellipse([256 - 40, 256 - 40, 256 + 40, 256 + 40], fill=BLACK)
        d.ellipse([256 + 8, 256 - 60, 256 + 52, 256 - 16], fill=WHITE)
    elif name == "aqi":
        # leaf with black stem/veins
        leaf = Image.new("L", (S, S), 0)
        ld = ImageDraw.Draw(leaf)
        ld.polygon([(90, 420)] + [(256 + int(200 * math.cos(t)) ,
                                   210 - int(150 * math.sin(t)))
                                  for t in [math.pi * i / 24
                                            for i in range(25)]] + [(90, 420)],
                   fill=255)
        ld.ellipse([120, 90, 440, 350], fill=255)
        ld.polygon([(90, 420), (170, 240), (300, 130)], fill=255)
        im.paste(Image.new("RGBA", (S, S), GREEN), (0, 0), leaf)
        _stroke_mask(im, leaf)
        d.line([(90, 420), (330, 210)], fill=BLACK, width=OUTLINE + 2)
    elif name == "dewpoint":
        drop(d, 300, 250, 135)
        legacy_thermo(d, 150, 130, 380, 34, 60)
        _outline_alpha(im)
    elif name in ("intemp", "inhumidity"):
        d.polygon([(256, 40), (30, 235), (95, 235), (95, 470), (417, 470),
                   (417, 235), (482, 235)], fill=BLACK)
        if name == "intemp":
            legacy_thermo(d, 256, 240, 400, 30, 56)
        else:
            drop(d, 256, 300, 100, fill=WHITE)
            drop(d, 256, 300, 72, fill=BLUE)
    return im


def legacy_thermo(d, cx, top, bot, w, bulb_r):
    d.rounded_rectangle([cx - w, top, cx + w, bot], radius=w, fill=WHITE)
    d.ellipse([cx - bulb_r, bot - bulb_r, cx + bulb_r, bot + bulb_r],
              fill=WHITE)
    iw = int(w * 0.45)
    ml = int(top + (bot - top) * 0.45)
    d.rounded_rectangle([cx - iw, ml, cx + iw, bot], radius=iw, fill=RED)
    ir = int(bulb_r * 0.62)
    d.ellipse([cx - ir, bot - ir, cx + ir, bot + ir], fill=RED)


def _outline_alpha(im):
    """Stroke a black outline around every opaque region of the canvas."""
    a = im.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
    _stroke_mask(im, a)


def _stroke_mask(im, mask):
    from PIL import ImageFilter
    grown = mask.filter(ImageFilter.MaxFilter(OUTLINE | 1))
    shrunk = mask.filter(ImageFilter.MinFilter(OUTLINE | 1))
    gp, sp = grown.load(), shrunk.load()
    px = im.load()
    for y in range(S):
        for x in range(S):
            if gp[x, y] and not sp[x, y]:
                px[x, y] = BLACK


# ------------------------------------------------------- quantize + sim

NATIVE = [(0, 0, 0), (255, 255, 255), (255, 0, 0),
          (255, 255, 0), (0, 128, 0), (0, 0, 255)]


def quantize_native(im, size):
    """Native-art quantization: gray pixels FS-dither black/white, all
    others snap to the nearest ink (they are already inks except for
    anti-aliased edge blends). Returns 4-bit indices like the legacy
    pipeline (0 transparent, 1..6 inks)."""
    im = im.resize((size, size), Image.LANCZOS)
    alpha = im.getchannel("A")
    rgb = Image.new("RGB", im.size, (255, 255, 255))
    rgb.paste(im, mask=alpha)
    px, ap = rgb.load(), alpha.load()
    gray = [[False] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            r, g, b = px[x, y]
            if abs(r - g) < 14 and abs(g - b) < 14 and abs(r - b) < 14 \
               and 20 < r < 235:
                gray[y][x] = True
    g1 = rgb.convert("L").convert("1")   # FS dither for gray regions
    gp = g1.load()
    out = []
    for y in range(size):
        for x in range(size):
            if ap[x, y] < legacy.ALPHA_THRESHOLD:
                out.append(0)
            elif gray[y][x]:
                out.append(2 if gp[x, y] else 1)
            else:
                r, g, b = px[x, y]
                best, bd = 1, 1 << 30
                for i, (pr, pg, pb) in enumerate(NATIVE):
                    dd = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                    if dd < bd:
                        bd, best = dd, i
                out.append(best + 1)
    return out


def simulate(indices, size, scale=1):
    """Render an index array in the MEASURED ink colors on paper."""
    im = Image.new("RGB", (size, size), tuple(PAPER))
    px = im.load()
    for y in range(size):
        for x in range(size):
            idx = indices[y * size + x]
            if idx:
                px[x, y] = tuple(MEASURED[idx])
    if scale != 1:
        im = im.resize((size * scale, size * scale), Image.NEAREST)
    return im


# --------------------------------------------------------------- output

def build_preview():
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    os.makedirs(NATIVE_DIR, exist_ok=True)
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    legacy.draw_custom_icons(icon_dir)

    cell, pad, label_h = 200, 16, 34
    cond_size, wgt_size, wgt_scale = 168, 48, 3

    rows = []
    for name in ICONS:
        art = draw_condition(name)
        art.save(os.path.join(NATIVE_DIR, name + ".png"))
        new_idx = quantize_native(art, cond_size)
        old_idx = legacy.quantize_condition(
            os.path.join(icon_dir, name + ".png"), cond_size)
        rows.append((name, simulate(old_idx, cond_size),
                     simulate(new_idx, cond_size)))
    wrows = []
    for name in WIDGET_ICONS:
        art = draw_widget(name)
        art.save(os.path.join(NATIVE_DIR, name + ".png"))
        new_idx = quantize_native(art, wgt_size)
        sat = None if name in legacy.SOFT_WIDGET_ICONS else legacy.SATURATION
        old_idx = legacy.quantize(os.path.join(icon_dir, name + ".png"),
                                  wgt_size, dither=True, saturation=sat)
        wrows.append((name, simulate(old_idx, wgt_size, wgt_scale),
                      simulate(new_idx, wgt_size, wgt_scale)))

    cols = 2
    per_row = 4  # icons per sheet row (name + old + new = one unit)
    units = rows + wrows
    unit_w = cell * 2 + pad
    sheet_w = per_row * (unit_w + pad * 2) + pad
    n_lines = (len(units) + per_row - 1) // per_row
    sheet_h = pad + n_lines * (cell + label_h + pad)
    sheet = Image.new("RGB", (sheet_w, sheet_h), tuple(PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, (name, old_im, new_im) in enumerate(units):
        gx = pad + (i % per_row) * (unit_w + pad * 2)
        gy = pad + (i // per_row) * (cell + label_h + pad)
        sd.text((gx + 4, gy + 2), name + "  (old | new)", fill=(0, 0, 0))
        for j, cim in enumerate((old_im, new_im)):
            ox = gx + j * (cell + pad)
            # center in cell
            cx = ox + (cell - cim.width) // 2
            cy = gy + label_h + (cell - label_h - cim.height) // 2
            sheet.paste(cim, (cx, max(cy, gy + label_h)))
        sd.rectangle([gx - 4, gy, gx + unit_w + 4, gy + cell + label_h - 6],
                     outline=(120, 130, 132))
    out = os.path.join(PREVIEW_DIR, "native_vs_inkypi.png")
    sheet.save(out)
    print("wrote", out)
    return out


def emit_header():
    total = 0
    with open(HEADER_PATH, "w", newline="\n") as f:
        f.write(
"""/* Native-drawn full-color weather icons for multicolor e-paper panels.
 *
 * GENERATED FILE -- do not edit by hand; regenerate with
 *   python tools/generate_native_icons.py --header
 *
 * Unlike icons_color.h (InkyPi artwork quantized to six inks), these
 * icons are drawn directly in the panel's native inks with bold black
 * outlines; grays are Floyd-Steinberg dithered black/white. Same data
 * format as icons_color.h: 4-bit palette indices, two pixels per byte
 * (high nibble first), row-major; index 0 transparent, 1..6 = black,
 * white, red, yellow, green, blue.
 */

#ifndef __ICONS_NATIVE_H__
#define __ICONS_NATIVE_H__

#include <Arduino.h>

""")
        for name in ICONS:
            art = draw_condition(name)
            for size in SIZES:
                data = legacy.pack(quantize_native(art, size))
                total += len(data)
                legacy.emit(f, "ni_%s_%d" % (name, size), data)
        for name in WIDGET_ICONS:
            art = draw_widget(name)
            for wsize in (48, 40):
                data = legacy.pack(quantize_native(art, wsize))
                total += len(data)
                legacy.emit(f, "ni_w_%s_%d" % (name, wsize), data)
        f.write("typedef struct {\n"
                "  const char code[8];\n"
                "  const uint8_t *px168;\n"
                "  const uint8_t *px64;\n"
                "  const uint8_t *px48;\n"
                "  const uint8_t *px32;\n"
                "} native_icon_t;\n\n")
        f.write("static const native_icon_t NATIVE_ICONS[] = {\n")
        for name in ICONS:
            f.write('  {"%s", ni_%s_168, ni_%s_64, ni_%s_48, ni_%s_32},\n'
                    % (name, name, name, name, name))
        f.write("};\n\n#endif\n")
    print("wrote %s (%d bytes)" % (HEADER_PATH, total))


if __name__ == "__main__":
    build_preview()
    if "--header" in sys.argv:
        emit_header()
