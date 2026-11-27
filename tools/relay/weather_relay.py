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
import datetime
import gzip
import json
import logging
import math
import os
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


_TS_RE = re.compile(
    r"^(\d{4}-\d{2}-\d{2})[T ](\d{2}:\d{2}:\d{2})(?:\.(\d+))?(Z|[+-]\d{2}:?\d{2})?$"
)


def parse_iso_timestamp(value):
    """Parses an ISO-8601 timestamp into POSIX seconds, or returns None."""
    if not value or not isinstance(value, str):
        return None
    m = _TS_RE.match(value.strip())
    if not m:
        return None
    date_part, time_part, frac, tz = m.groups()
    micros = int((frac + "000000")[:6]) if frac else 0
    tzinfo = datetime.timezone.utc
    if tz and tz != "Z":
        sign = 1 if tz[0] == "+" else -1
        digits = tz[1:].replace(":", "")
        offset_min = int(digits[:2]) * 60 + int(digits[2:4])
        tzinfo = datetime.timezone(datetime.timedelta(minutes=sign * offset_min))
    try:
        dt = datetime.datetime(
            int(date_part[:4]), int(date_part[5:7]), int(date_part[8:10]),
            int(time_part[:2]), int(time_part[3:5]), int(time_part[6:8]),
            micros, tzinfo=tzinfo,
        )
        return dt.timestamp()
    except ValueError:
        return None


def normalize_temp_c(val, unit):
    """Converts a temperature value to deg C and bounds-checks it."""
    if not math.isfinite(val):
        return None
    u = (unit or "").strip().upper()
    if u in ("°F", "F"):
        c = (val - 32.0) * (5.0 / 9.0)
    elif u == "K":
        c = val - 273.15
    else:
        c = val
    if c < -60.0 or c > 65.0:
        return None
    return c


class OutdoorSensor:
    """Reads an outdoor temperature (and optional humidity) sensor from
    Home Assistant's local REST API, with short-lived caching and staleness checks."""

    def __init__(self, ha_url, ha_token, temp_entity, humidity_entity="",
                 max_age=3600, cache_ttl=60, opener=None):
        self.ha_url = ha_url.rstrip("/")
        self.ha_token = ha_token
        self.temp_entity = temp_entity.strip()
        self.humidity_entity = (humidity_entity or "").strip()
        self.max_age = max_age
        self.cache_ttl = cache_ttl
        self.opener = opener or urllib.request.urlopen
        self.lock = threading.Lock()
        self.cached_at = 0.0
        self.reported_ts = 0.0
        self.cached_payload = None

    def _fetch_entity(self, entity_id):
        url = (f"{self.ha_url}/api/states/"
               f"{urllib.parse.quote(entity_id, safe='')}")
        req = urllib.request.Request(
            url, headers={"Authorization": f"Bearer {self.ha_token}"})
        with self.opener(req, timeout=5) as resp:
            status = getattr(resp, "status", 200)
            raw = resp.read()
        if status != 200:
            raise RuntimeError(f"HA returned HTTP {status}")
        return json.loads(raw.decode("utf-8"))

    def _freshness_ts(self, doc):
        for key in ("last_reported", "last_updated", "last_changed"):
            ts = parse_iso_timestamp(doc.get(key))
            if ts is not None:
                return ts
        return None

    def read(self, now=None):
        if now is None:
            now = time.time()
        with self.lock:
            if (self.cached_payload is not None
                    and self.cache_ttl > 0
                    and (now - self.cached_at) < self.cache_ttl):
                age_s = max(0, int(now - self.reported_ts))
                if age_s <= self.max_age:
                    payload = dict(self.cached_payload)
                    payload["age_s"] = age_s
                    return 200, payload

            try:
                tdoc = self._fetch_entity(self.temp_entity)
            except Exception as err:
                log.warning("outdoor temp fetch failed (%s): %s",
                            self.temp_entity, err)
                return 502, f"HA temp read failed: {err}"

            state_str = str(tdoc.get("state", "")).strip()
            try:
                raw_temp = float(state_str)
            except ValueError:
                log.warning("outdoor temp %s unusable: %r",
                            self.temp_entity, state_str)
                return 503, f"outdoor temp unusable: {state_str}"

            unit = (tdoc.get("attributes") or {}).get("unit_of_measurement", "")
            temp_c = normalize_temp_c(raw_temp, unit)
            if temp_c is None:
                log.warning("outdoor temp %s out of range: %r %r",
                            self.temp_entity, state_str, unit)
                return 503, f"outdoor temp out of range: {state_str} {unit}"

            ts = self._freshness_ts(tdoc)
            if ts is None:
                return 503, "outdoor temp missing timestamp"
            age_s = max(0, int(now - ts))
            if age_s > self.max_age:
                log.warning("outdoor temp %s stale (%ds old > %ds)",
                            self.temp_entity, age_s, self.max_age)
                return 503, f"outdoor temp stale ({age_s}s old)"

            payload = {"temp_c": round(temp_c, 2), "age_s": age_s}

            if self.humidity_entity:
                try:
                    hdoc = self._fetch_entity(self.humidity_entity)
                    h_str = str(hdoc.get("state", "")).strip()
                    h_val = float(h_str)
                    hts = self._freshness_ts(hdoc)
                    h_age = max(0, int(now - hts)) if hts is not None else self.max_age + 1
                    if math.isfinite(h_val) and 0.0 <= h_val <= 100.0 and h_age <= self.max_age:
                        payload["humidity"] = round(h_val, 1)
                    else:
                        log.warning("outdoor humidity %s ignored (state=%r age=%ss)",
                                    self.humidity_entity, h_str, h_age)
                except Exception as err:
                    log.warning("outdoor humidity fetch failed (%s): %s",
                                self.humidity_entity, err)

            self.cached_at = now
            self.reported_ts = ts
            self.cached_payload = dict(payload)
            return 200, payload

    def status_line(self, now=None):
        if now is None:
            now = time.time()
        with self.lock:
            entities = self.temp_entity + (
                f", {self.humidity_entity}" if self.humidity_entity else "")
            if self.cached_payload is None:
                return f"outdoor sensor ({entities}): no cached reading yet"
            age_s = max(0, int(now - self.reported_ts))
            hum = self.cached_payload.get("humidity")
            hpart = f", {hum}%" if hum is not None else ""
            return (f"outdoor sensor ({entities}): "
                    f"{self.cached_payload['temp_c']} C{hpart}, reported {age_s} s ago")


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
    def __init__(self, max_age, lead, allowed, max_entries=300, outdoor_sensor=None):
        self.max_age = max_age
        self.lead = lead
        self.allowed = allowed
        self.max_entries = max_entries
        self.outdoor_sensor = outdoor_sensor
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
        if self.path == "/sensor/outdoor":
            if r.outdoor_sensor is None:
                self.reply(404, b"outdoor sensor not configured\n")
                return
            status, data = r.outdoor_sensor.read()
            if status == 200:
                log.info("%s /sensor/outdoor -> 200 (temp=%.2fC, age=%ds)",
                         self.client_address[0], data["temp_c"], data["age_s"])
                self.reply(200, json.dumps(data).encode("utf-8"),
                           "application/json",
                           extra=[("X-Relay-Age", str(data.get("age_s", 0)))])
            else:
                self.reply(status, (str(data) + "\n").encode("utf-8"))
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
        if self.relay.outdoor_sensor is not None:
            lines.extend([self.relay.outdoor_sensor.status_line(now), ""])
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
    ap.add_argument("--ha-url", default=os.environ.get("HA_URL", ""),
                    help="Home Assistant base URL for /sensor/outdoor (env HA_URL)")
    ap.add_argument("--ha-token", default=os.environ.get("HA_TOKEN", ""),
                    help="Home Assistant token (env HA_TOKEN)")
    ap.add_argument("--ha-outdoor-temp", default=os.environ.get("HA_OUTDOOR_TEMP", ""),
                    help="HA outdoor temperature entity id (env HA_OUTDOOR_TEMP)")
    ap.add_argument("--ha-outdoor-humidity", default=os.environ.get("HA_OUTDOOR_HUMIDITY", ""),
                    help="HA outdoor humidity entity id (env HA_OUTDOOR_HUMIDITY)")
    ap.add_argument("--ha-max-age", type=int,
                    default=int(os.environ.get("HA_MAX_AGE", "3600")),
                    help="max HA sensor age in seconds before falling back (default 3600)")
    ap.add_argument("--ha-cache", type=int,
                    default=int(os.environ.get("HA_CACHE", "60")),
                    help="seconds to cache HA sensor state (default 60)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING,
                        format="%(asctime)s %(message)s")
    if args.max_age <= args.lead:
        ap.error("--max-age must be longer than --lead, or prefetched copies "
                 "would be too old to serve")

    outdoor = None
    if args.ha_url and args.ha_token and args.ha_outdoor_temp:
        outdoor = OutdoorSensor(
            args.ha_url, args.ha_token, args.ha_outdoor_temp,
            humidity_entity=args.ha_outdoor_humidity,
            max_age=args.ha_max_age, cache_ttl=args.ha_cache,
        )
        log.warning("outdoor sensor enabled: %s at %s",
                    args.ha_outdoor_temp, args.ha_url)
    elif args.ha_url or args.ha_token or args.ha_outdoor_temp:
        log.warning("HA outdoor sensor partially configured; set HA_URL, HA_TOKEN, and HA_OUTDOOR_TEMP")

    Handler.relay = Relay(args.max_age, args.lead,
                          ALLOWED_HOSTS | {h.lower() for h in args.allow},
                          outdoor_sensor=outdoor)
    threading.Thread(target=Handler.relay.prefetch_loop, daemon=True).start()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    server.daemon_threads = True
    logging.getLogger("relay").warning("weather relay on %s:%d", args.bind, args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
