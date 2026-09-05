"""A/B sheet for the long-band-name question: today's abbreviation (badges.ppm)
beside the two-line wrap (badges_2line.ppm, from badge_preview --twoline).

  /tmp/badge_preview badges.ppm > badges.tsv
  /tmp/badge_preview badges_2line.ppm --twoline > badges_2line.tsv
  python3 tools/badge_preview/ab_sheet.py   # -> badges_ab.png (cwd)
"""
from PIL import Image, ImageDraw, ImageFont
import csv

CELL_W, CELL_H = 162, 56
A = Image.open("badges.ppm"); B = Image.open("badges_2line.ppm")
ra = list(csv.reader(open("badges.tsv"), delimiter="\t"))
rb = list(csv.reader(open("badges_2line.tsv"), delimiter="\t"))

def f(sz, bold=False):
    try:
        return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf"
                                  % ("-Bold" if bold else ""), sz)
    except OSError:
        return ImageFont.load_default()
FB, F, FS = f(15, True), f(14), f(12)

SHOW = ["151-200", "101-150", "201-300", "301-500", "> 500"]
idx = [i for i, r in enumerate(ra) if r[0].startswith("AQI (US)") and r[1] in SHOW]
idx.sort(key=lambda i: SHOW.index(ra[i][1]))

SC = 3
CW, CH = CELL_W * SC, CELL_H * SC
PAD, LBL = 18, 250
COLA = LBL
COLB = COLA + CW + 30
W = COLB + CW + PAD
ROW = CH + 46
HEAD = 132
H = HEAD + len(idx) * ROW + PAD

img = Image.new("RGB", (W, H), (247, 247, 248))
d = ImageDraw.Draw(img)
d.text((PAD, 14), "Long band names: abbreviate, or wrap to two lines?", font=f(20, True), fill=(20, 20, 20))
for i, t in enumerate([
  "One full widget row (162 x 56 px, the 5-row layout's pitch), drawn at 3x with the widget's real label and value baselines.",
  "A = today: full name at 7pt, then 5pt, then the locale's short form.   B = proposed: full name at 7pt, then wrapped to two 5pt lines, short form only as a last resort.",
  "The dashed line is where the next widget row begins."]):
    d.text((PAD, 44 + i * 16), t, font=FS, fill=(95, 95, 95))
d.text((COLA, HEAD - 26), "A — abbreviate (today)", font=FB, fill=(60, 60, 60))
d.text((COLB, HEAD - 26), "B — wrap to two lines", font=FB, fill=(30, 90, 160))

for n, i in enumerate(idx):
    y = HEAD + n * ROW
    if n % 2 == 0:
        d.rectangle([0, y - 8, W, y + ROW - 12], fill=(238, 238, 241))
    d.text((PAD, y + 2), "AQI %s" % ra[i][1], font=FB, fill=(20, 20, 20))
    d.text((PAD, y + 24), '"%s"' % ra[i][3], font=FS, fill=(90, 90, 90))
    same = ra[i][4] == rb[i][4] and ra[i][6] == rb[i][6]
    d.text((PAD, y + 46), "unchanged" if same else "B shows the full name",
           font=FS, fill=(140, 140, 140) if same else (30, 110, 60))
    if ra[i][1] == "101-150":
        d.text((PAD, y + 64), "two lines are not enough:", font=FS, fill=(175, 75, 15))
        d.text((PAD, y + 78), '"Sensitive Groups" is 71 px', font=FS, fill=(175, 75, 15))
        d.text((PAD, y + 92), "at 5pt, against 59 available", font=FS, fill=(175, 75, 15))
    if ra[i][1] == "> 500":
        d.text((PAD, y + 64), "one word: nothing to break on", font=FS, fill=(175, 75, 15))

    for src, cx, tsv in ((A, COLA, ra), (B, COLB, rb)):
        cell = src.crop((0, i * CELL_H, CELL_W, (i + 1) * CELL_H)).resize((CW, CH), Image.NEAREST)
        img.paste(cell, (cx, y))
        d.rectangle([cx - 1, y - 1, cx + CW, y + CH], outline=(205, 205, 205))
        for xd in range(cx, cx + CW, 12):
            d.line([xd, y + CH, xd + 6, y + CH], fill=(170, 170, 190), width=2)
        d.text((cx, y + CH + 8), tsv[i][6], font=FS, fill=(110, 110, 110))

img.save("badges_ab.png")
print("badges_ab.png", img.size)
