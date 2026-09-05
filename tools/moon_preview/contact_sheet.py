"""Lays moon_preview's renders out as one sheet: today's 8 steps, a
16-step version, and the pairs a 28-step version cannot separate.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moon_preview as M
from PIL import Image, ImageDraw, ImageFont

def f(sz, bold=False):
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"
                                  % ("-Bold" if bold else ""), sz)
    except OSError:
        return ImageFont.load_default()
F, FB, FS = f(14), f(15, True), f(11)

NAMES = ["New", "Wax. Cresc.", "First Qtr", "Wax. Gibb.",
         "Full", "Wan. Gibb.", "Third Qtr", "Wan. Cresc."]
illum = lambda frac: round((1 - math.cos(2 * math.pi * frac)) / 2 * 100)

frames = {n: M.render(n, 48) for n in (8, 16, 28)}
diffs = {n: [sum(1 for p, q in zip(frames[n][i], frames[n][(i + 1) % n]) if p != q)
             for i in range(n)] for n in frames}

SC, CELL, PAD = 3, 48 * 3, 20
W = PAD * 2 + 16 * CELL + 15 * 10
img = Image.new("RGB", (W, 760), (247, 247, 248))
d = ImageDraw.Draw(img)
d.text((PAD, 14), "Moon phase granularity", font=f(21, True), fill=(20, 20, 20))
d.text((PAD, 44), "The project's own moon geometry (draw_moons), through the same Floyd-Steinberg quantizer that builds icons_moon.h, at the 48 px widget size.",
       font=FS, fill=(95, 95, 95))
d.text((PAD, 60), "Under each icon: illuminated %, then how many of the 2304 pixels changed from the icon to its left.",
       font=FS, fill=(95, 95, 95))

def strip(y, n, title, sub):
    d.text((PAD, y), title, font=FB, fill=(20, 20, 20))
    d.text((PAD + 190, y + 2), sub, font=FS, fill=(120, 120, 120))
    yy = y + 22
    step = (W - PAD * 2 - CELL) / (n - 1)
    for i in range(n):
        x = int(PAD + i * step)
        img.paste(M.to_image(frames[n][i], 48, SC), (x, yy))
        d.rectangle([x - 1, yy - 1, x + CELL, yy + CELL], outline=(210, 210, 210))
        d.text((x, yy + CELL + 4), "%d%%" % illum(i / n), font=FS, fill=(50, 50, 50))
        dlt = diffs[n][i - 1]
        d.text((x + 34, yy + CELL + 4), "+%d" % dlt, font=FS,
               fill=(200, 45, 45) if dlt < 60 else (150, 150, 150))
        if n == 8:
            d.text((x, yy + CELL + 18), NAMES[i], font=FS, fill=(120, 120, 120))
    return yy + CELL + 36

y = strip(96, 8, "Today — 8 steps", "one icon per named phase")
y = strip(y + 18, 16, "Proposed — 16 steps",
          "every step still distinct: the smallest change is 109 px")

# the pairs 28 steps cannot separate
d.text((PAD, y + 20), "Why not 28 (one per day)", font=FB, fill=(20, 20, 20))
d.text((PAD + 268, y + 22),
       "four adjacent pairs quantize to byte-identical icons — the sliver near new and full is thinner than a dithered pixel",
       font=FS, fill=(120, 120, 120))
yy = y + 46
dead = [i for i in range(28) if diffs[28][i] == 0]
x = PAD
for i in dead:
    j = (i + 1) % 28
    for k, lab in ((i, "%d%%" % illum(i / 28)), (j, "%d%%" % illum(j / 28))):
        img.paste(M.to_image(frames[28][k], 48, SC), (x, yy))
        d.rectangle([x - 1, yy - 1, x + CELL, yy + CELL], outline=(200, 55, 55))
        d.text((x, yy + CELL + 4), lab, font=FS, fill=(50, 50, 50))
        x += CELL + 4
    d.text((x - CELL - 4, yy + CELL + 18), "identical", font=FS, fill=(200, 45, 45))
    x += 46
img.save("moon.png")
print("moon.png", img.size)
