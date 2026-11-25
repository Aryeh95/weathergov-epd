#!/usr/bin/env python3
"""Serve the setup portal's page on a PC, with made-up device answers, to
look at it or take its screenshots for the README.

    python tools/portal_preview.py [port] [--board=e1002|709|426|ee04]
                                   [--locale=en_US|he_IL|pl_PL]
    then open http://127.0.0.1:8770/

The page is platformio/data/portal.html, as the firmware serves it. The
device's answers are invented: /config is the committed placeholder
config.json (taken from git, never the working copy, which may hold your
real WiFi and keys) with a demo network name, /info describes a device in
setup mode, /scan lists made-up networks, and saves are answered "ok"
without storing anything.

    python tools/portal_preview.py --shots [--out=FILE]
writes the whole page as one tall picture (default portal-full.png here)
with Microsoft Edge or Chrome in headless mode, 500 px wide at 2x, in the
light theme. The README's showcase/portal-*.png are cut from it.
"""
import json, os, re, shutil, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PAGE = os.path.join(ROOT, "platformio", "data", "portal.html")

BOARDS = {
    "e1002": {"board": "Seeed reTerminal E1002", "target": "seeed_reterminal_e1002"},
    "709": {"board": "Seeed XIAO EE02, GDEB0709E01 1600x1200",
            "target": "seeed_xiao_ee02_709", "layout": "fixed", "graphs": True},
    "426": {"board": "Seeed XIAO EE05, 4.26in 800x480", "target": "seeed_xiao_ee05",
            "layout": "fixed", "tiles": 3},
    "ee04": {"board": "Seeed XIAO EE04 (TRMNL kit), 7.5in 800x480, greys",
             "target": "seeed_xiao_ee04_grey"},
}


def placeholder_config(locale="en_US"):
    raw = subprocess.check_output(["git", "-C", ROOT, "show", "HEAD:platformio/data/config.json"])
    text = raw.decode("utf-8")
    if locale.startswith("he"):
        # as an Israeli device is set up: IMS, Israel's time, Tel Aviv
        for key, value in (("forecast_source", "ims"),
                           ("timezone", "IST-2IDT,M3.4.4/26,M10.5.0"),
                           ("lat", "32.0853"), ("lon", "34.7818"),
                           ("city", "Tel Aviv")):
            text = re.sub(r'("%s":\s*)"[^"]*"' % key,
                          lambda m: m.group(1) + '"' + value + '"', text, count=1)
    # a demo network instead of the placeholder words
    text = re.sub(r'("ssid":\s*)"ssid"', r'\1"Home WiFi"', text, count=1)
    text = re.sub(r'("password":\s*)"password"', r'\1"correct-horse-battery"', text, count=1)
    return text


def info(board, locale="en_US"):
    now = int(time.time())
    wake = {"when": now - 600, "outcome": 0, "code": 200, "cause": 4, "awake_ms": 6800,
            "wifi_ms": 1400, "clock_ms": 300, "fetch_ms": 2600, "draw_ms": 2100,
            "sleep_s": 1800, "battery_mv": 4020, "rssi": -58}
    d = {"mode": "hotspot", "ssid": "WeatherEPD-Setup", "ap_password": "weatherepd",
         "ip": "192.168.4.1", "timeout_minutes": 10,
         "build": "main (preview)", "locale": locale, "rtl": locale.startswith("he"),
         "heat_stress_default": locale.startswith("he"),
         "default_timezone": ("IST-2IDT,M3.4.4/26,M10.5.0" if locale.startswith("he")
                              else "EST5EDT,M3.2.0,M11.1.0"),
         "default_forecast_source": "ims" if locale.startswith("he") else "nws",
         "fonts": ["Bitter", "FreeSans", "Lato", "Montserrat", "Ubuntu"],
         "last_wake": wake}
    d.update(BOARDS[board])
    return d


SCAN = {"status": "done", "networks": [
    {"ssid": "Home WiFi", "rssi": -48, "open": False},
    {"ssid": "Home WiFi Guest", "rssi": -55, "open": False},
    {"ssid": "Neighbour-5G-ext", "rssi": -71, "open": False},
    {"ssid": "CoffeeShop", "rssi": -83, "open": True}]}


def handler(board, locale):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype):
            b = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path in ("/", "/index.html"):
                self.send(200, open(PAGE, "rb").read(), "text/html; charset=utf-8")
            elif path == "/config":
                self.send(200, placeholder_config(locale), "application/json")
            elif path == "/info":
                self.send(200, json.dumps(info(board, locale)), "application/json")
            elif path == "/scan":
                self.send(200, json.dumps(SCAN), "application/json")
            else:
                self.send(404, "Not found", "text/plain")

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            self.rfile.read(n)
            self.send(200, json.dumps({"ok": True}), "application/json")
    return H


def browser():
    for name in ("msedge", "chrome", "google-chrome", "chromium"):
        p = shutil.which(name)
        if p:
            return p
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if os.path.exists(p):
            return p
    sys.exit("no Edge or Chrome found for --shots")


def shots(port, full):
    """The whole page as one tall picture."""
    from PIL import Image
    exe = browser()
    full = os.path.abspath(full)
    # 500 px: a Windows browser window cannot be narrower, even headless, so
    # a narrower capture only cuts the page's right edge off
    w, h = 500, 9000
    # a throwaway profile: with the browser already open, a headless call on
    # the usual profile hands the page to it and writes nothing
    import tempfile
    prof = tempfile.mkdtemp(prefix="portal_preview_")
    try:
        subprocess.run([exe, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        f"--user-data-dir={prof}", "--no-first-run",
                        "--force-device-scale-factor=2", f"--window-size={w},{h}",
                        "--blink-settings=preferredColorScheme=1",   # light
                        "--virtual-time-budget=4000", f"--screenshot={full}",
                        f"http://127.0.0.1:{port}/"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        shutil.rmtree(prof, ignore_errors=True)
    im = Image.open(full).convert("RGB")
    # trim the empty space below the page
    px = im.load()
    bg = px[im.width // 2, im.height - 1]
    bottom = im.height - 1
    while bottom > 0 and all(px[x, bottom] == bg for x in range(0, im.width, 7)):
        bottom -= 1
    im = im.crop((0, 0, im.width, bottom + 40))
    im.save(full)
    print("wrote", full, im.size)


def main():
    port, board, take, full, locale = 8770, "e1002", False, "portal-full.png", "en_US"
    for a in sys.argv[1:]:
        if a.isdigit():
            port = int(a)
        elif a.startswith("--board="):
            board = a.split("=", 1)[1]
        elif a == "--shots":
            take = True
        elif a.startswith("--locale="):
            locale = a.split("=", 1)[1]
        elif a.startswith("--out="):
            full = a.split("=", 1)[1]
    srv = ThreadingHTTPServer(("127.0.0.1", port), handler(board, locale))
    if take:
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        shots(port, full)
        srv.shutdown()
        return
    print(f"portal preview ({BOARDS[board]['board']}) at http://127.0.0.1:{port}/")
    srv.serve_forever()


if __name__ == "__main__":
    main()
