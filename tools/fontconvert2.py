#!/usr/bin/env python3
"""Generate 2-bit (anti-aliased) twins of a family's Adafruit GFX fonts, for
the 7.5in panel's four greys (GREYS_75).

Each glyph is rendered by FreeType's auto-hinter in its grey mode (stems on
whole pixels, the edges of curves and diagonals shaded), and each dot is
stored as one of four 2-bit levels by how much of it the glyph covers:
0 none, 1 light grey, 2 dark grey, 3 foreground (black). At 14 pt and
under, where stems are thin, only 0/1/3 are emitted so strokes stay crisp
(0.25/0.60 at <= 7 pt; 0.35/0.82 at 8..14 pt). From 16 pt up, all four
levels are used (0.20 light grey, 0.50 dark grey, 0.80 black) to smooth
large curves and hero numerals with zero extra flash. Four dots a byte,
the first in the top bits, each glyph starting on a byte.

The glyph table is the GFX one (GFXglyph), with the grey hinting's own
advances: the 1-bit hinting moves stems by a dot here and there, and its
advances under these glyphs left uneven gaps ("Poi nt"). The renderer
measures text with the 2-bit face too (Grey75Display::getTextBounds), so it
lines up as drawn. Each face also carries a signature of its 1-bit twin (line
height and a few glyphs' metrics) by which the renderer finds the twin of
the face it has been asked to draw with.

Usage:
    python tools/fontconvert2.py [--autohint] <Family> <font.ttf> [size ...]

Pass --autohint exactly when the 1-bit family was made with it (Bitter was),
so the signatures match. Writes lib/esp32-weather-epd-assets/fonts/
<Family>_2bit/<Family>_<size>pt8b_2bit.h for each size (default: the 1-bit
family's 17), the 48 pt temperature face, and fonts/<Family>_2bit.h, which
defines Font2Face FONT2[] guarded by FONT_TABLE_HAS_<size>.

    pip install freetype-py
"""
import os
import sys

import freetype

import fontconvert as fc

SIG_CHARS = "Hag0"


def levels_for(size):
    if size <= 7:
        return (0.25, None, 0.60)
    if size <= 14:
        return (0.35, None, 0.82)
    return (0.20, 0.50, 0.80)


def render2(ttf, size, only=None, autohint=False):
    """(bitmaps, glyphs, yAdvance, signature) for one face in 2 bits."""
    mono_bitmaps, mono_glyphs, y_advance = fc.render(ttf, size, only=only,
                                                     autohint=autohint)
    face = fc.open_face(ttf)
    face.set_char_size(size << 6, 0, fc.DPI, 0)
    flags = freetype.FT_LOAD_TARGET_NORMAL | freetype.FT_LOAD_FORCE_AUTOHINT
    lo, mid, hi = levels_for(size)
    bitmaps = bytearray()
    glyphs = []
    for i, code in enumerate(range(fc.FIRST, fc.LAST + 1)):
        mono = mono_glyphs[i]
        char = bytes([code]).decode("latin-1")
        if only is not None and char not in only:
            glyphs.append((len(bitmaps), 1, 1, mono[3], 0, 0))
            bitmaps.append(0)
            continue
        face.load_char(char, flags)
        face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch, buf = bm.width, bm.rows, bm.pitch, bm.buffer
        off = len(bitmaps)
        cov = [buf[y * pitch + x] / 255.0 for y in range(h) for x in range(w)]
        lv = [3 if c >= hi else (2 if (mid is not None and c >= mid)
                                 else (1 if c >= lo else 0))
              for c in cov]
        # A glyph with no core dot -- a full stop at 7 pt covers about half
        # of each of three dots -- would be all light grey and all but
        # vanish: its darkest dots (within 10 % of the darkest) become core (3).
        if lv and 3 not in lv and max(cov) > 0:
            top = max(cov)
            lv = [3 if c >= 0.9 * top else v for c, v in zip(cov, lv)]
        acc, n = 0, 0
        for v in lv:
            acc |= v << (6 - 2 * n)
            n += 1
            if n == 4:
                bitmaps.append(acc)
                acc, n = 0, 0
        if n:
            bitmaps.append(acc)
        if w == 0 or h == 0:      # a space: no ink, as the 1-bit face
            glyphs.append((off, 0, 0, g.advance.x >> 6, 0, 0))
            continue
        adv, xo = g.advance.x >> 6, g.bitmap_left
        if char in ".,:":
            # a dot of clear space each side: at 6 pt the full stop's two-dot
            # advance put it against the foot of the "1" after it ("0.1"
            # read "01")
            xo = max(xo, 1)
            adv = max(adv, xo + w + 1)
        glyphs.append((off, w, h, adv, xo, 1 - g.bitmap_top))
    sig = [y_advance]
    for ch in SIG_CHARS:
        m = mono_glyphs[ord(ch) - fc.FIRST]
        sig += [m[3], m[1], m[2], m[4], m[5]]
    if len(bitmaps) > 0xFFFF:
        sys.exit("%s %d pt: %d bytes, past GFXglyph's 16-bit offsets"
                 % (ttf, size, len(bitmaps)))
    return bytes(bitmaps), glyphs, y_advance, sig


def write_family(family, ttf, sizes, autohint=False):
    out_name = family + "_2bit"
    fam_dir = os.path.join(fc.FONT_DIR, out_name)
    os.makedirs(fam_dir, exist_ok=True)
    faces = [(s, None, "%s_%dpt8b_2bit" % (family, s), "%dpt8b" % s)
             for s in sizes]
    faces.append((48, fc.TEMP_CHARS,
                  "%s_48pt8b_temperature_2bit" % family,
                  "48pt8b_temperature"))
    written = []
    for size, only, name, _ in faces:
        bitmaps, glyphs, ya, sig = render2(ttf, size, only, autohint)
        text = fc.emit(name, bitmaps, glyphs, ya)
        text = ("// 2-bit: 0 none, 1 light grey, 2 dark grey, 3 black; "
                "four dots a byte (tools/fontconvert2.py)\n"
                "const int16_t %sSig[] PROGMEM = { %s };\n\n"
                % (name, ", ".join(str(v) for v in sig))) + text
        with open(os.path.join(fam_dir, name + ".h"), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(text)
        written.append((name, len(bitmaps) + len(glyphs) * 7))
    with open(os.path.join(fc.FONT_DIR, out_name + ".h"), "w",
              encoding="utf-8", newline="\n") as f:
        f.write("// %s in 2 bits (tools/fontconvert2.py): the faces and the\n"
                "// signature of each one's 1-bit twin, for renderer.cpp\n"
                "#pragma once\n\n" % family)
        for _, _, name, tag in faces:
            f.write("#if defined(FONT_TABLE_HAS_%s) && FONT_TABLE_HAS_%s\n"
                    '#include "%s/%s.h"\n'
                    "#endif\n" % (tag, tag, out_name, name))
        f.write("\nstruct Font2Face\n"
                "{\n"
                "  const GFXfont *grey;\n"
                "  const int16_t *sig;\n"
                "};\n\n"
                "static const Font2Face FONT2[] = {\n")
        for _, _, name, tag in faces:
            f.write("#if defined(FONT_TABLE_HAS_%s) && FONT_TABLE_HAS_%s\n"
                    "  {&%s, %sSig},\n"
                    "#endif\n" % (tag, tag, name, name))
        f.write("  {nullptr, nullptr},\n"
                "};\n")
    return written


if __name__ == "__main__":
    args = sys.argv[1:]
    autohint = "--autohint" in args
    args = [a for a in args if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    family, ttf = args[0], args[1]
    sizes = [int(s) for s in args[2:]] or fc.DEFAULT_SIZES
    total = 0
    for name, size in write_family(family, ttf, sizes, autohint):
        print("%-40s %7d bytes" % (name, size))
        total += size
    print("%-40s %7d bytes (%.0f KB of flash)" % ("total", total, total / 1024))
