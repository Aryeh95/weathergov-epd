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
import urllib.parse
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

# Ephemeral guest tokens (e.g. Sviva's hourly JWT in Authorization/Cookie)
# rotate between wakes for the same public feed; forward the latest headers
# without splitting the cache into a new entry on every rotation.
EPHEMERAL_HEADERS = {"authorization", "cookie"}

# Query parameters whose values are kept out of the log
SECRET_PARAMS = re.compile(r"(?i)((?:api_?)?key|token|secret)=[^&]*")

log = logging.getLogger("relay")


def censor(url):
    return SECRET_PARAMS.sub(lambda m: m.group(1) + "=***", url)


def decompress_body(raw, encoding):
    enc = (encoding or "").strip().lower()
    if enc == "gzip":
        return gzip.decompress(raw)
    if enc == "deflate":
        try:
            return zlib.decompress(raw)
        except zlib.error:
            return zlib.decompress(raw, -zlib.MAX_WBITS)
    return raw


class SafeRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Blocks redirects to non-HTTPS URLs or hosts outside ALLOWED_HOSTS."""

    def __init__(self, allowed):
        super().__init__()
        self.allowed = allowed

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        parsed = urllib.parse.urlparse(newurl)
        host = (parsed.hostname or "").lower()
        if parsed.scheme != "https" or host not in self.allowed:
            raise urllib.error.URLError(
                f"refused redirect to {parsed.scheme}://{host}")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class Entry:
    """One URL (with its non-ephemeral headers) and what the relay knows
    about each display asking for it."""

    def __init__(self, url, headers):
        self.url = url
        self.headers = dict(headers)
        self.status = None
        self.body = b""
        self.ctype = "application/json"
        self.fetched = 0.0        # when the cached 200 body was fetched
        self.attempted = 0.0      # last fetch/prefetch attempt (even on error)
        self.asked_by_client = {} # client_ip -> list of wake timestamps
        self.lock = threading.RLock()

    def record_ask(self, client_ip, now):
        with self.lock:
            stale = [ip for ip, ts in self.asked_by_client.items()
                     if not ts or now - ts[-1] > 86400]
            for ip in stale:
                if ip != client_ip:
                    del self.asked_by_client[ip]
            ts = self.asked_by_client.get(client_ip, [])
            if not ts or now - ts[-1] > 60:  # a retry is not a new wake
                self.asked_by_client[client_ip] = (ts + [now])[-6:]

    def last_asked(self):
        with self.lock:
            return max((ts[-1] for ts in self.asked_by_client.values() if ts),
                       default=0.0)

    def _client_interval(self, ts):
        gaps = [b - a for a, b in zip(ts, ts[1:])]
        return statistics.median(gaps) if gaps else None

    def interval(self):
        """Representative interval (seconds) across active clients, for /status."""
        with self.lock:
            intervals = [iv for ts in self.asked_by_client.values()
                         for iv in (self._client_interval(ts),) if iv is not None]
        return min(intervals) if intervals else None

    def should_prefetch(self, now, max_age, lead):
        """True when any active display's next expected wake is within `lead`
        seconds and the currently cached copy would be expired by that wake."""
        with self.lock:
            clients = [(ip, list(ts)) for ip, ts in self.asked_by_client.items() if ts]
            status, fetched, attempted = self.status, self.fetched, self.attempted
        for _, ts in clients:
            every = self._client_interval(ts)
            if every is None:
                continue
            last = ts[-1]
            if now - last > 3 * every + 3600:
                continue
            wake = last + every
            due = wake - lead
            if status == 200 and fetched > 0 and wake - fetched <= max_age:
                continue  # cached copy will still be fresh when this display wakes
            if now >= due and max(fetched, attempted) < due:
                return True
        return False


class Relay:
    def __init__(self, max_age, lead, allowed, max_entries=300):
        self.max_age = max_age
        self.lead = lead
        self.allowed = allowed
        self.max_entries = max_entries
        self.entries = {}
        self.lock = threading.Lock()
        self.opener = urllib.request.build_opener(SafeRedirectHandler(self.allowed))

    def _cache_key(self, url, headers):
        stable = sorted((k.lower(), v) for k, v in headers.items()
                        if k.lower() not in EPHEMERAL_HEADERS)
        return url + "\n" + "\n".join(f"{k}: {v}" for k, v in stable)

    def entry(self, url, headers):
        key = self._cache_key(url, headers)
        with self.lock:
            e = self.entries.get(key)
            if e is None:
                if len(self.entries) >= self.max_entries:
                    oldest = min(self.entries,
                                 key=lambda k: self.entries[k].last_asked())
                    del self.entries[oldest]
                e = self.entries[key] = Entry(url, headers)
                return e
        with e.lock:
            e.headers = dict(headers)
        return e

    def fetch(self, e, timeout, why):
        """Fetches e.url. On live fetch returns (status, body, ctype, err);
        stores 200 responses in e for caching."""
        req = urllib.request.Request(e.url, headers=dict(e.headers))
        req.add_header("Accept-Encoding", "gzip, deflate")
        start = time.time()
        e.attempted = start
        try:
            with self.opener.open(req, timeout=timeout) as resp:
                status, raw, hdrs = resp.status, resp.read(), resp.headers
            raw = decompress_body(raw, hdrs.get("Content-Encoding"))
        except urllib.error.HTTPError as err:
            try:
                err_body = err.read()
                raw = decompress_body(err_body, err.headers.get("Content-Encoding"))
                status, hdrs = err.code, err.headers
            except Exception as dec_err:
                log.warning("%s %s error-body decode failed: %s",
                            why, censor(e.url), dec_err)
                return None, b"", "text/plain", str(dec_err) or type(dec_err).__name__
        except Exception as err:  # DNS, refused, timeout, TLS, redirect, bad gzip
            log.warning("%s %s failed: %s", why, censor(e.url), err)
            return None, b"", "text/plain", str(err) or type(err).__name__
        ctype = hdrs.get("Content-Type") or "application/json"
        done = time.time()
        e.attempted = done
        if status == 200:
            e.status, e.body, e.ctype, e.fetched = status, raw, ctype, done
        elif why != "prefetch":
            e.status = None  # never serve a failure from the cache
        log.info("%s %s -> %d, %d bytes in %.1f s", why, censor(e.url),
                 status, len(raw), done - start)
        return status, raw, ctype, None

    def prefetch_one_pass(self, now=None):
        if now is None:
            now = time.time()
        with self.lock:
            entries = list(self.entries.items())
        for key, e in entries:
            last = e.last_asked()
            if last == 0.0:
                continue
            if now - last > 86400:
                with self.lock:
                    self.entries.pop(key, None)
                continue
            if e.should_prefetch(now, self.max_age, self.lead):
                with e.lock:
                    if e.should_prefetch(now, self.max_age, self.lead):
                        self.fetch(e, 30, "prefetch")

    def prefetch_loop(self, period=15):
        while True:
            time.sleep(period)
            try:
                self.prefetch_one_pass()
            except Exception:
                log.exception("unexpected error in prefetch_loop")


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
        e.record_ask(self.client_address[0], now)
        with e.lock:
            age = now - e.fetched
            if e.status == 200 and age <= r.max_age:
                status, body, ctype, how = e.status, e.body, e.ctype, "cached"
            else:
                status, body, ctype, err = r.fetch(e, timeout, "fetch")
                if err:
                    self.reply(502, (err + "\n").encode(),
                               extra=[("X-Relay-Error", err.replace("\n", " ")[:200])])
                    return
                age, how = 0, "live"
        log.info("%s %s -> %d (%s, %d s old)", self.client_address[0],
                 censor(url), status, how, age)
        self.reply(status, body, ctype, extra=[("X-Relay-Age", str(int(age)))])

    def status_page(self):
        now = time.time()
        with self.relay.lock:
            entries = list(self.relay.entries.values())
        lines = ["weather relay: %d URLs" % len(entries), ""]
        for e in sorted(entries, key=lambda e: e.url):
            every = e.interval()
            last = e.last_asked()
            lines.append("%s\n    asked %d s ago, every %s, cached copy %s"
                         % (censor(e.url),
                            now - last if last else -1,
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
