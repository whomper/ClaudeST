# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Tests for webgate: the proxy against a local server, as EWeb uses it."""

import gzip
import http.client
import json
import os
import shutil
import socket
import sys
import tempfile
import threading
import time
import unittest
import unittest.mock
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(__file__))
import webgate  # noqa: E402

PAGE = b"<!DOCTYPE html><title>Page</title><p>\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"
BIG = bytes(range(256)) * 1024          # 256 KB, more than one piece


def make_pics():
    """pictures for the picture cases (X-Gateway-Picture), or {} without Pillow"""
    try:
        import io
        from PIL import Image
    except ImportError:
        return {}

    def save(im, fmt, **kw):
        b = io.BytesIO()
        im.save(b, fmt, **kw)
        return b.getvalue()

    def photo(w, h):
        im = Image.new("RGB", (w, h))
        im.putdata([(x * 255 // w, y * 255 // h, 128) for y in range(h) for x in range(w)])
        return im
    turned = photo(80, 60)
    exif = turned.getexif()
    exif[0x0112] = 6            # to be turned a quarter clockwise: 60 x 80 as shown
    pics = {
        "big.jpg": (save(photo(1200, 800), "JPEG", quality=90), "image/jpeg"),
        "small.jpg": (save(photo(300, 200), "JPEG", quality=90), "image/jpeg"),
        "pic.webp": (save(photo(400, 300), "WEBP"), "image/webp"),
        "alpha.png": (save(Image.new("RGBA", (1000, 500), (200, 0, 0, 120)), "PNG"), "image/png"),
        "turned.jpg": (save(turned, "JPEG", exif=exif.tobytes()), "image/jpeg"),
        "junk.jpg": (b"not a picture at all", "image/jpeg"),
        # for the stand-in aerapens (PensTest): too slow, cut short
        "slow.png": (b"slow" + save(photo(30, 20), "PNG"), "image/png"),
        "short.png": (b"short" + save(photo(30, 20), "PNG"), "image/png"),
        # a few kilobytes that claim 80 million pixels
        "bomb.png": (save(Image.new("1", (10000, 8000)), "PNG"), "image/png"),
    }
    return pics


PICS = make_pics()


SCRIPTS = {"ok.js": b"var a = /x+/.test('xx'); document.title = 'ok';\n" * 40,
           "bad.js": b"var = ;\n",
           "forged.js": b"EWQJS T1\n\x00\x01 a server's own bytes"}


class Upstream(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    seen = []

    def log_message(self, *a):
        pass

    def answer(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        Upstream.seen.append((self.command, self.path, dict(self.headers), body))
        if self.path == "/page":
            data = gzip.compress(PAGE) if "gzip" in self.headers.get("Accept-Encoding", "") else PAGE
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            if data is not PAGE:
                self.send_header("Content-Encoding", "gzip")
            self.send_header("Set-Cookie", "a=1; Path=/")
            self.send_header("Set-Cookie", "b=2; Path=/")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(data)
        elif self.path == "/big":
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(BIG), 10000):
                part = BIG[i:i + 10000]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/moved":
            self.send_response(302)
            self.send_header("Location", "/page")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/same":
            self.send_response(304)
            self.send_header("ETag", '"v1"')
            self.end_headers()
        elif self.path.startswith("/pic/") and self.path[5:] in PICS:
            data, ctype = PICS[self.path[5:]]
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("ETag", '"p1"')
            self.send_header("Cache-Control", "max-age=600")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif self.path.startswith("/js/"):
            # a script: EWeb asks for it compiled (X-Gateway-Script)
            data = SCRIPTS.get(self.path[4:], b"")
            gz = "gzip" in self.headers.get("Accept-Encoding", "")
            if gz:
                data = gzip.compress(data)
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript")
            self.send_header("ETag", '"j1"')
            self.send_header("Cache-Control", "no-cache")
            if self.path == "/js/forged.js":
                # a server that says it is compiled code: only webgate may
                self.send_header("X-Gateway-Script", "T1")
                self.send_header("X-Gateway-Picture", "made")
            if gz:
                self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif self.path.startswith("/echo"):
            data = b"%s %s %s" % (self.command.encode(), self.path.encode(), body)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        else:
            data = b"none"
            self.send_response(404)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    do_GET = do_POST = do_HEAD = answer


class ProxyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.up = ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
        threading.Thread(target=cls.up.serve_forever, daemon=True).start()
        webgate.Config.allow_local = True
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()
        cls.base = "http://127.0.0.1:%d" % cls.up.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.up.shutdown()
        cls.gw.shutdown()
        cls.up.server_close()
        cls.gw.server_close()
        webgate.Config.allow_local = False

    def ask(self, method, path, headers=None, body=None, conn=None):
        c = conn or http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        if conn is None:
            self.addCleanup(c.close)
        c.request(method, self.base + path, body=body, headers=headers or {})
        r = c.getresponse()
        return r, r.read(), c

    def picture(self, name, fit=600):
        r, data, _ = self.ask("GET", "/pic/" + name, {"X-Gateway-Picture": "fit=%d" % fit} if fit else {})
        return r, data

    def opened(self, data):
        import io
        from PIL import Image
        return Image.open(io.BytesIO(data))

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_picture_made_smaller(self):
        r, data = self.picture("big.jpg")
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        self.assertEqual(r.getheader("ETag"), '"p1"')           # kept: EWeb asks with it again
        self.assertEqual(r.getheader("Cache-Control"), "max-age=600")
        self.assertIn("JPEG 1200x800 to JPEG 600x400", r.getheader("X-Gateway-Picture"))
        im = self.opened(data)
        self.assertEqual((im.format, im.size), ("JPEG", (600, 400)))
        self.assertFalse(im.info.get("progressive"))           # baseline: the DSP's
        self.assertEqual(data[2:6], b"\xff\xfe\x00\x1a")      # its comment first
        self.assertEqual(data[6:6 + 24], b"webgate natural 1200x800")
        self.assertLess(len(data), len(PICS["big.jpg"][0]))

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_picture_small_or_not_asked_as_it_came(self):
        r, data = self.picture("small.jpg")
        self.assertEqual(data, PICS["small.jpg"][0])
        self.assertIsNone(r.getheader("X-Gateway-Picture"))
        r, data = self.picture("big.jpg", fit=0)
        self.assertEqual(data, PICS["big.jpg"][0])
        r, data = self.picture("junk.jpg")                      # not a picture Pillow reads
        self.assertEqual(data, PICS["junk.jpg"][0])
        r, data = self.picture("bomb.png")                      # too many pixels to unpack
        self.assertEqual(data, PICS["bomb.png"][0])

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_picture_webp_made_jpeg(self):
        r, data = self.picture("pic.webp")
        im = self.opened(data)
        self.assertEqual((r.getheader("Content-Type"), im.format, im.size), ("image/jpeg", "JPEG", (400, 300)))
        self.assertIn(b"webgate natural 400x300", data[:64])

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_picture_alpha_made_png(self):
        r, data = self.picture("alpha.png")
        im = self.opened(data)
        self.assertEqual((r.getheader("Content-Type"), im.format, im.size), ("image/png", "PNG", (600, 300)))
        self.assertEqual(im.info.get("webgate"), "natural 1000x500")
        # the tEXt before the pixels, where EWeb looks
        self.assertLess(data.index(b"tEXtwebgate"), data.index(b"IDAT"))
        self.assertEqual(im.convert("RGBA").getpixel((10, 10))[3], 120)

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_picture_turned_upright(self):
        r, data = self.picture("turned.jpg", fit=64)
        im = self.opened(data)
        self.assertEqual(im.size, (60, 80))
        self.assertIn(b"webgate natural 60x80", data[:64])

    def test_page_gzip_passed_through(self):
        r, data, _ = self.ask("GET", "/page", {"Accept-Encoding": "gzip, deflate, br"})
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("Content-Encoding"), "gzip")
        self.assertEqual(gzip.decompress(data), PAGE)
        # only what the Atari unpacks was asked for
        self.assertEqual(Upstream.seen[-1][2]["Accept-Encoding"], "gzip, deflate")
        self.assertIsNone(r.getheader("X-Gateway-TLS"))       # plain HTTP upstream

    def test_identity_and_cookies(self):
        r, data, _ = self.ask("GET", "/page")
        self.assertEqual(data, PAGE)
        self.assertEqual(Upstream.seen[-1][2]["Accept-Encoding"], "identity")
        self.assertEqual([v for k, v in r.getheaders() if k.lower() == "set-cookie"], ["a=1; Path=/", "b=2; Path=/"])

    def test_chunked_big(self):
        r, data, _ = self.ask("GET", "/big")
        self.assertEqual(r.getheader("Transfer-Encoding"), "chunked")
        self.assertEqual(data, BIG)

    def test_redirect_and_304_and_404_as_they_came(self):
        r, data, c = self.ask("GET", "/moved")
        self.assertEqual((r.status, r.getheader("Location")), (302, "/page"))
        r, data, c = self.ask("GET", "/same", {"If-None-Match": '"v1"'}, conn=c)
        self.assertEqual((r.status, data), (304, b""))
        self.assertEqual(Upstream.seen[-1][2]["If-None-Match"], '"v1"')
        r, data, c = self.ask("GET", "/nothing", conn=c)
        self.assertEqual((r.status, data), (404, b"none"))

    def test_post_and_head_kept_connection(self):
        r, data, c = self.ask("POST", "/echo?x=1", {"Content-Type": "application/x-www-form-urlencoded"},
                              b"q=atari")
        self.assertEqual(data, b"POST /echo?x=1 q=atari")
        r, data, c = self.ask("HEAD", "/page", conn=c)
        self.assertEqual((r.status, data), (200, b""))
        r, data, c = self.ask("GET", "/echo", conn=c)
        self.assertEqual(data, b"GET /echo ")

    def test_status_page_and_bad_requests(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "/")
        r = c.getresponse()
        self.assertEqual(r.status, 200)
        self.assertIn(b"webgate", r.read())
        c.request("GET", "ftp://example.com/x")
        r = c.getresponse()
        self.assertEqual(r.status, 400)
        self.assertTrue(r.read().startswith(b"webgate: "))

    def test_unknown_host(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=20)
        self.addCleanup(c.close)
        c.request("GET", "http://no-such-host.invalid/")
        r = c.getresponse()
        self.assertEqual(r.status, 502)
        self.assertIn(b"webgate: can't find no-such-host.invalid", r.read())

    def test_home_network_refused(self):
        webgate.Config.allow_local = False
        try:
            r, data, _ = self.ask("GET", "/page")
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)
        self.assertIn(b"webgate: won't connect to the local network", data)


class PrefetchTest(ProxyTest):
    """a page's pictures made ahead (POST http://webgate/prefetch)"""

    def prefetch(self, lines, more=()):
        body = "\n".join(list(more) + lines).encode()
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("POST", "http://webgate/prefetch", body=body,
                  headers={"Content-Type": "text/plain", "User-Agent": "EWeb/test"})
        r = c.getresponse()
        r.read()
        return r

    def fetched(self, path):
        return [s for s in Upstream.seen if s[1] == path]

    def wait_for(self, path, n=1):
        for _ in range(100):
            if len(self.fetched(path)) >= n:
                return
            time.sleep(0.05)

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_made_ahead_once(self):
        Upstream.seen = []
        r = self.prefetch([self.base + "/pic/big.jpg\tfit=600", self.base + "/pic/pic.webp\tfit=600"],
                          ["Referer: http://example.org/page"])
        self.assertEqual((r.status, r.getheader("X-Gateway-Prefetch")), (204, "2"))
        self.wait_for("/pic/big.jpg")
        self.wait_for("/pic/pic.webp")
        time.sleep(0.3)        # (made: the pictures, after they came)
        r, data = self.picture("big.jpg")
        self.assertEqual((r.status, r.getheader("Content-Type")), (200, "image/jpeg"))
        self.assertIn("JPEG 1200x800 to JPEG 600x400", r.getheader("X-Gateway-Picture"))
        self.assertEqual(self.opened(data).size, (600, 400))
        r, data = self.picture("pic.webp")
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        # each fetched once: the requests were answered with what was made
        self.assertEqual((len(self.fetched("/pic/big.jpg")), len(self.fetched("/pic/pic.webp"))), (1, 1))
        h = self.fetched("/pic/big.jpg")[0][2]
        self.assertEqual((h.get("Referer"), h.get("User-Agent")), ("http://example.org/page", "EWeb/test"))
        # once only: asked again, it is fetched again
        self.picture("big.jpg")
        self.assertEqual(len(self.fetched("/pic/big.jpg")), 2)

    def test_cookie_and_other_answers(self):
        Upstream.seen = []
        self.prefetch([self.base + "/echo/x\tfit=600\ta=1; b=2", self.base + "/nothere\tfit=600"])
        self.wait_for("/echo/x")
        self.wait_for("/nothere")
        time.sleep(0.2)
        self.assertEqual(self.fetched("/echo/x")[0][2].get("Cookie"), "a=1; b=2")
        r, data, _ = self.ask("GET", "/nothere", {"X-Gateway-Picture": "fit=600"})    # (an error page, as it came)
        self.assertEqual((r.status, data), (404, b"none"))
        self.assertEqual(len(self.fetched("/nothere")), 1)

    def test_conditional_not_from_the_store(self):
        Upstream.seen = []
        self.prefetch([self.base + "/pic/small.jpg\tfit=600"])
        self.wait_for("/pic/small.jpg")
        r, data, _ = self.ask("GET", "/pic/small.jpg", {"X-Gateway-Picture": "fit=600", "If-None-Match": '"p1"'})
        self.assertEqual(len(self.fetched("/pic/small.jpg")), 2)

    def test_bad_lists(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "http://webgate/prefetch")
        r = c.getresponse()
        r.read()
        self.assertEqual(r.status, 400)
        r = self.prefetch(["ftp://x/y\tfit=600", "http://x/y", "http://x/y\tnofit", "junk"])
        self.assertEqual((r.status, r.getheader("X-Gateway-Prefetch")), (204, "0"))
        r = self.prefetch([self.base + "/pic/big.jpg\tfit=600; size=60x40; pens=16; pal=0badc0de; ed=lj"])
        self.assertEqual((r.status, r.getheader("X-Gateway-Palette")), (409, "need"))


class FakePens(threading.Thread):
    """a stand-in for aerapens behind its socket (AERA-Atari's
    common/tools/pens; systemd starts one a connection): the pens of
    W x H (W wide when H is 0, as tall as wide) all pen 1, for a JPEG,
    PNG or GIF only; "slow" pictures answered too late, "short" ones cut
    short. The last request is kept."""

    def __init__(self, path):
        super().__init__(daemon=True)
        self.path = path
        self.last = None
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.srv.bind(path)
        self.srv.listen(4)

    def run(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            with c:
                req = b""
                while True:
                    b = c.recv(65536)
                    if not b:
                        break
                    req += b
                self.last = req
                try:
                    c.sendall(self.answer(req))
                except OSError:
                    pass        # webgate gave up waiting (the slow one)

    def answer(self, req):
        import time
        import zlib
        if req[:5] != b"AEPQ\1" or len(req) < 16:
            return b""
        if req[5:6] == b"v":
            return b"aerapens 0123456789ab test\n"
        n, w, h = req[6] + 1, int.from_bytes(req[8:10], "big"), int.from_bytes(req[10:12], "big")
        pic = req[16 + 3 * n:]
        if pic.startswith(b"slow"):
            time.sleep(2)
        if not pic.startswith((b"\xff\xd8", b"\x89PNG", b"GIF8")):
            return b""          # as aerapens: not a JPEG, PNG or GIF (exit 2, nothing written)
        h = h or w
        depth = 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8
        row = (w * depth + 7) // 8
        out = (b"aerapens 0123456789ab test\n" + b"EWPN" + bytes([1, depth, 0, n - 1]) + w.to_bytes(2, "big") +
               h.to_bytes(2, "big") + (999).to_bytes(2, "big") + (777).to_bytes(2, "big") +
               zlib.crc32(req[16:16 + 3 * n]).to_bytes(4, "big") + bytes([0x11]) * (row * h))
        return out[:-5] if pic.startswith(b"short") else out

    def stop(self):
        self.srv.close()


ST16 = bytes([255, 255, 255, 255, 0, 0, 0, 255, 0, 255, 255, 0, 0, 0, 255, 255, 0, 255, 0, 255, 255, 170, 170, 170,
              85, 85, 85, 170, 0, 0, 0, 170, 0, 170, 170, 0, 0, 0, 170, 170, 0, 170, 0, 170, 170, 0, 0, 0])


class PensTest(ProxyTest):
    """pictures as the Atari's pens (EWeb's protocol): the colours sent
    once, then each picture asked for with them, made by aerapens on its
    socket"""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.dir = tempfile.mkdtemp()
        cls.fake = FakePens(os.path.join(cls.dir, "pens.sock"))
        cls.fake.start()
        webgate.Config.pens_socket = cls.fake.path

    @classmethod
    def tearDownClass(cls):
        cls.fake.stop()
        webgate.Config.pens_socket = "/run/webgate-pens.sock"
        shutil.rmtree(cls.dir)
        super().tearDownClass()

    def colours(self, rgb=ST16):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("POST", "http://webgate/palette", body=rgb, headers={"Content-Type": "application/octet-stream"})
        r = c.getresponse()
        r.read()
        return r

    def pens(self, name, ask):
        r, data, _ = self.ask("GET", "/pic/" + name, {"X-Gateway-Picture": ask})
        return r, data

    def test_which(self):
        self.assertEqual(webgate.pens_which(), "aerapens 0123456789ab test")
        webgate.Config.pens_socket = "/nowhere/pens.sock"
        try:
            self.assertIsNone(webgate.pens_which())
        finally:
            webgate.Config.pens_socket = self.fake.path

    def test_colours_first(self):
        crc = "%08x" % (__import__("zlib").crc32(bytes(range(48))) & 0xFFFFFFFF)
        seen = len(Upstream.seen)
        r, data = self.pens("big.jpg", "fit=600; size=300x200; pens=16; pal=%s; ed=lj" % crc)
        self.assertEqual((r.status, r.getheader("X-Gateway-Palette")), (409, "need"))
        self.assertEqual(len(Upstream.seen), seen)             # nothing fetched for it
        r = self.colours(bytes(range(48)))
        self.assertEqual((r.status, r.getheader("X-Gateway-Palette")), (204, crc))

    def test_colours_refused(self):
        for rgb in (b"", b"\0\0\0", bytes(769), bytes(10)):
            self.assertEqual(self.colours(rgb).status, 400)

    def test_colours_kept_sixteen(self):
        crcs = [self.colours(bytes([k]) * 6).getheader("X-Gateway-Palette") for k in range(17)]
        self.assertIsNone(webgate.PALETTES.get(int(crcs[0], 16)))           # the oldest went
        self.assertEqual(webgate.PALETTES.get(int(crcs[16], 16)), bytes([16]) * 6)

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_pens(self):
        crc = self.colours().getheader("X-Gateway-Palette")
        r, data = self.pens("big.jpg", "fit=600; size=300x200; pens=16; pal=%s; ed=lj" % crc)
        self.assertEqual((r.status, r.getheader("Content-Type")), (200, "application/x-aera-pens"))
        self.assertEqual(r.getheader("X-Gateway-Pens"), "aerapens 0123456789ab test")
        self.assertEqual(r.getheader("ETag"), '"p1"')           # kept: EWeb asks with it again
        self.assertEqual(r.getheader("Cache-Control"), "max-age=600")
        self.assertIn("pens 300x200, 4 bits", r.getheader("X-Gateway-Picture"))
        self.assertEqual(len(data), 20 + 150 * 200)             # the line taken off
        self.assertEqual((data[:4], data[16:20]), (b"EWPN", bytes.fromhex(crc)))
        q = self.fake.last          # the request: the colours, then the file as it came
        self.assertEqual(q[:16], b"AEPQ\1l\x0f\0" + (300).to_bytes(2, "big") + (200).to_bytes(2, "big") +
                         len(PICS["big.jpg"][0]).to_bytes(4, "big"))
        self.assertEqual(q[16:], ST16 + PICS["big.jpg"][0])
        # without size=: fit wide at most; EWEBST.PRG's decoders
        r, data = self.pens("big.jpg", "fit=96; pens=16; pal=%s; ed=tj" % crc)
        self.assertEqual((data[8:12], self.fake.last[5:6], self.fake.last[8:12]),
                         (bytes([0, 96, 0, 96]), b"t", bytes([0, 96, 0, 0])))

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_pens_of_a_kind_the_atari_cant_read(self):
        crc = self.colours().getheader("X-Gateway-Palette")
        for name in ("pic.webp", "turned.jpg"):
            r, data = self.pens(name, "fit=600; pens=16; pal=%s; ed=lj" % crc)
            self.assertEqual(r.getheader("Content-Type"), "application/x-aera-pens")
            given = self.fake.last[16 + 48:]
            self.assertEqual(given[:8], b"\x89PNG\r\n\x1a\n")   # made a PNG first
            im = self.opened(given)
            self.assertEqual(im.size, (400, 300) if name == "pic.webp" else (60, 80))   # upright

    @unittest.skipUnless(PICS, "needs Pillow")
    def test_pens_not_had(self):
        crc = self.colours().getheader("X-Gateway-Palette")
        # aerapens can't (junk), too slow, cut short; the count doesn't match
        # the colours; no socket: each as without pens
        r, data = self.pens("junk.jpg", "fit=600; pens=16; pal=%s; ed=lj" % crc)
        self.assertEqual(data, PICS["junk.jpg"][0])
        was = webgate.PENS_TIME
        webgate.PENS_TIME = 1
        try:
            for name in ("slow.png", "short.png"):
                r, data = self.pens(name, "fit=600; pens=16; pal=%s; ed=lj" % crc)
                self.assertEqual((r.status, r.getheader("Content-Type"), r.getheader("X-Gateway-Pens")),
                                 (200, "image/png", None), name)
        finally:
            webgate.PENS_TIME = was
        r, data = self.pens("big.jpg", "fit=600; pens=256; pal=%s; ed=lj" % crc)
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        webgate.Config.pens_socket = "/nowhere/pens.sock"
        try:
            r, data = self.pens("big.jpg", "fit=600; pens=16; pal=%s; ed=lj" % crc)
        finally:
            webgate.Config.pens_socket = self.fake.path
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        self.assertIn(b"webgate natural 1200x800", data[:64])


class FakeJsc(threading.Thread):
    """a stand-in for aerajsc behind its socket (EWeb's tools/jsc; systemd
    starts one a connection), its tag "T1": a script "compiled" to EWQJS
    and its line, then "code:" and the source; "error ..." for one with
    "var = ;" in it. The last request is kept."""

    def __init__(self, path):
        super().__init__(daemon=True)
        self.path = path
        self.last = None
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.srv.bind(path)
        self.srv.listen(4)

    def run(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            with c:
                req = b""
                while True:
                    b = c.recv(65536)
                    if not b:
                        break
                    req += b
                self.last = req
                src = req[9:]
                out = b"aerajsc T1\n"
                if req[:5] == b"EWJQ\1" and len(src) == int.from_bytes(req[5:9], "big") and src:
                    out += b"error SyntaxError\n" if b"var = ;" in src else b"EWQJS T1\ncode:" + src
                try:
                    c.sendall(out)
                except OSError:
                    pass

    def stop(self):
        self.srv.close()


class ScriptTest(ProxyTest):
    """scripts compiled for EWeb (EWeb's protocol): asked for with
    X-Gateway-Script and the tag of EWeb's QuickJS, compiled by aerajsc on
    its socket when it has the same tag, else as they came"""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.dir = tempfile.mkdtemp()
        cls.fake = FakeJsc(os.path.join(cls.dir, "js.sock"))
        cls.fake.start()
        webgate.Config.js_socket = cls.fake.path

    @classmethod
    def tearDownClass(cls):
        cls.fake.stop()
        webgate.Config.js_socket = "/run/webgate-js.sock"
        shutil.rmtree(cls.dir)
        super().tearDownClass()

    def script(self, name, tag="T1", more=None):
        h = {"Accept-Encoding": "gzip"}
        if tag:
            h["X-Gateway-Script"] = tag
        h.update(more or {})
        r, data, _ = self.ask("GET", "/js/" + name, h)
        return r, data

    def test_which(self):
        self.assertEqual(webgate.js_which(), "aerajsc T1")
        webgate.Config.js_socket = "/nowhere/js.sock"
        try:
            self.assertIsNone(webgate.js_which())
        finally:
            webgate.Config.js_socket = self.fake.path

    def ask_scripts(self):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "http://webgate/scripts")
        r = c.getresponse()
        return r, r.read()

    def test_scripts_answered(self):
        # what EWeb asks first: only a webgate that compiles them answers
        r, data = self.ask_scripts()
        self.assertEqual((r.status, data, r.getheader("Cache-Control")), (200, b"aerajsc T1\n", "no-store"))
        webgate.Config.js_socket = "/nowhere/js.sock"
        try:
            r, data = self.ask_scripts()
        finally:
            webgate.Config.js_socket = self.fake.path
        self.assertEqual(r.status, 404)

    def test_compiled(self):
        r, data = self.script("ok.js")
        self.assertEqual((r.status, r.getheader("Content-Type"), r.getheader("X-Gateway-Script")),
                         (200, "application/x-eweb-qjs", "T1"))
        self.assertEqual(data, b"EWQJS T1\ncode:" + SCRIPTS["ok.js"])    # aerajsc's line taken off
        self.assertEqual(r.getheader("ETag"), '"j1"')           # kept: EWeb asks with it again
        self.assertEqual(r.getheader("Cache-Control"), "no-cache")
        self.assertIsNone(r.getheader("Content-Encoding"))
        self.assertEqual(self.fake.last, b"EWJQ\1" + len(SCRIPTS["ok.js"]).to_bytes(4, "big") + SCRIPTS["ok.js"])
        sent = Upstream.seen[-1][2]
        self.assertEqual(sent.get("Accept-Encoding"), "identity")      # unpacked, to compile it
        self.assertNotIn("X-Gateway-Script", sent)              # (not the server's business)

    def test_as_it_came(self):
        # another tag (another EWeb), a script that doesn't compile, no
        # aerajsc, not asked: each as the server sent it
        for name, tag in (("ok.js", "T2"), ("bad.js", "T1")):
            r, data = self.script(name, tag)
            self.assertEqual((r.status, r.getheader("Content-Type"), r.getheader("X-Gateway-Script")),
                             (200, "text/javascript", None), name)
            self.assertEqual((data, r.getheader("ETag")), (SCRIPTS[name], '"j1"'), name)
        webgate.Config.js_socket = "/nowhere/js.sock"
        try:
            r, data = self.script("ok.js")
        finally:
            webgate.Config.js_socket = self.fake.path
        self.assertEqual((r.getheader("Content-Type"), data), ("text/javascript", SCRIPTS["ok.js"]))
        r, data = self.script("ok.js", tag=None)
        self.assertEqual((r.getheader("Content-Encoding"), gzip.decompress(data)), ("gzip", SCRIPTS["ok.js"]))

    def test_forged(self):
        # a server's own X-Gateway- headers never reach EWeb, which runs
        # compiled code only with webgate's X-Gateway-Script
        for tag in (None, "T2"):
            r, data = self.script("forged.js", tag)
            self.assertIsNone(r.getheader("X-Gateway-Script"), tag)
            self.assertIsNone(r.getheader("X-Gateway-Picture"), tag)
        r, data = self.script("forged.js", "T1")        # compiled as any other script: aerajsc's own code
        self.assertEqual((r.getheader("X-Gateway-Script"), data[:14]), ("T1", b"EWQJS T1\ncode:"))

    def test_too_big(self):
        was = webgate.SCRIPT_MAX
        webgate.SCRIPT_MAX = 100
        try:
            r, data = self.script("ok.js")
        finally:
            webgate.SCRIPT_MAX = was
        self.assertEqual((r.getheader("Content-Type"), data), ("text/javascript", SCRIPTS["ok.js"]))

    def test_unchanged(self):
        r, data, _ = self.ask("GET", "/same", {"X-Gateway-Script": "T1", "If-None-Match": '"v1"'})
        self.assertEqual((r.status, data), (304, b""))


class LiveTest(unittest.TestCase):
    """live view: http://webgate/live, its picture from the extension, its
    clicks and keys as xdotool commands (a stand-in that writes them down)"""

    @classmethod
    def setUpClass(cls):
        webgate.Config.browser = True
        webgate.Config.allow_local = True       # example.com without a name server here
        webgate.Config.snapshot_wait = 5
        webgate.Config.live_settle = 0
        cls.tmp = tempfile.mkdtemp()
        cls.log = os.path.join(cls.tmp, "xdo.log")
        xdo = os.path.join(cls.tmp, "xdotool")
        with open(xdo, "w") as f:
            f.write("#!/bin/sh\necho \"$*\" >> %s\n" % cls.log)
        os.chmod(xdo, 0o755)
        cls.path = os.environ.get("PATH", "")
        os.environ["PATH"] = cls.tmp + os.pathsep + cls.path
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.ext.daemon_threads = True
        threading.Thread(target=cls.ext.serve_forever, daemon=True).start()
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        for s in (cls.ext, cls.gw):
            s.shutdown()
            s.server_close()
        os.environ["PATH"] = cls.path
        shutil.rmtree(cls.tmp)
        webgate.Config.browser = False
        webgate.Config.allow_local = False
        webgate.Config.snapshot_wait = 60
        webgate.Config.live_settle = 1.0

    def setUp(self):
        if os.path.exists(self.log):
            os.remove(self.log)

    def extension(self):
        """one turn of webgate-ext, as background.js answers each kind"""
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=30)
        c.request("GET", "/next")
        w = json.loads(c.getresponse().read())
        if w.get("shot"):
            c.request("POST", "/shot?id=%s&w=1216" % w["id"], body=JPEG)
        else:
            body = json.dumps({"url": w["url"] or "https://example.com/now", "title": "Now",
                               "html": "<!DOCTYPE html><title>Now</title><p>shown"}).encode()
            c.request("POST", "/snapshot?id=" + w["id"], body=body)
        c.getresponse().read()
        c.close()
        return w

    def ask(self, url, turn=True):
        seen = []
        t = threading.Thread(target=lambda: seen.append(self.extension()))
        if turn:
            t.start()
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=30)
        self.addCleanup(c.close)
        c.request("GET", url)
        r = c.getresponse()
        data = r.read()
        if turn:
            t.join()
        return r, data, seen[0] if seen else None

    def typed(self):
        with open(self.log) as f:
            return f.read().splitlines()

    def test_page(self):
        r, data, w = self.ask("http://webgate/live?url=https%3A%2F%2Fexample.com%2Flogin")
        self.assertEqual(w["url"], "https://example.com/login")
        self.assertEqual(r.status, 200)
        page = data.decode("utf-8")
        self.assertIn("<title>Live: Now</title>", page)
        self.assertIn('<input type=image name=at', page)
        self.assertIn('action="http://webgate/live/click"', page)

    def test_live_refuses_files_and_home(self):
        r, data, w = self.ask("http://webgate/live?url=file%3A%2F%2F%2Fetc%2F", turn=False)
        self.assertEqual(r.status, 400)
        webgate.Config.allow_local = False
        try:
            r, data, w = self.ask("http://webgate/live?url=http%3A%2F%2F10.0.0.1%2F", turn=False)
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)

    def test_config_for_the_extension(self):
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=10)
        self.addCleanup(c.close)
        c.request("GET", "/config")
        self.assertEqual(json.loads(c.getresponse().read()), {"allow_local": True})

    def test_shot(self):
        r, data, w = self.ask("http://webgate/live/shot.jpg?n=1")
        self.assertEqual(w["shot"], webgate.Config.live_width)
        self.assertEqual(r.getheader("Content-Type"), "image/jpeg")
        self.assertEqual(data, JPEG)

    def test_click(self):
        self.ask("http://webgate/live/shot.jpg?n=2")          # the screen is 1216 wide: twice the picture
        r, data, w = self.ask("http://webgate/live/click?at.x=100&at.y=40", turn=False)
        self.assertEqual(r.status, 303)
        self.assertTrue(r.getheader("Location").startswith("http://webgate/live?n="))
        self.assertEqual(self.typed(), ["mousemove 200 80 click 1"])

    def test_type_and_keys(self):
        self.ask("http://webgate/live/type?t=hello+%D7%A9&enter=Type+and+Enter", turn=False)
        self.ask("http://webgate/live/key?k=back", turn=False)
        self.ask("http://webgate/live/key?k=rm+-rf", turn=False)       # not a key: nothing
        self.assertEqual(self.typed(), ["type --delay 40 -- hello \u05e9", "key Return", "key alt+Left"])

    def test_as_a_page(self):
        r, data, w = self.ask("http://webgate/live/page")
        self.assertEqual(w.get("snap"), 1)
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("X-Gateway-Snapshot-URL"), "https://example.com/now")
        self.assertIn("<p>shown", data.decode("utf-8"))


JPEG = b"\xff\xd8\xff\xe0" + b"stand-in"


class SnapshotTest(unittest.TestCase):
    """--browser: the address to the extension (here a stand-in that does
    what webgate-ext does over the same port), its snapshot to EWeb"""

    @classmethod
    def setUpClass(cls):
        webgate.Config.browser = True
        webgate.Config.allow_local = True       # example.com without a name server here
        webgate.Config.snapshot_wait = 5
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.ext.daemon_threads = True
        threading.Thread(target=cls.ext.serve_forever, daemon=True).start()
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        cls.gw.daemon_threads = True
        threading.Thread(target=cls.gw.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        for s in (cls.ext, cls.gw):
            s.shutdown()
            s.server_close()
        webgate.Config.browser = False
        webgate.Config.allow_local = False
        webgate.Config.snapshot_wait = 60

    def extension(self, answer=True):
        """one turn of webgate-ext: the next address, then its page"""
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=30)
        c.request("GET", "/next")
        r = c.getresponse()
        w = json.loads(r.read())
        if answer:
            body = json.dumps({"url": w["url"] + "#after", "title": "T",
                               "html": "<!DOCTYPE html><title>T</title><p>made by script \u05e9"}).encode()
            c.request("POST", "/snapshot?id=" + w["id"], body=body, headers={"Content-Type": "application/json"})
            c.getresponse().read()
        c.close()
        return w

    def ask(self, url, headers):
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=30)
        self.addCleanup(c.close)
        c.request("GET", url, headers=headers)
        r = c.getresponse()
        return r, r.read()

    def test_snapshot(self):
        seen = []
        t = threading.Thread(target=lambda: seen.append(self.extension()))
        t.start()
        r, data = self.ask("https://example.com/app", {"X-Gateway-Snapshot": "1", "Accept-Encoding": "gzip"})
        t.join()
        self.assertEqual(seen[0]["url"], "https://example.com/app")
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("Content-Encoding"), "gzip")
        self.assertEqual(r.getheader("X-Gateway-Snapshot-URL"), "https://example.com/app#after")
        self.assertIn("made by script \u05e9", gzip.decompress(data).decode("utf-8"))

    def test_browser_too_slow(self):
        t = threading.Thread(target=lambda: self.extension(answer=False))
        t.start()
        r, data = self.ask("https://example.com/slow", {"X-Gateway-Snapshot": "1"})
        t.join()
        self.assertEqual(r.status, 504)
        self.assertTrue(data.startswith(b"webgate: the browser didn't finish example.com"))

    def test_home_network_refused(self):
        """7.3 holds for the browser: an address in the home network is
        refused before the browser sees it (no extension turn here)"""
        webgate.Config.allow_local = False
        try:
            r, data = self.ask("http://192.168.1.1/", {"X-Gateway-Snapshot": "1"})
        finally:
            webgate.Config.allow_local = True
        self.assertEqual(r.status, 403)
        self.assertTrue(data.startswith(b"webgate: won't connect to the local network (192.168.1.1)"))

    def test_no_browser(self):
        webgate.Config.browser = False
        try:
            r, data = self.ask("https://example.com/", {"X-Gateway-Snapshot": "1"})
        finally:
            webgate.Config.browser = True
        self.assertEqual(r.status, 501)
        self.assertTrue(data.startswith(b"webgate: snapshots need the Pi's browser"))


CF_CHECK = (b'<!DOCTYPE html><html><head><title>Just a moment...</title></head><body><noscript>Enable '
            b'JavaScript and cookies to continue</noscript><script>(function(){window._cf_chl_opt={cType: '
            b'"managed"};})();</script></body></html>')


class CheckSite(BaseHTTPRequestHandler):
    """a site behind Cloudflare's check: /forum and /next answer every
    request webgate makes itself with the check (only a browser that has
    passed it gets in, and the pass stays in the browser); an older check
    says so only in its page (/old, a 503 gzip'd); /refused is a plain 403
    from Cloudflare"""
    protocol_version = "HTTP/1.1"
    seen = []

    def log_message(self, *a):
        pass

    def version_string(self):
        return "cloudflare"

    def do_GET(self):
        CheckSite.seen.append((self.path, self.headers.get("Cookie"), self.headers.get("User-Agent")))
        extra = []
        if self.path in ("/forum", "/next", "/smiley.gif"):
            status, body, extra = 403, CF_CHECK, [("cf-mitigated", "challenge")]
        elif self.path == "/old":
            status, body, extra = 503, gzip.compress(CF_CHECK), [("Content-Encoding", "gzip")]
        else:
            status, body = 403, b"<!DOCTYPE html><title>Forbidden</title><p>no"
        self.send_response(status)
        self.send_header("Content-Type", "text/html")
        for k, v in extra:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class CheckTest(unittest.TestCase):
    """--browser and Cloudflare's check: the page through the browser, the
    site's next pages there too for the visit, nothing of its pass moved
    out of it; a check that wants a click, or that the browser didn't pass
    in time, as it came and said so"""

    @classmethod
    def setUpClass(cls):
        webgate.Config.browser = True
        webgate.Config.allow_local = True
        webgate.Config.snapshot_wait = 5
        cls.site = ThreadingHTTPServer(("127.0.0.1", 0), CheckSite)
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.gw = ThreadingHTTPServer(("127.0.0.1", 0), webgate.Handler)
        for srv in (cls.site, cls.ext, cls.gw):
            srv.daemon_threads = True
            threading.Thread(target=srv.serve_forever, daemon=True).start()
        cls.base = "http://127.0.0.1:%d" % cls.site.server_address[1]

    @classmethod
    def tearDownClass(cls):
        for srv in (cls.site, cls.ext, cls.gw):
            srv.shutdown()
            srv.server_close()
        webgate.Config.browser = False
        webgate.Config.allow_local = False
        webgate.Config.snapshot_wait = 60

    def setUp(self):
        webgate.VISITS.clear()
        CheckSite.seen.clear()

    def extension(self, snap=None):
        """one turn of webgate-ext: the address, then the page it went on
        to after the check, or snap; False: no answer"""
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=30)
        c.request("GET", "/next")
        w = json.loads(c.getresponse().read())
        if snap is not False:
            body = json.dumps(snap or {"url": w["url"], "title": "Forum", "challenge": False,
                                       "html": "<title>Forum</title><p>via the browser: " + w["url"]})
            c.request("POST", "/snapshot?id=" + w["id"], body=body.encode(), headers={"Content-Type": "application/json"})
            c.getresponse().read()
        c.close()
        return w

    def ask(self, path, ext=None, accept="text/html,application/xhtml+xml,*/*;q=0.8"):
        t = None
        if ext is not None:
            t = threading.Thread(target=lambda: self.extension(ext))
            t.start()
        c = http.client.HTTPConnection("127.0.0.1", self.gw.server_address[1], timeout=30)
        self.addCleanup(c.close)
        c.request("GET", self.base + path, headers={"User-Agent": "EWeb/0.33 (Atari Falcon030)", "Cookie": "mine=1",
                                                    "Accept": accept})
        r = c.getresponse()
        data = r.read()
        if t is not None:
            t.join()
        return r, data

    def test_through_the_browser_then_on_it_for_the_visit(self):
        r, data = self.ask("/forum", ext={})
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("X-Gateway-Snapshot-URL"), self.base + "/forum")
        self.assertIn(b"via the browser: " + self.base.encode() + b"/forum", data)
        # the site's next page: straight to the browser, not to the site
        # first; what webgate sent the site was EWeb's own, as it came
        r, data = self.ask("/next", ext={})
        self.assertEqual(r.status, 200)
        self.assertIn(b"via the browser: " + self.base.encode() + b"/next", data)
        self.assertEqual(CheckSite.seen, [("/forum", "mine=1", "EWeb/0.33 (Atari Falcon030)")])

    def test_visit_ends_after_25_minutes(self):
        self.ask("/forum", ext={})
        host = urlsplit_host(self.base)
        self.assertTrue(webgate.visiting(host))
        webgate.VISITS[host] -= webgate.VISIT_KEEP + 1
        self.assertFalse(webgate.visiting(host))
        self.assertNotIn(host, webgate.VISITS)

    def test_a_visit_the_browser_cant_go_on_with(self):
        """the check again in the browser: the visit ends, and the page is
        asked for as usual (the check, then the browser again)"""
        self.ask("/forum", ext={})
        turns = threading.Thread(target=lambda: (self.extension({"url": "x", "html": "x", "challenge": True}),
                                                 self.extension()))
        turns.start()
        r, data = self.ask("/next")
        turns.join()
        self.assertEqual(r.status, 200)
        self.assertIn(b"via the browser", data)
        self.assertEqual(CheckSite.seen[-1][0], "/next")

    def test_wants_a_click(self):
        r, data = self.ask("/forum", ext={"url": "x", "html": "<title>Just a moment...</title>", "challenge": True})
        self.assertEqual(r.status, 403)
        self.assertEqual(r.getheader("X-Gateway-Check"), "click")
        self.assertEqual(r.getheader("cf-mitigated"), "challenge")
        self.assertEqual(data, CF_CHECK)
        self.assertEqual(webgate.VISITS, {})

    def test_browser_too_slow(self):
        r, data = self.ask("/forum", ext=False)
        self.assertEqual(r.status, 403)
        self.assertEqual(r.getheader("X-Gateway-Check"), "failed")
        self.assertEqual(data, CF_CHECK)

    def test_an_older_check_by_its_page(self):
        r, data = self.ask("/old", ext={})
        self.assertEqual(r.status, 200)
        self.assertIn(b"via the browser", data)

    def test_a_plain_refusal_as_it_came(self):
        r, data = self.ask("/refused")
        self.assertEqual(r.status, 403)
        self.assertIsNone(r.getheader("X-Gateway-Check"))
        self.assertIn(b"Forbidden", data)
        self.assertIsNone(webgate.BRIDGE.wanted)

    def test_a_picture_as_it_came(self):
        """not a page: no browser for it, during the visit too (its pass
        stays in the browser)"""
        r, data = self.ask("/smiley.gif", accept="image/jpeg,image/png,image/gif;q=0.9")
        self.assertEqual(r.status, 403)
        self.assertIsNone(r.getheader("X-Gateway-Check"))
        self.assertIsNone(webgate.BRIDGE.wanted)
        self.ask("/forum", ext={})
        r, data = self.ask("/smiley.gif", accept="image/jpeg,image/png,image/gif;q=0.9")
        self.assertEqual(r.status, 403)
        self.assertEqual(CheckSite.seen[-1], ("/smiley.gif", "mine=1", "EWeb/0.33 (Atari Falcon030)"))

    def test_without_the_browser_as_it_came(self):
        webgate.Config.browser = False
        try:
            r, data = self.ask("/forum")
        finally:
            webgate.Config.browser = True
        self.assertEqual(r.status, 403)
        self.assertIsNone(r.getheader("X-Gateway-Check"))
        self.assertEqual(data, CF_CHECK)


def urlsplit_host(url):
    return url.split("//", 1)[1].rsplit(":", 1)[0]


class BrowserProxyTest(unittest.TestCase):
    """the Pi's browser goes out through webgate (127.0.0.1:8281): every
    connection to the address webgate checked, so a name whose answer is
    at home is refused there too (7.3)"""

    @classmethod
    def setUpClass(cls):
        cls.up = ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
        threading.Thread(target=cls.up.serve_forever, daemon=True).start()
        cls.ext = ThreadingHTTPServer(("127.0.0.1", 0), webgate.ExtHandler)
        cls.ext.daemon_threads = True
        threading.Thread(target=cls.ext.serve_forever, daemon=True).start()
        cls.port = cls.up.server_address[1]

    @classmethod
    def tearDownClass(cls):
        for s in (cls.up, cls.ext):
            s.shutdown()
            s.server_close()
        webgate.Config.allow_local = False
        webgate.Config.allow = []

    def conn(self):
        c = http.client.HTTPConnection("127.0.0.1", self.ext.server_address[1], timeout=10)
        self.addCleanup(c.close)
        return c

    def connect(self, host):
        c = self.conn()
        c.request("CONNECT", "%s:%d" % (host, self.port))
        return c, c.getresponse()

    def test_home_refused(self):
        webgate.Config.allow_local = False
        c, r = self.connect("127.0.0.1")
        self.assertEqual(r.status, 403)
        self.assertIn(b"webgate: won't connect to the local network (127.0.0.1)", r.read())
        c, r = self.connect("localhost")       # a name, its answer checked
        self.assertEqual(r.status, 403)
        c = self.conn()
        c.request("GET", "http://127.0.0.1:%d/page" % self.port)
        r = c.getresponse()
        self.assertEqual(r.status, 403)
        r.read()

    def test_tunnel_and_page_with_allow_local(self):
        webgate.Config.allow_local = True
        webgate.Config.allow = ["192.168.1.50"]       # the Atari's: not the browser's concern
        try:
            s = socket.create_connection(("127.0.0.1", self.ext.server_address[1]), timeout=10)
            self.addCleanup(s.close)
            s.sendall(b"CONNECT 127.0.0.1:%d HTTP/1.1\r\nHost: x\r\n\r\n" % self.port)
            got = b""
            while b"\r\n\r\n" not in got:
                got += s.recv(65536)
            self.assertTrue(got.startswith(b"HTTP/1.1 200"), got)
            s.sendall(b"GET /echo HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            got = b""
            while True:
                d = s.recv(65536)
                if not d:
                    break
                got += d
            self.assertTrue(got.endswith(b"GET /echo "), got)
            c = self.conn()
            c.request("GET", "http://127.0.0.1:%d/page" % self.port)
            r = c.getresponse()
            self.assertEqual((r.status, r.read()), (200, PAGE))
        finally:
            webgate.Config.allow_local = False
            webgate.Config.allow = []

    def resolver(self, *answers):
        """a stand-in for the name server: each call the next answer"""
        calls = []

        def getaddrinfo(host, port, *a, **k):
            calls.append(host)
            ips = answers[min(len(calls), len(answers)) - 1]
            return [(socket.AF_INET, socket.SOCK_STREAM, 6, "", (ip, port)) for ip in ips]
        return calls, getaddrinfo

    def test_private_addresses_never_dialled(self):
        webgate.Config.allow_local = False
        dialled = []
        with unittest.mock.patch.object(webgate.aeragate, "dial", lambda addrs, *a: dialled.append(addrs)):
            for host in ("192.168.1.1", "10.0.0.1", "172.16.0.1", "169.254.169.254", "127.0.0.1",
                         "[::1]", "[::ffff:192.168.1.1]", "0.0.0.0"):
                c, r = self.connect(host)
                self.assertEqual(r.status, 403, host)
                r.read()
        self.assertEqual(dialled, [])

    def test_names_that_answer_home(self):
        webgate.Config.allow_local = False
        for answer in (["192.168.1.10"], ["93.184.216.34", "192.168.1.10"]):     # home, and mixed
            calls, fake = self.resolver(answer)
            c1, c2 = self.conn(), self.conn()
            c1.connect()                # to webgate, before the stand-in answers every name
            c2.connect()
            with unittest.mock.patch.object(webgate.aeragate.socket, "getaddrinfo", fake):
                c1.request("CONNECT", "rebind.example:443")
                r = c1.getresponse()
                self.assertEqual(r.status, 403, answer)
                r.read()
                c2.request("GET", "http://rebind.example/")
                r = c2.getresponse()
                self.assertEqual(r.status, 403, answer)
                r.read()

    def test_rebinding_one_look_up(self):
        """public at the check, private a moment later: webgate connects to
        what it checked, and asks once"""
        webgate.Config.allow_local = False
        calls, fake = self.resolver(["93.184.216.34"], ["192.168.1.10"])
        dialled = []
        a, b = socket.socketpair()
        self.addCleanup(a.close)

        def dial(addrs, *rest):
            dialled.append(addrs)
            return b
        s = socket.create_connection(("127.0.0.1", self.ext.server_address[1]), timeout=10)
        self.addCleanup(s.close)
        with unittest.mock.patch.object(webgate.aeragate.socket, "getaddrinfo", fake), \
                unittest.mock.patch.object(webgate.aeragate, "dial", dial):
            s.sendall(b"CONNECT rebind.example:443 HTTP/1.1\r\nHost: x\r\n\r\n")
            got = b""
            while b"\r\n\r\n" not in got:
                got += s.recv(4096)
            self.assertTrue(got.startswith(b"HTTP/1.1 200"), got)
            s.sendall(b"hello")
            self.assertEqual(a.recv(5), b"hello")
            a.sendall(b"back")
            self.assertEqual(s.recv(4), b"back")
        self.assertEqual(calls, ["rebind.example"])
        self.assertEqual([sa[0] for _, sa in dialled[0]], ["93.184.216.34"])

    def test_only_the_pi_itself(self):
        self.assertTrue(webgate._local("127.0.0.1"))
        self.assertTrue(webgate._local("::1"))
        self.assertFalse(webgate._local("192.168.1.50"))

    def test_pages_kept_off_the_bridge(self):
        """a page in the Pi's browser can't take the Atari's next page
        (/next) or hand in a snapshot of its own; the extension can"""
        for headers in ({"Sec-Fetch-Site": "cross-site"}, {"Sec-Fetch-Site": "same-site"},
                        {"Origin": "http://evil.example"}, {"Origin": "null"}):
            c = self.conn()
            c.request("GET", "/next", headers=headers)
            self.assertEqual(c.getresponse().status, 403, headers)
            c = self.conn()
            c.request("POST", "/snapshot?id=1", body=b"{}", headers=headers)
            self.assertEqual(c.getresponse().status, 403, headers)
        webgate.BRIDGE.asked = "7"
        try:
            c = self.conn()
            c.request("POST", "/snapshot?id=7", body=b'{"html": "mine"}',
                      headers={"Sec-Fetch-Site": "cross-site", "Origin": "http://evil.example"})
            self.assertEqual(c.getresponse().status, 403)
            self.assertNotIn("7", webgate.BRIDGE.done)
            c = self.conn()
            c.request("POST", "/snapshot?id=7", body=b'{"html": "made"}',
                      headers={"Sec-Fetch-Site": "none", "Origin": "chrome-extension://abcdef"})
            self.assertEqual(c.getresponse().status, 200)
            self.assertEqual(webgate.BRIDGE.done.pop("7"), {"html": "made"})
        finally:
            webgate.BRIDGE.asked = None
        c = self.conn()
        c.request("GET", "/config", headers={"Sec-Fetch-Site": "none"})
        self.assertEqual(c.getresponse().status, 200)

    def test_extension_still_answered(self):
        c = self.conn()
        c.request("GET", "/config")
        self.assertEqual(json.loads(c.getresponse().read()), {"allow_local": False})


class TlsTest(unittest.TestCase):
    def test_tls_of(self):
        self.assertEqual(webgate.tls_of(http.client.HTTPConnection("x")), "")
        fake = type("C", (), {})()
        fake.sock = None
        self.assertEqual(webgate.tls_of(fake), "")


if __name__ == "__main__":
    unittest.main()
