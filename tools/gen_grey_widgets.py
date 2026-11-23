#!/usr/bin/env python3
"""Generate 4-bit nibble-packed multi-tone greyscale widget icons (48x48 and
40x40) for GREYS_75.

Palette codes per nibble (high nibble = even x, low nibble = odd x):
  0: transparent (background)
  1: GxEPD_BLACK
  2: G_DG       (GxEPD_DARKGREY)
  3: G_LG       (GxEPD_LIGHTGREY)
  4: G75_LGW    (light grey / white 50% checker)
  5: G75_LGW4   (light grey / white 25% checker)
  6: GxEPD_WHITE

Writes platformio/lib/esp32-weather-epd-assets/icons/icons_widgets_grey.h.

    pip install pymupdf pillow numpy
"""
import math
import os
import numpy as np
import pymupdf
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SVG_DIR = os.path.join(ROOT, "icons", "svg")
OUT = os.path.join(
    ROOT, "platformio", "lib", "esp32-weather-epd-assets", "icons",
    "icons_widgets_grey.h",
)
SS = 8

# Internal composition color codes (mapped to nibble palette at pack time)
C_BLK = 0
C_DG = 1
C_LG = 2
C_WHT = 3
C_LGW = 4
C_LGW4 = 5

TO_NIBBLE = {
    C_WHT: 0,
    C_BLK: 1,
    C_DG: 2,
    C_LG: 3,
    C_LGW: 4,
    C_LGW4: 5,
}


def svg_cov(svg_str, size, ss=SS):
    """Rasterize an SVG string to (size, size) float coverage in [0, 1]."""
    doc = pymupdf.open(stream=svg_str.encode("utf-8"), filetype="svg")
    page = doc[0]
    res = size * ss
    z = res / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(z, z),
                          colorspace=pymupdf.csGRAY, alpha=False)
    im = Image.frombytes("L", (pix.width, pix.height), pix.samples)
    if (pix.width, pix.height) != (res, res):
        im = im.resize((res, res), Image.BOX)
    hr = 1.0 - np.array(im, dtype=np.float32) / 255.0
    return hr.reshape(size, ss, size, ss).mean(axis=(1, 3))


def svg_file_cov(filename, size, ss=SS):
    path = os.path.join(SVG_DIR, filename)
    return svg_cov(open(path, encoding="utf-8").read(), size, ss=ss)


def composite_layers(size, layers):
    """Composite ordered vector layers onto a white canvas with greyscale AA."""
    grid = np.full((size, size), C_WHT, dtype=np.uint8)
    for item in layers:
        cov, col = item[0], item[1]
        if col in (C_LG, C_LGW, C_LGW4):
            th = item[2] if len(item) > 2 else 0.45
            grid[cov >= th] = col
        elif col == C_DG:
            lo = item[2] if len(item) > 2 else 0.35
            hi = item[3] if len(item) > 3 else 0.72
            grid[(cov >= lo) & (cov < hi)] = C_LG
            grid[cov >= hi] = C_DG
        elif col == C_BLK:
            lo = item[2] if len(item) > 2 else 0.35
            hi = item[3] if len(item) > 3 else 0.80
            grid[(cov >= lo) & (cov < hi) & (grid == C_WHT)] = C_LG
            grid[(cov >= lo) & (cov < hi) & (grid != C_WHT) & (grid != C_LG)] = C_DG
            grid[cov >= hi] = C_BLK
        elif col == C_WHT:
            th = item[2] if len(item) > 2 else 0.50
            grid[cov >= th] = C_WHT
    return grid


# ---------------------------------------------------------------------------
# Selected 4-greyscale widget recipes (each works cleanly at both 40x40 and 48x48)
# ---------------------------------------------------------------------------
def build_pollen(size):
    """Option B — Dandelion Seedhead + Wind Wisps."""
    cx, cy = 11.4, 12.2
    spokes, tips = [], []
    for k in range(11):
        deg = k * (360.0 / 11.0) + 12.0
        if 350 <= deg % 360 or deg % 360 <= 38:
            continue
        ang = math.radians(deg)
        x1, y1 = cx + 2.2 * math.cos(ang), cy + 2.2 * math.sin(ang)
        x2, y2 = cx + 6.9 * math.cos(ang), cy + 6.9 * math.sin(ang)
        spokes.append(
            f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}"/>'
        )
        tips.append(f'<circle cx="{x2:.2f}" cy="{y2:.2f}" r="0.95"/>')

    svg_halo = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<circle cx="{cx}" cy="{cy}" r="7.5" fill="#000"/></svg>'
    )
    svg_spokes = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        '<g stroke="#000" stroke-width="1.2" stroke-linecap="round">'
        + "".join(spokes) + '</g>'
        '<g fill="#000">' + "".join(tips) + '</g>'
        '<circle cx="21.0" cy="9.2" r="1.1" fill="#000"/>'
        '<line x1="21.0" y1="9.2" x2="24.2" y2="7.8" stroke="#000" '
        'stroke-width="1.15" stroke-linecap="round"/>'
        '<circle cx="20.4" cy="14.4" r="1.05" fill="#000"/>'
        '<line x1="20.4" y1="14.4" x2="23.5" y2="13.8" stroke="#000" '
        'stroke-width="1.15" stroke-linecap="round"/>'
        '<circle cx="24.8" cy="11.8" r="0.9" fill="#000"/>'
        '</svg>'
    )
    svg_core_stem = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<path fill="none" stroke="#000" stroke-width="1.95" stroke-linecap="round" '
        f'd="M {cx},{cy + 1.8} Q 10.7,20.0 9.0,25.6"/>'
        f'<circle cx="{cx}" cy="{cy}" r="2.3" fill="#000"/>'
        '</svg>'
    )
    return composite_layers(size, [
        (svg_cov(svg_halo, size), C_LGW4, 0.35),
        (svg_cov(svg_spokes, size), C_DG, 0.30, 0.65),
        (svg_cov(svg_core_stem, size), C_BLK, 0.35, 0.78),
    ])


def build_visibility(size):
    """Option A — Balanced AA Line-Art Eye + LG Iris + Black Pupil & Glint."""
    almond = (
        "M 4.2,15 C 7.5,9.6 11.2,7.9 15,7.9 C 18.8,7.9 22.5,9.6 25.8,15 "
        "C 22.5,20.4 18.8,22.1 15,22.1 C 11.2,22.1 7.5,20.4 4.2,15 Z"
    )
    svg_bg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><path fill="#000" d="{almond}"/></svg>'
    svg_iris = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><circle cx="15" cy="15" r="4.8" fill="#000"/></svg>'
    svg_lids = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<path fill="none" stroke="#000" stroke-width="1.9" stroke-linejoin="round" d="{almond}"/>'
        '<circle cx="15" cy="15" r="4.8" fill="none" stroke="#000" stroke-width="1.45"/>'
        '<circle cx="15" cy="15" r="2.35" fill="#000"/>'
        '</svg>'
    )
    svg_glint = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><circle cx="16.2" cy="13.8" r="0.95" fill="#000"/></svg>'
    return composite_layers(size, [
        (svg_cov(svg_bg, size), C_LGW4, 0.35),
        (svg_cov(svg_iris, size), C_LG, 0.35),
        (svg_cov(svg_lids, size), C_BLK, 0.35, 0.78),
        (svg_cov(svg_glint, size), C_WHT, 0.45),
    ])


def build_humidity(size):
    """Option A — Outlined AA Water Droplet + LGW Fill + Crisp Black '%'."""
    drop_d = "M 15,5.2 C 15,5.2 8.2,13.4 8.2,18.2 A 6.8,6.8 0 1 0 21.8,18.2 C 21.8,13.4 15,5.2 15,5.2 Z"
    svg_fill = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><path fill="#000" d="{drop_d}"/></svg>'
    svg_holes = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        '<circle cx="12.7" cy="16.2" r="1.45" fill="#000"/>'
        '<circle cx="17.3" cy="20.6" r="1.45" fill="#000"/>'
        '</svg>'
    )
    svg_outline = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<path fill="none" stroke="#000" stroke-width="1.9" stroke-linejoin="round" d="{drop_d}"/>'
        '<circle cx="12.7" cy="16.2" r="1.35" fill="none" stroke="#000" stroke-width="1.2"/>'
        '<circle cx="17.3" cy="20.6" r="1.35" fill="none" stroke="#000" stroke-width="1.2"/>'
        '<line x1="17.6" y1="14.8" x2="12.4" y2="21.8" stroke="#000" '
        'stroke-width="1.35" stroke-linecap="round"/>'
        '</svg>'
    )
    return composite_layers(size, [
        (svg_cov(svg_fill, size), C_LGW, 0.35),
        (svg_cov(svg_holes, size), C_WHT, 0.35),
        (svg_cov(svg_outline, size), C_BLK, 0.35, 0.78),
    ])


def build_dewpoint(size):
    """Option C — Ticked AA Thermometer + Trio of Condensation Mist Beads."""
    therm_body = "M 9.6,6.4 A 2.2,2.2 0 0 1 14.0,6.4 L 14.0,16.8 A 4.1,4.1 0 1 1 9.6,16.8 Z"
    svg_bg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><path fill="#000" d="{therm_body}"/></svg>'
    svg_mercury = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        '<circle cx="11.8" cy="20.3" r="2.2" fill="#000"/>'
        '<line x1="11.8" y1="20.3" x2="11.8" y2="11.5" stroke="#000" '
        'stroke-width="1.55" stroke-linecap="round"/>'
        '</svg>'
    )
    svg_beads = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        '<line x1="15.8" y1="8.8" x2="17.8" y2="8.8" stroke="#000" stroke-width="1.35" stroke-linecap="round"/>'
        '<line x1="15.8" y1="12.0" x2="17.5" y2="12.0" stroke="#000" stroke-width="1.35" stroke-linecap="round"/>'
        '<line x1="15.8" y1="15.2" x2="17.8" y2="15.2" stroke="#000" stroke-width="1.35" stroke-linecap="round"/>'
        '<circle cx="22.4" cy="10.2" r="2.1" fill="#000"/>'
        '<circle cx="20.2" cy="16.4" r="1.65" fill="#000"/>'
        '<circle cx="23.2" cy="21.2" r="2.35" fill="#000"/>'
        '</svg>'
    )
    svg_outlines = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<path fill="none" stroke="#000" stroke-width="1.8" stroke-linejoin="round" d="{therm_body}"/>'
        '<circle cx="22.4" cy="10.2" r="2.1" fill="none" stroke="#000" stroke-width="1.35"/>'
        '<circle cx="20.2" cy="16.4" r="1.65" fill="none" stroke="#000" stroke-width="1.25"/>'
        '<circle cx="23.2" cy="21.2" r="2.35" fill="none" stroke="#000" stroke-width="1.35"/>'
        '</svg>'
    )
    return composite_layers(size, [
        (svg_cov(svg_bg, size), C_WHT, 0.35),
        (svg_cov(svg_beads, size), C_LG, 0.35),
        (svg_cov(svg_mercury, size), C_DG, 0.30, 0.65),
        (svg_cov(svg_outlines, size), C_BLK, 0.35, 0.78),
    ])


def build_aqi(size):
    """Option A — Refined AA Air Filter Perspective Frame + Waves & Particles."""
    box_pts = "8.0,6.8 14.5,5.5 14.5,24.2 8.0,23.0"
    svg_fill = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><polygon fill="#000" points="{box_pts}"/></svg>'
    svg_waves = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        '<line x1="11.2" y1="8.0" x2="11.2" y2="21.8" stroke="#000" stroke-width="1.0" stroke-linecap="round"/>'
        '<path fill="none" stroke="#000" stroke-width="1.5" stroke-linecap="round" d="M 15.0,10.5 Q 18.8,12.2 22.8,9.6"/>'
        '<path fill="none" stroke="#000" stroke-width="1.5" stroke-linecap="round" d="M 15.0,14.9 Q 18.8,16.6 22.8,14.0"/>'
        '<path fill="none" stroke="#000" stroke-width="1.5" stroke-linecap="round" d="M 15.0,19.3 Q 18.8,21.0 22.8,18.4"/>'
        '<circle cx="4.6" cy="11.2" r="1.05" fill="#000"/>'
        '<circle cx="4.6" cy="15.0" r="1.05" fill="#000"/>'
        '<circle cx="4.6" cy="18.8" r="1.05" fill="#000"/>'
        '</svg>'
    )
    svg_frame = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<polygon fill="none" stroke="#000" stroke-width="1.85" stroke-linejoin="round" points="{box_pts}"/>'
        '</svg>'
    )
    return composite_layers(size, [
        (svg_cov(svg_fill, size), C_LGW, 0.35),
        (svg_cov(svg_waves, size), C_DG, 0.30, 0.65),
        (svg_cov(svg_frame, size), C_BLK, 0.35, 0.78),
    ])


def build_house(filename, is_temp, size):
    """Indoor Pair Option A — 2-bit AA Existing Houses + DG Bulb / Clean % Zone."""
    cov_full = svg_file_cov(filename, size)
    vb = "0 0 135.46667 135.46667"
    svg_tint = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}"><polygon fill="#000" points="67.7,24 111,59 107,113 28,113 24,59"/></svg>'
    if is_temp:
        svg_bulb = (
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}">'
            '<circle cx="67.7" cy="88.7" r="7.5" fill="#000"/>'
            '<line x1="67.7" y1="88.7" x2="67.7" y2="57.0" stroke="#000" '
            'stroke-width="6.0" stroke-linecap="round"/></svg>'
        )
        return composite_layers(size, [
            (svg_cov(svg_tint, size), C_LGW4, 0.35),
            (svg_cov(svg_bulb, size), C_DG, 0.30, 0.65),
            (cov_full, C_BLK, 0.35, 0.78),
        ])
    svg_clear = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}"><circle cx="67.7" cy="84.5" r="19.0" fill="#000"/></svg>'
    return composite_layers(size, [
        (svg_cov(svg_tint, size), C_LGW, 0.35),
        (svg_cov(svg_clear, size), C_WHT, 0.40),
        (cov_full, C_BLK, 0.35, 0.78),
    ])


def build_heat_stress(size):
    """Option A — 2-bit AA wi-hot.svg + Soft LGW Upper Sun Dome."""
    cov = svg_file_cov("wi-hot.svg", size)
    svg_dome = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><path fill="#000" d="M 10.6,14.8 A 4.4,4.4 0 0 1 19.4,14.8 Z"/></svg>'
    return composite_layers(size, [
        (svg_cov(svg_dome, size), C_LGW, 0.35),
        (cov, C_BLK, 0.35, 0.78),
    ])


def build_pressure(size):
    """Option C — Circular Dial Bezel + Shaded Scale Band + Bold Pointer Hand."""
    cx, cy, r_out = 15.0, 14.0, 8.2
    band_d = f"M {cx - 5.2},{cy + 3.2} A 6.1,6.1 0 1 1 {cx + 5.2},{cy + 3.2}"
    ticks = []
    for deg in (-210, -150, -90, -30, 30):
        ang = math.radians(deg)
        x1, y1 = cx + 5.4 * math.cos(ang), cy + 5.4 * math.sin(ang)
        x2, y2 = cx + 7.6 * math.cos(ang), cy + 7.6 * math.sin(ang)
        ticks.append(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}"/>')
    svg_band = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><path fill="none" stroke="#000" stroke-width="2.4" stroke-linecap="round" d="{band_d}"/></svg>'
    svg_ticks = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30"><g stroke="#000" stroke-width="1.2" stroke-linecap="round">' + "".join(ticks) + '</g></svg>'
    svg_blk = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 30 30">'
        f'<circle cx="{cx}" cy="{cy}" r="{r_out}" fill="none" stroke="#000" stroke-width="1.85"/>'
        '<line x1="11.5" y1="24.2" x2="18.5" y2="24.2" stroke="#000" stroke-width="1.75" stroke-linecap="round"/>'
        f'<line x1="{cx}" y1="{cy + 8.2}" x2="{cx}" y2="24.2" stroke="#000" stroke-width="1.75"/>'
        f'<line x1="{cx}" y1="{cy}" x2="19.6" y2="9.4" stroke="#000" stroke-width="2.0" stroke-linecap="round"/>'
        f'<circle cx="{cx}" cy="{cy}" r="2.2" fill="#000"/>'
        '</svg>'
    )
    return composite_layers(size, [
        (svg_cov(svg_band, size), C_LGW, 0.35),
        (svg_cov(svg_ticks, size), C_DG, 0.30, 0.65),
        (svg_cov(svg_blk, size), C_BLK, 0.35, 0.78),
    ])


RECIPES = [
    ("pollen", build_pollen),
    ("visibility", build_visibility),
    ("humidity", build_humidity),
    ("dewpoint", build_dewpoint),
    ("aqi", build_aqi),
    ("intemp", lambda sz: build_house("house_thermometer.svg", True, sz)),
    ("inhumidity", lambda sz: build_house("house_humidity.svg", False, sz)),
    ("heat_stress", build_heat_stress),
    ("pressure", build_pressure),
]


def pack_4bit(grid):
    h, w = grid.shape
    assert w % 2 == 0
    nibbles = np.vectorize(TO_NIBBLE.__getitem__, otypes=[np.uint8])(grid)
    flat = nibbles.reshape(-1)
    return ((flat[0::2] << 4) | flat[1::2]).tolist()


def main():
    out = [
        "// 4-greyscale multi-tone widget icons (48x48 and 40x40) for GREYS_75.",
        "// GENERATED FILE (tools/gen_grey_widgets.py; do not edit by hand).",
        "// 4-bit packed: hi nibble even x, lo nibble odd x:",
        "// 0=transparent, 1=BLACK, 2=G_DG, 3=G_LG, 4=G75_LGW, 5=G75_LGW4, 6=WHITE.",
        "#ifndef __ICONS_WIDGETS_GREY_H__",
        "#define __ICONS_WIDGETS_GREY_H__",
        "",
        "#include <Arduino.h>",
        "#include <string.h>",
        "",
    ]
    entries = []
    total_bytes = 0
    for name, fn in RECIPES:
        for sz in (48, 40):
            grid = fn(sz)
            assert (grid != C_WHT).any(), f"{name} {sz}px produced empty bitmap"
            packed = pack_4bit(grid)
            total_bytes += len(packed)
            var = f"grey_wgt_{name}_{sz}"
            out.append(f"static const uint8_t {var}[{len(packed)}] PROGMEM = {{")
            for k in range(0, len(packed), 24):
                out.append("  " + ", ".join(f"0x{b:02x}" for b in packed[k:k + 24]) + ",")
            out.append("};")
        entries.append(f'  {{"{name}", grey_wgt_{name}_48, grey_wgt_{name}_40}},')

    out.extend([
        "",
        "struct GreyWidgetIcon {",
        "  const char *name;",
        "  const uint8_t *dots48;",
        "  const uint8_t *dots40;",
        "};",
        "static const GreyWidgetIcon GREY_WIDGET_ICONS[] = {",
        *entries,
        "};",
        "",
        "static inline const uint8_t *getGreyWidgetIcon(const char *name, int16_t size)",
        "{",
        "  if (!name || (size != 40 && size != 48)) return nullptr;",
        "  for (const GreyWidgetIcon &e : GREY_WIDGET_ICONS)",
        "  {",
        "    if (strcmp(e.name, name) == 0)",
        "    {",
        "      return (size == 40) ? e.dots40 : e.dots48;",
        "    }",
        "  }",
        "  return nullptr;",
        "}",
        "#endif",
    ])
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"{len(RECIPES)} widget icons x 2 sizes, {total_bytes} bytes ({total_bytes / 1024:.1f} KB)")


if __name__ == "__main__":
    main()
