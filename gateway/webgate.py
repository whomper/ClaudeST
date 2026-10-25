#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""webgate - the Raspberry Pi gateway for EWeb on the Atari.

EWeb on a Falcon makes its own secure connections (TLS 1.2, the curves on
the DSP); the ST edition, EWEBST.PRG, can't, and some sites take TLS 1.3
only. So EWeb, when its gateway is set, sends every request to the Pi in
plain HTTP on your home network, in proxy form:

    GET https://en.wikipedia.org/wiki/Atari_ST HTTP/1.1

and webgate makes the request, checks the server's certificate (TLS 1.2
or 1.3) and hands the answer back as it comes:

  - the server's own compression (gzip, deflate) is passed through as it
    is, and only those are asked for: less to carry over a serial line,
    and EWeb unpacks them;
  - a long answer is passed on a piece at a time (chunked when its length
    isn't known), so a download needn't fit in the Pi's memory either;
  - redirects, cookies and error pages go back as they came: EWeb follows
    them and keeps the cookies itself;
  - X-Gateway-TLS says how the Pi reached the server ("TLS 1.3,
    TLS_AES_128_GCM_SHA256"), for EWeb's info line;
  - one connection per server is kept open and reused;
  - a picture asked for with "X-Gateway-Picture: fit=WIDTH" (EWeb's
    width for the page, in pixels) is decoded here when it is wider than
    that or of a kind the Atari can't read (WebP, AVIF, TIFF, BMP...), and
    sent as a baseline JPEG no wider (a PNG of 256 colours when it has
    transparency or was a GIF or a palette PNG), its own size written in
    it ("webgate natural WxH": a JPEG's comment, a PNG's tEXt) so that the
    page is laid out as with the original. A megabyte of photo becomes a
    few dozen kilobytes, and the Atari decodes it at its size. Needs
    Pillow (python3-pil); without it pictures pass as they came.
  - a picture asked for with pens ("X-Gateway-Picture: fit=608;
    size=400x300; pens=16; pal=CRC; ed=lj") comes as the Atari's screen
    shows it: the screen's own pens, made here by aerapens (AERA-Atari's
    common/tools/pens: the Atari's own decoder, scaler and dither, so the
    same pens to the bit; ed=lj as EWEB.PRG decodes, tj as EWEBST.PRG),
    as application/x-aera-pens. The colours are EWeb's, sent once to
    http://webgate/palette (2 to 256 of them, R G B bytes; webgate keeps
    the last 16 by their CRC-32); a request naming colours webgate hasn't
    got is answered 409 with "X-Gateway-Palette: need" before anything is
    fetched. A kind the Atari can't read, or a photo its EXIF says is
    turned, is made a PNG first (upright, nothing lost) and then pens.
    aerapens answers on a socket (--pens-socket, /run/webgate-pens.sock:
    systemd starts one a connection, as its own user, with little memory
    and time); X-Gateway-Pens names it ("aerapens COMMON BUILD"), and the
    log says which once. Without it, or when it fails or is too slow, the
    picture comes as above.
  - a page's pictures made ahead: EWeb POSTs http://webgate/prefetch, a
    line a picture, "URL<TAB>X-Gateway-Picture<TAB>Cookie" (the cookie
    may be left out), the window's first, after "Referer: " and "Accept: "
    lines for all of them (webgate answers 204 and X-Gateway-Prefetch:
    the number taken; 409 and "X-Gateway-Palette: need" when one names
    colours it hasn't got). webgate fetches and makes them four at a time
    (Prefetch), each as the request will ask for it, and answers that
    request (the address in proxy form and its X-Gateway-Picture alike,
    no If-None-Match or If-Modified-Since) with what was made, once,
    waiting for it if it is being made: a page's pictures come in about
    the time of the slowest four, not one after another. What isn't asked
    for in three minutes goes, 48 MB at most are kept, and the Atari's
    next list drops what of its last hasn't begun.

Snapshots (--browser): a GET with "X-Gateway-Snapshot: 1" is opened in an
ordinary Chromium on the Pi (its own profile: your logins stay there), and
once the page has settled after its scripts ran, its extension
(webgate-ext/) sends the page back as plain HTML: no scripts, each canvas
as a picture, the fields as filled, a <base> so EWeb fetches the
stylesheets and pictures through webgate as usual. X-Gateway-Snapshot-URL
says where the browser ended up. The extension talks to webgate on
127.0.0.1:8281 only. One page at a time: the browser has one tab. The
proxy's rules hold for the browser too: webgate gives it only http and
https addresses outside the home network (unless --allow-local), and the
extension blocks every request into the home network that its address
shows (addresses, single names, .local and the like), so that a link
followed in live view can't lead there either.

Cloudflare's check (--browser): a site behind Cloudflare may answer a
browser it doesn't know with its check, "Just a moment...", a script
for the big browsers that must pass before the site lets it in, which
EWeb can't run (a 403 or 503 with "cf-mitigated: challenge"). webgate
then opens the address (a page's: its Accept names text/html) in the
Pi's browser, which passes the check by itself (webgate-ext waits for
the page it goes on to), and sends EWeb that page as a snapshot. The
site stays on the browser for the rest of the visit: its next pages go
to the browser straight away, until none has been asked for in 25
minutes. Nothing of the browser's pass leaves it (no cookie, no
User-Agent: EWeb's design rules that out), so a picture the check
refuses stays refused. A check that wants a click comes to EWeb as it
was, with "X-Gateway-Check: click" (live view, below, ticks it); one
the browser didn't get through in time, with "X-Gateway-Check:
failed".

Live view (--browser), for what a snapshot can't do (a CAPTCHA, a login
made by scripts): EWeb opens http://webgate/live (webgate keeps the name
for itself), a page with a picture of the browser's screen as a picture
button, a field to type into and buttons for keys. A click on the picture
is a real click on the Pi's screen (xdotool, in the browser's X display:
the browser runs full screen there), typing real typing; then the page
comes again with a new picture. http://webgate/live?url=ADDRESS opens
ADDRESS first; "As a page" ends with a snapshot of what is shown.

A request that can't be made is answered 502 (403 for the home network,
504 for a server that is too slow) with a line starting "webgate:", which
EWeb shows on its "can't open" page.

Only standard Python 3 is needed, and aeragate.py next to it.
`aera-gateway install web` sets it up as a service.

    webgate.py [--port 8280] [--allow 192.168.1.50] [--allow-local] [--cafile F] [--pens-socket P] [-v]
"""

import argparse
import gzip
import html
import http.client
import io
import json
import os
import socket
import ssl
import subprocess
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

import aeragate

VERSION = "1.0"

HOP_BY_HOP = {
    "connection", "keep-alive", "proxy-connection", "proxy-authorization",
    "proxy-authenticate", "te", "trailer", "transfer-encoding", "upgrade",
    "host", "content-length", "accept-encoding",
}

PIECE = 65536
PICTURE_MAX = 40 << 20  # the most of a picture's file webgate takes in to make smaller
PICTURE_PIXELS = 40_000_000     # and the most pixels it unpacks: a few kilobytes can claim gigabytes
PALETTES_KEPT = 16              # the Ataris' colours kept for pens, the oldest dropped
PENS_TIME = 6                   # seconds aerapens may take (its unit stops it after 5)
PENS_ANSWER_MAX = 6 << 20       # the most of its answer read: 4 megapixels of pens and a mask
PENS_SEEN = [None]              # the aerapens last logged ("aerapens COMMON BUILD")
CHECK_MAX = 512 << 10           # the most of a 403 or 503 from Cloudflare read to see if it is its check
VISIT_KEEP = 25 * 60            # seconds a site behind the check stays on the browser after its last page
VISITS = {}                     # host -> when its last page went through the browser
VISITS_LOCK = threading.Lock()
PREFETCH_AT_ONCE = 4            # a page's pictures made ahead at a time
PREFETCH_KEEP = 180             # seconds one is kept for the Atari to ask for
PREFETCH_BYTES = 48 << 20       # what they may take in all
PREFETCH_ITEMS = 100            # the most of a list
PREFETCH_PLAIN_MAX = 1 << 20    # the most of an answer that isn't a picture made that is kept


def visiting(host, keep=None):
    """is host's visit on the browser (its check passed there)? keep:
    True to start or go on with it, False to end it"""
    host = host.lower()
    with VISITS_LOCK:
        if keep is True:
            VISITS[host] = time.time()
        elif keep is False:
            VISITS.pop(host, None)
        when = VISITS.get(host)
        if when is not None and time.time() - when > VISIT_KEEP:
            del VISITS[host]
            when = None
    return when is not None


def cf_check(resp, body):
    """is this answer Cloudflare's check (its managed challenge)? It says
    so (cf-mitigated: challenge); an older one only by the check's
    options in its page"""
    if (resp.getheader("cf-mitigated") or "").strip().lower() == "challenge":
        return True
    if resp.status not in (403, 503):
        return False
    if (resp.getheader("Content-Encoding") or "").lower() == "gzip":
        try:
            body = gzip.decompress(body)
        except (OSError, EOFError, zlib.error):
            return False
    return b"_cf_chl_opt" in body


class Palettes:
    """the colours EWeb sent (http://webgate/palette), by their CRC-32"""

    def __init__(self):
        self.lock = threading.Lock()
        self.kept = {}          # crc -> R G B bytes, oldest first

    def put(self, rgb):
        crc = zlib.crc32(rgb) & 0xFFFFFFFF
        with self.lock:
            self.kept.pop(crc, None)
            self.kept[crc] = rgb
            while len(self.kept) > PALETTES_KEPT:
                del self.kept[next(iter(self.kept))]
        return crc

    def get(self, crc):
        with self.lock:
            return self.kept.get(crc)


PALETTES = Palettes()


def pens_input(data):
    """data as aerapens takes it: a JPEG, PNG or GIF as it came; another
    kind Pillow reads, or a photo its EXIF says is turned, as a PNG
    (upright, nothing lost); None when it can't be had"""
    try:
        from PIL import Image, ImageOps
    except ImportError:
        return data
    try:
        im = Image.open(io.BytesIO(data))
        fmt, (w, h) = im.format, im.size
        if w * h > PICTURE_PIXELS:
            return None
        turned = fmt in ("JPEG", "WEBP", "TIFF", "MPO") and \
            (im.getexif() or {}).get(0x0112, 1) in (2, 3, 4, 5, 6, 7, 8)
        if fmt in ("JPEG", "PNG", "GIF") and not turned:
            return data
        im = ImageOps.exif_transpose(im)
        alpha = im.mode in ("RGBA", "LA", "PA") or "transparency" in im.info
        out = io.BytesIO()
        im.convert("RGBA" if alpha else "RGB").save(out, "PNG", compress_level=1)
        return out.getvalue()
    except Exception:       # Pillow's errors for what isn't a picture it reads are many kinds
        return data


def pens_ask(request):
    """aerapens' answer to request, over its socket (Config.pens_socket:
    systemd starts one aerapens a connection, with little memory and
    time), or None (no socket, an error, too slow, too long)"""
    path = Config.pens_socket
    if not path or not os.path.exists(path):
        return None
    deadline = time.monotonic() + PENS_TIME
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(PENS_TIME)
            s.connect(path)
            s.sendall(request)
            s.shutdown(socket.SHUT_WR)
            parts, size = [], 0
            while True:
                left = deadline - time.monotonic()
                if left <= 0:
                    return None
                s.settimeout(left)
                b = s.recv(65536)
                if not b:
                    break
                parts.append(b)
                size += len(b)
                if size > PENS_ANSWER_MAX:
                    return None
            return b"".join(parts)
    except OSError:
        return None


def pens_request(ed, n, w, h, rgb=b"", data=b""):
    """aerapens' request (AERA-Atari, common/tools/pens/aerapens.c): ed
    "l" (EWEB.PRG's decoders), "t" (EWEBST.PRG's) or "v" (which it is)"""
    return (b"AEPQ\1" + ed.encode() + bytes([n - 1 if n else 0, 0]) + w.to_bytes(2, "big") + h.to_bytes(2, "big") +
            len(data).to_bytes(4, "big") + rgb + data)


def pens_line(answer):
    """the answer's first line, "aerapens COMMON BUILD", and the rest; or None"""
    line, nl, rest = answer.partition(b"\n")
    if not nl or not line.startswith(b"aerapens ") or len(line) > 200:
        return None
    return line.decode("ascii", "replace"), rest


def pens_which():
    """which aerapens answers on the socket ("aerapens COMMON BUILD"), or None"""
    a = pens_ask(pens_request("v", 0, 1, 0))
    got = pens_line(a) if a else None
    return got[0] if got else None


def make_pens(data, want):
    """data (a picture's file) as the Atari's pens (aerapens), for want
    (picture_ask's): (bytes, what was done, "aerapens COMMON BUILD") or None"""
    rgb = PALETTES.get(want["pal"])
    if rgb is None or len(rgb) // 3 != want["pens"]:
        return None
    data = pens_input(data)
    if data is None:
        return None
    w, h = want["size"] or (want["fit"], 0)
    a = pens_ask(pens_request("l" if want["ed"] == "lj" else "t", want["pens"], w, h, rgb, data))
    got = pens_line(a) if a else None
    if got is None:
        return None
    which, out = got
    if len(out) < 20 or out[:4] != b"EWPN":
        return None
    pw, ph = int.from_bytes(out[8:10], "big"), int.from_bytes(out[10:12], "big")
    depth, mask = out[5], out[6] & 1
    if depth not in (1, 2, 4, 8) or len(out) != 20 + ph * ((pw * depth + 7) // 8 + (mask and (pw + 7) // 8)):
        return None
    if which != PENS_SEEN[0]:
        PENS_SEEN[0] = which
        aeragate.say("webgate: pictures as pens by %s" % which)
    return out, "pens %dx%d, %d bits%s, natural %dx%d" % (
        pw, ph, depth, ", a mask" if mask else "", int.from_bytes(out[12:14], "big"),
        int.from_bytes(out[14:16], "big")), which


def make_picture(data, fit):
    """data (a picture's file) made for a page fit pixels wide: (bytes,
    content type, what was done) or None to send it as it came (no Pillow,
    not a picture, too many pixels, or a JPEG, PNG or GIF the page can show
    as it is)"""
    try:
        from PIL import Image, ImageOps, PngImagePlugin
    except ImportError:
        return None
    try:
        im = Image.open(io.BytesIO(data))
        fmt, (w, h) = im.format, im.size
        if fmt in ("JPEG", "PNG", "GIF") and w <= fit or w * h > PICTURE_PIXELS:
            return None
        turned = fmt in ("JPEG", "WEBP", "TIFF", "MPO") and \
            (im.getexif() or {}).get(0x0112, 1) in (5, 6, 7, 8)
        nw, nh = (h, w) if turned else (w, h)       # as browsers show it, upright
        if fmt in ("JPEG", "MPO") and nw > fit:
            im.draft("RGB", (fit, fit) if turned else (fit, max(1, h * fit // w)))
        im = ImageOps.exif_transpose(im)
        flat = fmt == "GIF" or im.mode in ("P", "1", "L", "LA", "PA")
        alpha = im.mode in ("RGBA", "LA", "PA") or "transparency" in im.info
        im = im.convert("RGBA" if alpha else "RGB")
        if nw > fit:
            im = im.resize((fit, max(1, round(nh * fit / nw))), Image.LANCZOS)
        out = io.BytesIO()
        mark = "natural %dx%d" % (nw, nh)
        if alpha or flat:
            info = PngImagePlugin.PngInfo()
            info.add_text("webgate", mark)
            im.quantize(256, method=Image.FASTOCTREE if alpha else Image.MEDIANCUT).save(
                out, "PNG", optimize=True, pnginfo=info)
            body, ctype = out.getvalue(), "image/png"
        else:
            im.save(out, "JPEG", quality=80, optimize=True, progressive=False)
            jpg = out.getvalue()
            com = b"webgate " + mark.encode()
            # the comment first, after SOI (Pillow before 9.4 can't write one)
            body = jpg[:2] + b"\xff\xfe" + (len(com) + 2).to_bytes(2, "big") + com + jpg[2:]
            ctype = "image/jpeg"
        return body, ctype, "%s %dx%d to %s %dx%d" % (fmt, nw, nh, ctype[6:].upper(), im.size[0], im.size[1])
    except Exception:       # Pillow's errors for what isn't a picture it reads are many kinds
        return None


class Config:
    allow = []              # client addresses allowed; empty = any on the private networks
    allow_local = False     # may requests go to servers on the local network?
    verbose = False
    timeout = 60
    cafile = None           # the root certificates to check servers against (None: the Pi's)
    browser = False         # snapshots: the Pi's Chromium and webgate-ext are there
    browser_port = 8281     # where webgate-ext asks (127.0.0.1 only)
    snapshot_wait = 60      # seconds a page may take to settle
    live_width = 608        # live view's picture, pixels wide (EWeb's page on a 640-pixel screen)
    live_quality = 0.7      # its JPEG quality: small for STinG, enough to read
    live_settle = 1.0       # seconds the browser has after a click before the next picture
    insecure = False        # tests only: don't check certificates
    pens_socket = "/run/webgate-pens.sock"  # aerapens' socket (systemd's); not there: no pens


def _dial(address, timeout=None, source_address=None):
    """a connection to the address that was checked: without --allow-local
    never one on the home network, even if the name's answer changes"""
    return aeragate.connect(address[0], address[1], timeout, Config.allow_local)


def _proxy(scheme):
    """(host, port) of an upstream proxy from HTTPS_PROXY, for a Pi on a
    network that only goes out through one; None normally."""
    v = os.environ.get(scheme.upper() + "_PROXY") or os.environ.get(scheme + "_proxy")
    if not v:
        return None
    u = urlsplit(v if "://" in v else "http://" + v)
    return (u.hostname, u.port or 8080) if u.hostname else None


class Upstream(threading.local):
    """One kept-open connection per server, per thread (an Atari's
    connection is served by one thread)."""

    def __init__(self):
        self.conns = {}

    def get(self, scheme, host, port):
        key = (scheme, host, port)
        c = self.conns.get(key)
        if c is None:
            if scheme == "https":
                ctx = aeragate.tls_context(Config.cafile)
                if Config.insecure:
                    ctx.check_hostname = False
                    ctx.verify_mode = ssl.CERT_NONE
                proxy = _proxy("https")
                if proxy:
                    c = http.client.HTTPSConnection(proxy[0], proxy[1], timeout=Config.timeout, context=ctx)
                    c.set_tunnel(host, port)
                else:
                    c = http.client.HTTPSConnection(host, port, timeout=Config.timeout, context=ctx)
                    c._create_connection = _dial
            else:
                c = http.client.HTTPConnection(host, port, timeout=Config.timeout)
                c._create_connection = _dial
            self.conns[key] = c
        return c

    def drop(self, scheme, host, port):
        c = self.conns.pop((scheme, host, port), None)
        if c:
            c.close()


UP = Upstream()


class Bridge:
    """webgate-ext's side: the address the browser is to open next, and
    the snapshot it sends back. One page at a time."""

    def __init__(self):
        self.turn = threading.Lock()        # one Atari's page at a time
        self.cv = threading.Condition()
        self.wanted = None                  # {"id", "url"} not yet taken by the extension
        self.done = {}                      # id -> snapshot
        self.asked = None                   # the id being waited for
        self.n = 0

    def ask(self, url, wait, **more):
        """url opened in the browser: its snapshot ({"url", "title",
        "html"}), or None. more: shot=WIDTH for a picture of the screen
        ({"pic", "w"}), snap=1 for a snapshot of the page shown now."""
        with self.turn, self.cv:
            self.n += 1
            me = self.n
            self.wanted = dict({"id": str(me), "url": url}, **more)
            self.asked = str(me)
            self.cv.notify_all()
            ok = self.cv.wait_for(lambda: str(me) in self.done, wait)
            if self.wanted and self.wanted["id"] == str(me):
                self.wanted = None
            self.asked = None
            return self.done.pop(str(me), None) if ok else None

    def next(self, wait=25):
        """for the extension: the next address, or None after wait seconds"""
        with self.cv:
            self.cv.wait_for(lambda: self.wanted is not None, wait)
            w, self.wanted = self.wanted, None
            return w

    def give(self, id, snap):
        """what the extension sent; one that came too late is dropped"""
        with self.cv:
            if id == self.asked:
                self.done[id] = snap
                self.cv.notify_all()


BRIDGE = Bridge()


class Answer:
    """an answer to send the Atari as it is: status, reason, header
    (name, value) pairs without Content-Length, the body, a note for the
    log"""

    def __init__(self, status, reason, headers, data, note=""):
        self.status, self.reason, self.headers, self.data, self.note = status, reason, headers, data, note


def picture_answer(resp, conn, u, port, want):
    """a picture that came (resp, 200, not packed) as the Atari's pens
    (make_pens), else as the page can show it (make_picture), or as it
    came: an Answer, or why not (a str for a 502)"""
    try:
        data = resp.read(PICTURE_MAX + 1)
    except (OSError, http.client.HTTPException):
        UP.drop(u.scheme, u.hostname, port)
        return "webgate: %s closed the connection\n" % u.hostname
    if len(data) > PICTURE_MAX:
        UP.drop(u.scheme, u.hostname, port)
        return "webgate: the picture from %s is too big\n" % u.hostname
    if resp.will_close:
        UP.drop(u.scheme, u.hostname, port)
    pens = make_pens(data, want) if want["pens"] else None
    made = (pens[0], "application/x-aera-pens", pens[1]) if pens else make_picture(data, want["fit"])
    skip = HOP_BY_HOP | ({"content-type", "content-length"} if made else set())
    headers = [(k, v) for k, v in resp.getheaders() if k.lower() not in skip]
    if made:
        data = made[0]
        headers += [("Content-Type", made[1]), ("X-Gateway-Picture", made[2])]
    if pens:
        headers.append(("X-Gateway-Pens", pens[2]))    # which aerapens made them, for tracing
    tls = tls_of(conn)
    if tls:
        headers.append(("X-Gateway-TLS", tls))
    return Answer(resp.status, resp.reason, headers, data, " (%s)" % made[2] if made else "")


def picture_want(value):
    """X-Gateway-Picture: fit=WIDTH (0: none), and for pens size=WxH
    (None: fit wide at most), pens=N, pal=CRC (8 hex digits), ed=lj or
    tj: a dict; pens 0 when they weren't asked for, or not right"""
    want = {"fit": 0, "size": None, "pens": 0, "pal": None, "ed": "lj"}
    for part in (value or "").split(";"):
        k, _, v = part.strip().partition("=")
        if k == "fit" and v.isdigit() and 16 <= int(v) <= 4096:
            want["fit"] = int(v)
        elif k == "size":
            w, _, h = v.partition("x")
            if w.isdigit() and h.isdigit() and 1 <= int(w) <= 4096 and 1 <= int(h) <= 4096:
                want["size"] = (int(w), int(h))
        elif k == "pens" and v.isdigit() and 2 <= int(v) <= 256:
            want["pens"] = int(v)
        elif k == "pal" and len(v) == 8 and all(c in "0123456789abcdefABCDEF" for c in v):
            want["pal"] = int(v, 16)
        elif k == "ed" and v in ("lj", "tj"):
            want["ed"] = v
    if not want["fit"] or want["pal"] is None:
        want["pens"] = 0
    return want


class Prefetch:
    """A page's pictures made ahead (POST http://webgate/prefetch): EWeb
    names them as it will ask for them, the window's first, and they are
    fetched and made PREFETCH_AT_ONCE at a time, so that each of its
    requests is answered at once instead of in turn. Kept by (the Atari,
    the address, its X-Gateway-Picture) until asked for, PREFETCH_KEEP
    seconds at most, PREFETCH_BYTES in all; an Atari's next list drops
    what of its last hasn't begun."""

    def __init__(self):
        self.lock = threading.Lock()
        self.jobs = {}          # key -> {"done": Event, "answer": Answer or None, "t", "size", "how"}
        self.queue = []         # keys waiting for a worker, in order
        self.workers = 0

    def _trim(self):
        now = time.time()
        for k in [k for k, j in self.jobs.items() if now - j["t"] > PREFETCH_KEEP and j["done"].is_set()]:
            del self.jobs[k]
        total = sum(j["size"] for j in self.jobs.values())
        for k in sorted((k for k, j in self.jobs.items() if j["done"].is_set()), key=lambda k: self.jobs[k]["t"]):
            if total <= PREFETCH_BYTES:
                break
            total -= self.jobs[k]["size"]
            del self.jobs[k]

    def add(self, client, items, headers):
        """items: (url, X-Gateway-Picture, cookie) in the order to make
        them; headers: what every request carries (User-Agent, Referer,
        Accept...). The number queued"""
        n = 0
        with self.lock:
            # the last list's pictures not begun: not wanted now
            for k in [k for k in self.queue if k[0] == client]:
                self.queue.remove(k)
                self.jobs.pop(k, None)
            self._trim()
            for url, pic, cookie in items:
                key = (client, url, pic)
                if key in self.jobs:
                    continue
                self.jobs[key] = {"done": threading.Event(), "answer": None, "t": time.time(), "size": 0,
                                  "how": (url, pic, cookie, headers)}
                self.queue.append(key)
                n += 1
            while self.workers < min(PREFETCH_AT_ONCE, len(self.queue)):
                self.workers += 1
                threading.Thread(target=self._work, daemon=True).start()
        return n

    def _work(self):
        while True:
            with self.lock:
                if not self.queue:
                    self.workers -= 1
                    return
                key = self.queue.pop(0)
                job = self.jobs.get(key)
            if job is None:
                continue
            try:
                a = make_ahead(*job["how"])
            except Exception:       # (anything: the Atari's own request then fetches it)
                a = None
            if Config.verbose:      # who connected where (7.3), the host only, as note() says it
                try:
                    u = urlsplit(job["how"][0])
                    where = "%s://%s:%d" % (u.scheme, u.hostname, u.port or (443 if u.scheme == "https" else 80))
                except ValueError:  # (a port that isn't one: make_ahead fetched nothing)
                    where = "-"
                aeragate.say("%s GET %s -> %s (fetched ahead)" % (
                    key[0], where, "%d, %d bytes" % (a.status, len(a.data)) if a else "not made"))
            with self.lock:
                job["answer"] = a
                job["size"] = len(a.data) if a else 0
                job["t"] = time.time()
                job["done"].set()
                self._trim()

    def take(self, client, url, pic):
        """the answer made ahead for this request (waited for while it is
        being made), gone from the store; None: there is none"""
        key = (client, url, pic)
        with self.lock:
            job = self.jobs.get(key)
            if job is None:
                return None
            if key in self.queue:
                # not begun: made by this request itself, at once
                self.queue.remove(key)
                del self.jobs[key]
                return None
        if not job["done"].wait(Config.timeout + PENS_TIME):
            return None
        with self.lock:
            self.jobs.pop(key, None)
        return job["answer"]


PREFETCH = Prefetch()


def make_ahead(url, pic, cookie, headers):
    """a picture fetched and made as the Atari would ask for it: an Answer
    (whatever the server said, a picture made, an error page), or None
    when it couldn't be fetched (the Atari's own request tries then)"""
    u = urlsplit(url)
    if u.scheme not in ("http", "https") or not u.hostname:
        return None
    port = u.port or (443 if u.scheme == "https" else 80)
    path = (u.path or "/") + ("?" + u.query if u.query else "")
    want = picture_want(pic)
    if want["pens"] and PALETTES.get(want["pal"]) is None:
        return None
    h = dict(headers)
    h["Host"] = u.netloc.rsplit("@", 1)[-1]
    h["Accept-Encoding"] = "identity"
    if cookie:
        h["Cookie"] = cookie
    for attempt in (1, 2):
        conn = UP.get(u.scheme, u.hostname, port)
        try:
            conn.request("GET", path, headers=h)
            resp = conn.getresponse()
            break
        except (http.client.RemoteDisconnected, BrokenPipeError, ConnectionResetError,
                http.client.CannotSendRequest, http.client.ResponseNotReady):
            UP.drop(u.scheme, u.hostname, port)
            if attempt == 2:
                return None
        except (OSError, http.client.HTTPException, aeragate.HomeNetwork):
            UP.drop(u.scheme, u.hostname, port)
            return None
    if want["fit"] and resp.status == 200 and \
            (resp.getheader("Content-Type") or "").lower().startswith("image/") and \
            (resp.getheader("Content-Encoding") or "identity").lower() == "identity":
        a = picture_answer(resp, conn, u, port, want)
        return a if isinstance(a, Answer) else None
    # anything else (a redirect, an error, a type left as it is): kept as
    # it came when it is small, else left to the Atari's own request
    try:
        data = resp.read(PREFETCH_PLAIN_MAX + 1)
    except (OSError, http.client.HTTPException):
        UP.drop(u.scheme, u.hostname, port)
        return None
    if resp.will_close:
        UP.drop(u.scheme, u.hostname, port)
    if len(data) > PREFETCH_PLAIN_MAX or (resp.getheader("Transfer-Encoding") or "").lower() not in ("", "identity"):
        UP.drop(u.scheme, u.hostname, port)
        return None
    headers = [(k, v) for k, v in resp.getheaders() if k.lower() not in HOP_BY_HOP]
    tls = tls_of(conn)
    if tls:
        headers.append(("X-Gateway-TLS", tls))
    return Answer(resp.status, resp.reason, headers, data)


def tls_of(conn):
    """how conn reached its server: "TLS 1.3, TLS_AES_128_GCM_SHA256", or
    "" for plain HTTP"""
    s = getattr(conn, "sock", None)
    if not isinstance(s, ssl.SSLSocket):
        return ""
    v = (s.version() or "TLS").replace("TLSv", "TLS ")
    c = s.cipher()
    return "%s, %s" % (v, c[0]) if c else v


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "webgate/" + VERSION

    where = "-"             # the server a request goes to, for the log
    for_browser = False     # ExtHandler: the Pi's own browser, not an Atari

    def log_message(self, fmt, *args):
        if Config.verbose:
            aeragate.say("%s %s" % (self.address_string(), fmt % args))

    def log_request(self, code="-", size="-"):
        # the request line has the whole address, with what a form sent
        # in it: note() logs the server only (7.3)
        pass

    def note(self, status, size, more=""):
        if Config.verbose:
            aeragate.say("%s %s %s -> %d, %d bytes%s" % (
                self.address_string(), self.command, self.where, status, size, more))

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError, socket.timeout):
            pass        # the Atari went away (reset, switched off, Esc): nothing to say

    def do_GET(self):
        self.proxy()

    do_HEAD = do_POST = do_GET

    def reply(self, status, text, ctype="text/plain; charset=utf-8"):
        body = text.encode("utf-8")
        self.note(status, len(body), ": " + text.strip() if status >= 400 else "")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def proxy(self):
        self.where = "-"
        if not (_local(self.client_address[0]) if self.for_browser else
                aeragate.client_ok(self.client_address[0], Config.allow)):
            self.reply(403, "webgate: this address may not use the gateway\n")
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        if self.path.startswith("/"):
            self.reply(200, "webgate %s is running. Put this address in EWeb's gateway setting.\n" % VERSION)
            return
        u = urlsplit(self.path)
        if not self.for_browser and u.hostname == "webgate" and u.path.startswith("/live"):
            self.live(u)
            return
        if not self.for_browser and u.hostname == "webgate" and u.path == "/palette":
            self.palette(body)
            return
        if not self.for_browser and u.hostname == "webgate" and u.path == "/prefetch":
            self.prefetch(body)
            return
        if u.scheme not in ("http", "https") or not u.hostname:
            self.reply(400, "webgate: expected a request like GET https://host/path\n")
            return
        port = u.port or (443 if u.scheme == "https" else 80)
        self.where = "%s://%s:%d" % (u.scheme, u.hostname, port)
        path = (u.path or "/") + ("?" + u.query if u.query else "")
        if not self.for_browser and self.headers.get("X-Gateway-Snapshot") == "1" and self.command == "GET":
            self.snapshot(u)
            return
        want = self.picture_ask()
        if want["pens"] and PALETTES.get(want["pal"]) is None:
            # colours webgate hasn't got: EWeb sends them, then asks again
            self.send_body(409, b"webgate: send the colours first\n", "text/plain", [("X-Gateway-Palette", "need")])
            self.note(409, 0, " (no colours %08x)" % want["pal"])
            return
        if want["fit"] and self.command in ("GET", "HEAD") and not self.for_browser and \
                not self.headers.get("If-None-Match") and not self.headers.get("If-Modified-Since"):
            a = PREFETCH.take(self.client_address[0], self.path, self.headers.get("X-Gateway-Picture") or "")
            if a is not None:
                self.send_answer(a, " (made ahead)")
                return
        page = self.command == "GET" and Config.browser and not self.for_browser and \
            "text/html" in (self.headers.get("Accept") or "")
        if page and visiting(u.hostname) and self.browser_may_open(self.path, quiet=True):
            # a site whose check the browser passed: its pages stay there
            # for the visit (the pass is the browser's, and stays in it)
            snap = BRIDGE.ask(self.path, Config.snapshot_wait)
            if snap is not None and not snap.get("challenge"):
                visiting(u.hostname, True)
                self.send_snap(snap, self.path, " (on the browser for the visit)")
                return
            visiting(u.hostname, False)     # (asked as usual: the check again, if it is that)
        headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_BY_HOP}
        headers["Host"] = u.netloc.rsplit("@", 1)[-1]
        # what the Atari unpacks itself, and nothing it can't
        asked = (self.headers.get("Accept-Encoding") or "").lower()
        headers["Accept-Encoding"] = ", ".join(e for e in ("gzip", "deflate") if e in asked) or "identity"
        for attempt in (1, 2):
            conn = UP.get(u.scheme, u.hostname, port)
            try:
                conn.request(self.command, path, body=body, headers=headers)
                resp = conn.getresponse()
                break
            except ssl.SSLCertVerificationError as e:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(502, "webgate: %s has an untrusted certificate (%s)\n" % (u.hostname, e.verify_message))
                return
            except aeragate.HomeNetwork:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(403, "webgate: won't connect to the local network (%s)\n" % u.hostname)
                return
            except socket.gaierror:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(502, "webgate: can't find %s\n" % u.hostname)
                return
            except (http.client.RemoteDisconnected, BrokenPipeError, ConnectionResetError,
                    http.client.CannotSendRequest, http.client.ResponseNotReady):
                UP.drop(u.scheme, u.hostname, port)    # a kept connection went stale
                if attempt == 2:
                    self.reply(502, "webgate: %s closed the connection\n" % u.hostname)
                    return
            except (OSError, http.client.HTTPException) as e:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(504 if isinstance(e, socket.timeout) else 502,
                           "webgate: can't reach %s (%s)\n" % (u.hostname, e))
                return
        if page and resp.status in (403, 503) and \
                ((resp.getheader("cf-mitigated") or "").strip().lower() == "challenge" or
                 (resp.getheader("Server") or "").strip().lower() == "cloudflare"):
            # (a page: a picture or a stylesheet the check refused isn't
            # one for the browser's tab)
            self.cf_answer(resp, conn, u, port)
        elif want["fit"] and self.command == "GET" and resp.status == 200 and \
                (resp.getheader("Content-Type") or "").lower().startswith("image/") and \
                (resp.getheader("Content-Encoding") or "identity").lower() == "identity":
            self.picture(resp, conn, u, port, want)
        else:
            self.pass_on(resp, conn, u, port)

    def picture_ask(self):
        """this request's X-Gateway-Picture (picture_want)"""
        return picture_want(self.headers.get("X-Gateway-Picture"))

    def prefetch(self, body):
        """a page's pictures to make ahead (Prefetch): the body's lines,
        "Name: value" for what each request carries (Referer, Accept),
        then one a picture, "URL<TAB>X-Gateway-Picture<TAB>Cookie" (the
        cookie may be left out), in the order EWeb will ask for them.
        204 with X-Gateway-Prefetch: the number taken; 409 with
        X-Gateway-Palette: need when one names colours webgate hasn't got"""
        if self.command != "POST" or body is None or len(body) > 256 << 10:
            self.reply(400, "webgate: the pictures to make ahead are a POST of lines\n")
            return
        headers = {k: v for k, v in self.headers.items()
                   if k.lower() in ("user-agent", "accept-language")}
        items = []
        for line in body.decode("utf-8", "replace").split("\n"):
            line = line.rstrip("\r")
            if "\t" not in line and ": " in line:
                k, _, v = line.partition(": ")
                if k.lower() in ("referer", "accept"):
                    headers[k] = v
                continue
            parts = line.split("\t")
            if len(parts) < 2 or len(items) >= PREFETCH_ITEMS:
                continue
            u = urlsplit(parts[0])
            if u.scheme not in ("http", "https") or not u.hostname:
                continue
            want = picture_want(parts[1])
            if not want["fit"]:
                continue
            if want["pens"] and PALETTES.get(want["pal"]) is None:
                self.send_body(409, b"webgate: send the colours first\n", "text/plain",
                               [("X-Gateway-Palette", "need")])
                self.note(409, 0, " (no colours %08x)" % want["pal"])
                return
            items.append((parts[0], parts[1], parts[2] if len(parts) > 2 else ""))
        n = PREFETCH.add(self.client_address[0], items, headers)
        self.send_body(204, b"", "text/plain", [("X-Gateway-Prefetch", str(n))])
        self.note(204, 0, " (%d pictures to make ahead)" % n)

    def palette(self, body):
        """EWeb's colours for pens: 2 to 256, R G B bytes each"""
        if self.command != "POST" or body is None or not 6 <= len(body) <= 768 or len(body) % 3:
            self.reply(400, "webgate: the colours are 2 to 256 of R G B bytes\n")
            return
        crc = PALETTES.put(bytes(body))
        self.send_body(204, b"", "text/plain", [("X-Gateway-Palette", "%08x" % crc)])
        self.note(204, 0, " (colours %08x, %d)" % (crc, len(body) // 3))

    def picture(self, resp, conn, u, port, want):
        """a picture as the Atari's pens (make_pens), else as the page can
        show it (make_picture), or as it came"""
        a = picture_answer(resp, conn, u, port, want)
        if isinstance(a, str):
            self.reply(502, a)
        else:
            self.send_answer(a)

    def send_answer(self, a, more=""):
        """an Answer (a picture made, or one made ahead: Prefetch)"""
        self.send_response(a.status, a.reason)
        for k, v in a.headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(a.data)))
        self.end_headers()
        try:
            if self.command != "HEAD":
                self.wfile.write(a.data)
        except OSError:
            self.close_connection = True
        self.note(a.status, len(a.data), a.note + more)

    # ------------------------------------------------------------ live view

    def send_body(self, status, body, ctype, extra=()):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def again(self):
        """after a click or keys: the live page again, with a new picture"""
        time.sleep(Config.live_settle)
        self.send_body(303, b"", "text/plain", [("Location", "http://webgate/live?n=%d" % int(time.time() * 10))])

    def xdo(self, *args):
        """real input on the browser's screen"""
        try:
            subprocess.run(["xdotool"] + list(args), timeout=10, check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        except (OSError, subprocess.SubprocessError):
            return False

    def live(self, u):
        if not Config.browser:
            self.reply(501, "webgate: live view needs the Pi's browser (webgate --browser)\n")
            return
        a = parse_qs(u.query, keep_blank_values=True)
        part = u.path[len("/live"):]
        if part == "/shot.jpg":
            r = BRIDGE.ask("", 20, shot=Config.live_width, quality=Config.live_quality)
            if not r or not r.get("pic"):
                why = (r or {}).get("why") or "it didn't answer"
                self.reply(504, "webgate: the browser's screen didn't come: %s\n" % why[:200])
                return
            LIVE["full"] = r.get("w") or Config.live_width
            self.send_body(200, r["pic"], "image/jpeg")
            return
        if part == "/click":
            try:
                x, y = int(a.get("at.x", ["0"])[0]), int(a.get("at.y", ["0"])[0])
            except ValueError:
                x = y = 0
            k = LIVE.get("full", Config.live_width) / float(Config.live_width)
            self.xdo("mousemove", str(int(x * k)), str(int(y * k)), "click", "1")
            self.again()
            return
        if part == "/type":
            t = a.get("t", [""])[0]
            if t:
                self.xdo("type", "--delay", "40", "--", t)
            if "enter" in a:
                self.xdo("key", "Return")
            self.again()
            return
        if part == "/key":
            k = a.get("k", [""])[0]
            if k in LIVE_KEYS:
                self.xdo("key", LIVE_KEYS[k])
            self.again()
            return
        if part == "/page":
            snap = BRIDGE.ask("", Config.snapshot_wait, snap=1)
            if snap is None:
                self.reply(504, "webgate: the browser's page didn't come\n")
                return
            self.send_snap(snap, snap.get("url") or self.path)
            return
        url = a.get("url", [""])[0]
        title = "the Pi's browser"
        if url and not self.browser_may_open(url):
            return
        if url:
            snap = BRIDGE.ask(url, Config.snapshot_wait)
            if snap:
                title = snap.get("title") or url
        n = int(time.time() * 10)
        page = LIVE_PAGE % {"title": html.escape(title), "n": n, "w": Config.live_width}
        self.send_body(200, page.encode("utf-8"), "text/html; charset=utf-8")

    def browser_may_open(self, url, quiet=False):
        """may the Pi's browser open url? The proxy's rules (7.3): http and
        https only, nowhere in the home network without --allow-local. When
        not, the Atari has been answered (quiet: not yet)."""
        u = urlsplit(url)
        if u.scheme not in ("http", "https") or not u.hostname:
            if not quiet:
                self.reply(400, "webgate: expected a request like GET https://host/path\n")
            return False
        if Config.allow_local:
            return True
        try:
            aeragate.lookup(u.hostname, u.port or (443 if u.scheme == "https" else 80), False)
        except aeragate.HomeNetwork:
            if not quiet:
                self.reply(403, "webgate: won't connect to the local network (%s)\n" % u.hostname)
            return False
        except (socket.gaierror, UnicodeError, ValueError):
            if not quiet:
                self.reply(502, "webgate: can't find %s\n" % u.hostname)
            return False
        return True

    def snapshot(self, u):
        """the page as the Pi's browser shows it, after its scripts ran"""
        if not Config.browser:
            self.reply(501, "webgate: snapshots need the Pi's browser (webgate --browser)\n")
            return
        if not self.browser_may_open(self.path):
            return
        snap = BRIDGE.ask(self.path, Config.snapshot_wait)
        if snap is None:
            self.reply(504, "webgate: the browser didn't finish %s in %d s\n" % (u.hostname, Config.snapshot_wait))
            return
        self.send_snap(snap, self.path)

    def send_snap(self, snap, url, why=" (a snapshot)"):
        """a snapshot to the Atari, with the address it came from (its
        <base href> says the same for the page's own links)"""
        body = (snap.get("html") or "").encode("utf-8")
        packed = "gzip" in (self.headers.get("Accept-Encoding") or "").lower()
        if packed:
            body = gzip.compress(body, 6)
        self.note(200, len(body), why)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        if packed:
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Gateway-Snapshot-URL", snap.get("url") or url)
        self.end_headers()
        self.wfile.write(body)

    def cf_answer(self, resp, conn, u, port):
        """a 403 or 503 from Cloudflare: when it is its check, the page
        through the Pi's browser, which passes it, and the site on the
        browser for the visit; else, or when the browser can't, the answer
        as it came"""
        try:
            first = resp.read(CHECK_MAX)
        except (OSError, http.client.HTTPException):
            UP.drop(u.scheme, u.hostname, port)
            self.reply(502, "webgate: %s closed the connection\n" % u.hostname)
            return
        check = ()
        if len(first) < CHECK_MAX and cf_check(resp, first) and self.browser_may_open(self.path, quiet=True):
            snap = BRIDGE.ask(self.path, Config.snapshot_wait)
            if snap is not None and not snap.get("challenge"):
                visiting(u.hostname, True)
                if resp.will_close:
                    UP.drop(u.scheme, u.hostname, port)
                self.send_snap(snap, self.path, " (Cloudflare's check, through the browser)")
                return
            check = (("X-Gateway-Check", "click" if snap is not None else "failed"),)
        self.pass_on(resp, conn, u, port, first, check)

    def pass_on(self, resp, conn, u, port, first=b"", extra=()):
        """the answer to the Atari as it comes, a piece at a time (first:
        the start of its body, read already; extra: more headers)"""
        clen = resp.getheader("Content-Length")
        known = clen is not None and clen.strip().isdigit() and resp.chunked is False
        empty = self.command == "HEAD" or resp.status in (204, 304) or 100 <= resp.status < 200
        self.send_response(resp.status, resp.reason)
        for k, v in resp.getheaders():
            if k.lower() not in HOP_BY_HOP:
                self.send_header(k, v)
        tls = tls_of(conn)
        if tls:
            self.send_header("X-Gateway-TLS", tls)
        for k, v in extra:
            self.send_header(k, v)
        if empty:
            self.send_header("Content-Length", "0")
        elif known:
            self.send_header("Content-Length", clen.strip())
        else:
            self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        sent = 0
        try:
            while not empty:
                piece, first = first or resp.read(PIECE), b""
                if not piece:
                    break
                sent += len(piece)
                self.wfile.write(piece if known else b"%x\r\n%s\r\n" % (len(piece), piece))
            if not empty and not known:
                self.wfile.write(b"0\r\n\r\n")
            resp.read()
        except (OSError, http.client.HTTPException):
            # the server or the Atari went away halfway: neither connection
            # can carry another answer
            UP.drop(u.scheme, u.hostname, port)
            self.close_connection = True
        if resp.will_close:
            UP.drop(u.scheme, u.hostname, port)
        self.note(resp.status, sent)


LIVE = {}           # the browser's screen width, as the last picture said

# live view's keys: what EWeb's buttons send, what xdotool presses
LIVE_KEYS = {"enter": "Return", "tab": "Tab", "backspace": "BackSpace", "esc": "Escape",
             "up": "Page_Up", "down": "Page_Down", "back": "alt+Left", "reload": "F5"}

LIVE_PAGE = """<!DOCTYPE html><html><head><meta charset=utf-8><title>Live: %(title)s</title>
<style>body{margin:4px;font-family:sans-serif;background:#fff}form{display:inline;margin:0}
p{margin:4px 0}input[type=image]{border:1px solid #888}</style></head><body>
<form action="http://webgate/live/click"><input type=image name=at width=%(w)d
 src="http://webgate/live/shot.jpg?n=%(n)d" alt="the Pi's browser"></form>
<p><form action="http://webgate/live/type"><input name=t size=40> <input type=submit value=Type>
 <input type=submit name=enter value="Type and Enter"></form></p>
<p><form action="http://webgate/live/key"><button name=k value=enter>Enter</button>
 <button name=k value=tab>Tab</button> <button name=k value=backspace>Backspace</button>
 <button name=k value=esc>Esc</button> <button name=k value=up>Page up</button>
 <button name=k value=down>Page down</button> <button name=k value=back>Back</button>
 <button name=k value=reload>Reload</button></form>
 <a href="http://webgate/live?n=%(n)d">Picture again</a> &middot; <a href="http://webgate/live/page">As a page</a></p>
</body></html>
"""


def _local(addr):
    """the Pi itself (127.0.0.1, ::1): the only one its browser's proxy serves"""
    ip = aeragate._ip(addr)
    return ip is not None and ip.is_loopback


class ExtHandler(Handler):
    """127.0.0.1 only. webgate-ext talks here (/next, /config, /shot,
    /snapshot), and the Pi's browser goes out through here, as through
    a proxy (--proxy-server): its pages and what they load in proxy form,
    https and websockets as CONNECT tunnels. Every connection is made by
    webgate to the address it checked, so the browser reaches the home
    network only with --allow-local, whatever a name's answer says (7.3)."""
    for_browser = True

    def answer(self, status, body=b"", ctype="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def from_a_page(self):
        """Is this bridge request from a page the browser opened (an <img>,
        a fetch, a form), not from webgate-ext? Pages can't set these
        headers, and Chromium always sends Sec-Fetch-Site to 127.0.0.1;
        the extension's own say "none" and chrome-extension://."""
        site, origin = self.headers.get("Sec-Fetch-Site"), self.headers.get("Origin")
        if (site is not None and site != "none") or (origin is not None and not origin.startswith("chrome-extension://")):
            self.answer(403)
            return True
        return False

    def do_GET(self):
        if not self.path.startswith("/"):
            self.proxy()            # the browser, going out
            return
        if self.from_a_page():
            return
        if self.path.startswith("/config"):
            # what the extension blocks: the home network, unless --allow-local
            self.answer(200, json.dumps({"allow_local": Config.allow_local}).encode())
            return
        if self.path.startswith("/next"):
            w = BRIDGE.next()
            if w is None:
                self.answer(204)
            else:
                self.answer(200, json.dumps(w).encode())
        else:
            self.answer(404)

    def do_POST(self):
        if not self.path.startswith("/"):
            self.proxy()
            return
        if self.from_a_page():
            return
        n = int(self.headers.get("Content-Length") or 0)
        data = self.rfile.read(n) if n else b""
        q = urlsplit(self.path)
        if q.path == "/shot":
            a = parse_qs(q.query)
            BRIDGE.give(a.get("id", [""])[0], {"pic": data, "w": int(a.get("w", ["0"])[0] or 0),
                                               "why": a.get("why", [""])[0]})
            self.answer(200, b"{}")
            return
        if q.path != "/snapshot" or not q.query.startswith("id="):
            self.answer(404)
            return
        try:
            snap = json.loads(data.decode("utf-8"))
        except ValueError:
            self.answer(400)
            return
        BRIDGE.give(q.query[3:], snap)
        self.answer(200, b"{}")

    def do_HEAD(self):
        self.proxy()

    do_PUT = do_DELETE = do_OPTIONS = do_PATCH = do_HEAD

    def do_CONNECT(self):
        """a tunnel for the browser (https, websockets), to the address
        webgate checked: one look-up, and the connection to what it said"""
        if not _local(self.client_address[0]):
            self.reply(403, "webgate: this address may not use the gateway\n")
            return
        host, _, port = self.path.rpartition(":")
        host = host.strip("[]")
        if not host or not port.isdigit():
            self.reply(400, "webgate: expected CONNECT host:port\n")
            return
        port = int(port)
        self.where = "%s:%d" % (host, port)
        try:
            up = aeragate.connect(host, port, Config.timeout, Config.allow_local)
        except aeragate.HomeNetwork:
            self.reply(403, "webgate: won't connect to the local network (%s)\n" % host)
            return
        except (socket.gaierror, UnicodeError, ValueError):
            self.reply(502, "webgate: can't find %s\n" % host)
            return
        except OSError as e:
            self.reply(504 if isinstance(e, socket.timeout) else 502, "webgate: can't reach %s (%s)\n" % (host, e))
            return
        self.close_connection = True
        try:
            self.send_response(200, "Connection established")
            self.end_headers()
            self.note(200, 0, " (a tunnel)")
            aeragate.relay(self.connection, up, 300, 60)
        except OSError:
            pass
        finally:
            up.close()


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="HTTP and HTTPS gateway for EWeb on the Atari")
    ap.add_argument("--port", type=int, default=8280)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this Atari's address may use the gateway (may repeat)")
    ap.add_argument("--allow-local", action="store_true",
                    help="also forward to servers on the local network")
    ap.add_argument("--cafile", help="check servers against these root certificates, not the Pi's")
    ap.add_argument("--browser", action="store_true",
                    help="snapshots: the Pi's Chromium with webgate-ext is running")
    ap.add_argument("--browser-port", type=int, default=8281, help="where webgate-ext asks (127.0.0.1 only)")
    ap.add_argument("--pens-socket", default="/run/webgate-pens.sock",
                    help="aerapens' socket, for pictures as the Atari's pens (not there: none)")
    ap.add_argument("--insecure", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    Config.allow = a.allow
    Config.allow_local = a.allow_local
    Config.verbose = a.verbose
    Config.cafile = a.cafile
    Config.insecure = a.insecure
    Config.browser = a.browser
    Config.browser_port = a.browser_port
    Config.pens_socket = a.pens_socket
    srv = ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    if a.browser:
        ext = ThreadingHTTPServer(("127.0.0.1", a.browser_port), ExtHandler)
        ext.daemon_threads = True
        threading.Thread(target=ext.serve_forever, daemon=True).start()
    aeragate.say("webgate %s listening on %s:%d" % (VERSION, a.bind, a.port))
    # once, at the start: which aerapens makes pens, if any
    PENS_SEEN[0] = pens_which()
    aeragate.say("webgate: pictures as pens by %s" % PENS_SEEN[0] if PENS_SEEN[0] else
                 "webgate: no pictures as pens (nothing answers on %s)" % Config.pens_socket)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
