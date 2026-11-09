#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""pdfgate - PDFs converted for the Atari, for EConvert and EPDF.

The Atari sends a PDF; the Pi converts it with aerapdf (EPDF's converter,
the same C code EConvert runs on a Falcon, built here from pdf/) and sends
back the converted PDF, the same file EConvert would have written
(EPDF's docs/CONVERTED-PDF.md). The protocol is EPDF's
(docs/PI-GATEWAY.md there):

    POST /convert?pages=mono&photos=indexed&pictures=screen&page_width=640
                  &picture_width=640&double=1&name=REPORT.PDF HTTP/1.1
    Content-Length: N                   (the PDF's bytes follow)

  answered, while aerapdf works, by

    HTTP/1.1 102 Processing
    X-Progress: DONE/TOTAL              (thousandths of the file's pages)

  and then by 200 with the converted PDF (Content-Length), or by an error
  with one line "pdfgate: why". The options are all optional (name: the
  PDF's own name, as the result records it; "in.pdf"); anything else is
  refused (400).

    GET /version   ->  "aerapdf EPDF-COMMIT"   (which converter answers)

translate=CODE (Erez, 8 Nov 2026; EPDF's docs/PI-GATEWAY.md): the
converted PDF also carries each page's text translated into CODE, by
translategate on this machine (aerapdf runs translateclient, its
translator, once a page; the text never leaves the Gateway). The 102s
cover both; the 200 says X-Translate-From: CODE (most pages' language),
and X-Translate-Same: 1 when every page was in CODE already. Refused as
422 "pdfgate: no translation from LANG into LANG", 503 "pdfgate: the
translation service is busy" or 504. Pages the conversion's time didn't
reach come as they were. Without translate, nothing changes.

The rules are aeragate's: the home network only, or --allow; the log says
who converted how much, never the file's name or what is in it. One
conversion at a time (--jobs): an Atari that waits for its turn is told so
every few seconds (102, X-Progress: 0/1000). Each has its own time (--time)
and memory (--memory); the PDF and the result are kept on disk only while
it runs.

Standard Python 3 only.
"""

import argparse
import os
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aeragate  # noqa: E402

VERSION = "1.0"
PIECE = 65536
QUEUED_SAY = 5                          # seconds between a waiting Atari's 102s


class Config:
    allow = []
    aerapdf = "/opt/aera-gateway-pdf/aerapdf"
    fonts = "/opt/aera-gateway-pdf/fonts"
    source = "/opt/aera-gateway-pdf/SOURCE"
    max_bytes = 64 << 20
    seconds = 600
    memory = 768 << 20
    jobs = threading.BoundedSemaphore(1)
    tmp = None
    verbose = False
    translator = os.path.join(os.path.dirname(os.path.abspath(__file__)), "translateclient.py")


# the options EConvert has, each as aerapdf's flag; values checked
OPTIONS = {
    "pages": ("--pages", ("none", "mono", "colour")),
    "photos": ("--photos", ("indexed", "jpeg")),
    "pictures": ("--pictures", ("page", "screen")),
    "page_width": ("--page-width", (160, 4096)),
    "picture_width": ("--picture-width", (64, 4096)),
    "double": (None, ("0", "1")),
    "name": (None, None),               # the PDF's own name, kept in the result's AERA entry
    "translate": ("--translate", None),     # a language, ISO 639-1
}
LAST_SECONDS = 60                       # of a conversion's time, kept for converting after translating
NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_. -]{0,63}")


def flags(query):
    """aerapdf's flags for the query, or None (with why) if anything in it
    isn't one of OPTIONS with a value it may have"""
    out = []
    seen = set()
    name = "in.pdf"
    for k, v in parse_qsl(query, keep_blank_values=True, strict_parsing=False):
        if k not in OPTIONS or k in seen:
            return None, "no option %s" % k[:20]
        seen.add(k)
        flag, ok = OPTIONS[k]
        if k == "name":
            if not NAME.fullmatch(v):
                return None, "name?"
            name = v
            continue
        if k == "translate":
            if not re.fullmatch(r"[a-z]{2}", v):
                return None, "translate=%s" % v[:10]
            out += [flag, v, "--translator", Config.translator]
            continue
        if isinstance(ok[0], int):
            if not re.fullmatch(r"[0-9]{1,5}", v) or not ok[0] <= int(v) <= ok[1]:
                return None, "%s %s?" % (k, v[:10])
        elif v not in ok:
            return None, "%s %s?" % (k, v[:10])
        if k == "double":
            if v == "0":
                out.append("--normal-pages")
        else:
            out += [flag, v]
    return (out, name), None


def which():
    """the converter that answers: "aerapdf COMMIT", or None"""
    if not os.access(Config.aerapdf, os.X_OK):
        return None
    try:
        with open(Config.source) as f:
            return "aerapdf " + (f.read().strip() or "?")[:64]
    except OSError:
        return "aerapdf ?"


def limits(pid):
    """aerapdf's: its memory, its CPU time (a little over the wall's), no
    core; set from here once it runs (preexec_fn isn't safe in a threaded
    server; what it does before this is read its arguments)"""
    resource.prlimit(pid, resource.RLIMIT_AS, (Config.memory, Config.memory))
    resource.prlimit(pid, resource.RLIMIT_CPU, (Config.seconds + 5, Config.seconds + 10))
    resource.prlimit(pid, resource.RLIMIT_CORE, (0, 0))


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "pdfgate/" + VERSION
    timeout = 120                       # a connection silent this long is closed

    def log_message(self, fmt, *args):
        """-v: each request, its method and page only (the query names the
        PDF: never in the log)"""
        if Config.verbose and getattr(self, "command", None):
            aeragate.say("pdfgate: %s %s %s" % (self.client_address[0], self.command,
                                                 urlsplit(self.path).path[:40]))

    def note(self, status, n_in, n_out, more=""):
        aeragate.say("pdfgate: %s %d, %d bytes in, %d out%s" %
                     (self.client_address[0], status, n_in, n_out, more))

    def reply(self, status, text, n_in=0, logged=True):
        body = text.encode("utf-8", "replace")
        self.close_connection = True
        try:
            self.send_response(status)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        except OSError:
            pass
        # (aerapdf's own words, a 422's, may quote what is in the PDF: the
        # Atari gets them, the log only the status and the bytes)
        self.note(status, n_in, 0, " (%s)" % text.strip() if logged else "")

    def allowed(self):
        if aeragate.client_ok(self.client_address[0], Config.allow):
            return True
        self.reply(403, "pdfgate: not from this computer\n")
        return False

    def do_GET(self):
        if not self.allowed():
            return
        if urlsplit(self.path).path != "/version":
            self.reply(404, "pdfgate: no such page\n")
            return
        w = which()
        if not w:
            self.reply(503, "pdfgate: no aerapdf\n")
            return
        body = (w + "\n").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if not self.allowed():
            return
        url = urlsplit(self.path)
        if url.path != "/convert":
            self.reply(404, "pdfgate: no such page\n")
            return
        got, why = flags(url.query)
        if got is None:
            self.reply(400, "pdfgate: %s\n" % why)
            return
        size = self.headers.get("Content-Length", "")
        if not size.isdigit():
            self.reply(411, "pdfgate: no length\n")
            return
        size = int(size)
        if size > Config.max_bytes:
            self.reply(413, "pdfgate: the PDF is over %d MB\n" % (Config.max_bytes >> 20))
            return
        if not which():
            self.reply(503, "pdfgate: no aerapdf\n")
            return
        work = tempfile.mkdtemp(prefix="pdfgate-", dir=Config.tmp)
        try:
            self.convert(work, size, *got)
        finally:
            shutil.rmtree(work, ignore_errors=True)

    def convert(self, work, size, args, name):
        # the PDF under its own name (the result's AERA entry names it), and
        # the result, each in a folder of its own, so no name can meet another
        os.mkdir(os.path.join(work, "in"))
        os.mkdir(os.path.join(work, "out"))
        src, dst = os.path.join(work, "in", name), os.path.join(work, "out", "out.pdf")
        got = 0
        with open(src, "wb") as f:
            while got < size:
                b = self.rfile.read(min(PIECE, size - got))
                if not b:
                    break
                f.write(b)
                got += len(b)
        if got < size:
            self.close_connection = True
            self.note(400, got, 0, " (cut off)")
            return
        with open(src, "rb") as f:
            if not f.read(1024).lstrip(b"\x00\t\n\r ").startswith(b"%PDF-"):
                self.reply(415, "pdfgate: not a PDF\n", got)
                return
        if not self.wait_turn():
            self.close_connection = True
            self.note(499, got, 0, " (the Atari went away while waiting)")
            return
        t0 = time.monotonic()
        tr = {}
        try:
            status, text = self.run(args, src, dst, t0, tr)
        finally:
            Config.jobs.release()
        if status == 499:
            self.close_connection = True
            self.note(499, got, 0, " (the Atari went away)")
            return
        if status != 200:
            self.reply(status, text, got, logged=status != 422)
            return
        n = os.path.getsize(dst)
        self.close_connection = True
        try:
            self.send_response(200)
            self.send_header("Content-Type", "application/pdf")
            self.send_header("Content-Length", str(n))
            if tr.get("from"):
                self.send_header("X-Translate-From", tr["from"])
            if tr.get("same"):
                self.send_header("X-Translate-Same", "1")
            self.send_header("Connection", "close")
            self.end_headers()
            with open(dst, "rb") as f:
                shutil.copyfileobj(f, self.wfile, PIECE)
        except OSError:
            self.note(200, got, 0, " (the Atari went away)")
            return
        self.note(200, got, n, " in %.1f s" % (time.monotonic() - t0))

    def wait_turn(self):
        """the converter's turn (one conversion at a time): while another
        Atari's runs, "0/1000" every few seconds, so this one knows it is
        waiting and not lost; False if it went away meanwhile"""
        while not Config.jobs.acquire(timeout=QUEUED_SAY):
            try:
                self.wfile.write(b"HTTP/1.1 102 Processing\r\nX-Progress: 0/1000\r\n\r\n")
                self.wfile.flush()
            except OSError:
                return False
        return True

    def run(self, args, src, dst, t0, tr):
        """aerapdf on src: (200, None), or (status, "pdfgate: why"); tr gets
        the translation's language ("from") and whether it was all in it
        ("same")"""
        cmd = [Config.aerapdf, "--progress", "--fonts", Config.fonts] + args + [src, dst]
        env = None
        if "--translate" in args:
            to = args[args.index("--translate") + 1]
            # translateclient's: the language last seen, and the time
            # translating ends, so the conversion ends in its own time
            noted = os.path.join(os.path.dirname(os.path.dirname(src)), "language")
            last = min(LAST_SECONDS, Config.seconds // 4)
            env = dict(os.environ, TRANSLATECLIENT_NOTE=noted,
                       TRANSLATECLIENT_UNTIL="%.0f" % (time.time() + Config.seconds - last))
        try:
            p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, cwd=os.path.dirname(src), env=env)
        except OSError:
            return 503, "pdfgate: no aerapdf\n"
        try:
            limits(p.pid)
        except OSError:
            p.kill()
            p.wait()
            return 503, "pdfgate: no limits for aerapdf\n"
        timer = threading.Timer(Config.seconds, p.kill)
        timer.start()
        last, said = 0.0, ""
        try:
            for line in p.stderr:
                line = line.decode("utf-8", "replace").strip()
                m = re.fullmatch(r"progress (\d+) (\d+)", line)
                if m:
                    now = time.monotonic()
                    if now - last >= 0.5:
                        last = now
                        self.wfile.write(("HTTP/1.1 102 Processing\r\nX-Progress: %s/%s\r\n\r\n" %
                                          m.groups()).encode())
                        self.wfile.flush()
                elif env and re.fullmatch(r"(from|same) [a-z]{2,8}", line):
                    k, v = line.split()
                    tr["from"] = to if k == "same" else v
                    tr["same"] = k == "same"
                elif line:
                    said = line
            p.wait()
        except OSError:
            p.kill()
            p.wait()
            return 499, "pdfgate: the Atari went away\n"
        finally:
            timer.cancel()
        if p.returncode == 0 and os.path.exists(dst):
            return 200, None
        if time.monotonic() - t0 >= Config.seconds or p.returncode in (-9, -24):
            return 504, "pdfgate: the PDF took over %d s\n" % Config.seconds
        if env and p.returncode == 2:
            try:
                with open(noted) as f:
                    lang = f.read().strip()
            except OSError:
                lang = ""
            if re.fullmatch(r"[a-z]{2,3}", lang):
                return 422, "pdfgate: no translation from %s into %s\n" % (lang, to)
            return 422, "pdfgate: no translation into %s\n" % to
        if env and p.returncode == 3:
            return 503, "pdfgate: the translation service is busy\n"
        if env and p.returncode == 4:
            return 504, "pdfgate: the translation took too long\n"
        # aerapdf's last line, its own words, without the Pi's paths
        said = said.replace(src, "the PDF").replace(dst, "the result")
        said = re.sub(r"^econvert: ", "", said)[:200] or "it couldn't be converted"
        return 422, "pdfgate: %s\n" % said


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="PDFs converted for EConvert and EPDF on the Atari")
    ap.add_argument("--port", type=int, default=8380)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this Atari's address may use the gateway (may repeat)")
    ap.add_argument("--aerapdf", default=Config.aerapdf, help="the converter")
    ap.add_argument("--fonts", default=Config.fonts, help="the standard fonts' files (Liberation*.ttf)")
    ap.add_argument("--source", default=Config.source, help="the file naming the EPDF commit aerapdf is from")
    ap.add_argument("--max-mb", type=int, default=64, help="the biggest PDF taken")
    ap.add_argument("--time", type=int, default=600, help="seconds a conversion may take")
    ap.add_argument("--memory-mb", type=int, default=768, help="the memory a conversion may take")
    ap.add_argument("--jobs", type=int, default=1, help="conversions at once")
    ap.add_argument("--tmp", help="where the PDFs are kept while they are converted")
    ap.add_argument("--translator", default=Config.translator, help="aerapdf's translator (translateclient)")
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    Config.allow = a.allow
    Config.aerapdf, Config.fonts, Config.source = (os.path.abspath(x) for x in (a.aerapdf, a.fonts, a.source))
    Config.max_bytes = a.max_mb << 20
    Config.seconds = a.time
    Config.memory = a.memory_mb << 20
    Config.jobs = threading.BoundedSemaphore(max(1, a.jobs))
    Config.tmp = a.tmp
    Config.translator = os.path.abspath(a.translator)
    Config.verbose = a.verbose
    srv = ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    aeragate.say("pdfgate %s listening on %s:%d" % (VERSION, a.bind, a.port))
    w = which()
    aeragate.say("pdfgate: converting with %s" % w if w else "pdfgate: no converter at %s" % Config.aerapdf)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
