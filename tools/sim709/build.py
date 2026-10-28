#!/usr/bin/env python3
"""Build and run the PC preview of the 1600x1200 layout.

Compiles the firmware's renderer709.cpp and epd709.cpp, together with the
few project files they depend on, for this computer; runs the result on
sample weather; and turns what it drew into PNGs. Each picture comes in two
versions: the dots exactly as the panel is told to set them, and
"_blended", averaged 2x2, which is closer to what the eye sees from a desk.

    python tools/sim709/build.py [output folder]

Needs a C++17 compiler. The ZIG environment variable names one if it is
set (any Zig works: `pip install ziglang` brings zig.exe); otherwise
clang++ or g++ is used.
    pip install pillow
"""
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PIO = os.path.join(ROOT, "platformio")
SOURCES = [os.path.join(PIO, "src", n) for n in
           ("renderer709.cpp", "epd709.cpp", "_strftime.cpp", "hebcal.cpp", "locale.cpp", "conversions.cpp",
            "sun.cpp", "config.cpp")] + [os.path.join(HERE, "sim_main.cpp")]
C_SOURCES = [os.path.join(PIO, "lib", "pollutant-concentration-to-aqi", "aqi.c")]
INCLUDES = [os.path.join(HERE, "shim"), os.path.join(PIO, "include"),
            os.path.join(PIO, "lib", "esp32-weather-epd-assets"),
            os.path.join(PIO, "lib", "pollutant-concentration-to-aqi")]
DEFINES = ["BOARD_XIAO_EE02", "EPD709_HOST", "ARDUINOJSON_ENABLE_COMMENTS=1", "_USE_MATH_DEFINES"]


def compiler():
    zig = os.environ.get("ZIG")
    if zig:
        return [zig, "c++"], [zig, "cc"]
    for cxx, cc in (("clang++", "clang"), ("g++", "gcc")):
        if shutil.which(cxx):
            return [cxx], [cc]
    sys.exit("no C++ compiler found; set ZIG to a zig executable, or install clang or gcc")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    balance = "--balance" in sys.argv[1:]
    out = os.path.abspath(args[0] if args else os.path.join(HERE, "out"))
    os.makedirs(out, exist_ok=True)
    cxx, cc = compiler()
    flags = ["-O2", "-w"] + ["-I" + i for i in INCLUDES] + ["-D" + d for d in DEFINES]
    objects = []
    for src in C_SOURCES:
        obj = os.path.join(out, os.path.basename(src) + ".o")
        subprocess.check_call(cc + flags + ["-c", src, "-o", obj])
        objects.append(obj)
    exe = os.path.join(out, "sim709.exe" if os.name == "nt" else "sim709")
    subprocess.check_call(cxx + ["-std=gnu++17"] + flags + SOURCES + objects + ["-o", exe])
    subprocess.check_call([exe, out])
    for name in sorted(os.listdir(out)):
        if not name.endswith(".ppm"):
            continue
        im = Image.open(os.path.join(out, name)).convert("RGB")
        base = os.path.join(out, name[:-4])
        im.save(base + ".png")
        w, h = im.size
        # dots mix in the eye as light, so average them in linear light (in
        # floating point: 8-bit linear loses the dark shades): an average of
        # the screen values makes dithered mixes look too light
        a = np.asarray(im, dtype=np.float64) / 255.0
        a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
        # ...but on this panel a mix of dots comes out darker than their
        # average, by how much depending on the inks: where the renderer
        # says which screen a dot belongs to (<name>.tag), use the colour
        # measured on the panel for that screen (measured_mixes.json)
        a = black_gain(a, im)
        tagfile = os.path.join(out, name[:-4] + ".tag")
        if os.path.exists(tagfile):
            tags = np.fromfile(tagfile, dtype=np.uint32).reshape(1200, 1600)
            if tags.shape == a.shape[:2]:
                apply_measured(a, tags)
            os.remove(tagfile)
        a = a[:h // 2 * 2, :w // 2 * 2].reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3))
        a = np.where(a <= 0.0031308, 12.92 * a, 1.055 * a ** (1 / 2.4) - 0.055)
        half = Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8))
        half.resize((w, h), Image.BICUBIC).save(base + "_blended.png")
        if balance:
            white_balance(base + ".png")
            white_balance(base + "_blended.png")
        os.remove(os.path.join(out, name))
    print("pictures are in", out)


# --balance: the 7.5in black-and-white preview's white, which every panel's
# own white is scaled to
BALANCE_WHITE = (214, 216, 212)
# the panel's own white (sim_main.cpp's INK_RGB[1]): not the picture's most
# common colour, which in dark mode is the black
PANEL_WHITE = (177, 194, 207)


def white_balance(path):
    """The picture at `path` white-balanced to BALANCE_WHITE: each channel of
    the image in linear light is scaled so the panel's own white comes out
    as that white, as the eye adjusts to the panel in front of it. The inks
    keep their measured relation to the panel's white; only the comparison
    with paper (a Spectra 6 panel reflects about 40 % as much light as
    paper, a black-and-white one nearer 70 %) is given up, so that pictures
    of different panels side by side compare their inks, not their whites."""
    a = np.asarray(Image.open(path).convert("RGB"))
    white = np.array(PANEL_WHITE) / 255.0
    lin = lambda c: np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    gain = lin(np.array(BALANCE_WHITE) / 255.0) / np.maximum(lin(white), 1e-6)
    v = np.clip(lin(a / 255.0) * gain, 0, 1)
    v = np.where(v <= 0.0031308, 12.92 * v, 1.055 * v ** (1 / 2.4) - 0.055)
    Image.fromarray(np.round(v * 255).astype(np.uint8)).save(path)



MEASURED = None

# Dot gain: on the panel a black dot spreads into its white neighbours, so a
# pattern of black reads darker than the average of its dots. Each dot not
# painted from a measured screen -- text, lines, outlines -- moves
# BLACK_GAIN / 4 of the way toward the black of each 4-neighbour that is
# black. 0.62 is the value of test card 9's black/white checker
# (2026-10-25: 10.5 % of white's luminance, where the average says 26.6 %),
# the pattern of grey text; the screens themselves are measured.
BLACK_GAIN = 0.62


def black_gain(a, im):
    """a (linear rgb of the picture im) with black's dot gain applied"""
    import re
    src = open(os.path.join(HERE, "sim_main.cpp"), encoding="utf-8").read()
    body = re.search(r"INK_RGB\[8\]\[3\] = \{(.*?)\};", src, re.S).group(1)
    black = np.array([int(v) for v in re.findall(r"\d+", body)][:3])
    is_k = (np.asarray(im) == black).all(axis=-1)
    k = np.where(black / 255.0 <= 0.04045, black / 255.0 / 12.92, ((black / 255.0 + 0.055) / 1.055) ** 2.4)
    out = a.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        nb = np.roll(np.roll(is_k, dy, 0), dx, 1)
        out += np.where(nb[..., None], (BLACK_GAIN / 4) * (k - a), 0)
    return out


def _measured():
    """(top, under, cells) -> [(share, linear rgb)], with the bare inks at
    shares 0 and 1, from measured_mixes.json and INK_RGB in sim_main.cpp."""
    global MEASURED
    if MEASURED is None:
        import json
        import re
        src = open(os.path.join(HERE, "sim_main.cpp"), encoding="utf-8").read()
        body = re.search(r"INK_RGB\[8\]\[3\] = \{(.*?)\};", src, re.S).group(1)
        rgb = np.array([int(v) for v in re.findall(r"\d+", body)], np.float64).reshape(8, 3) / 255.0
        ink = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
        MEASURED = {}
        for e in json.load(open(os.path.join(HERE, "measured_mixes.json")))["mixes"]:
            k = (e["top"], e["under"], e["cells"])
            if k not in MEASURED:
                MEASURED[k] = [(0.0, ink[e["under"]]), (1.0, ink[e["top"]])]
            MEASURED[k].append((e["share"], np.array(e["lin"])))
        for k in MEASURED:
            MEASURED[k].sort(key=lambda p: p[0])
    return MEASURED


def apply_measured(a, tags):
    """Paint every tagged screen that has measurements in its measured
    colour, interpolated along its pair's measured shares."""
    table = _measured()
    for tag in np.unique(tags):
        tag = int(tag)
        if not tag >> 31:
            continue
        top, under, cells = (tag >> 28) & 7, (tag >> 25) & 7, (tag >> 24) & 1
        share, cover = ((tag >> 12) & 0xFFF) / 1000.0, (tag & 0xFFF) / 1000.0
        if cover < 0.999:
            continue
        curve = table.get((top, under, cells))
        if curve is None and (under, top, cells) in table:
            curve, share = table[(under, top, cells)], 1.0 - share
        if curve is None:
            continue                     # never measured: the plain average stands
        xs = np.array([p[0] for p in curve])
        col = np.array([np.interp(share, xs, [p[1][c] for p in curve]) for c in range(3)])
        a[tags == tag] = col

if __name__ == "__main__":
    main()
