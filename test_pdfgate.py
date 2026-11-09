# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Tests for pdfgate: a PDF sent as EConvert and EPDF send it.

The converter is a stand-in (a small script that answers as aerapdf
does) unless AERAPDF names a real one (EPDF's build/econvert, or the
Pi's /opt/aera-gateway-pdf/aerapdf) with AERAPDF_FONTS its fonts: then a
real PDF goes through it too, and its answer is checked byte for byte
against what aerapdf writes on its own (Direct against Pi).
"""

import http.client
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(__file__))
import pdfgate  # noqa: E402

PDF = (b"%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n"
       b"2 0 obj<</Type/Pages/Kids[]/Count 0>>endobj\ntrailer<</Root 1 0 R>>\n%%EOF\n")

# answers as aerapdf does: progress on stderr, the result written; an input
# holding FAIL fails, SLOW never ends, and the flags it got are the result
FAKE = r'''#!/usr/bin/env python3
import sys, time
a = sys.argv[1:]
src, dst = a[-2], a[-1]
data = open(src, "rb").read()
if b"FAIL" in data:
    print("econvert: %s: no pages" % src, file=sys.stderr)
    sys.exit(1)
if b"SLOW" in data:
    time.sleep(60)
said = None
if "--translate" in a:
    # a page's text through the translator, as aerapdf does
    import subprocess
    to, tr = a[a.index("--translate") + 1], a[a.index("--translator") + 1]
    text = data.split(b"TEXT:", 1)[1] if b"TEXT:" in data else b"Hallo, das ist gut.\n\nZwei."
    p = subprocess.run([tr, "--to", to], input=text, capture_output=True)
    if p.returncode:
        print("econvert: the translation failed", file=sys.stderr)
        sys.exit(p.returncode if 2 <= p.returncode <= 4 else 1)
    said = p.stderr.decode().splitlines()[0]
    a = a + ["=", p.stdout.decode()]
for i in range(0, 1001, 250):
    print("progress %d 1000" % i, file=sys.stderr, flush=True)
    time.sleep(0.6)
open(dst, "wb").write(b"%PDF-converted " + " ".join(a[:-2] if said is None else a[:-4] + a[-2:]).encode())
if said:
    print(said, file=sys.stderr)
'''


# answers as translateclient does: zz the pair isn't installed (the language
# noted), yy busy, xx too long; else "[CODE]" before each paragraph
TRANSLATOR = r'''#!/usr/bin/env python3
import os, sys
to = sys.argv[2]
assert float(os.environ["TRANSLATECLIENT_UNTIL"]) > 0
if to in ("zz", "yy", "xx"):
    open(os.environ["TRANSLATECLIENT_NOTE"], "w").write("he\n")
    sys.exit({"zz": 2, "yy": 3, "xx": 4}[to])
paras = sys.stdin.read().split("\n\n")
if to == "de":
    print("same de", file=sys.stderr)
    sys.exit(0)
print("\n\n".join("[%s] %s" % (to, p) for p in paras))
print("from he", file=sys.stderr)
'''


def raw(port, request, body=b""):
    """the whole answer to request (bytes), the connection read to its end"""
    s = socket.create_connection(("127.0.0.1", port), timeout=30)
    s.sendall(request + body)
    out = b""
    while True:
        b = s.recv(65536)
        if not b:
            break
        out += b
    s.close()
    return out


def post(port, query, body):
    head = ("POST /convert%s HTTP/1.1\r\nHost: pi\r\nContent-Length: %d\r\n\r\n" %
            (query, len(body))).encode()
    return raw(port, head, body)


def final(answer):
    """the last answer's status, headers' text and body (after any 102s)"""
    while answer.startswith(b"HTTP/1.1 102"):
        answer = answer.split(b"\r\n\r\n", 1)[1]
    head, body = answer.split(b"\r\n\r\n", 1)
    return int(head.split()[1]), head.decode(), body


class Gate(unittest.TestCase):
    real = None

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(prefix="test-pdfgate-")
        fake = os.path.join(cls.dir, "aerapdf")
        with open(fake, "w") as f:
            f.write(FAKE)
        os.chmod(fake, os.stat(fake).st_mode | stat.S_IEXEC)
        tr = os.path.join(cls.dir, "translator")
        with open(tr, "w") as f:
            f.write(TRANSLATOR)
        os.chmod(tr, os.stat(tr).st_mode | stat.S_IEXEC)
        pdfgate.Config.translator = tr
        with open(os.path.join(cls.dir, "SOURCE"), "w") as f:
            f.write("0123abc\n")
        pdfgate.Config.aerapdf = fake
        pdfgate.Config.source = os.path.join(cls.dir, "SOURCE")
        pdfgate.Config.fonts = cls.dir
        pdfgate.Config.seconds = 5
        pdfgate.Config.allow = []
        cls.srv = ThreadingHTTPServer(("127.0.0.1", 0), pdfgate.Handler)
        cls.srv.daemon_threads = True
        cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        shutil.rmtree(cls.dir, ignore_errors=True)

    def setUp(self):
        pdfgate.Config.aerapdf = os.path.join(self.dir, "aerapdf")
        pdfgate.Config.fonts = self.dir
        pdfgate.Config.allow = []

    def test_version(self):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("GET", "/version")
        r = c.getresponse()
        self.assertEqual((r.status, r.read()), (200, b"aerapdf 0123abc\n"))

    def test_converted_with_progress(self):
        a = post(self.port, "?pages=mono&page_width=1280&double=0", PDF)
        self.assertTrue(a.startswith(b"HTTP/1.1 102 Processing\r\nX-Progress: "), a[:80])
        self.assertIn(b"X-Progress: 250/1000", a)
        status, head, body = final(a)
        self.assertEqual(status, 200)
        self.assertIn("Content-Type: application/pdf", head)
        self.assertIn("Content-Length: %d" % len(body), head)
        self.assertEqual(body, b"%PDF-converted --progress --fonts " + self.dir.encode() +
                         b" --pages mono --page-width 1280 --normal-pages")

    def test_options_checked(self):
        for q in ("?pages=all", "?page_width=99999", "?page_width=12a", "?color=1",
                  "?pages=mono&pages=none", "?double=2", "?picture_width=8",
                  "?name=../x.pdf", "?name=.hidden", "?name=a/b", "?name=" + "x" * 70):
            status, _, body = final(post(self.port, q, PDF))
            self.assertEqual(status, 400, q)
            self.assertTrue(body.startswith(b"pdfgate: "), body)

    def test_translated(self):
        """translate=CODE: aerapdf with the translator, the language said"""
        a = post(self.port, "?pages=mono&translate=en", PDF)
        self.assertIn(b"X-Progress: 250/1000", a)
        status, head, body = final(a)
        self.assertEqual(status, 200)
        self.assertIn("X-Translate-From: he\r\n", head)
        self.assertNotIn("X-Translate-Same", head)
        tr = os.path.join(self.dir, "translator").encode()
        self.assertEqual(body, b"%PDF-converted --progress --fonts " + self.dir.encode() +
                         b" --pages mono --translate en --translator " + tr +
                         b" = [en] Hallo, das ist gut.\n\n[en] Zwei.\n")
        status, head, _ = final(post(self.port, "?translate=de", PDF))
        self.assertEqual(status, 200)
        self.assertIn("X-Translate-From: de\r\nX-Translate-Same: 1\r\n", head)

    def test_without_translate_unchanged(self):
        status, head, _ = final(post(self.port, "?pages=mono", PDF))
        self.assertEqual(status, 200)
        self.assertNotIn("X-Translate", head)

    def test_translate_refused(self):
        for to, want in (("zz", (422, b"pdfgate: no translation from he into zz\n")),
                         ("yy", (503, b"pdfgate: the translation service is busy\n")),
                         ("xx", (504, b"pdfgate: the translation took too long\n"))):
            status, _, body = final(post(self.port, "?translate=" + to, PDF))
            self.assertEqual((status, body), want, to)
        for q in ("?translate=EN", "?translate=eng", "?translate=", "?translate=e1"):
            status, _, body = final(post(self.port, q, PDF))
            self.assertEqual(status, 400, q)
            self.assertTrue(body.startswith(b"pdfgate: translate="), body)

    def test_translated_by_translategate(self):
        """the real translateclient against translategate's local socket
        (its stand-in engine): the text and no word of it in the log"""
        import contextlib
        import io
        import translategate as tg
        d = tempfile.mkdtemp(prefix="test-pdfgate-tg-")
        sock = os.path.join(d, "local.sock")
        tg.Config.engine = tg.StandIn()
        tg.LANES = tg.Lanes(2)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            srv = tg.serve_local(sock)
            os.environ["TRANSLATECLIENT_SOCKET"] = sock
            pdfgate.Config.translator = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     "translateclient.py")
            try:
                status, head, body = final(post(self.port, "?translate=en", PDF + b"TEXT:Pomegranate ist gut."))
                refused = final(post(self.port, "?translate=fr", PDF + b"TEXT:Pomegranate ist nicht gut."))
            finally:
                pdfgate.Config.translator = os.path.join(self.dir, "translator")
                del os.environ["TRANSLATECLIENT_SOCKET"]
                srv.shutdown()
                srv.server_close()
                shutil.rmtree(d, ignore_errors=True)
        self.assertEqual(status, 200)
        self.assertIn("X-Translate-From: de\r\n", head)
        self.assertTrue(body.endswith(b" = en: POMEGRANATE IST GUT.\n"), body[-60:])
        self.assertEqual(refused[:3:2], (422, b"pdfgate: no translation from de into fr\n"))
        self.assertNotIn("pomegranate", err.getvalue().lower())

    def test_not_a_pdf(self):
        status, _, body = final(post(self.port, "", b"GIF89a" + b"x" * 100))
        self.assertEqual((status, body), (415, b"pdfgate: not a PDF\n"))

    def test_too_big(self):
        old = pdfgate.Config.max_bytes
        pdfgate.Config.max_bytes = 1000
        try:
            a = raw(self.port, b"POST /convert HTTP/1.1\r\nContent-Length: 2000\r\n\r\n")
        finally:
            pdfgate.Config.max_bytes = old
        self.assertEqual(final(a)[0], 413)

    def test_no_length(self):
        a = raw(self.port, b"POST /convert HTTP/1.1\r\n\r\n")
        self.assertEqual(final(a)[0], 411)

    def test_failed_says_why_without_paths(self):
        status, _, body = final(post(self.port, "", PDF + b"FAIL"))
        self.assertEqual((status, body), (422, b"pdfgate: the PDF: no pages\n"))
        status, _, body = final(post(self.port, "?name=REPORT.PDF", PDF + b"FAIL"))
        self.assertEqual((status, body), (422, b"pdfgate: the PDF: no pages\n"))

    def test_name_out(self):
        """a PDF named as the result's folder: converted all the same"""
        for n in ("out", "OUT", "in"):
            self.assertEqual(final(post(self.port, "?name=" + n, PDF))[0], 200, n)

    def test_why_not_logged(self):
        """a 422's words (aerapdf's, which may quote the PDF) go to the
        Atari, not to the log"""
        import contextlib
        import io
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            status, _, body = final(post(self.port, "", PDF + b"FAIL"))
        self.assertEqual((status, body), (422, b"pdfgate: the PDF: no pages\n"))
        self.assertIn("422", err.getvalue())
        self.assertNotIn("no pages", err.getvalue())

    def test_verbose_names_no_file(self):
        """-v logs each request without its query (the PDF's name)"""
        import contextlib
        import io
        err = io.StringIO()
        pdfgate.Config.verbose = True
        try:
            with contextlib.redirect_stderr(err):
                final(post(self.port, "?name=SECRET.PDF", PDF))
        finally:
            pdfgate.Config.verbose = False
        self.assertIn("POST /convert", err.getvalue())
        self.assertNotIn("SECRET", err.getvalue())

    def test_waiting_told(self):
        """a second Atari, while the first converts, hears 0/1000 until its turn"""
        old = pdfgate.QUEUED_SAY
        pdfgate.QUEUED_SAY = 0.3
        first = threading.Thread(target=post, args=(self.port, "", PDF))
        try:
            first.start()
            import time
            time.sleep(0.3)
            a = post(self.port, "", PDF)
        finally:
            first.join()
            pdfgate.QUEUED_SAY = old
        self.assertTrue(a.startswith(b"HTTP/1.1 102 Processing\r\nX-Progress: 0/1000\r\n\r\n"), a[:80])
        self.assertEqual(final(a)[0], 200)

    def test_too_long(self):
        status, _, body = final(post(self.port, "", PDF + b"SLOW"))
        self.assertEqual(status, 504)
        self.assertIn(b"over 5 s", body)

    def test_no_converter(self):
        pdfgate.Config.aerapdf = os.path.join(self.dir, "none")
        self.assertEqual(final(post(self.port, "", PDF))[0], 503)

    def test_only_allowed(self):
        pdfgate.Config.allow = ["192.168.1.99"]
        self.assertEqual(final(post(self.port, "", PDF))[0], 403)

    def test_wrong_page(self):
        status, _, _ = final(raw(self.port, b"GET /etc/passwd HTTP/1.1\r\n\r\n"))
        self.assertEqual(status, 404)

    def test_nothing_left_behind(self):
        tmp = tempfile.mkdtemp(prefix="test-pdfgate-tmp-")
        pdfgate.Config.tmp = tmp
        try:
            final(post(self.port, "", PDF))
            final(post(self.port, "", PDF + b"FAIL"))
            self.assertEqual(os.listdir(tmp), [])
        finally:
            pdfgate.Config.tmp = None
            shutil.rmtree(tmp, ignore_errors=True)

    @unittest.skipUnless(os.environ.get("AERAPDF"), "AERAPDF names no real aerapdf")
    def test_real_same_as_direct(self):
        """a real PDF through the real aerapdf: the same bytes as aerapdf
        writes on its own (EPDF's Direct mode against the Pi's)"""
        real = os.path.abspath(os.environ["AERAPDF"])
        fonts = os.path.abspath(os.environ.get("AERAPDF_FONTS", "/usr/share/fonts/truetype/liberation"))
        src = os.environ.get("AERAPDF_PDF")
        if not src:
            self.skipTest("AERAPDF_PDF names no PDF")
        pdfgate.Config.aerapdf, pdfgate.Config.fonts = real, fonts
        old = pdfgate.Config.seconds
        pdfgate.Config.seconds = 300
        try:
            with open(src, "rb") as f:
                body = f.read()
            status, _, got = final(post(self.port, "?pages=mono&page_width=640&picture_width=640&name=" +
                                         os.path.basename(src), body))
        finally:
            pdfgate.Config.seconds = old
        self.assertEqual(status, 200)
        out = os.path.join(self.dir, "direct.pdf")
        subprocess.run([real, "--fonts", fonts, "--pages", "mono", "--page-width", "640",
                        "--picture-width", "640", src, out], check=True, stdout=subprocess.DEVNULL)
        with open(out, "rb") as f:
            self.assertEqual(got, f.read())


if __name__ == "__main__":
    unittest.main()
