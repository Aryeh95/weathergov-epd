#!/usr/bin/env python3
"""Generate Adafruit GFX font headers for esp32-weather-epd from a TTF/OTF.

A port of Adafruit's fontconvert.c (the GFX library's font tool) onto the
freetype-py binding, reproducing the conventions of the headers already in
lib/esp32-weather-epd-assets/fonts: 141 DPI, characters 0x20-0xFF (the "8b"
suffix), mono rendering, MSB-first bitmaps that are byte-aligned per glyph,
and yAdvance taken from the face's line height. The 48 pt "_temperature"
face only carries the characters the big temperature readout draws
(0-9 . - and the degree sign); every other slot is an empty 1x1 glyph, as
in the shipped families, which keeps its bitmap table under the 64 KB that
GFXglyph's 16-bit offsets can address.

Usage:
    python tools/fontconvert.py [--autohint] [--encoding=<codec>]
                                [--weight=<n>] <Family> <font.ttf> [size ...]

--encoding names the single-byte code page the slots 0xA0-0xFF stand for.
The default, latin-1, is what every family upstream ships and what the
locale files' octal escapes ("\260C") assume. iso-8859-8 puts the Hebrew
alphabet at 0xE0-0xFA (and keeps the degree sign at 0xB0), which is how the
Hebrew locale's UTF-8 strings reach the panel: the renderer transcodes them
to this page (renderer.cpp, shapeText) when the family's ENCODING file says
so. The family directory gets an ENCODING file for gen_font_table.py.

--weight picks a named weight from a variable font (Heebo, Noto ...) instead
of its default instance.

--autohint runs FreeType's auto-hinter (FT_LOAD_FORCE_AUTOHINT) instead of
the font's own hints. For TrueType fonts, and instanced variable fonts in
particular, it snaps every stem to the pixel grid, so 1-bit text at 5-8 pt
comes out with even strokes instead of a mix of 1- and 2-pixel ones (Bitter
was generated with it). The GNU FreeFont headers upstream ships were made
without it, and a plain run still reproduces them exactly.

Writes lib/esp32-weather-epd-assets/fonts/<Family>/<Family>_<size>pt8b.h for
each size (default: the same 17 sizes every existing family ships), plus
<Family>_48pt8b_temperature.h, and the selector header fonts/<Family>.h kept
for parity with the families upstream ships. Afterwards run
tools/gen_font_table.py and switch the family on in platformio/include/
config.h (FONT_INCLUDE_<Family>).

    pip install freetype-py     (wheels bundle the FreeType library)
"""
import os
import sys

import freetype

DPI = 141
FIRST, LAST = 0x20, 0xFF
DEFAULT_SIZES = [4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26]
TEMP_CHARS = "0123456789.-" + chr(0xB0)   # what the 48 pt face must cover
HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, "..", "platformio", "lib",
                        "esp32-weather-epd-assets", "fonts")


def open_face(ttf, weight=None):
    face = freetype.Face(ttf)
    if weight is not None and face.has_multiple_masters:
        info = face.get_variation_info()
        coords = [a.default for a in info.axes]
        for i, a in enumerate(info.axes):
            if a.tag == b"wght" or a.name.lower() == "weight":
                coords[i] = float(weight)
        face.set_var_design_coords(coords)
    return face


def render(ttf, size, first=FIRST, last=LAST, only=None, autohint=False,
           encoding="latin-1", weight=None):
    """Returns (bitmaps: bytes, glyphs: [(off, w, h, xAdv, xOff, yOff)], yAdvance).

    With `only` (a string), characters outside it become empty 1x1 glyphs
    carrying the face's .notdef advance -- the same shape the shipped subset
    faces have.
    """
    face = open_face(ttf, weight)
    face.set_char_size(size << 6, 0, DPI, 0)
    flags = freetype.FT_LOAD_TARGET_MONO
    if autohint:
        flags |= freetype.FT_LOAD_FORCE_AUTOHINT
    bitmaps = bytearray()
    glyphs = []
    face.load_glyph(0, flags)
    notdef_advance = face.glyph.advance.x >> 6
    for code in range(first, last + 1):
        # the slot's character under the chosen code page; slots the page
        # leaves undefined (0xFF in iso-8859-8, say) render as .notdef
        try:
            char = bytes([code]).decode(encoding)
        except UnicodeDecodeError:
            char = "\ufffd"
        if only is not None and char not in only:
            glyphs.append((len(bitmaps), 1, 1, notdef_advance, 0, 0))
            bitmaps.append(0)
            continue
        face.load_char(char, flags)
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch, buf = bm.width, bm.rows, bm.pitch, bm.buffer
        off = len(bitmaps)
        acc, mask = 0, 0x80
        for y in range(h):
            row = y * pitch
            for x in range(w):
                if buf[row + (x >> 3)] & (0x80 >> (x & 7)):
                    acc |= mask
                mask >>= 1
                if mask == 0:
                    bitmaps.append(acc)
                    acc, mask = 0, 0x80
        if mask != 0x80:          # pad the glyph out to a byte boundary
            bitmaps.append(acc)
        glyphs.append((off, w, h, g.advance.x >> 6, g.bitmap_left,
                       1 - g.bitmap_top))
    y_advance = face.size.height >> 6
    return bytes(bitmaps), glyphs, y_advance


def emit(name, bitmaps, glyphs, y_advance, first=FIRST, last=LAST):
    # (labels are the Latin-1 reading of each slot; for another code page
    # they are only a position aid)
    out = []
    out.append("const uint8_t %sBitmaps[] PROGMEM = {" % name)
    for i in range(0, len(bitmaps), 12):
        chunk = ", ".join("0x%02X" % b for b in bitmaps[i:i + 12])
        out.append("  " + chunk + ("," if i + 12 < len(bitmaps) else " };"))
    out.append("")
    out.append("const GFXglyph %sGlyphs[] PROGMEM = {" % name)
    for i, (off, w, h, xa, xo, yo) in enumerate(glyphs):
        code = first + i
        label = "'%s'" % chr(code) if 0x20 <= code < 0x7F or code >= 0xA1 else ""
        end = " }," if i + 1 < len(glyphs) else " } };"
        out.append("  { %5d, %3d, %3d, %3d, %4d, %4d%s // 0x%02X %s"
                   % (off, w, h, xa, xo, yo, end, code, label))
    out.append("")
    out.append("const GFXfont %s PROGMEM = {" % name)
    out.append("  (uint8_t  *)%sBitmaps," % name)
    out.append("  (GFXglyph *)%sGlyphs," % name)
    out.append("  0x%02X, 0x%02X, %d };" % (first, last, y_advance))
    out.append("")
    out.append("// Approx. %d bytes" % (len(bitmaps) + len(glyphs) * 7 + 7))
    return "\n".join(out) + "\n"


def write_family(family, ttf, sizes, autohint=False, encoding="latin-1",
                 weight=None):
    fam_dir = os.path.join(FONT_DIR, family)
    os.makedirs(fam_dir, exist_ok=True)
    enc_path = os.path.join(fam_dir, "ENCODING")
    if encoding.lower().replace("_", "-") != "latin-1":
        with open(enc_path, "w", newline="\n") as f:
            f.write(encoding.lower().replace("_", "-") + "\n")
    elif os.path.exists(enc_path):
        os.remove(enc_path)
    written = []
    for size in sizes:
        name = "%s_%dpt8b" % (family, size)
        bitmaps, glyphs, ya = render(ttf, size, autohint=autohint,
                                     encoding=encoding, weight=weight)
        path = os.path.join(fam_dir, name + ".h")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(emit(name, bitmaps, glyphs, ya))
        written.append((name, len(bitmaps) + len(glyphs) * 7))
    # the 48 pt face used for the current temperature: digits and friends only
    name = "%s_48pt8b_temperature" % family
    bitmaps, glyphs, ya = render(ttf, 48, only=TEMP_CHARS, autohint=autohint,
                                 encoding=encoding, weight=weight)
    with open(os.path.join(fam_dir, name + ".h"), "w", encoding="utf-8",
              newline="\n") as f:
        f.write(emit(name, bitmaps, glyphs, ya))
    written.append((name, len(bitmaps) + len(glyphs) * 7))
    # selector header, kept for parity with the families upstream ships
    names = ["%s_%dpt8b" % (family, s) for s in sizes] + [name]
    with open(os.path.join(FONT_DIR, family + ".h"), "w", encoding="utf-8",
              newline="\n") as f:
        f.write("#pragma once\n\n")
        for n in sorted(names):
            f.write('#include "%s/%s.h"\n' % (family, n))
        f.write("\n")
        for n in sorted(names):
            f.write("#define FONT_%s %s\n" % (n.split("_", 1)[1], n))
    return written


if __name__ == "__main__":
    args = sys.argv[1:]
    autohint = "--autohint" in args
    encoding = "latin-1"
    weight = None
    for a in list(args):
        if a.startswith("--encoding="):
            encoding = a.split("=", 1)[1]
        elif a.startswith("--weight="):
            weight = float(a.split("=", 1)[1])
    args = [a for a in args if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    family, ttf = args[0], args[1]
    sizes = [int(s) for s in args[2:]] or DEFAULT_SIZES
    total = 0
    for name, size in write_family(family, ttf, sizes, autohint, encoding,
                                   weight):
        print("%-36s %7d bytes" % (name, size))
        total += size
    print("%-36s %7d bytes (%.0f KB of flash)" % ("total", total, total / 1024))
