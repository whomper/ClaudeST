#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""radiogate - web radio for EPlay through the Gateway (port 8580).

EPlay (on a Falcon) plays MP3 stations by itself over plain HTTP. Through
the Gateway it asks this service for any station, and gets plain MP3 in
Icecast's way (an ICY stream, its songs' titles in it), as from a station
of its own:

    GET /stream?url=<the station's URL, %-encoded> HTTP/1.0
    Icy-MetaData: 1                         (optional: the titles wanted)

  answered by

    HTTP/1.0 200 OK
    Content-Type: audio/mpeg
    icy-name: ...        icy-br: ...        icy-metaint: 8192 (when asked)

  and the stream, until the Atari closes the connection, or by an error
  with one line "radiogate: why" (400 a URL it doesn't take, 403 not from
  the home network, 502 the station doesn't answer, 503 too many at once).

    GET /art?url=<a picture's URL, %-encoded> HTTP/1.0   (radiogate 1.1)

  answered by the picture (a station's StreamUrl: a song's cover or its
  logo; JPEG, PNG, GIF, WebP...) as a 64 x 64 baseline JPEG, its middle
  square, made by ffmpeg ("Content-Type: image/jpeg"), the last 64 kept;
  or by an error as above (502 too: not a picture).

    GET /logo?station=<the station's URL>&name=<its name>   (radiogate 1.2)

  answered by the station's logo from radio-browser.info (found by its
  address, else by its exact name), as a 64 x 64 JPEG as /art's; 404 when
  the directory has none (asked again a day on), 502 when it doesn't
  answer.

    GET /version   ->  "radiogate 1.2, ffmpeg 6.1"

What the station sends:
  - MP3 (audio/mpeg): passed on as it comes, over HTTP or HTTPS, its ICY
    titles with it;
  - AAC, Ogg, or anything else that isn't MP3: converted with ffmpeg to
    MP3 (128 kbit/s at most, 44.1 kHz, stereo), the station's ICY titles
    taken out of what goes to ffmpeg and put back into the MP3 (Erez,
    8 Nov 2026: ffmpeg on the Gateway, AAC on the Falcon's DSP later);
  - HLS (an .m3u8 of pieces): its playlists and pieces fetched here, each
    through aeragate's checked connection, the pieces (MPEG-TS or ADTS
    AAC) fed to ffmpeg;
  - a station list (.pls, .m3u): its first station.
Redirects are followed, five at most.

The rules are aeragate's: the home network only, or --allow; never back
into the home network without --allow-local, and only to the address that
was checked (each redirect and each HLS piece too); TLS 1.2 or newer, the
certificate and name checked. The log says who listened to which host, how
long and how much, never the path or the titles. --jobs streams at once
(2); ffmpeg runs at a low priority, with --memory.

Standard Python 3 only, and ffmpeg for what isn't MP3.
"""

import argparse
import os
import re
import resource
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl, quote, urljoin, urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
for d in (HERE, os.path.join(HERE, "..", "gateway")):
    if os.path.exists(os.path.join(d, "aeragate.py")):
        sys.path.insert(0, d)
        break
import aeragate  # noqa: E402

VERSION = "1.2"
METAINT = 8192                      # the Atari's: a title block every 8 KB of MP3
HEAD_MAX = 16384                    # a station's answer head, at most
LIST_MAX = 65536                    # a station list or an HLS playlist, at most
PIC_MAX = 4 << 20                   # a station's picture, at most
ART_SIDE = 64                       # /art's pictures: 64 x 64, EPlay's art square
PIECE_MAX = 16 << 20                # an HLS piece, at most
URL_MAX = 2048
REDIRECTS = 5
KBPS = 128                          # what a conversion makes


class Config:
    allow = []
    allow_local = False
    cafile = None
    ffmpeg = "ffmpeg"
    jobs = threading.BoundedSemaphore(2)
    memory = 256 << 20
    timeout = 20
    verbose = False
    # radio-browser.info, /logo's station directory: its mirrors, each tried in turn (one often
    # refuses a connection)
    rb_api = ["https://de1.api.radio-browser.info", "https://fi1.api.radio-browser.info",
              "https://nl1.api.radio-browser.info", "https://de2.api.radio-browser.info"]


class Refused(Exception):
    """a URL or an answer this service doesn't take: (status, words)"""

    def __init__(self, status, words):
        super().__init__(words)
        self.status, self.words = status, words


# ---- the station's side: HTTP/1.0, every host checked ----

def checked_url(url):
    """(scheme, host, port, path) for an http or https URL, else Refused"""
    if len(url) > URL_MAX or any(ord(c) <= 32 or ord(c) == 127 for c in url):
        raise Refused(400, "not a station's address")
    u = urlsplit(url)
    if u.scheme not in ("http", "https") or not u.hostname or u.username or u.password:
        raise Refused(400, "not a station's address")
    try:
        port = u.port or (443 if u.scheme == "https" else 80)
    except ValueError:
        raise Refused(400, "not a station's address")
    path = (u.path or "/") + ("?" + u.query if u.query else "")
    return u.scheme, u.hostname, port, path


def open_station(url, icy=True):
    """the station's socket (TLS for https) with its head read: (sock,
    status, headers {lowercase: value}, the bytes after the head)"""
    scheme, host, port, path = checked_url(url)
    try:
        s = aeragate.connect(host, port, Config.timeout, Config.allow_local)
    except aeragate.HomeNetwork:
        raise Refused(403, "that station is on the home network")
    except OSError:
        raise Refused(502, "the station doesn't answer")
    try:
        if scheme == "https":
            s = aeragate.tls_context(Config.cafile).wrap_socket(s, server_hostname=host)
        s.settimeout(Config.timeout)
        hostport = host if port in (80, 443) else "%s:%d" % (host, port)
        s.sendall(("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: radiogate/%s (AERA Gateway)\r\n"
                   "Accept: */*\r\n%sConnection: close\r\n\r\n" %
                   (path, hostport, VERSION, "Icy-MetaData: 1\r\n" if icy else "")).encode())
        got = b""
        while b"\r\n\r\n" not in got and b"\n\n" not in got:
            d = s.recv(4096)
            if not d:
                break
            got += d
            if len(got) > HEAD_MAX:
                raise Refused(502, "the station's answer isn't one this reads")
        cut = got.find(b"\r\n\r\n")
        cut, skip = (cut, 4) if cut >= 0 else (got.find(b"\n\n"), 2)
        if cut < 0:
            raise Refused(502, "the station closed the connection")
        lines = got[:cut].decode("latin-1").replace("\r", "").split("\n")
        m = re.match(r"^(ICY|HTTP/1\.[01]) (\d{3})", lines[0])
        if not m:
            raise Refused(502, "the station's answer isn't one this reads")
        heads = {}
        for l in lines[1:]:
            k, _, v = l.partition(":")
            heads[k.strip().lower()] = v.strip()
        return s, int(m.group(2)), heads, got[cut + skip:]
    except Refused:
        s.close()
        raise
    except (OSError, ssl.SSLError):
        s.close()
        raise Refused(502, "the station doesn't answer")


def read_all(s, have, limit):
    """the rest of a short answer (a list, a playlist, a piece)"""
    data = bytearray(have)
    while len(data) <= limit:
        try:
            d = s.recv(65536)
        except (OSError, ssl.SSLError):
            break
        if not d:
            break
        data += d
    s.close()
    if len(data) > limit:
        raise Refused(502, "the station sent more than this takes")
    return bytes(data)


def fetch(url, limit):
    """a short answer's bytes, redirects followed"""
    for _ in range(REDIRECTS + 1):
        s, status, heads, have = open_station(url, icy=False)
        if status in (301, 302, 303, 307, 308) and heads.get("location"):
            s.close()
            url = urljoin(url, heads["location"])
            continue
        if status != 200:
            s.close()
            raise Refused(502, "the station answered %d" % status)
        return url, read_all(s, have, limit)
    raise Refused(502, "the station sent this from one address to another too often")


def kind_of(url, ctype):
    t = ctype.split(";")[0].strip().lower()
    path = urlsplit(url).path.lower()
    if t in ("audio/mpeg", "audio/mp3", "audio/x-mpeg", "audio/mpeg3", "audio/x-mp3"):
        return "mp3"
    if t in ("application/vnd.apple.mpegurl", "application/x-mpegurl") or path.endswith(".m3u8"):
        return "hls"
    if t in ("audio/x-scpls", "audio/x-mpegurl", "audio/mpegurl") or path.endswith((".pls", ".m3u")):
        return "list"
    return "other"


def first_station(text, base):
    """a .pls's File1, or an .m3u's first URL; or 'hls' when it is one"""
    if "#EXT-X-" in text:
        return "hls"
    found = []
    for l in text.replace("\r", "").split("\n"):
        l = l.strip()
        m = re.match(r"^File(\d+)=(.+)$", l, re.I)
        if m:
            found.append((int(m.group(1)), m.group(2).strip()))
        elif l and not l.startswith(("#", "[")) and "=" not in l.split("/")[0]:
            found.append((10 ** 6 + len(found), l))
    for _, u in sorted(found):
        u = urljoin(base, u)
        if urlsplit(u).scheme in ("http", "https"):
            return u
    return None


# ---- ICY: titles out of what comes, into what goes ----

class IcyIn:
    """the station's metadata taken out (metaint bytes of audio, then a
    length byte and 16 times it of 'StreamTitle=...;StreamUrl=...;'): its
    title, and its StreamUrl (a picture's address on some stations)"""

    def __init__(self, metaint):
        self.metaint, self.left, self.meta, self.need = metaint, metaint, b"", -1
        self.title = self.url = None

    def feed(self, data):
        if not self.metaint:
            return data
        out, i = bytearray(), 0
        while i < len(data):
            if self.left:
                k = min(self.left, len(data) - i)
                out += data[i:i + k]
                i += k
                self.left -= k
            elif self.need < 0:
                self.need, self.meta = data[i] * 16, b""
                i += 1
                if not self.need:
                    self.need, self.left = -1, self.metaint
            else:
                k = min(self.need - len(self.meta), len(data) - i)
                self.meta += data[i:i + k]
                i += k
                if len(self.meta) == self.need:
                    m = re.search(rb"StreamTitle='(.*?)';", self.meta, re.S)
                    if m:
                        self.title = m.group(1).decode("utf-8", "replace")[:200]
                    m = re.search(rb"StreamUrl='(.*?)';", self.meta, re.S)
                    if m:
                        self.url = m.group(1).decode("latin-1")[:255]
                    self.need, self.left = -1, self.metaint
        return bytes(out)


class IcyOut:
    """MP3 with a title block every METAINT bytes (an empty one when the
    title hasn't changed)"""

    def __init__(self):
        self.left, self.sent = METAINT, None

    def wrap(self, data, title, url=None):
        out, i = bytearray(), 0
        while i < len(data):
            k = min(self.left, len(data) - i)
            out += data[i:i + k]
            i += k
            self.left -= k
            if not self.left:
                if title is not None and (title, url) != self.sent:
                    t = ("StreamTitle='%s';" % title.replace("'", "")).encode("utf-8", "replace")
                    if url and "'" not in url and all(32 < ord(c) < 127 for c in url):
                        t += ("StreamUrl='%s';" % url).encode()     # (the station's own, for its picture)
                    t = t[:16 * 255]
                    t += b"\0" * (-len(t) % 16)
                    out += bytes([len(t) // 16]) + t
                    self.sent = (title, url)
                else:
                    out += b"\0"
                self.left = METAINT
        return bytes(out)


# ---- ffmpeg ----

def ffmpeg_version():
    try:
        r = subprocess.run([Config.ffmpeg, "-version"], capture_output=True, text=True, timeout=10)
        m = re.match(r"ffmpeg version (\S+)", r.stdout)
        return m.group(1) if m else None
    except (OSError, subprocess.SubprocessError):
        return None


def start_ffmpeg(informat=None):
    # (-protocol_whitelist pipe: what the station sends can't make ffmpeg
    # open addresses or files of its own, past aeragate's checks: 6.1 and
    # 7 refuse that already, an older one may not; the Gateway's review)
    # (one thread: a single audio stream gains nothing from more, and each
    # thread's stack counts toward RLIMIT_DATA: the Gateway's CI)
    args = [Config.ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-protocol_whitelist", "pipe",
            "-threads", "1"]
    if informat:
        args += ["-f", informat]
    args += ["-i", "pipe:0", "-threads", "1", "-vn", "-map", "0:a:0", "-c:a", "libmp3lame", "-b:a", "%dk" % KBPS,
             "-ar", "44100", "-ac", "2", "-f", "mp3", "pipe:1"]
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        # (its data, not its address space: ffmpeg maps large libraries
        # read-only, and 256 MB of address space kept it from starting)
        resource.prlimit(p.pid, resource.RLIMIT_DATA, (Config.memory, Config.memory))
        resource.prlimit(p.pid, resource.RLIMIT_CORE, (0, 0))
        os.setpriority(os.PRIO_PROCESS, p.pid, 10)
    except (OSError, ValueError):
        pass
    return p


def shrink(data):
    """a picture (JPEG, PNG, GIF, WebP...) as a 64 x 64 baseline JPEG, its
    middle square, by ffmpeg; None if it isn't one ffmpeg reads"""
    # (one thread each, decoder, filters and encoder: a 64 x 64 picture
    # needs none, and the thread pools' stacks took ffmpeg 6.1 over 256 MB
    # of RLIMIT_DATA on a 4-core runner: the Gateway's CI, 8 Nov 2026)
    args = [Config.ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-protocol_whitelist", "pipe",
            "-threads", "1", "-i", "pipe:0", "-filter_threads", "1", "-threads", "1", "-frames:v", "1",
            "-vf", "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d" % ((ART_SIDE,) * 4),
            "-pix_fmt", "yuvj420p", "-q:v", "3", "-f", "mjpeg", "pipe:1"]
    try:
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            resource.prlimit(p.pid, resource.RLIMIT_DATA, (Config.memory, Config.memory))
            resource.prlimit(p.pid, resource.RLIMIT_CORE, (0, 0))
        except (OSError, ValueError):
            pass
        out, _ = p.communicate(data, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    return out if p.returncode == 0 and out[:2] == b"\xff\xd8" else None


class ArtCache:
    """the last pictures made, by their address (a song's cover comes again)"""
    keep = 64

    def __init__(self):
        self.d, self.lock = {}, threading.Lock()

    def get(self, url):
        with self.lock:
            v = self.d.pop(url, None)
            if v is not None:
                self.d[url] = v
            return v

    def put(self, url, jpg):
        with self.lock:
            self.d[url] = jpg
            while len(self.d) > self.keep:
                self.d.pop(next(iter(self.d)))


ART = ArtCache()
LOGOS = ArtCache()                  # /logo's, by station: a JPEG, or b"" for none (asked again a day on)
LOGOS.keep = 256
LOGO_NONE_S = 24 * 3600
ART_JOBS = threading.BoundedSemaphore(2)    # pictures made at once (an ffmpeg each), else 503


# ---- the Atari's side ----

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "radiogate/" + VERSION
    timeout = 60

    def log_message(self, fmt, *args):
        if Config.verbose and getattr(self, "command", None):
            aeragate.say("radiogate: %s %s %s" % (self.client_address[0], self.command,
                                                   urlsplit(self.path).path[:40]))

    def reply(self, status, text):
        body = ("radiogate: %s\n" % text).encode("utf-8", "replace")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except OSError:
            pass
        aeragate.say("radiogate: %s %d (%s)" % (self.client_address[0], status, text))

    def do_GET(self):
        if not aeragate.client_ok(self.client_address[0], Config.allow):
            self.reply(403, "not from this computer")
            return
        u = urlsplit(self.path)
        if u.path == "/version":
            body = ("radiogate %s, ffmpeg %s\n" % (VERSION, ffmpeg_version() or "none")).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if u.path == "/logo":
            q = dict(parse_qsl(u.query, keep_blank_values=True))
            self.logo(q.get("station", ""), q.get("name", ""))
            return
        if u.path == "/art":
            self.art(dict(parse_qsl(u.query, keep_blank_values=True)).get("url", ""))
            return
        if u.path != "/stream":
            self.reply(404, "no such page")
            return
        q = dict(parse_qsl(u.query, keep_blank_values=True))
        url = q.get("url", "")
        if not Config.jobs.acquire(blocking=False):
            self.reply(503, "too many stations at once")
            return
        t0, sent = time.time(), [0]
        host = urlsplit(url).hostname or "?"
        self.fault = None               # (the feed thread's, when it failed)
        try:
            self.stream(url, self.headers.get("Icy-MetaData", "") == "1", sent)
            why = self.fault or "ended"
        except Refused as e:
            if sent[0]:
                why = e.words
            else:
                self.reply(e.status, e.words)
                return
        except (OSError, ssl.SSLError):
            why = "the Atari closed it"
        finally:
            Config.jobs.release()
        aeragate.say("radiogate: %s listened to %s for %d s, %d KB (%s)" %
                     (self.client_address[0], host, time.time() - t0, sent[0] // 1024, why))

    def begin(self, name, kbps, meta):
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        if name:
            self.send_header("icy-name", re.sub(r"[\r\n]", " ", name)[:120])
        if kbps:
            self.send_header("icy-br", str(kbps))
        if meta:
            self.send_header("icy-metaint", str(METAINT))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

    def stream(self, url, meta, sent):
        """the station, followed to its audio, sent on as MP3"""
        for _ in range(REDIRECTS + 1):
            s, status, heads, have = open_station(url)
            if status in (301, 302, 303, 307, 308) and heads.get("location"):
                s.close()
                url = urljoin(url, heads["location"])
                checked_url(url)
                continue
            if status != 200:
                s.close()
                raise Refused(502, "the station answered %d" % status)
            kind = kind_of(url, heads.get("content-type", ""))
            if kind == "list":
                text = read_all(s, have, LIST_MAX).decode("utf-8", "replace")
                nxt = first_station(text, url)
                if nxt == "hls":
                    return self.hls(url, text, meta, sent)
                if not nxt:
                    raise Refused(502, "the station list holds no station")
                url = nxt
                continue
            if kind == "hls":
                text = read_all(s, have, LIST_MAX).decode("utf-8", "replace")
                return self.hls(url, text, meta, sent)
            name = heads.get("icy-name", "")
            try:
                metaint = int(heads.get("icy-metaint", "0"))
            except ValueError:
                metaint = 0
            if not 0 <= metaint <= 65535:
                raise Refused(502, "the station's answer isn't one this reads")
            if kind == "mp3":
                return self.pass_mp3(s, have, name, heads.get("icy-br", ""), metaint, meta, sent)
            return self.convert(s, have, name, metaint, meta, sent)
        raise Refused(502, "the station sent this from one address to another too often")

    def out(self, data, sent):
        self.wfile.write(data)
        self.wfile.flush()
        sent[0] += len(data)

    def pass_mp3(self, s, have, name, br, metaint, meta, sent):
        """MP3 as it comes: with its own metadata when the Atari asked for it
        and the station sends some, else without"""
        try:
            kbps = int(br.split(",")[0])
        except ValueError:
            kbps = 0
        icy_in = IcyIn(metaint)
        icy_out = IcyOut() if meta else None
        try:
            self.begin(name, kbps, meta)
            data = have
            while True:
                if data:
                    audio = icy_in.feed(data)
                    self.out(icy_out.wrap(audio, icy_in.title, icy_in.url) if icy_out else audio, sent)
                try:
                    data = s.recv(16384)
                except socket.timeout:
                    raise Refused(502, "the station fell silent")
                if not data:
                    return
        finally:
            s.close()  # (the station's too, when the Atari went first)

    def logo(self, station, name):
        """GET /logo?station=<the station's URL>&name=<its icy-name> (EPlay,
        when a Gateway is set, for a station that names no picture of its
        own: Erez, 8 Nov 2026): its logo from radio-browser.info, found by
        the station's address, else by its exact name (the most voted), as
        a 64 x 64 JPEG; 404 when there is none"""
        try:
            checked_url(station)
        except Refused as e:
            self.reply(e.status, e.words)
            return
        name = name.strip()[:120]
        key = station + "\n" + name.lower()
        got = LOGOS.get(key)
        if got is not None and (got[0] or time.time() - got[1] < LOGO_NONE_S):
            jpg = got[0]
        else:
            if not ffmpeg_version():
                self.reply(502, "pictures need ffmpeg on the Gateway")
                return
            if not ART_JOBS.acquire(blocking=False):
                self.reply(503, "too many pictures at once")
                return
            try:
                jpg = self.find_logo(station, name)
            finally:
                ART_JOBS.release()
            if jpg is None:
                return                  # (a reply made: the directory didn't answer)
            LOGOS.put(key, (jpg, time.time()))
        if not jpg:
            self.reply(404, "no logo for this station")
            return
        try:
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpg)))
            self.end_headers()
            self.wfile.write(jpg)
        except OSError:
            pass

    def find_logo(self, station, name):
        """the logo's JPEG, b"" when the directory has none, None (a reply
        made) when it doesn't answer. Asked in turn: by the station's
        address; by its exact name; by its name's first words, taken only
        from an entry whose stream is on the station's own domain (no other
        station's logo)"""
        import json

        def domain(u):
            return ".".join((urlsplit(u).hostname or "").split(".")[-2:])

        words = re.split(r" - |[,:(|\[]", name)[0].strip() if name else ""
        asks = [("/json/stations/byurl?url=" + quote(station, safe=""), False)]
        if name:
            asks.append(("/json/stations/search?name=%s&nameExact=true&hidebroken=true&order=votes&reverse=true"
                         "&limit=5" % quote(name, safe=""), False))
        if words:
            asks.append(("/json/stations/search?name=%s&hidebroken=true&order=votes&reverse=true&limit=30" %
                         quote(words, safe=""), True))
        answered, mine = False, domain(station)
        for a, same_domain in asks:
            found = None
            for api in Config.rb_api:
                try:
                    _, data = fetch(api + a, 1 << 20)
                    found = json.loads(data.decode("utf-8", "replace"))
                    answered = True
                    break
                except (Refused, ValueError):
                    continue
            for st in found if isinstance(found, list) else []:
                fav = st.get("favicon") if isinstance(st, dict) else None
                if not isinstance(fav, str) or not fav.startswith(("http://", "https://")):
                    continue
                if same_domain and mine not in (domain(str(st.get("url", ""))), domain(str(st.get("url_resolved", "")))):
                    continue
                try:
                    _, pic = fetch(fav, PIC_MAX)
                except Refused:
                    continue
                jpg = shrink(pic)
                if jpg:
                    return jpg
        if not answered:
            self.reply(502, "the station directory doesn't answer")
            return None
        return b""

    def art(self, url):
        """GET /art?url=<a picture's address> (EPlay through the Gateway, a
        station's StreamUrl): the picture fetched (the same checks as a
        station's), as a 64 x 64 JPEG"""
        host = urlsplit(url).hostname or "?"
        jpg = ART.get(url)
        if jpg is None:
            if not ffmpeg_version():
                self.reply(502, "pictures need ffmpeg on the Gateway")
                return
            if not ART_JOBS.acquire(blocking=False):
                self.reply(503, "too many pictures at once")
                return
            try:
                _, data = fetch(url, PIC_MAX)
                jpg = shrink(data)
            except Refused as e:
                self.reply(e.status, e.words)
                return
            finally:
                ART_JOBS.release()
            if jpg is None:
                self.reply(502, "not a picture this reads")
                return
            ART.put(url, jpg)
        try:
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpg)))
            self.end_headers()
            self.wfile.write(jpg)
        except OSError:
            return
        if Config.verbose:
            aeragate.say("radiogate: %s a picture from %s, %d bytes" % (self.client_address[0], host, len(jpg)))

    def convert(self, s, have, name, metaint, meta, sent):
        """AAC, Ogg and the like through ffmpeg; the station's titles kept"""
        if not ffmpeg_version():
            s.close()
            raise Refused(502, "this station needs ffmpeg on the Gateway")
        p = start_ffmpeg()
        icy_in = IcyIn(metaint)
        stop = threading.Event()

        def feed():
            data = have
            try:
                while not stop.is_set():
                    if data:
                        p.stdin.write(icy_in.feed(data))
                        p.stdin.flush()
                    data = s.recv(16384)
                    if not data:
                        break
            except (OSError, ValueError, ssl.SSLError):
                if not stop.is_set():
                    self.fault = "the station's feed failed"
            finally:
                try:
                    p.stdin.close()
                except OSError:
                    pass

        return self.run_ffmpeg(p, feed, stop, s, name, meta, sent, lambda: (icy_in.title, icy_in.url))

    def run_ffmpeg(self, p, feed, stop, s, name, meta, sent, title):
        t = threading.Thread(target=feed, daemon=True)
        t.start()
        icy_out = IcyOut() if meta else None
        try:
            first = p.stdout.read1(16384) if hasattr(p.stdout, "read1") else p.stdout.read(16384)
            if not first:
                raise Refused(502, "the station's sound couldn't be converted")
            self.begin(name, KBPS, meta)
            data = first
            while data:
                self.out(icy_out.wrap(data, *title()) if icy_out else data, sent)
                data = p.stdout.read1(16384) if hasattr(p.stdout, "read1") else p.stdout.read(16384)
        finally:
            stop.set()
            p.kill()
            p.wait()
            if s:
                try:
                    s.close()
                except OSError:
                    pass

    def hls(self, url, text, meta, sent):
        """HLS: a variant chosen (the first at 128 kbit/s or less, else the
        lowest), its playlist read again as it grows, each new piece
        fetched here and fed to ffmpeg"""
        if not ffmpeg_version():
            raise Refused(502, "this station needs ffmpeg on the Gateway")
        if "#EXT-X-STREAM-INF" in text:
            variants = []
            lines = text.replace("\r", "").split("\n")
            for i, l in enumerate(lines):
                if l.startswith("#EXT-X-STREAM-INF"):
                    m = re.search(r"BANDWIDTH=(\d+)", l)
                    nxt = next((x.strip() for x in lines[i + 1:] if x.strip() and not x.startswith("#")), None)
                    if nxt:
                        variants.append((int(m.group(1)) if m else 10 ** 9, urljoin(url, nxt)))
            # (the best at 160 kbit/s or less, else the lowest there is)
            fit = [v for v in variants if v[0] <= 160000]
            best = max(fit) if fit else (min(variants) if variants else None)
            if not best:
                raise Refused(502, "the station's HLS has no stream")
            url, text = fetch(best[1], LIST_MAX)
            text = text.decode("utf-8", "replace")
        if "#EXT-X-MAP" in text:
            raise Refused(502, "this station's HLS (fragmented MP4) isn't taken yet")
        p = start_ffmpeg()
        stop = threading.Event()
        state = {"url": url, "text": text}

        def feed():
            done = set()
            try:
                while not stop.is_set():
                    lines = state["text"].replace("\r", "").split("\n")
                    seq = 0
                    target = 6
                    for l in lines:
                        if l.startswith("#EXT-X-MEDIA-SEQUENCE:"):
                            seq = int(l.split(":")[1] or 0)
                        elif l.startswith("#EXT-X-TARGETDURATION:"):
                            target = max(1, min(30, int(float(l.split(":")[1] or 6))))
                    pieces = [x.strip() for x in lines if x.strip() and not x.startswith("#")]
                    if not done:
                        pieces = pieces[-3:]        # (live: the last few, not the whole window)
                        seq += len([x for x in lines if x.strip() and not x.startswith("#")]) - len(pieces)
                    for i, piece in enumerate(pieces):
                        n = seq + i
                        if n in done or stop.is_set():
                            continue
                        _, data = fetch(urljoin(state["url"], piece), PIECE_MAX)
                        p.stdin.write(data)
                        p.stdin.flush()
                        done.add(n)
                    if "#EXT-X-ENDLIST" in state["text"]:
                        break
                    stop.wait(target / 2)
                    u, t = fetch(state["url"], LIST_MAX)
                    state["text"] = t.decode("utf-8", "replace")
            except Refused as e:
                if not stop.is_set():
                    self.fault = "a piece: " + e.words
            except (OSError, ValueError, ssl.SSLError):
                if not stop.is_set():
                    self.fault = "the station's feed failed"
            finally:
                try:
                    p.stdin.close()
                except OSError:
                    pass

        return self.run_ffmpeg(p, feed, stop, None, "", meta, sent, lambda: (None, None))


def parser():
    p = argparse.ArgumentParser(description="web radio for EPlay through the Gateway")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--port", type=int, default=8580)
    p.add_argument("--allow", action="append", default=[], help="the Atari's address (again for more)")
    p.add_argument("--allow-local", action="store_true", help="stations on the home network too")
    p.add_argument("--cafile", help="root certificates in place of the system's")
    p.add_argument("--ffmpeg", default="ffmpeg")
    p.add_argument("--jobs", type=int, default=2, help="stations at once")
    p.add_argument("--memory-mb", type=int, default=256, help="ffmpeg's memory")
    p.add_argument("--timeout", type=int, default=20, help="seconds a station may fall silent")
    p.add_argument("-v", "--verbose", action="store_true")
    return p


def main(argv=None):
    a = parser().parse_args(argv)
    Config.allow, Config.allow_local, Config.cafile = a.allow, a.allow_local, a.cafile
    Config.ffmpeg = shutil.which(a.ffmpeg) or a.ffmpeg
    Config.jobs = threading.BoundedSemaphore(max(1, a.jobs))
    Config.memory = a.memory_mb << 20
    Config.timeout = a.timeout
    Config.verbose = a.verbose
    srv = ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    aeragate.say("radiogate %s listening on %s:%d" % (VERSION, a.bind, a.port))
    v = ffmpeg_version()
    aeragate.say("radiogate: converting with ffmpeg %s" % v if v else
                 "radiogate: no ffmpeg: MP3 stations only")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
