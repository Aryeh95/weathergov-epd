#!/usr/bin/env python3
"""Build and run the PC preview of the 800x480 layout on saved API replies.

Compiles the firmware's renderer.cpp, display_utils.cpp and api_response.cpp
(and the Adafruit GFX library they draw with) for this computer, runs the
parsers over the replies in data/ and the renderer over the result, and
turns what it drew into PNGs: the dots as the panel is told to set them in
the Spectra 6 inks' measured colours, and a "_blended" copy averaged 2x2.

    python tools/sim480/build.py [output folder] [data folder] [--locale=he_IL|pl_PL]

Built for the Hebrew (LOCALE he_IL) / Israel Meteorological Service
configuration on the reTerminal E1002's panel, in metric units. Needs a
C++17 compiler (clang++ or g++ on the path) and `pip install pillow numpy`. The
Adafruit GFX and ArduinoJson sources are taken from a PlatformIO build's
.pio/libdeps, so build the firmware once first.
"""
import glob
import json
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
           ("renderer.cpp", "display_utils.cpp", "api_response.cpp", "rtl.cpp", "hebcal.cpp",
            "_strftime.cpp", "locale.cpp", "conversions.cpp", "sun.cpp",
            "config.cpp", "precip.cpp")] + [os.path.join(HERE, "sim_main.cpp")]
C_SOURCES = [os.path.join(PIO, "lib", "pollutant-concentration-to-aqi", "aqi.c")]
# --panel: which e-paper panel the page is drawn for. The defines mirror
# the firmware's own environments: e6 is the reTerminal E1002 (Spectra 6),
# bwv2 the reTerminal E1001 (7.5in black and white, DISP_BW_V2), 3c the
# DESPI-C02 build with the red/black/white 7.5in (B) panel.
PANELS = {
    "e6":   ["BOARD_RETERMINAL_E1002"],
    "bwv2": ["BOARD_RETERMINAL_E1002", "BOARD_RETERMINAL_E1001"],
    "3c":   ["DISP_3C_B"],
}
# --locale: the build's locale, with the font family its defaults file would
# switch on. he_IL draws the Netanya pages from data/; pl_PL the Kraków
# pages (Open-Meteo forecast source) from data/krakow.
LOCALES = {
    "he_IL": ["LOCALE=he_IL", "FONT_INCLUDE_Heebo=1"],
    "pl_PL": ["LOCALE=pl_PL", "FONT_INCLUDE_Lato_CE=1"],
}
DEFINES = ["ARDUINO=10000",
           "UNITS_TEMP_CELSIUS", "UNITS_SPEED_KILOMETERSPERHOUR",
           "UNITS_PRES_HECTOPASCALS", "UNITS_DIST_KILOMETERS",
           "UNITS_DAILY_PRECIP_MILLIMETERS",
           "ARDUINOJSON_ENABLE_ARDUINO_STREAM=0", "ARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
           "ARDUINOJSON_ENABLE_PROGMEM=0", "ARDUINOJSON_ENABLE_COMMENTS=1",
           "_USE_MATH_DEFINES"]


def libdep(name):
    hits = glob.glob(os.path.join(PIO, ".pio", "libdeps", "*", name))
    if not hits:
        sys.exit("no %s under platformio/.pio/libdeps: build the firmware once first" % name)
    return hits[0]


def compiler():
    for cxx, cc in (("clang++", "clang"), ("g++", "gcc")):
        if shutil.which(cxx):
            return [cxx], [cc]
    sys.exit("no C++ compiler found; install clang or gcc")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    panel = "e6"
    locale = "he_IL"
    for a in sys.argv[1:]:
        if a.startswith("--panel="):
            panel = a.split("=", 1)[1]
        if a.startswith("--locale="):
            locale = a.split("=", 1)[1]
    if panel not in PANELS:
        sys.exit("--panel must be one of: " + ", ".join(PANELS))
    if locale not in LOCALES:
        sys.exit("--locale must be one of: " + ", ".join(LOCALES))
    out = os.path.abspath(args[0] if len(args) > 0 else os.path.join(HERE, "out"))
    data = os.path.abspath(args[1] if len(args) > 1 else
                           os.path.join(HERE, "data", "krakow" if locale == "pl_PL" else ""))
    # each panel and locale builds and draws in its own folder: the defines differ
    if panel != "e6":
        out = os.path.join(out, panel)
    if locale != "he_IL":
        out = os.path.join(out, locale)
    os.makedirs(out, exist_ok=True)
    gfx = libdep("Adafruit GFX Library")
    includes = [os.path.join(HERE, "shim"), os.path.join(PIO, "include"),
                os.path.join(PIO, "lib", "esp32-weather-epd-assets"),
                os.path.join(PIO, "lib", "pollutant-concentration-to-aqi"),
                os.path.join(libdep("ArduinoJson"), "src"), gfx]
    cxx, cc = compiler()
    flags = ["-O1", "-w"] + ["-I" + i for i in includes] + ["-D" + d for d in DEFINES + PANELS[panel] + LOCALES[locale]]
    if os.name == "nt":
        # MinGW: localtime_r for every source, not only those that see Arduino.h
        flags += ["-include", os.path.join(HERE, "shim", "win_time.h")]
    objects = []
    for src in C_SOURCES:
        obj = os.path.join(out, os.path.basename(src) + ".o")
        subprocess.check_call(cc + flags + ["-c", src, "-o", obj])
        objects.append(obj)
    exe = os.path.join(out, "sim480" + (".exe" if os.name == "nt" else ""))
    subprocess.check_call(cxx + ["-std=gnu++17"] + flags + SOURCES
                          + [os.path.join(gfx, "Adafruit_GFX.cpp")] + objects + ["-o", exe])
    subprocess.check_call([exe, out, data, panel])
    for name in sorted(os.listdir(out)):
        if not name.endswith(".ppm"):
            continue
        im = Image.open(os.path.join(out, name)).convert("RGB")
        base = os.path.join(out, name[:-4])
        im.save(base + ".png")
        w, h = im.size
        idx_path = base + ".idx"
        if panel == "e6" and os.path.exists(idx_path):
            idx = np.fromfile(idx_path, dtype=np.uint8).reshape(h, w)
            half = blend_measured(idx)
        else:
            # the dots averaged as light (in linear sRGB), 2x2
            a = np.asarray(im, dtype=np.float64) / 255.0
            a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
            half = a[:h // 2 * 2, :w // 2 * 2].reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3))
        half = np.where(half <= 0.0031308, 12.92 * half, 1.055 * np.clip(half, 0, 1) ** (1 / 2.4) - 0.055)
        Image.fromarray(np.clip(half * 255 + 0.5, 0, 255).astype(np.uint8)).resize(
            (w, h), Image.BICUBIC).save(base + "_blended.png")
        os.remove(os.path.join(out, name))
        if os.path.exists(idx_path):
            os.remove(idx_path)
    print("pictures are in", out)


# The Spectra 6 dots as the eye sees them from a desk, 2x2 cells at a time,
# from the reTerminal E1002's ink card (measured_e6.json). A cell of one ink
# is that ink; a cell of two inks whose mix was measured is the measured
# colour at its share, between the pure inks; anything else is the dots
# after dot gain (each pixel takes on gain[ink] / 4 of each 4-neighbour's
# ink), averaged. On this panel two inks dot by dot come out darker than
# their average: a black/white checker is 29 % of white, not 51 %.
INK_OF_INDEX = ["W", "K", "W", "R", "Y", "G", "B", "O"]


def blend_measured(idx):
    m = json.load(open(os.path.join(HERE, "measured_e6.json")))
    ink = {k: np.array(v) for k, v in m["ink"].items()}
    mixes = {}
    for e in m["mixes"]:
        mixes.setdefault((e["a"], e["b"]), [(0.0, ink[e["b"]]), (1.0, ink[e["a"]])]).append(
            (e["share"], np.array(e["lin"])))
    for k in mixes:
        mixes[k].sort(key=lambda p: p[0])
    # orange: the red/yellow checker the panel shows for it
    ink["O"] = next(c for s, c in mixes[("R", "Y")] if s == 0.5)
    codes = INK_OF_INDEX
    lut = np.array([ink[c] for c in codes])                       # index -> linear colour
    gain = np.array([m["gain"].get(c, 0.0) for c in codes])
    col = lut[idx]
    out = col.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        nb = np.roll(np.roll(idx, dy, 0), dx, 1)
        out += (gain[nb] / 4)[..., None] * (lut[nb] - col)
    h, w = idx.shape
    h2, w2 = h // 2, w // 2
    half = out[:h2 * 2, :w2 * 2].reshape(h2, 2, w2, 2, 3).mean(axis=(1, 3))
    # cells of one or two inks
    cell = np.array([codes[i] for i in range(8)])[idx[:h2 * 2, :w2 * 2]].reshape(h2, 2, w2, 2)
    cell = cell.transpose(0, 2, 1, 3).reshape(h2, w2, 4)
    for y in range(h2):
        row = cell[y]
        for x in range(w2):
            c = row[x]
            first = c[0]
            if (c == first).all():
                half[y, x] = ink[first]
                continue
            kinds = set(c.tolist())
            if len(kinds) != 2:
                continue
            a, b = sorted(kinds)
            curve = mixes.get((a, b)) or mixes.get((b, a))
            if curve is None:
                continue
            top = a if (a, b) in mixes else b
            share = (c == top).sum() / 4.0
            xs = [p[0] for p in curve]
            half[y, x] = [np.interp(share, xs, [p[1][ch] for p in curve]) for ch in range(3)]
    return half


if __name__ == "__main__":
    main()
