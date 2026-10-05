#!/usr/bin/env python3
"""Build and run the PC preview of the 800x480 layout on saved API replies.

Compiles the firmware's renderer.cpp, display_utils.cpp and api_response.cpp
(and the Adafruit GFX library they draw with) for this computer, runs the
parsers over the replies in data/ and the renderer over the result, and
turns what it drew into PNGs: the dots as the panel is told to set them in
the Spectra 6 inks' measured colours, and a "_blended" copy averaged 2x2.

    python tools/sim480/build.py [output folder] [data folder]

Built for the Hebrew (LOCALE he_IL) / Israel Meteorological Service
configuration on the reTerminal E1002's panel, in metric units. Needs a
C++17 compiler (clang++ or g++ on the path) and `pip install pillow`. The
Adafruit GFX and ArduinoJson sources are taken from a PlatformIO build's
.pio/libdeps, so build the firmware once first.
"""
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PIO = os.path.join(ROOT, "platformio")
SOURCES = [os.path.join(PIO, "src", n) for n in
           ("renderer.cpp", "display_utils.cpp", "api_response.cpp", "rtl.cpp",
            "_strftime.cpp", "locale.cpp", "conversions.cpp", "sun.cpp",
            "config.cpp", "precip.cpp")] + [os.path.join(HERE, "sim_main.cpp")]
C_SOURCES = [os.path.join(PIO, "lib", "pollutant-concentration-to-aqi", "aqi.c")]
DEFINES = ["ARDUINO=10000", "BOARD_RETERMINAL_E1002", "LOCALE=he_IL", "FONT_INCLUDE_Heebo=1",
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
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out"))
    data = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "data"))
    os.makedirs(out, exist_ok=True)
    gfx = libdep("Adafruit GFX Library")
    includes = [os.path.join(HERE, "shim"), os.path.join(PIO, "include"),
                os.path.join(PIO, "lib", "esp32-weather-epd-assets"),
                os.path.join(PIO, "lib", "pollutant-concentration-to-aqi"),
                os.path.join(libdep("ArduinoJson"), "src"), gfx]
    cxx, cc = compiler()
    flags = ["-O1", "-w"] + ["-I" + i for i in includes] + ["-D" + d for d in DEFINES]
    objects = []
    for src in C_SOURCES:
        obj = os.path.join(out, os.path.basename(src) + ".o")
        subprocess.check_call(cc + flags + ["-c", src, "-o", obj])
        objects.append(obj)
    exe = os.path.join(out, "sim480")
    subprocess.check_call(cxx + ["-std=gnu++17"] + flags + SOURCES
                          + [os.path.join(gfx, "Adafruit_GFX.cpp")] + objects + ["-o", exe])
    subprocess.check_call([exe, out, data])
    for name in sorted(os.listdir(out)):
        if not name.endswith(".ppm"):
            continue
        im = Image.open(os.path.join(out, name)).convert("RGB")
        base = os.path.join(out, name[:-4])
        im.save(base + ".png")
        w, h = im.size
        im.resize((w // 2, h // 2), Image.BOX).resize((w, h), Image.BICUBIC).save(base + "_blended.png")
        os.remove(os.path.join(out, name))
    print("pictures are in", out)


if __name__ == "__main__":
    main()
