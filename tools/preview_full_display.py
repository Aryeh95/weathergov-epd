#!/usr/bin/env python3
"""Full-display mock of the final icon mix, in measured Spectra 6 ink.

Approximates the 800x480 layout with the user's 7-day / 12-widget
config: Google set-2 icon for current conditions (168px) and the daily
forecast (48px), native outline icons in the 24-hour graph (32px), and
the InkyPi widget set (40px, 6-row layout). Sample weather data.

Usage: python tools/preview_full_display.py
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import generate_color_icons as legacy      # noqa: E402
import generate_native_icons as native     # noqa: E402
import generate_google_preview as gpre     # noqa: E402

W, H = 800, 480
PAPER = tuple(native.PAPER)
INK = {i: tuple(c) for i, c in native.MEASURED.items()}
BLACK, RED, BLUE, YELLOW = INK[1], INK[3], INK[6], INK[4]

FONTS = "C:/Windows/Fonts/"


def font(size, bold=False):
    try:
        return ImageFont.truetype(
            FONTS + ("arialbd.ttf" if bold else "arial.ttf"), size)
    except OSError:
        return ImageFont.load_default()


def paste_indices(canvas, indices, size, x, y):
    px = canvas.load()
    for j in range(size):
        for i in range(size):
            idx = indices[j * size + i]
            if idx:
                px[x + i, y + j] = INK[idx]


def google_icon(code, size):
    return legacy.quantize_condition(gpre.flatten(gpre.GOOGLE_MAP[code]),
                                     size)


def native_icon(code, size):
    return native.quantize_native(native.draw_condition(code), size)


def inkypi_widget(name, size):
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    sat = None if name in legacy.SOFT_WIDGET_ICONS else legacy.SATURATION
    return legacy.quantize(os.path.join(icon_dir, name + ".png"),
                           size, dither=True, saturation=sat)


def moon_widget(size):
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.draw_moons(icon_dir)
    return legacy.quantize(os.path.join(icon_dir, "waxinggibbous.png"),
                           size, dither=True, allowed=legacy.MOON_PALETTE)


def quantize_on_black(path, size, sat=1.35):
    """Dark-mode quantization: composite over black, keep the artwork's
    light tones (no paper-darkening pass), dither neutrals black/white,
    snap colors to the nearest ink. Transparent pixels stay index 0 so
    the black background shows through."""
    import colorsys
    im = Image.open(path).convert("RGBA").resize((size, size), Image.LANCZOS)
    alpha = im.getchannel("A")
    rgb = Image.new("RGB", im.size, (0, 0, 0))
    rgb.paste(im, mask=alpha)
    px = rgb.load()
    neutral = [[False] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            r, g, b = px[x, y]
            hh, ss, vv = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if ss < 0.16:
                neutral[y][x] = True
            else:
                r2, g2, b2 = colorsys.hsv_to_rgb(hh, min(1.0, ss * sat), vv)
                px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255))
    g1 = rgb.convert("L").convert("1")
    gp, ap = g1.load(), alpha.load()
    out = []
    for y in range(size):
        for x in range(size):
            if ap[x, y] < legacy.ALPHA_THRESHOLD:
                out.append(0)
            elif neutral[y][x]:
                out.append(2 if gp[x, y] else 1)
            else:
                r, g, b = px[x, y]
                best, bd = 0, 1 << 30
                for i, (pr, pg, pb) in enumerate(native.NATIVE):
                    dd = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                    if dd < bd:
                        bd, best = dd, i
                out.append(best + 1)
    return out


def google_icon_dark(code, size):
    path = os.path.join(TOOLS_DIR, "google_icons",
                        gpre.GOOGLE_MAP[code] + ".png")
    return quantize_on_black(path, size)


def invert_bw(indices):
    """Swap black and white indices (for outline art on a dark ground)."""
    return [2 if i == 1 else 1 if i == 2 else i for i in indices]


def draw_custom_icons_dark(icon_dir):
    """Dark-mode variants of the indoor icons: white house so the black
    background doesn't swallow the silhouette."""
    WHITE = (255, 255, 255, 255)
    BLACK4 = (0, 0, 0, 255)
    BLUE4 = (30, 110, 220, 255)
    RED4 = (230, 40, 40, 255)
    for name, inner in [("intemp_dark", "temp"), ("inhumidity_dark", "hum")]:
        im = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.polygon([(256, 40), (30, 235), (95, 235), (95, 470), (417, 470),
                   (417, 235), (482, 235)], fill=WHITE)
        if inner == "temp":
            legacy._thermometer(d, 256, 240, 400, 30, 56, BLACK4, RED4)
        else:
            legacy._droplet(d, 256, 330, 100, BLACK4)
            legacy._droplet(d, 256, 330, 74, BLUE4)
        im.save(os.path.join(icon_dir, name + ".png"))


ORANGE = "orange"  # sentinel: render as a yellow/red pixel checker


def paste_checker(im, mask):
    """Fill the masked pixels with a fine yellow/red checker -- the
    panel's closest thing to orange ink."""
    px, mp = im.load(), mask.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            if mp[x, y] >= 128:
                px[x, y] = INK[4] if (x + y) % 2 else INK[3]


def draw_mixed(im, fn, fill):
    """Run drawing callback fn(draw, color); if fill is the ORANGE
    sentinel, draw into a mask and checker-fill it instead."""
    if fill is not ORANGE:
        fn(ImageDraw.Draw(im), fill)
        return
    mask = Image.new("L", im.size, 0)
    fn(ImageDraw.Draw(mask), 255)
    paste_checker(im, mask)


def outline_only(indices):
    """Line-art approximation of the native icons for the BW panel: keep
    the black outline pixels (as white index 2 for a dark ground), drop
    fills."""
    return [2 if i == 1 else 0 for i in indices]


def main(dark=False, bw=False):
    icon_dir = os.path.join(TOOLS_DIR, "inkypi_icons")
    legacy.fetch_icons(icon_dir)
    legacy.draw_custom_icons(icon_dir)
    draw_custom_icons_dark(icon_dir)

    global RED, BLUE, YELLOW
    if bw:  # BW V2 dark: one ink; everything collapses to white-on-black
        dark = True
        bw_white = (226, 230, 231)
        for i in range(2, 7):
            INK[i] = bw_white
        INK[1] = (24, 26, 28)
        RED = BLUE = YELLOW = bw_white

    BG = INK[1] if dark else PAPER
    FG = INK[2] if dark else BLACK
    HI = RED   # dark mode: solid red, bold text to compensate for dimness

    def cond_icon(code, size):
        if bw:
            art = native.draw_condition(code)
            return outline_only(native.quantize_native(art, size))
        return google_icon_dark(code, size) if dark \
            else google_icon(code, size)

    def wgt_icon(name, size):
        if bw:
            art = native.draw_widget(name)
            return outline_only(native.quantize_native(art, size))
        if dark:
            if name in ("intemp", "inhumidity"):
                name += "_dark"
            return quantize_on_black(
                os.path.join(icon_dir, name + ".png"), size, sat=1.8)
        return inkypi_widget(name, size)

    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)

    # ---- current conditions: Google 168px icon in the 196 slot
    paste_indices(im, cond_icon("02d", 168), 168, 14, 12)
    d.text((196, 30), "82", font=font(72, True), fill=FG)
    d.text((288, 36), "\u00b0F", font=font(28, True), fill=FG)
    d.text((196, 118), "Feels Like 84\u00b0", font=font(17), fill=FG)
    d.text((196, 144), "Partly Cloudy", font=font(17), fill=FG)

    # ---- header: city + date
    d.text((792, 10), "New York", font=font(22, True), fill=FG,
           anchor="ra")
    d.text((792, 40), "Thursday, August 27", font=font(15), fill=FG,
           anchor="ra")

    # ---- widget column: 6 rows x 2 cols, 40px InkyPi icons
    widgets = [("Sunrise", "sunrise", "6:24"), ("Sunset", "sunset", "7:42"),
               ("Wind", "wind", "8 mph"), ("Humidity", "humidity", "64%"),
               ("UV Index", "uvi", "6 High"), ("Pressure", "pressure",
                                               "30.1 in"),
               ("Air Quality", "aqi", "42 Good"), ("Visibility", "visibility",
                                                   "10 mi"),
               ("Dew Point", "dewpoint", "68\u00b0"),
               ("Moon", None, "84% Gib"),
               ("Indoor Temp", "intemp", "75\u00b0"),
               ("Indoor Hum", "inhumidity", "48%")]
    x0s, y0, pitch, colw = (10, 190), 182, 46, 178
    for i, (label, icon, value) in enumerate(widgets):
        col, row = i % 2, i // 2
        x, y = x0s[col], y0 + row * pitch
        if icon is None:
            idx = moon_widget(40)
            if dark:
                idx = invert_bw(idx)
        else:
            idx = wgt_icon(icon, 40)
        paste_indices(im, idx, 40, x, y + 2)
        d.text((x + 48, y + 4), label, font=font(13), fill=FG)
        val_color = FG
        if label == "UV Index":
            val_color = INK[4]
        if label == "Air Quality":
            val_color = INK[5]
        d.text((x + 48, y + 22), value, font=font(15, True), fill=val_color)

    # ---- 7-day forecast: Google 48px icons, Hi red | Lo blue
    days = [("Thu", "02d", 88, 71), ("Fri", "01d", 90, 72),
            ("Sat", "10d", 84, 69), ("Sun", "09d", 79, 66),
            ("Mon", "11d", 77, 64), ("Tue", "022d", 81, 65),
            ("Wed", "01d", 85, 67)]
    fx0, fw, fy = 380, (W - 380) // 7, 68
    small = font(14)
    for i, (day, code, hi, lo) in enumerate(days):
        cx = fx0 + i * fw + fw // 2
        d.text((cx, fy), day, font=small, fill=FG, anchor="ma")
        paste_indices(im, cond_icon(code, 48), 48, cx - 24, fy + 20)
        hi_s, lo_s = str(hi), str(lo)
        seg = "%s|%s" % (hi_s, lo_s)
        tw = d.textlength(seg, font=small)
        hx = cx - tw / 2
        hi_font = font(14, True) if dark else small
        draw_mixed(im, lambda dd, c, hx=hx, hi_s=hi_s, hi_font=hi_font:
                   dd.text((hx, fy + 74), hi_s, font=hi_font, fill=c), HI)
        hx += d.textlength(hi_s, font=small)
        d.text((hx, fy + 74), "|", font=small, fill=FG)
        hx += d.textlength("|", font=small)
        d.text((hx, fy + 74), lo_s, font=small, fill=BLUE)

    # ---- 24-hour graph: boxed axes, temp line + precip bars
    gx0, gy0, gx1, gy1 = 420, 210, 780, 440
    d.line([(gx0, gy1), (gx1, gy1)], fill=FG, width=2)
    d.line([(gx0, gy0 + 40), (gx0, gy1)], fill=FG, width=2)
    for i, t in enumerate(range(gy0 + 40, gy1, 40)):
        d.line([(gx0 - 4, t), (gx1, t)],
               fill=(70, 78, 82) if dark else INK[2], width=1)
        d.text((gx0 - 8, t), str(90 - i * 10), font=font(11), fill=FG,
               anchor="rm")
    hours = ["3PM", "9PM", "3AM", "9AM"]
    for i, hlab in enumerate(hours):
        x = gx0 + i * (gx1 - gx0) // 4
        d.text((x, gy1 + 4), hlab, font=font(11), fill=FG, anchor="ma")
    pts = []
    for i in range(25):
        t = 76 + 10 * math.sin(math.pi * (i - 2) / 24) - i * 0.2
        x = gx0 + i * (gx1 - gx0) / 24
        y = gy1 - (t - 55) / 40 * (gy1 - gy0 - 40)
        pts.append((x, y))
    for i in (14, 16, 18):
        x = gx0 + i * (gx1 - gx0) / 24
        d.rectangle([x - 4, gy1 - 36 - (i % 5) * 8, x + 4, gy1 - 1],
                    fill=BLUE)
    draw_mixed(im, lambda dd, c: dd.line(pts, fill=c,
                                         width=4 if dark else 3), HI)
    # 32px native icons above the graph, every 6 hours
    for i, code in [(1, "02d"), (7, "01n"), (13, "10d"), (19, "09d")]:
        x = int(gx0 + i * (gx1 - gx0) / 24)
        icon32 = native_icon(code, 32)
        if dark:
            icon32 = invert_bw(icon32)
        paste_indices(im, icon32, 32, x - 16, gy0)

    fname = ("full_display_bw_dark.png" if bw else
             "full_display_dark.png" if dark else "full_display_mock.png")
    out = os.path.join(native.PREVIEW_DIR, fname)
    os.makedirs(native.PREVIEW_DIR, exist_ok=True)
    im.resize((W * 2, H * 2), Image.NEAREST).save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(dark="--dark" in sys.argv, bw="--bw-dark" in sys.argv)
