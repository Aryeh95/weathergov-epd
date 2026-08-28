#!/usr/bin/env python3
"""Hybrid condition icons: Google's rounded geometry, native technique.

Reuses generate_native_icons.py (flat inks, bold black outlines, grays
that dither black/white) but overrides the shape vocabulary with
Google-weather-style geometry: blobby round clouds with no flat base,
suns with round-capped bar rays, fat raindrops, thick crescents. All
shapes are drawn by this script -- no Google artwork is used, so the set
is ours to vendor.

Usage: python tools/generate_hybrid_icons.py
       writes tools/native_preview/four_way_conditions.png and saves the
       hybrid master PNGs to tools/hybrid_icons/
"""
import math
import os
import sys

from PIL import Image, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy        # noqa: E402
import generate_native_icons as native       # noqa: E402
import generate_google_preview as gpre       # noqa: E402

S = native.S
OUTLINE = native.OUTLINE
BLACK, WHITE = native.BLACK, native.WHITE
YELLOW, BLUE = native.YELLOW, native.BLUE
GRAY_LT, GRAY_MD = native.GRAY_LT, native.GRAY_MD

HYBRID_DIR = os.path.join(TOOLS_DIR, "hybrid_icons")


# ------------------------------------------------ Google-ish shapes

def sun_h(d, cx, cy, r, rays=True, ray_len=None, fill=YELLOW):
    """Disc with round-capped bar rays (Google style)."""
    if rays:
        rl = ray_len if ray_len else int(r * 0.42)
        gap = int(r * 0.30)
        w = max(OUTLINE + 10, int(r * 0.22))
        for i in range(8):
            a = math.pi * 2 * i / 8 + math.pi / 8
            x0, y0 = cx + math.cos(a) * (r + gap), cy + math.sin(a) * (r + gap)
            x1, y1 = cx + math.cos(a) * (r + gap + rl), \
                cy + math.sin(a) * (r + gap + rl)
            # black outline pass then yellow fill pass, round caps
            for col, grow in ((BLACK, OUTLINE), (YELLOW, 0)):
                ww = w + grow
                d.line([(x0, y0), (x1, y1)], fill=col, width=ww)
                for xx, yy in ((x0, y0), (x1, y1)):
                    d.ellipse([xx - ww / 2, yy - ww / 2,
                               xx + ww / 2, yy + ww / 2], fill=col)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill,
              outline=BLACK, width=OUTLINE)


def moon_h(d, cx, cy, r):
    """Thick crescent, gray dither fill, bold outline. Drawn slightly
    smaller and higher than the native moon so the lower horn tucks
    behind an overlapping cloud instead of hooking out under it."""
    r = int(r * 0.94)
    cy -= int(r * 0.20)
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    off = int(r * 0.52)   # smaller offset -> fatter crescent than native
    md.ellipse([cx - r + off, cy - r - off // 2,
                cx + r + off, cy + r - off // 2], fill=0)
    d._image.paste(Image.new("RGBA", (S, S), GRAY_LT), (0, 0), mask)
    native._stroke_mask(d._image, mask)


# Google clouds: one dominant lobe with a smaller side lobe, everything
# round -- no flat base.
HYB_LOBES = [(-0.42, 0.16, 0.50), (0.12, -0.14, 0.62),
             (0.52, 0.22, 0.44), (-0.02, 0.28, 0.52)]


def cloud_h(d, cx, cy, sc, body=GRAY_LT, shade=True):
    from PIL import ImageChops
    for pass_fill, grow in ((BLACK, OUTLINE), (body, 0)):
        for lx, ly, lr in HYB_LOBES:
            r = int(lr * sc) + grow
            x, y = cx + int(lx * sc), cy + int(ly * sc)
            d.ellipse([x - r, y - r, x + r, y + r], fill=pass_fill)
    if shade and body != GRAY_MD:
        # darker belly, clipped to the cloud interior
        inner = Image.new("L", (S, S), 0)
        idr = ImageDraw.Draw(inner)
        for lx, ly, lr in HYB_LOBES:
            r = int(lr * sc) - OUTLINE
            x, y = cx + int(lx * sc), cy + int(ly * sc)
            idr.ellipse([x - r, y - r, x + r, y + r], fill=255)
        belly = Image.new("L", (S, S), 0)
        bdr = ImageDraw.Draw(belly)
        r = int(0.52 * sc)
        x, y = cx - int(0.02 * sc), cy + int(0.44 * sc)
        bdr.ellipse([x - r, y - r // 2, x + r, y + r], fill=255)
        clip = ImageChops.multiply(inner, belly)
        d._image.paste(Image.new("RGBA", (S, S), GRAY_MD), (0, 0), clip)


def drop_h(d, cx, cy, r, fill=BLUE):
    """Fat Google-style drop: nearly circular with a short point."""
    d.polygon([(cx, cy - int(r * 1.35)), (cx - int(r * 0.82), cy - r // 4),
               (cx + int(r * 0.82), cy - r // 4)], fill=fill)
    d.ellipse([cx - r, cy - r // 2, cx + r, cy + int(r * 1.5)], fill=fill)


def flake_h(d, cx, cy, r):
    w = max(OUTLINE // 2 + 4, int(r * 0.30))
    for i in range(3):
        a = math.pi * i / 3 + math.pi / 6
        dx, dy = math.cos(a) * r, math.sin(a) * r
        d.line([(cx - dx, cy - dy), (cx + dx, cy + dy)], fill=BLUE, width=w)
    d.ellipse([cx - r * 0.26, cy - r * 0.26, cx + r * 0.26, cy + r * 0.26],
              fill=BLUE)


def _with_hybrid_shapes(fn, *args):
    saved = (native.sun, native.moon, native.cloud, native.drop, native.flake)
    native.sun, native.moon = sun_h, moon_h
    native.cloud, native.drop, native.flake = cloud_h, drop_h, flake_h
    try:
        return fn(*args)
    finally:
        (native.sun, native.moon, native.cloud,
         native.drop, native.flake) = saved


def draw_hybrid(code):
    """Render one condition icon with the hybrid shape set by temporarily
    overriding the native module's shape functions."""
    return _with_hybrid_shapes(native.draw_condition, code)


def draw_hybrid_widget(name):
    """Widget icons with the same rounded shape treatment (fat drops,
    round-capped ray suns); shapes without a hybrid variant fall through
    to the native drawing unchanged."""
    return _with_hybrid_shapes(native.draw_widget, name)


def main():
    os.makedirs(HYBRID_DIR, exist_ok=True)
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    COND, CELL, PAD, LABEL = 168, 200, 16, 34
    units = []
    for code in legacy.ICONS:
        old_idx = legacy.quantize_condition(
            os.path.join(icon_dir, code + ".png"), COND)
        nat_idx = native.quantize_native(native.draw_condition(code), COND)
        hyb = draw_hybrid(code)
        hyb.save(os.path.join(HYBRID_DIR, code + ".png"))
        hyb_idx = native.quantize_native(hyb, COND)
        goo_idx = legacy.quantize_condition(
            gpre.flatten(gpre.GOOGLE_MAP[code]), COND)
        units.append((code, [native.simulate(i, COND) for i in
                             (old_idx, nat_idx, hyb_idx, goo_idx)]))

    per_row = 2
    unit_w = CELL * 4 + PAD * 3
    sheet_w = per_row * (unit_w + PAD * 2) + PAD
    n_lines = (len(units) + per_row - 1) // per_row
    sheet_h = PAD + n_lines * (CELL + LABEL + PAD)
    sheet = Image.new("RGB", (sheet_w, sheet_h), tuple(native.PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, (code, ims) in enumerate(units):
        gx = PAD + (i % per_row) * (unit_w + PAD * 2)
        gy = PAD + (i // per_row) * (CELL + LABEL + PAD)
        sd.text((gx + 4, gy + 2),
                code + "  (inkypi | native | HYBRID | google)", fill=(0, 0, 0))
        for j, cim in enumerate(ims):
            ox = gx + j * (CELL + PAD)
            sheet.paste(cim, (ox + (CELL - cim.width) // 2,
                              gy + LABEL + (CELL - LABEL - cim.height) // 2
                              + LABEL // 2))
        sd.rectangle([gx - 4, gy, gx + unit_w + 4, gy + CELL + LABEL - 6],
                     outline=(120, 130, 132))
    out = os.path.join(native.PREVIEW_DIR, "four_way_conditions.png")
    os.makedirs(native.PREVIEW_DIR, exist_ok=True)
    sheet.save(out)
    print("wrote", out)
    widget_sheet(icon_dir)
    hourly_strip()


def widget_sheet(icon_dir, wgt=48, scale=3):
    """InkyPi | native | hybrid comparison for the widget icons."""
    legacy.draw_custom_icons(icon_dir)
    CELL, PAD, LABEL = wgt * scale + 24, 16, 34
    units = []
    for name in legacy.WIDGET_ICONS:
        sat = None if name in legacy.SOFT_WIDGET_ICONS else legacy.SATURATION
        old_idx = legacy.quantize(os.path.join(icon_dir, name + ".png"),
                                  wgt, dither=True, saturation=sat)
        nat_idx = native.quantize_native(native.draw_widget(name), wgt)
        hyb = draw_hybrid_widget(name)
        hyb.save(os.path.join(HYBRID_DIR, "w_" + name + ".png"))
        hyb_idx = native.quantize_native(hyb, wgt)
        units.append((name, [native.simulate(i, wgt, scale) for i in
                             (old_idx, nat_idx, hyb_idx)]))
    per_row = 3
    unit_w = CELL * 3 + PAD * 2
    sheet_w = per_row * (unit_w + PAD * 2) + PAD
    n_lines = (len(units) + per_row - 1) // per_row
    sheet_h = PAD + n_lines * (CELL + LABEL + PAD)
    sheet = Image.new("RGB", (sheet_w, sheet_h), tuple(native.PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, (name, ims) in enumerate(units):
        gx = PAD + (i % per_row) * (unit_w + PAD * 2)
        gy = PAD + (i // per_row) * (CELL + LABEL + PAD)
        sd.text((gx + 4, gy + 2),
                name + "  (inkypi | native | hybrid)", fill=(0, 0, 0))
        for j, cim in enumerate(ims):
            ox = gx + j * (CELL + PAD)
            sheet.paste(cim, (ox + (CELL - cim.width) // 2,
                              gy + LABEL + (CELL - LABEL - cim.height) // 2
                              + LABEL // 2))
        sd.rectangle([gx - 4, gy, gx + unit_w + 4, gy + CELL + LABEL - 6],
                     outline=(120, 130, 132))
    out = os.path.join(native.PREVIEW_DIR, "widgets_three_way.png")
    sheet.save(out)
    print("wrote", out)


def hourly_strip(size=32, scale=4):
    """The 24-hour graph runs the 32px size: show the native (outline)
    icons that slot there under the hybrid-everywhere-else plan."""
    PAD = 12
    codes = legacy.ICONS
    cell = size * scale
    sheet = Image.new("RGB",
                      (PAD + len(codes) * (cell + PAD), cell + PAD * 2 + 20),
                      tuple(native.PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, code in enumerate(codes):
        idx = native.quantize_native(native.draw_condition(code), size)
        im = native.simulate(idx, size, scale)
        x = PAD + i * (cell + PAD)
        sheet.paste(im, (x, PAD + 20))
        sd.text((x, 4), code, fill=(0, 0, 0))
    out = os.path.join(native.PREVIEW_DIR, "hourly_32px_native.png")
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
