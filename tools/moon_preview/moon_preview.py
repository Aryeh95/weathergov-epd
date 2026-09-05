"""How many moon-phase steps can a 48px dithered icon actually show?

Draws the project's own moon geometry (generate_color_icons.draw_moons) at
N steps instead of 8, runs each through the SAME quantizer that builds
icons_moon.h, and reports how many pixels change between adjacent steps --
the answer to "could the widget show more than eight phases?" measured
rather than guessed.

  python3 tools/moon_preview/moon_preview.py          # the measurement
  python3 tools/moon_preview/contact_sheet.py         # -> moon.png (cwd)
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image
import generate_color_icons as gen

# scratch dir for the intermediate 512px PNGs; override with MOON_PREVIEW_OUT
OUT = os.environ.get("MOON_PREVIEW_OUT", "moon_preview_out")
os.makedirs(OUT, exist_ok=True)
S, R, CX, CY = 512, 220, 256, 256
LIT, SHADOW, EDGE = 208, 58, 40
CRATERS = gen.CRATERS if hasattr(gen, "CRATERS") else [
    (-70, -60, 46), (60, 30, 34), (-20, 90, 28), (95, -95, 24), (-115, 55, 20)]

def draw(theta, path):
    """draw_moons' geometry, parameterised on the phase angle instead of
    the 8-entry name list. theta 0 = new, pi = full."""
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    px = im.load()
    waxing = theta < math.pi
    for y in range(S):
        dy = y - CY
        if abs(dy) > R:
            continue
        half = math.sqrt(R * R - dy * dy)
        xt = math.cos(theta) * half
        for x in range(S):
            dx = x - CX
            d2 = dx * dx + dy * dy
            if d2 > R * R:
                continue
            if abs(theta) < 1e-9:
                lit = False
            elif abs(theta - math.pi) < 1e-9:
                lit = True
            elif waxing:
                lit = dx >= xt
            else:
                lit = dx <= -xt
            g = LIT if lit else SHADOW
            if lit:
                for cx2, cy2, cr in CRATERS:
                    if (dx - cx2) ** 2 + (dy - cy2) ** 2 <= cr * cr:
                        g = LIT - 40
                        break
            if d2 > (R - 7) * (R - 7):
                g = EDGE
            px[x, y] = (g, g, g, 255)
    im.save(path)

def render(n, size=48):
    """Returns the quantized 4-bit index lists for n equally spaced phases."""
    out = []
    for i in range(n):
        p = os.path.join(OUT, "m%02d_%02d.png" % (n, i))
        draw(2 * math.pi * i / n, p)
        out.append(gen.quantize(p, size, dither=True, allowed=gen.MOON_PALETTE))
    return out

def to_image(idx, size=48, scale=1):
    im = Image.new("RGB", (size, size), (255, 255, 255))
    px = im.load()
    for y in range(size):
        for x in range(size):
            v = idx[y * size + x]
            px[x, y] = (255, 255, 255) if v != 1 else (0, 0, 0)
    return im.resize((size * scale, size * scale), Image.NEAREST) if scale > 1 else im

if __name__ == "__main__":
    import json
    report = {}
    for n in (8, 16, 24, 28):
        frames = render(n)
        diffs = []
        for i in range(n):
            a, b = frames[i], frames[(i + 1) % n]
            diffs.append(sum(1 for p, q in zip(a, b) if p != q))
        report[n] = diffs
        print("N=%2d  adjacent-step pixel changes: min %3d  median %3d  max %4d"
              % (n, min(diffs), sorted(diffs)[len(diffs)//2], max(diffs)))
        print("      per step:", diffs)
    json.dump(report, open(os.path.join(OUT, "report.json"), "w"))
