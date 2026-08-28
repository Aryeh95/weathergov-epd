#!/usr/bin/env python3
"""Modern (Material-rounded style) widget icons, drawn in native inks.

Filled silhouettes and thick round-capped strokes, no outline-around-fill
-- the Google Material Symbols look, drawn from scratch so the set is
ours. Emits a comparison sheet against the InkyPi widget set.

Usage: python tools/generate_modern_widgets.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy    # noqa: E402
import generate_native_icons as native   # noqa: E402

S = native.S
BLACK, WHITE = native.BLACK, native.WHITE
RED, YELLOW = native.RED, native.YELLOW
GREEN, BLUE = native.GREEN, native.BLUE

MODERN_DIR = os.path.join(TOOLS_DIR, "modern_widgets")


def rline(d, p0, p1, width, fill=BLACK):
    """Round-capped stroke."""
    d.line([p0, p1], fill=fill, width=width)
    for x, y in (p0, p1):
        d.ellipse([x - width / 2, y - width / 2,
                   x + width / 2, y + width / 2], fill=fill)


def rarc(d, box, start, end, width, fill=BLACK):
    d.arc(box, start=start, end=end, fill=fill, width=width)
    for ang in (start, end):
        a = math.radians(ang)
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        rx, ry = (box[2] - box[0]) / 2 - width / 2, \
            (box[3] - box[1]) / 2 - width / 2
        x, y = cx + math.cos(a) * (rx + width / 2 - width / 2), \
            cy + math.sin(a) * (ry + width / 2 - width / 2)
        d.ellipse([x - width / 2, y - width / 2,
                   x + width / 2, y + width / 2], fill=fill)


def drop_shape(d, cx, cy, r, fill=BLUE):
    d.polygon([(cx, cy - int(r * 1.45)), (cx - int(r * 0.86), cy),
               (cx + int(r * 0.86), cy)], fill=fill)
    d.ellipse([cx - r, cy - r // 2, cx + r, cy + int(r * 1.45)], fill=fill)


def sun_disc(d, cx, cy, r, ray_len, ray_w, n=8, a0=None):
    for i in range(n):
        a = math.pi * 2 * i / n + (a0 if a0 is not None else math.pi / n)
        gap = int(r * 0.34)
        rline(d, (cx + math.cos(a) * (r + gap), cy + math.sin(a) * (r + gap)),
              (cx + math.cos(a) * (r + gap + ray_len),
               cy + math.sin(a) * (r + gap + ray_len)), ray_w, YELLOW)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=YELLOW)


def new_canvas():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def draw_modern(name):
    im, d = new_canvas()
    W = 44  # standard stroke
    if name in ("sunrise", "sunset"):
        up = name == "sunrise"
        # half sun over horizon, short rays, chevron arrow above
        hy = 384
        cx, r = 256, 118
        for i in range(5):
            a = math.pi * (1 + i / 4)
            gap, rl = 40, 62
            rline(d, (cx + math.cos(a) * (r + gap),
                      hy + math.sin(a) * (r + gap)),
                  (cx + math.cos(a) * (r + gap + rl),
                   hy + math.sin(a) * (r + gap + rl)), 34, YELLOW)
        d.pieslice([cx - r, hy - r, cx + r, hy + r], 180, 360, fill=YELLOW)
        rline(d, (48, hy), (464, hy), 36)
        # chevron
        ay = 118 if up else 172
        tip = (256, ay) if up else (256, ay + 54)
        if up:
            rline(d, (256 - 74, ay + 60), (256, ay), 40)
            rline(d, (256, ay), (256 + 74, ay + 60), 40)
        else:
            rline(d, (256 - 74, ay), (256, ay + 60), 40)
            rline(d, (256, ay + 60), (256 + 74, ay), 40)
    elif name == "wind":
        # two breeze strokes ending in spirals
        rline(d, (56, 200), (330, 200), W)
        rarc(d, [300, 96, 404, 200], 270, 90, W)
        rarc(d, [326, 122, 378, 174], 90, 270, W)
        rline(d, (56, 330), (330, 330), W)
        rarc(d, [300, 330, 404, 434], 270, 90, W)
        rarc(d, [326, 356, 378, 408], 90, 270, W)
    elif name == "humidity":
        drop_shape(d, 256, 236, 158)
        # percent mark in white
        d.ellipse([196, 236, 248, 288], outline=WHITE, width=26)
        d.ellipse([268, 320, 320, 372], outline=WHITE, width=26)
        rline(d, (306, 244), (212, 366), 26, WHITE)
    elif name == "pressure":
        rarc(d, [76, 76, 436, 436], 135, 45, W)
        for adeg in (135, 202, 270, 338, 45):
            a = math.radians(adeg)
            x0 = 256 + math.cos(a) * 122
            y0 = 256 + math.sin(a) * 122
            d.ellipse([x0 - 14, y0 - 14, x0 + 14, y0 + 14], fill=BLACK)
        a = math.radians(210)
        rline(d, (256, 256), (256 + math.cos(a) * 132,
                              256 + math.sin(a) * 132), 38, RED)
        d.ellipse([256 - 34, 256 - 34, 256 + 34, 256 + 34], fill=BLACK)
    elif name == "uvi":
        # sun with upper rays only, clear of the meter below
        sun_disc(d, 256, 170, 96, 52, 32, n=8, a0=math.pi / 8)
        d.rectangle([0, 330, S, S], fill=(0, 0, 0, 0))
        # rainbow severity meter under the sun (arc over the top half)
        box = [96, 360, 416, 680]
        rarc(d, box, 180, 235, 36, GREEN)
        rarc(d, box, 245, 295, 36, YELLOW)
        rarc(d, box, 305, 360, 36, RED)
    elif name == "visibility":
        # material eye: filled almond, blue iris, white glint
        lens = Image.new("L", (S, S), 0)
        ld = ImageDraw.Draw(lens)
        pts_top = [(256 + int(212 * math.cos(t)),
                    256 - int(140 * math.sin(t)))
                   for t in [math.pi * i / 24 for i in range(25)]]
        pts_bot = [(256 + int(212 * math.cos(t)),
                    256 + int(140 * math.sin(t)))
                   for t in [math.pi * i / 24 for i in range(25)]]
        ld.polygon(pts_top + pts_bot[::-1], fill=255)
        im.paste(Image.new("RGBA", (S, S), BLACK), (0, 0), lens)
        d.ellipse([256 - 104, 256 - 104, 256 + 104, 256 + 104], fill=WHITE)
        d.ellipse([256 - 86, 256 - 86, 256 + 86, 256 + 86], fill=BLUE)
        d.ellipse([256 - 40, 256 - 40, 256 + 40, 256 + 40], fill=BLACK)
        d.ellipse([256 + 14, 256 - 62, 256 + 58, 256 - 18], fill=WHITE)
    elif name == "aqi":
        # material eco leaf
        # leaf = lens (intersection of two discs) on the 45-degree diagonal
        from PIL import ImageChops
        c1 = Image.new("L", (S, S), 0)
        ImageDraw.Draw(c1).ellipse([-110, -110, 430, 430], fill=255)
        c2 = Image.new("L", (S, S), 0)
        ImageDraw.Draw(c2).ellipse([82, 82, 622, 622], fill=255)
        leaf = ImageChops.multiply(c1, c2)
        im.paste(Image.new("RGBA", (S, S), GREEN), (0, 0), leaf)
        # stem: short green round stroke past the lower tip
        rline(d, (100, 412), (52, 460), 34, GREEN)
        # central vein: a white bezier from stem tip to leaf tip
        p0, p1, p2 = (104, 408), (256, 300), (408, 104)
        prev = p0
        for i in range(1, 21):
            t = i / 20
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
            rline(d, prev, (x, y), 28, WHITE)
            prev = (x, y)
    elif name == "dewpoint":
        drop_shape(d, 318, 250, 128)
        # thermometer beside it
        rline(d, (140, 120), (140, 350), 58, BLACK)
        rline(d, (140, 160), (140, 348), 26, WHITE)
        rline(d, (140, 250), (140, 348), 26, RED)
        d.ellipse([140 - 62, 350 - 30, 140 + 62, 350 + 94], fill=BLACK)
        d.ellipse([140 - 40, 350 - 8, 140 + 40, 350 + 72], fill=RED)
    elif name in ("intemp", "inhumidity"):
        # rounded house silhouette
        house = Image.new("L", (S, S), 0)
        hd = ImageDraw.Draw(house)
        hd.polygon([(256, 52), (36, 244), (476, 244)], fill=255)
        hd.rounded_rectangle([92, 210, 420, 470], radius=36, fill=255)
        # rounded ridge
        hd.ellipse([236, 36, 276, 76], fill=255)
        im.paste(Image.new("RGBA", (S, S), BLACK), (0, 0), house)
        if name == "intemp":
            rline(d, (256, 250), (256, 380), 46, WHITE)
            rline(d, (256, 300), (256, 380), 22, RED)
            d.ellipse([256 - 48, 380 - 22, 256 + 48, 380 + 74], fill=WHITE)
            d.ellipse([256 - 32, 380 - 6, 256 + 32, 380 + 58], fill=RED)
        else:
            drop_shape(d, 256, 340, 86, fill=WHITE)
            drop_shape(d, 256, 340, 64, fill=BLUE)
    return im


def main():
    os.makedirs(MODERN_DIR, exist_ok=True)
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    legacy.draw_custom_icons(icon_dir)
    wgt, scale = 48, 3
    CELL, PAD, LABEL = wgt * scale + 24, 16, 34
    units = []
    for name in legacy.WIDGET_ICONS:
        sat = None if name in legacy.SOFT_WIDGET_ICONS else legacy.SATURATION
        old_idx = legacy.quantize(os.path.join(icon_dir, name + ".png"),
                                  wgt, dither=True, saturation=sat)
        art = draw_modern(name)
        art.save(os.path.join(MODERN_DIR, name + ".png"))
        mod_idx = native.quantize_native(art, wgt)
        units.append((name, [native.simulate(i, wgt, scale) for i in
                             (old_idx, mod_idx)]))
    per_row = 4
    unit_w = CELL * 2 + PAD
    sheet_w = per_row * (unit_w + PAD * 2) + PAD
    n_lines = (len(units) + per_row - 1) // per_row
    sheet_h = PAD + n_lines * (CELL + LABEL + PAD)
    sheet = Image.new("RGB", (sheet_w, sheet_h), tuple(native.PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, (name, ims) in enumerate(units):
        gx = PAD + (i % per_row) * (unit_w + PAD * 2)
        gy = PAD + (i // per_row) * (CELL + LABEL + PAD)
        sd.text((gx + 4, gy + 2), name + "  (inkypi | modern)",
                fill=(0, 0, 0))
        for j, cim in enumerate(ims):
            ox = gx + j * (CELL + PAD)
            sheet.paste(cim, (ox + (CELL - cim.width) // 2,
                              gy + LABEL + (CELL - LABEL - cim.height) // 2
                              + LABEL // 2))
        sd.rectangle([gx - 4, gy, gx + unit_w + 4, gy + CELL + LABEL - 6],
                     outline=(120, 130, 132))
    out = os.path.join(native.PREVIEW_DIR, "widgets_modern.png")
    os.makedirs(native.PREVIEW_DIR, exist_ok=True)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
