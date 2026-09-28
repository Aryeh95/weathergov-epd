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

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PIO = os.path.join(ROOT, "platformio")
SOURCES = [os.path.join(PIO, "src", n) for n in
           ("renderer709.cpp", "epd709.cpp", "_strftime.cpp", "locale.cpp", "conversions.cpp",
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
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out"))
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
        im.resize((w // 2, h // 2), Image.BOX).resize((w, h), Image.BICUBIC).save(base + "_blended.png")
        os.remove(os.path.join(out, name))
    print("pictures are in", out)


if __name__ == "__main__":
    main()
