#!/usr/bin/env python3
"""Unit tests for weather_relay.py."""

import io
import json
import os
import sys
import unittest
import urllib.error
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weather_relay as wr


class FakeResponse:
    def __init__(self, status=200, body=b'{"ok":true}', headers=None):
        self.status = status
        self._body = body
        self.headers = headers or {"Content-Type": "application/json"}

    def read(self):
        return self._body

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class WeatherRelayTests(unittest.TestCase):
    def test_raw_deflate_and_corrupt_gzip_do_not_crash(self):
        payload = b'{"forecast":"clear"}'
        comp = zlib.compressobj(level=6, wbits=-zlib.MAX_WBITS)
        raw_deflate = comp.compress(payload) + comp.flush()
        self.assertEqual(wr.decompress_body(raw_deflate, "deflate"), payload)

        relay = wr.Relay(max_age=600, lead=120, allowed={"api.weather.gov"})
        e = relay.entry("https://api.weather.gov/points/40,-74", {})
        e.record_ask("192.168.1.10", 1000.0)
        e.record_ask("192.168.1.10", 1900.0)

        def bad_open(req, timeout=None):
            return FakeResponse(
                200, b"\x1f\x8b\x08\x00truncated",
                {"Content-Encoding": "gzip", "Content-Type": "application/json"},
            )

        relay.opener.open = bad_open
        # Must not raise or kill prefetch_one_pass
        relay.prefetch_one_pass(now=2700.0)
        self.assertIsNone(e.status)

    def test_sviva_rotating_jwt_reuses_single_cache_entry(self):
        relay = wr.Relay(max_age=600, lead=120, allowed={"air-papi.sviva.gov.il"})
        url = "https://air-papi.sviva.gov.il/v1/envista/stations/index/latest?hoursBack=4"
        hdr1 = {"Authorization": "JwtToken tok1", "Cookie": "X-Access-Token=tok1"}
        hdr2 = {"Authorization": "JwtToken tok2", "Cookie": "X-Access-Token=tok2"}

        e1 = relay.entry(url, hdr1)
        e1.status, e1.body, e1.fetched = 200, b"[1]", 1000.0
        e2 = relay.entry(url, hdr2)
        self.assertIs(e1, e2)
        self.assertEqual(len(relay.entries), 1)
        self.assertEqual(e2.headers["Authorization"], "JwtToken tok2")

    def test_multi_display_offset_does_not_poison_interval(self):
        relay = wr.Relay(max_age=600, lead=120, allowed={"api.open-meteo.com"})
        e = relay.entry("https://api.open-meteo.com/v1/forecast", {})
        # Display A wakes every 1800s at 0, 1800; Display B wakes 180s later at 180, 1980
        e.record_ask("192.168.1.21", 0.0)
        e.record_ask("192.168.1.22", 180.0)
        e.record_ask("192.168.1.21", 1800.0)
        e.record_ask("192.168.1.22", 1980.0)

        self.assertEqual(e.interval(), 1800.0)
        # Should NOT prefetch 1 min after Display B (t=2040)
        self.assertFalse(e.should_prefetch(now=2040.0, max_age=600, lead=120))
        # Should prefetch 2 min before Display A's next wake at t=3600 (due=3480)
        self.assertTrue(e.should_prefetch(now=3485.0, max_age=600, lead=120))
        e.status, e.body, e.fetched, e.attempted = 200, b"{}", 3485.0, 3485.0
        # Display B wakes at t=3780; 3780 - 3485 = 295s <= 600s max_age, so NO second prefetch!
        self.assertFalse(e.should_prefetch(now=3665.0, max_age=600, lead=120))

    def test_redirect_allowlist_blocks_disallowed_hosts(self):
        handler = wr.SafeRedirectHandler({"api.weather.gov", "ims.gov.il"})
        req = urllib.request.Request("https://ims.gov.il/en/now_analysis/1")
        fp = io.BytesIO(b"")
        allowed_req = handler.redirect_request(
            req, fp, 301, "Moved", {}, "https://ims.gov.il/en/now_analysis"
        )
        self.assertEqual(allowed_req.full_url, "https://ims.gov.il/en/now_analysis")

        with self.assertRaises(urllib.error.URLError):
            handler.redirect_request(
                req, fp, 302, "Found", {}, "http://127.0.0.1:8080/status"
            )
        with self.assertRaises(urllib.error.URLError):
            handler.redirect_request(
                req, fp, 302, "Found", {}, "https://evil.example.com/data"
            )

    def test_outdoor_sensor_f_to_c_humidity_and_cache(self):
        calls = []

        def fake_open(req, timeout=None):
            calls.append((req.full_url, req.get_header("Authorization")))
            if req.full_url.endswith("/sensor.outside_temperature"):
                return FakeResponse(200, json.dumps({
                    "state": "47.48",
                    "attributes": {"unit_of_measurement": "\u00b0F"},
                    "last_reported": "2026-11-27T19:12:27.216889+00:00",
                }).encode())
            return FakeResponse(200, json.dumps({
                "state": "69",
                "attributes": {"unit_of_measurement": "%"},
                "last_reported": "2026-11-27T19:12:27+00:00",
            }).encode())

        s = wr.OutdoorSensor(
            "http://ha.local", "tok123",
            "sensor.outside_temperature", "sensor.outside_humidity",
            max_age=3600, cache_ttl=60, opener=fake_open,
        )
        now = wr.parse_iso_timestamp("2026-11-27T19:15:00+00:00")
        st, data = s.read(now=now)
        self.assertEqual(st, 200)
        self.assertAlmostEqual(data["temp_c"], 8.6, places=2)
        self.assertEqual(data["humidity"], 69.0)
        self.assertEqual(data["age_s"], 152)
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][1], "Bearer tok123")

        # Second call within cache_ttl hits cache and advances age_s
        st2, data2 = s.read(now=now + 30)
        self.assertEqual(st2, 200)
        self.assertEqual(data2["age_s"], 182)
        self.assertEqual(len(calls), 2)

    def test_outdoor_sensor_staleness_unusable_bounds_and_partial(self):
        now = wr.parse_iso_timestamp("2026-11-27T19:30:00+00:00")

        # Stale reading (> max_age) -> 503
        stale_s = wr.OutdoorSensor(
            "http://ha", "t", "sensor.temp", max_age=600, opener=lambda r, timeout=None: FakeResponse(
                200, json.dumps({
                    "state": "10.0",
                    "attributes": {"unit_of_measurement": "\u00b0C"},
                    "last_reported": "2026-11-27T19:00:00+00:00",
                }).encode()
            ),
        )
        st, _ = stale_s.read(now=now)
        self.assertEqual(st, 503)

        # Unavailable -> 503
        unavail_s = wr.OutdoorSensor(
            "http://ha", "t", "sensor.temp", opener=lambda r, timeout=None: FakeResponse(
                200, json.dumps({
                    "state": "unavailable",
                    "attributes": {},
                    "last_reported": "2026-11-27T19:29:00+00:00",
                }).encode()
            ),
        )
        st, _ = unavail_s.read(now=now)
        self.assertEqual(st, 503)

        # Out-of-bounds -> 503
        oob_s = wr.OutdoorSensor(
            "http://ha", "t", "sensor.temp", opener=lambda r, timeout=None: FakeResponse(
                200, json.dumps({
                    "state": "999",
                    "attributes": {"unit_of_measurement": "\u00b0F"},
                    "last_reported": "2026-11-27T19:29:00+00:00",
                }).encode()
            ),
        )
        st, _ = oob_s.read(now=now)
        self.assertEqual(st, 503)

        # Valid temp + unavailable humidity -> 200 without humidity
        def partial_open(req, timeout=None):
            if req.full_url.endswith("/sensor.temp"):
                return FakeResponse(200, json.dumps({
                    "state": "12.5",
                    "attributes": {"unit_of_measurement": "\u00b0C"},
                    "last_reported": "2026-11-27T19:29:00+00:00",
                }).encode())
            return FakeResponse(200, json.dumps({
                "state": "unavailable",
                "attributes": {},
                "last_reported": "2026-11-27T19:29:00+00:00",
            }).encode())

        part_s = wr.OutdoorSensor(
            "http://ha", "t", "sensor.temp", "sensor.hum", opener=partial_open)
        st, data = part_s.read(now=now)
        self.assertEqual(st, 200)
        self.assertEqual(data["temp_c"], 12.5)
        self.assertNotIn("humidity", data)


if __name__ == "__main__":
    unittest.main()
