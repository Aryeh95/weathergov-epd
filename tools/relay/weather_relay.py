#!/usr/bin/env python3
"""A caching relay for esp32-weather-epd, for an always-on machine at home.

The display asks the relay for each URL over plain HTTP on the home network:

    GET http://<relay>:8080/https/api.weather.gov/alerts/active?point=...

The relay fetches it from the service, passes the status and body back
unchanged, and remembers how often each display asks for it. Shortly before
the next expected wake it fetches the URL again, so the reply is waiting and
the display's whole fetch takes about a second instead of 10-20 seconds of
TLS handshakes and slow servers.

It knows nothing about the weather services themselves: whatever the
firmware asks for is fetched, so new sources need no change here. Only the
hosts in ALLOWED_HOSTS (or --allow) are relayed, so it is not an open proxy
for the rest of the network.

Python 3.7+, standard library only. See README.md for running it as a
service.
"""

import argparse
import gzip
import logging
import re
import statistics
import threading
import time
import urllib.error
import urllib.request
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ALLOWED_HOSTS = {
    "api.weather.gov",
    "api.open-meteo.com",
    "air-quality-api.open-meteo.com",
    "www.airnowapi.org",
    "pollen.googleapis.com",
    "weather.googleapis.com",
    "ims.gov.il",
    "air.sviva.gov.il",
    "air-papi.sviva.gov.il",
}

# Request headers that are about the hop to the relay, not the request
HOP_HEADERS = {
    "host", "connection", "keep-alive", "accept-encoding", "content-length",
    "proxy-connection", "te", "upgrade", "x-relay-timeout",
}

# Query parameters whose values are kept out of the log
SECRET_PARAMS = re.compile(r"(?i)((?:api_?)?key|token|secret)=[^&]*")

log = logging.getLogger("relay")


def censor(url):
    return SECRET_PARAMS.sub(lambda m: m.group(1) + "=***", url)


class Entry:
    """One URL (with the headers it was asked with) and what the relay
    knows about it."""

    def __init__(self, url, headers):
        self.url = url
        self.headers = headers
        self.status = None
        self.body = b""
        self.ctype = "application/json"
        self.fetched = 0.0        # when the cached body was fetched
        self.asked = []           # when a display asked, most recent last
        self.lock = threading.Lock()

    def interval(self):
        """How often the display asks: the median gap between its last
        few requests, so one long night's sleep doesn't throw it off."""
        gaps = [b - a for a, b in zip(self.asked, self.asked[1:])]
        return statistics.median(gaps) if gaps else None


class Relay:
    def __init__(self, max_age, lead, allowed, max_entries=300):
        self.max_age = max_age
        self.lead = lead
        self.allowed = allowed
        self.max_entries = max_entries
        self.entries = {}
        self.lock = threading.Lock()

    def entry(self, url, headers):
        key = url + "\n" + "\n".join(f"{k}: {v}" for k, v in sorted(headers.items()))
        with self.lock:
            e = self.entries.get(key)
            if e is None:
                if len(self.entries) >= self.max_entries:
                    oldest = min(self.entries, key=lambda k: self.entries[k].asked[-1]
                                 if self.entries[k].asked else 0)
                    del self.entries[oldest]
                e = self.entries[key] = Entry(url, headers)
            return e

    def fetch(self, e, timeout, why):
        """Fetches e.url into e. Returns an error string when the service
        could not be reached at all (no HTTP status)."""
        req = urllib.request.Request(e.url, headers=dict(e.headers))
        req.add_header("Accept-Encoding", "gzip, deflate")
        start = time.time()
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                status, raw, hdrs = resp.status, resp.read(), resp.headers
        except urllib.error.HTTPError as err:
            status, raw, hdrs = err.code, err.read(), err.headers
        except Exception as err:  # DNS, refused, timeout, TLS
            log.warning("%s %s failed: %s", why, censor(e.url), err)
            return str(err) or type(err).__name__
        enc = (hdrs.get("Content-Encoding") or "").lower()
        if enc == "gzip":
            raw = gzip.decompress(raw)
        elif enc == "deflate":
            raw = zlib.decompress(raw)
        e.status, e.body = status, raw
        e.ctype = hdrs.get("Content-Type") or "application/json"
        e.fetched = time.time()
        log.info("%s %s -> %d, %d bytes in %.1f s", why, censor(e.url),
                 status, len(raw), e.fetched - start)
        return None

    def prefetch_loop(self, period=15):
        while True:
            time.sleep(period)
            now = time.time()
            with self.lock:
                entries = list(self.entries.items())
            for key, e in entries:
                if not e.asked:
                    continue
                every = e.interval()
                if every is None:
                    if now - e.asked[-1] > 86400:
                        with self.lock:
                            self.entries.pop(key, None)
                    continue
                due = e.asked[-1] + every   # the next expected request
                if now - e.asked[-1] > 3 * every + 3600:
                    # the display stopped asking: asleep, gone, or the URL
                    # changed; keep the entry a day in case it comes back
                    if now - e.asked[-1] > 86400:
                        with self.lock:
                            self.entries.pop(key, None)
                    continue
                if now >= due - self.lead and e.fetched < due - self.lead:
                    with e.lock:
                        if e.fetched < due - self.lead:
                            self.fetch(e, 30, "prefetch")


class Handler(BaseHTTPRequestHandler):
    relay = None
    protocol_version = "HTTP/1.1"
    timeout = 30
    server_version = "weather-relay/1"

    def log_message(self, fmt, *args):
        pass  # requests are logged in do_GET, with keys censored

    def reply(self, status, body, ctype="text/plain; charset=utf-8", extra=()):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        r = self.relay
        if self.path in ("/", "/status"):
            self.status_page()
            return
        m = re.match(r"^/https/([^/?#]+)(.*)$", self.path)
        if not m:
            self.reply(404, b"use /https/<host>/<path>\n")
            return
        host, rest = m.group(1), m.group(2) or "/"
        if host.split(":")[0].lower() not in r.allowed:
            log.warning("refused %s (not in the allowed hosts)", host)
            self.reply(403, f"{host} is not in the relay's allowed hosts\n".encode())
            return
        url = "https://" + host + rest
        headers = {k: v for k, v in self.headers.items()
                   if k.lower() not in HOP_HEADERS}
        try:
            timeout = min(max(int(self.headers.get("X-Relay-Timeout", "")) / 1000, 5), 60)
        except ValueError:
            timeout = 20
        e = r.entry(url, headers)
        now = time.time()
        if not e.asked or now - e.asked[-1] > 60:  # a retry is not a new wake
            e.asked = (e.asked + [now])[-6:]
        with e.lock:
            age = now - e.fetched
            if e.status == 200 and age <= r.max_age:
                how = "cached"
            else:
                err = r.fetch(e, timeout, "fetch")
                if err:
                    self.reply(502, (err + "\n").encode(),
                               extra=[("X-Relay-Error", err.replace("\n", " ")[:200])])
                    return
                age, how = 0, "live"
            status, body, ctype = e.status, e.body, e.ctype
            if status != 200:
                e.status = None  # never serve a failure from the cache
        log.info("%s %s -> %d (%s, %d s old)", self.client_address[0],
                 censor(url), status, how, age)
        self.reply(status, body, ctype, extra=[("X-Relay-Age", str(int(age)))])

    def status_page(self):
        now = time.time()
        lines = ["weather relay: %d URLs" % len(self.relay.entries), ""]
        with self.relay.lock:
            entries = list(self.relay.entries.values())
        for e in sorted(entries, key=lambda e: e.url):
            every = e.interval()
            lines.append("%s\n    asked %d s ago, every %s, cached copy %s"
                         % (censor(e.url),
                            now - e.asked[-1] if e.asked else -1,
                            "%d min" % (every / 60) if every else "?",
                            "%d s old" % (now - e.fetched) if e.fetched else "none"))
        self.reply(200, ("\n".join(lines) + "\n").encode())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--max-age", type=int, default=600,
                    help="seconds a fetched copy may be served for (default 600)")
    ap.add_argument("--lead", type=int, default=120,
                    help="seconds before the expected wake to prefetch (default 120)")
    ap.add_argument("--allow", action="append", default=[],
                    help="another host to relay (repeatable)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING,
                        format="%(asctime)s %(message)s")
    if args.max_age <= args.lead:
        ap.error("--max-age must be longer than --lead, or prefetched copies "
                 "would be too old to serve")

    Handler.relay = Relay(args.max_age, args.lead,
                          ALLOWED_HOSTS | {h.lower() for h in args.allow})
    threading.Thread(target=Handler.relay.prefetch_loop, daemon=True).start()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    server.daemon_threads = True
    logging.getLogger("relay").warning("weather relay on %s:%d", args.bind, args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
