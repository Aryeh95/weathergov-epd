#!/usr/bin/env python3
"""Three-way condition-icon comparison: InkyPi | native-drawn | Google.

Google artwork from https://github.com/mrdarrengriffin/google-weather-icons
(sets/set-2, 192px PNGs -- Google's property; preview/personal use only,
do not vendor). Icons are quantized with the same split-layer pipeline as
the InkyPi set and simulated in the measured Spectra 6 ink colors.

Usage: python tools/generate_google_preview.py
"""
import os
import sys

from PIL import Image, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy      # noqa: E402
import generate_native_icons as native     # noqa: E402

GOOGLE_DIR = os.path.join(TOOLS_DIR, "google_icons")
GOOGLE_MAP = {
    "01d": "sunny", "01n": "clear_night",
    "022d": "mostly_sunny", "022n": "mostly_clear_night",
    "02d": "partly_cloudy", "02n": "partly_cloudy_night",
    "03d": "mostly_cloudy_day", "04d": "cloudy",
    "09d": "showers_rain",
    "10d": "scattered_showers_day", "10n": "scattered_showers_night",
    "11d": "strong_tstorms", "13d": "snow_showers_snow",
    "50d": "haze_fog_dust_smoke",
}

COND = 168
PAD, LABEL = 16, 34
CELL = 200


def flatten(name):
    """Google set-2 PNGs are pale pastels drawn for colored app
    backgrounds -- quantized as-is they dither to ghosts on white paper.
    Deepen the neutral tones into printable grays and boost the muted
    colors before handing them to the quantizer."""
    import colorsys
    im = Image.open(os.path.join(GOOGLE_DIR, name + ".png")).convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 16:
                continue
            hh, ss, vv = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if ss < 0.16:   # neutral: compress toward mid-gray
                vv = 0.28 + vv * 0.52
                r, g, b = (int(vv * 255),) * 3
            else:           # colored: saturate and darken slightly
                r2, g2, b2 = colorsys.hsv_to_rgb(
                    hh, min(1.0, ss * 1.9), min(1.0, vv * 1.05))
                r, g, b = int(r2 * 255), int(g2 * 255), int(b2 * 255)
            px[x, y] = (r, g, b, a)
    out = os.path.join(GOOGLE_DIR, "_rgba_" + name + ".png")
    im.save(out)
    return out


def main():
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    units = []
    for code in legacy.ICONS:
        old_idx = legacy.quantize_condition(
            os.path.join(icon_dir, code + ".png"), COND)
        nat_idx = native.quantize_native(native.draw_condition(code), COND)
        gpath = flatten(GOOGLE_MAP[code])
        goo_idx = legacy.quantize_condition(gpath, COND)
        units.append((code,
                      native.simulate(old_idx, COND),
                      native.simulate(nat_idx, COND),
                      native.simulate(goo_idx, COND)))

    per_row = 3
    unit_w = CELL * 3 + PAD * 2
    sheet_w = per_row * (unit_w + PAD * 2) + PAD
    n_lines = (len(units) + per_row - 1) // per_row
    sheet_h = PAD + n_lines * (CELL + LABEL + PAD)
    sheet = Image.new("RGB", (sheet_w, sheet_h), tuple(native.PAPER))
    sd = ImageDraw.Draw(sheet)
    for i, (code, a, b, c) in enumerate(units):
        gx = PAD + (i % per_row) * (unit_w + PAD * 2)
        gy = PAD + (i // per_row) * (CELL + LABEL + PAD)
        sd.text((gx + 4, gy + 2),
                code + "  (inkypi | native | google)", fill=(0, 0, 0))
        for j, cim in enumerate((a, b, c)):
            ox = gx + j * (CELL + PAD)
            cx = ox + (CELL - cim.width) // 2
            cy = gy + LABEL + (CELL - LABEL - cim.height) // 2
            sheet.paste(cim, (cx, max(cy, gy + LABEL)))
        sd.rectangle([gx - 4, gy, gx + unit_w + 4, gy + CELL + LABEL - 6],
                     outline=(120, 130, 132))
    out = os.path.join(native.PREVIEW_DIR, "three_way_conditions.png")
    os.makedirs(native.PREVIEW_DIR, exist_ok=True)
    sheet.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
