#!/usr/bin/env python3
"""Build and run the PC preview of the 800x480 layout on saved API replies.

Compiles the firmware's renderer.cpp, display_utils.cpp and api_response.cpp
(and the Adafruit GFX library they draw with) for this computer, runs the
parsers over the replies in data/ and the renderer over the result, and
turns what it drew into PNGs: the dots as the panel is told to set them in
the Spectra 6 inks' measured colours, and a "_blended" copy averaged 2x2.

    python tools/sim480/build.py [output folder] [data folder] [--locale=he_IL|pl_PL|en_US]

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
            "config.cpp", "precip.cpp", "settings.cpp", "renderer426.cpp")] + [os.path.join(HERE, "sim_main.cpp")]
C_SOURCES = [os.path.join(PIO, "lib", "pollutant-concentration-to-aqi", "aqi.c")]
# --panel: which e-paper panel the page is drawn for. The defines mirror
# the firmware's own environments: e6 is the reTerminal E1002 (Spectra 6),
# bwv2 the reTerminal E1001 (7.5in black and white, DISP_BW_V2), 3c the
# DESPI-C02 build with the red/black/white 7.5in (B) panel. 4g426 is the
# XIAO EE05's 4.26in in its four measured greys, with its own layout
# (renderer426.cpp): the home page and the pages KEY3 turns to, from the US
# build (--locale=en_US); SIM480_TESTCARD=1 draws the greys card instead.
PANELS = {
    "e6":   ["BOARD_RETERMINAL_E1002"],
    "bwv2": ["BOARD_RETERMINAL_E1002", "BOARD_RETERMINAL_E1001"],
    "3c":   ["DISP_3C_B"],
    "4g426": ["BOARD_XIAO_EE05"],
}
# --locale: the build's locale, with the font family its defaults file would
# switch on. he_IL draws the Netanya pages from data/; pl_PL the Kraków
# pages (Open-Meteo forecast source) from data/krakow.
METRIC = ["UNITS_TEMP_CELSIUS", "UNITS_SPEED_KILOMETERSPERHOUR",
          "UNITS_PRES_HECTOPASCALS", "UNITS_DIST_KILOMETERS",
          "UNITS_DAILY_PRECIP_MILLIMETERS"]
# en_US draws a real device's page: its own config.json (SIM480_FS names the
# folder) and replies saved for its location by fetch_us.py (the data folder
# argument), in the firmware's default US units.
LOCALES = {
    "he_IL": ["LOCALE=he_IL", "FONT_INCLUDE_Heebo=1"] + METRIC,
    "pl_PL": ["LOCALE=pl_PL", "FONT_INCLUDE_Lato_CE=1"] + METRIC,
    "en_US": ["LOCALE=en_US"],
}
DEFINES = ["ARDUINO=10000",
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
            view = blend_measured(idx)
        elif panel == "4g426" and os.path.exists(idx_path):
            idx = np.fromfile(idx_path, dtype=np.uint8).reshape(h, w)
            view = blend_greys(idx)
        else:
            # each dot with its right and lower neighbours, averaged as
            # light (in linear sRGB): full resolution, the dots blended
            a = np.asarray(im, dtype=np.float64) / 255.0
            a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
            view = windows2x2(a).mean(axis=0)
        view = np.where(view <= 0.0031308, 12.92 * view, 1.055 * np.clip(view, 0, 1) ** (1 / 2.4) - 0.055)
        Image.fromarray(np.clip(view * 255 + 0.5, 0, 255).astype(np.uint8)).save(base + "_blended.png")
        os.remove(os.path.join(out, name))
        if os.path.exists(idx_path):
            os.remove(idx_path)
    print("pictures are in", out)


def windows2x2(a):
    """the 2x2 window at every dot: the dot, its right, lower and lower
    right neighbours (the last row and column repeat), stacked"""
    pad = [(0, 1), (0, 1)] + [(0, 0)] * (a.ndim - 2)
    p = np.pad(a, pad, mode="edge")
    return np.stack([p[:-1, :-1], p[:-1, 1:], p[1:, :-1], p[1:, 1:]])


# The Spectra 6 dots as the eye sees them from a desk, from the reTerminal
# E1002's ink card (measured_e6.json), at full resolution: every dot is
# coloured by the 2x2 window it starts. A window of one ink is that ink; a
# window of two inks whose mix was measured is the measured colour at its
# share, between the pure inks; anything else is the dots after dot gain
# (each pixel takes on gain[ink] / 4 of each 4-neighbour's ink), averaged.
# On this panel two inks dot by dot come out darker than their average: a
# black/white checker is 29 % of white, not 51 %.
CODE = {"W": 0, "K": 1, "R": 3, "Y": 4, "G": 5, "B": 6, "O": 7}


def blend_measured(idx):
    m = json.load(open(os.path.join(HERE, "measured_e6.json")))
    ink = {k: np.array(v) for k, v in m["ink"].items()}
    mixes = {}
    for e in m["mixes"]:
        mixes.setdefault((e["a"], e["b"]), [(0.0, ink[e["b"]]), (1.0, ink[e["a"]])]).append(
            (e["share"], np.array(e["lin"])))
    for k in mixes:
        mixes[k].sort(key=lambda q: q[0])
    # orange: the red/yellow checker the panel shows for it
    ink["O"] = next(c for sh, c in mixes[("R", "Y")] if sh == 0.5)
    idx = np.where(idx == 2, 0, idx)                 # both paper indices are white
    lut = np.zeros((8, 3))
    gain = np.zeros(8)
    for k, c in CODE.items():
        lut[c] = ink[k]
        gain[c] = m["gain"].get(k, 0.0)
    col = lut[idx]
    dots = col.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        nb = np.roll(np.roll(idx, dy, 0), dx, 1)
        dots += (gain[nb] / 4)[..., None] * (lut[nb] - col)
    view = windows2x2(dots).mean(axis=0)
    q = windows2x2(idx)
    lo, hi = q.min(axis=0), q.max(axis=0)
    one = lo == hi
    view[one] = lut[lo[one]]
    two = ~one & ((q == lo) | (q == hi)).all(axis=0)
    for (ka, kb), curve in mixes.items():
        ca, cb = CODE[ka], CODE[kb]
        sel = two & (lo == min(ca, cb)) & (hi == max(ca, cb))
        if not sel.any():
            continue
        share = (q == ca).sum(axis=0)[sel] / 4.0
        xs = [c[0] for c in curve]
        view[sel] = np.stack([np.interp(share, xs, [c[1][ch] for c in curve]) for ch in range(3)], axis=-1)
    return view


# The 4.26in panel's four greys and their 50 % checkers, measured
# (measured_426.json), at full resolution like blend_measured: a 2x2 window
# of one level is that level; of two levels, the measured checker between
# them at its share, interpolated in linear light toward the pure levels
# (dot gain makes a black/white checker 40 % of white, not 51 %); of more,
# the dots averaged.
def blend_greys(idx):
    m = json.load(open(os.path.join(HERE, "measured_426.json")))
    lut = np.array([m["level"][str(i)] for i in range(4)])
    idx = np.minimum(idx, 3)
    q = windows2x2(idx)
    view = lut[q].mean(axis=0)
    lo, hi = q.min(axis=0), q.max(axis=0)
    one = lo == hi
    view[one] = lut[lo[one]]
    two = ~one & ((q == lo) | (q == hi)).all(axis=0)
    for key, mix in m["mix"].items():
        a, b = (int(c) for c in key.split("+"))
        sel = two & (lo == a) & (hi == b)
        if not sel.any():
            continue
        share = (q == a).sum(axis=0)[sel] / 4.0       # of the darker level
        xs = [0.0, 0.5, 1.0]
        view[sel] = np.stack([np.interp(share, xs, [lut[b][ch], mix[ch], lut[a][ch]]) for ch in range(3)], axis=-1)
    return view


if __name__ == "__main__":
    main()
