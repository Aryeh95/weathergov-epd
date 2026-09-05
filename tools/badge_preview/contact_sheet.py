"""Lays the rendered badge strips out as one annotated sheet.

badge_preview.cpp writes two PPMs -- the badges as they render now and, with
--old, as they rendered before the corner fix -- plus a TSV of chip rects.
This turns them into a single PNG: each widget's value row at 3x, then its
top-left corner at 18x in both versions with the changed pixels ringed.

  g++ -std=gnu++17 -I platformio/include \
      -I platformio/lib/esp32-weather-epd-assets/fonts \
      tools/badge_preview/badge_preview.cpp -o /tmp/badge_preview
  /tmp/badge_preview badges.ppm > badges.tsv
  /tmp/badge_preview badges_old.ppm --old
  python3 tools/badge_preview/contact_sheet.py   # -> badges.png (cwd)
"""
from PIL import Image, ImageDraw, ImageFont
import csv

CELL_W, CELL_H = 162, 30
new = Image.open("badges.ppm"); old = Image.open("badges_old.ppm")
rows = list(csv.reader(open("badges.tsv"), delimiter="\t"))
N = len(rows)

def f(sz, bold=False):
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"
                                  % ("-Bold" if bold else ""), sz)
    except OSError:
        return ImageFont.load_default()

F, FB, FS, FSB = f(15), f(15, True), f(12), f(12, True)
LEVEL = {0: ("plain", "no badge, plain text"),
         1: ("green", "solid ink"),
         2: ("yellow", "solid ink"),
         3: ("amber", "red / yellow dither"),
         4: ("red", "solid ink"),
         5: ("plum", "red / blue dither"),
         6: ("maroon", "red / black dither")}
DITHERED = {3, 5, 6}

SC, ZM, ZC = 3, 18, 6
LBL_W = 286
ROW_H = CELL_H * SC + 16
PAD, HEAD = 16, 118
COL1 = LBL_W
COL2 = COL1 + CELL_W * SC + 26
COL3 = COL2 + ZC * ZM + 20
Wt = COL3 + ZC * ZM + PAD
Ht = HEAD + ROW_H * N + PAD

img = Image.new("RGB", (Wt, Ht), (247, 247, 248))
d = ImageDraw.Draw(img)
d.text((PAD, 14), "Risk badges — every state drawRiskChip can draw", font=f(20, True), fill=(20, 20, 20))
for i, line in enumerate([
    "Rendered from the firmware's own chip geometry, its FreeSans 7pt/5pt metrics and the Spectra 6 ink palette, at 3x.",
    "The panel has no orange, plum or maroon ink, so those three are painted as a two-ink checkerboard.",
    "Right-hand pair: the badge's top-left corner at 18x, after the fix and before it. Blue rings mark the pixels the fix removed."]):
    d.text((PAD, 42 + i * 16), line, font=FS, fill=(95, 95, 95))
d.text((COL1, HEAD - 20), "widget value row (the 162 px left-panel column)", font=FS, fill=(130, 130, 130))
d.text((COL2, HEAD - 20), "corner", font=FS, fill=(130, 130, 130))
d.text((COL3, HEAD - 20), "was", font=FSB, fill=(185, 55, 55))

for i, (widget, cond, value, label, drawn, lvl, fit, x0, y0, x1, y1) in enumerate(rows):
    lvl, x0, y0 = int(lvl), int(x0), int(y0)
    y = HEAD + i * ROW_H
    if i % 2 == 0:
        d.rectangle([0, y - 8, Wt, y + ROW_H - 10], fill=(238, 238, 241))
    name, how = LEVEL[lvl]
    line = y + 2
    d.text((PAD, line), "%s  %s" % (widget, cond), font=FB, fill=(20, 20, 20))
    line += 20
    d.text((PAD, line), '"%s"' % drawn, font=F, fill=(60, 60, 60))
    line += 18
    if drawn != label:
        d.text((PAD, line), 'short for "%s"' % label, font=FS, fill=(110, 110, 110))
        line += 16
    d.text((PAD, line), "%s — %s" % (name, how), font=FS,
           fill=(175, 75, 15) if lvl in DITHERED else (120, 120, 120))
    line += 16
    note = {"wraps": "no badge: wraps as plain text",
            "full 5pt": "drops to the 5pt font to fit",
            "short 5pt": "at 5pt as well, to fit"}.get(fit)
    if note:
        d.text((PAD, line), note, font=FS, fill=(150, 80, 20))

    cell = new.crop((0, i * CELL_H, CELL_W, (i + 1) * CELL_H)).resize(
        (CELL_W * SC, CELL_H * SC), Image.NEAREST)
    img.paste(cell, (COL1, y))
    d.rectangle([COL1 - 1, y - 1, COL1 + CELL_W * SC, y + CELL_H * SC], outline=(205, 205, 205))

    if x0 >= 0:
        box = (x0 - 1, i * CELL_H + y0 - 1, x0 - 1 + ZC, i * CELL_H + y0 - 1 + ZC)
        ca, cb = new.crop(box), old.crop(box)
        for src, cx in ((ca, COL2), (cb, COL3)):
            img.paste(src.resize((ZC * ZM, ZC * ZM), Image.NEAREST), (cx, y))
            d.rectangle([cx - 1, y - 1, cx + ZC * ZM, y + ZC * ZM], outline=(205, 205, 205))
        # ring the pixels the fix actually changed, in both panels
        for px in range(ZC):
            for py in range(ZC):
                if ca.getpixel((px, py)) != cb.getpixel((px, py)):
                    for cx in (COL2, COL3):
                        d.rectangle([cx + px * ZM, y + py * ZM,
                                     cx + (px + 1) * ZM - 1, y + (py + 1) * ZM - 1],
                                    outline=(0, 120, 255), width=2)
        if lvl in DITHERED:
            d.rectangle([COL3 - 4, y - 4, COL3 + ZC * ZM + 3, y + ZC * ZM + 3],
                        outline=(200, 55, 55), width=2)

img.save("badges.png")
print("badges.png", img.size)
