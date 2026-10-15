# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Tests for ecalgate: the feed trimming, and the proxy against a local server."""

import contextlib
import gzip
import http.client
import io
import os
import socket
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest import mock

sys.path.insert(0, os.path.dirname(__file__))
import ecalgate  # noqa: E402

FEED = "\r\n".join([
    "BEGIN:VCALENDAR", "VERSION:2.0", "X-WR-CALNAME:Holidays",
    "BEGIN:VTIMEZONE", "TZID:Asia/Jerusalem", "END:VTIMEZONE",
    "BEGIN:VEVENT", "UID:old", "DTSTART;VALUE=DATE:20200101", "SUMMARY:Long ago", "END:VEVENT",
    "BEGIN:VEVENT", "UID:in", "DTSTART;VALUE=DATE:20261012", "DTEND;VALUE=DATE:20261013",
    "SUMMARY:In the window", "X-ALT-DESC;FMTTYPE=text/html:<html><body>big", " ger</body></html>",
    "END:VEVENT",
    "BEGIN:VEVENT", "UID:rep", "DTSTART:20150101T100000Z", "RRULE:FREQ=YEARLY", "SUMMARY:Every year",
    "BEGIN:VALARM", "TRIGGER:-PT5M", "END:VALARM", "END:VEVENT",
    "BEGIN:VEVENT", "UID:ended", "DTSTART:20150101T100000Z", "RRULE:FREQ=DAILY;UNTIL=20160101T000000Z",
    "SUMMARY:Stopped", "END:VEVENT",
    "BEGIN:VEVENT", "UID:future", "DTSTART:20300101T100000Z", "SUMMARY:Far away", "END:VEVENT",
    "BEGIN:VTODO", "UID:t", "SUMMARY:task", "END:VTODO",
    "END:VCALENDAR", ""])


class TrimTest(unittest.TestCase):
    def test_window(self):
        out = ecalgate.trim_ics(FEED, "20260901-20271231")
        self.assertIn("UID:in", out)
        self.assertIn("UID:rep", out)
        self.assertIn("TRIGGER:-PT5M", out)
        self.assertIn("TZID:Asia/Jerusalem", out)
        self.assertIn("X-WR-CALNAME:Holidays", out)
        for gone in ("UID:old", "UID:ended", "UID:future", "VTODO", "X-ALT-DESC", "ger</body>"):
            self.assertNotIn(gone, out)
        self.assertTrue(out.startswith("BEGIN:VCALENDAR\r\n"))
        self.assertTrue(out.endswith("END:VCALENDAR\r\n"))

    def test_no_window(self):
        self.assertEqual(ecalgate.trim_ics(FEED, ""), FEED)
        self.assertEqual(ecalgate.trim_ics(FEED, "nonsense"), FEED)

    def test_edges(self):
        # two days to spare on each side
        out = ecalgate.trim_ics(FEED, "20261014-20261020")
        self.assertIn("UID:in", out)
        out = ecalgate.trim_ics(FEED, "20261016-20261020")
        self.assertNotIn("UID:in", out)


class Upstream(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def log_message(self, *a):
        pass

    def answer(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        Upstream.seen.append((self.command, self.path, dict(self.headers), body))
        if self.path == "/feed.ics":
            data = gzip.compress(FEED.encode())
            self.send_response(200)
            self.send_header("Content-Type", "text/calendar; charset=utf-8")
            self.send_header("Content-Encoding", "gzip")
            self.send_header("ETag", '"v1"')
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif self.path == "/chunked":
            self.send_response(207, "Multi-Status")
            self.send_header("Content-Type", "application/xml")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (b"<multistatus>", b"\xd7\x90", b"</multistatus>"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/moved":
            self.send_response(301)
            self.send_header("Location", "https://elsewhere.example/new")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self.send_response(201)
            self.send_header("ETag", '"put1"')
            self.send_header("Content-Length", "0")
            self.end_headers()

    do_GET = do_PUT = do_PROPFIND = do_REPORT = answer


class ProxyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.up = ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
        threading.Thread(target=cls.up.serve_forever, daemon=True).start()
        ecalgate.Config.allow_local = True
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), ecalgate.Handler)
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()
        cls.base = "http://127.0.0.1:%d" % cls.up.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.up.shutdown()
        cls.gw.shutdown()
        cls.up.server_close()
        cls.gw.server_close()
        ecalgate.Config.allow_local = False

    def conn(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        return c

    def test_feed_unzipped_and_trimmed(self):
        c = self.conn()
        c.request("GET", self.base + "/feed.ics", headers={"X-Calendar-Window": "20260901-20271231"})
        r = c.getresponse()
        data = r.read().decode()
        self.assertEqual(r.status, 200)
        self.assertIsNone(r.getheader("Content-Encoding"))
        self.assertEqual(int(r.getheader("Content-Length")), len(data.encode()))
        self.assertEqual(r.getheader("ETag"), '"v1"')
        self.assertIn("UID:in", data)
        self.assertNotIn("UID:old", data)
        # the window header and hop-by-hop headers don't go upstream
        hdrs = {k.lower(): v for k, v in Upstream.seen[-1][2].items()}
        self.assertNotIn("x-calendar-window", hdrs)
        self.assertIn("gzip", hdrs["accept-encoding"])
        # the connection to the Atari stays open for the next request
        c.request("PROPFIND", self.base + "/chunked", body=b"<propfind/>",
                  headers={"Depth": "1", "Authorization": "Basic eDp5"})
        r = c.getresponse()
        self.assertEqual(r.status, 207)
        self.assertEqual(r.read(), b"<multistatus>\xd7\x90</multistatus>")
        self.assertIsNone(r.getheader("Transfer-Encoding"))
        cmd, path, hdrs, body = Upstream.seen[-1]
        self.assertEqual((cmd, path, body), ("PROPFIND", "/chunked", b"<propfind/>"))
        self.assertEqual(hdrs.get("Depth"), "1")
        self.assertEqual(hdrs.get("Authorization"), "Basic eDp5")

    def test_redirect_passed_back(self):
        c = self.conn()
        c.request("GET", self.base + "/moved")
        r = c.getresponse()
        r.read()
        self.assertEqual(r.status, 301)
        self.assertEqual(r.getheader("Location"), "https://elsewhere.example/new")

    def test_put(self):
        c = self.conn()
        c.request("PUT", self.base + "/cal/x.ics", body=b"BEGIN:VCALENDAR",
                  headers={"If-None-Match": "*", "Content-Type": "text/calendar"})
        r = c.getresponse()
        r.read()
        self.assertEqual(r.status, 201)
        self.assertEqual(r.getheader("ETag"), '"put1"')
        self.assertEqual(Upstream.seen[-1][2].get("If-None-Match"), "*")

    def test_status_page_and_bad_requests(self):
        c = self.conn()
        c.request("GET", "/")
        r = c.getresponse()
        self.assertIn(b"ecalgate", r.read())
        c.request("GET", "ftp://x/y")
        r = c.getresponse()
        r.read()
        self.assertEqual(r.status, 400)

    def test_local_network_refused_by_default(self):
        ecalgate.Config.allow_local = False
        try:
            c = self.conn()
            c.request("GET", self.base + "/feed.ics")
            r = c.getresponse()
            r.read()
            self.assertEqual(r.status, 403)
        finally:
            ecalgate.Config.allow_local = True


    def test_log_names_the_server_not_the_path(self):
        # a private feed's address is its password: the log has the host only
        log = io.StringIO()
        ecalgate.Config.verbose = True
        try:
            with contextlib.redirect_stderr(log):
                c = self.conn()
                c.request("GET", self.base + "/feed.ics?token=s3cret-feed-token")
                c.getresponse().read()
        finally:
            ecalgate.Config.verbose = False
        self.assertIn("GET %s -> 201" % self.base, log.getvalue())     # the test server: 201 for anything else
        self.assertNotIn("s3cret", log.getvalue())
        self.assertNotIn("feed.ics", log.getvalue())

    def test_one_look_up_and_the_answer_is_used(self):
        # the server's name is looked up once, checked, and connected to: a
        # name that answers "public" to a check and "home" to the connection
        # (DNS rebinding) can't lead the gateway home
        real = socket.getaddrinfo
        asked = []

        def gai(host, *a, **k):
            if host.endswith(".example"):
                asked.append(host)
            return real("127.0.0.1", *a, **k)
        port = self.up.server_address[1]
        with mock.patch("socket.getaddrinfo", gai):
            c = self.conn()
            c.request("GET", "http://calendar.example:%d/moved" % port)
            r = c.getresponse()
            r.read()
            self.assertEqual(r.status, 301)
            self.assertEqual(asked, ["calendar.example"])
            ecalgate.Config.allow_local = False
            try:
                c = self.conn()
                c.request("GET", "http://other.example:%d/moved" % port)
                r = c.getresponse()
                self.assertIn(b"won't connect to the local network (other.example)", r.read())
                self.assertEqual(r.status, 403)
            finally:
                ecalgate.Config.allow_local = True
        self.assertEqual(asked, ["calendar.example", "other.example"])


if __name__ == "__main__":
    unittest.main()
