#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ecalgate - the Raspberry Pi gateway for ECal on the Atari.

Calendar servers (iCloud, Google, Fastmail...) and published calendars
want HTTPS, which an 8 MHz ST can't do. So ECal sends its requests
to the Pi in plain HTTP on your home network, in proxy form:

    PROPFIND https://caldav.icloud.com/ HTTP/1.1

and ecalgate makes the HTTPS request, checks the server's certificate and
hands the answer back. On the way it does what a small Atari would find
hard:

  - it keeps one HTTPS connection per server open and reuses it;
  - it asks for compressed answers and unpacks them;
  - it turns chunked answers into one block with a Content-Length;
  - for a calendar subscription (an .ics feed) it keeps only the events
    in the window the Atari asks for (X-Calendar-Window: 20260701-20271101),
    plus every repeating event that still repeats, and leaves out bulky
    things the Atari doesn't show (HTML descriptions, attached files).
    Public feeds of holidays or sports can run to megabytes; trimmed,
    they fit in an ST's memory.

Only standard Python 3 is needed (Raspberry Pi OS has it), and aeragate.py
next to it. `aera-gateway install dav` sets it up as a service. Falcon mode
doesn't use it: the Falcon does TLS itself.

    ecalgate.py [--port 8180] [--allow 192.168.1.50] [--allow-local] [-v]
"""

import argparse
import datetime
import gzip
import http.client
import os
import re
import socket
import ssl
import threading
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

import aeragate

VERSION = "1.1"

HOP_BY_HOP = {
    "connection", "keep-alive", "proxy-connection", "proxy-authorization",
    "proxy-authenticate", "te", "trailer", "transfer-encoding", "upgrade",
    "accept-encoding", "x-calendar-window", "host", "content-length",
}

# properties the Atari never shows, often large
DROP_PROPS = ("X-ALT-DESC", "X-MICROSOFT-CDO-ALTDESC", "X-APPLE-STRUCTURED-LOCATION")


# ---------------------------------------------------------------- trimming

def _unfold(lines):
    """Physical lines -> (logical line, [physical lines])."""
    cur, raw = None, []
    for ln in lines:
        if ln[:1] in (" ", "\t") and cur is not None:
            cur += ln[1:]
            raw.append(ln)
            continue
        if cur is not None:
            yield cur, raw
        cur, raw = ln, [ln]
    if cur is not None:
        yield cur, raw


def _prop(line):
    """'DTSTART;TZID=X:2026...' -> ('DTSTART', '2026...')"""
    m = re.match(r"([A-Za-z0-9-]+)(;[^:]*)?:(.*)", line)
    if not m:
        return "", ""
    return m.group(1).upper(), m.group(3)


def _day(value):
    """The date part of an iCalendar time as an int YYYYMMDD, or None."""
    m = re.match(r"\s*(\d{8})", value)
    return int(m.group(1)) if m else None


def _keep_event(props, lo, hi):
    """Should a VEVENT with these properties stay for the window [lo, hi]?"""
    rrule = props.get("RRULE")
    start = _day(props.get("DTSTART", ""))
    end = _day(props.get("DTEND", "")) or start
    if "RECURRENCE-ID" in props:
        rid = _day(props["RECURRENCE-ID"])
        return (start is None or start <= hi) and (rid is None or rid >= lo or (end or 0) >= lo)
    if start is None:
        return True
    if rrule:
        m = re.search(r"UNTIL=(\d{8})", rrule.upper())
        if m and int(m.group(1)) < lo:
            return False        # finished repeating before the window
        return start <= hi
    return end >= lo and start <= hi


def _shift(day, days):
    d = datetime.date(day // 10000, day // 100 % 100, day % 100) + datetime.timedelta(days=days)
    return d.year * 10000 + d.month * 100 + d.day


def trim_ics(text, window):
    """Keep the calendar's header, its time zones, and the events that
    touch the window "YYYYMMDD-YYYYMMDD" (with two days to spare)."""
    m = re.fullmatch(r"\s*(\d{8})-(\d{8})\s*", window or "")
    if not m:
        return text
    lo, hi = _shift(int(m.group(1)), -2), _shift(int(m.group(2)), 2)
    nl = "\r\n" if "\r\n" in text[:2000] else "\n"
    out = []
    comp = None             # lines of the component being collected
    props = {}
    depth = 0
    skip = 0                # inside something we leave out
    for logical, raw in _unfold(text.splitlines()):
        name, value = _prop(logical)
        upper = logical.upper()
        if skip:
            if upper.startswith("BEGIN:"):
                skip += 1
            elif upper.startswith("END:"):
                skip -= 1
            continue
        if comp is None:
            if upper == "BEGIN:VEVENT":
                comp, props, depth = list(raw), {}, 1
            elif upper in ("BEGIN:VTODO", "BEGIN:VJOURNAL", "BEGIN:VFREEBUSY"):
                skip = 1
            else:
                out.extend(raw)
            continue
        if upper.startswith("BEGIN:"):
            depth += 1
        elif upper.startswith("END:"):
            depth -= 1
        if name in DROP_PROPS or (name == "ATTACH" and "BINARY" in logical[:80].upper()):
            continue
        comp.extend(raw)
        if depth == 1 and name and name not in props:
            props[name] = value
        if depth == 0:
            if _keep_event(props, lo, hi):
                out.extend(comp)
            comp = None
    return nl.join(out) + nl


# ---------------------------------------------------------------- the proxy

is_private = aeragate.is_private


class Config:
    allow = []              # client addresses allowed; empty = any on the private networks
    allow_local = False     # may requests go to servers on the local network?
    verbose = False
    timeout = 60
    insecure = False        # tests only: don't check certificates


def _dial(address, timeout=None, source_address=None):
    """http.client's connection to a server, made to the address that was
    checked: without --allow-local, never one on the home network, even if
    the name's answer changes between a look-up and the next"""
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
    """One kept-open connection per server, per thread."""

    def __init__(self):
        self.conns = {}

    def get(self, scheme, host, port):
        key = (scheme, host, port)
        c = self.conns.get(key)
        if c is None:
            if scheme == "https":
                ctx = aeragate.tls_context()
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


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ecalgate/" + VERSION

    where = "-"             # the server a request goes to, for the log

    def log_message(self, fmt, *args):
        if Config.verbose:
            aeragate.say("%s %s" % (self.address_string(), fmt % args))

    def log_request(self, code="-", size="-"):
        # the request line has the whole address, and a private calendar
        # feed's address is its password: note() logs the server only
        pass

    def note(self, status, size, more=""):
        if Config.verbose:
            aeragate.say("%s %s %s -> %d, %d bytes%s" % (
                self.address_string(), self.command, self.where, status, size, more))

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError, socket.timeout):
            pass        # the Atari went away (reset, switched off): nothing to say

    def __getattr__(self, name):
        # every method (GET, PUT, PROPFIND, REPORT, MKCALENDAR...) is proxied
        if name.startswith("do_"):
            return self.proxy
        raise AttributeError(name)

    def reply(self, status, text, ctype="text/plain; charset=utf-8", extra=None):
        body = text.encode("utf-8") if isinstance(text, str) else text
        self.note(status, len(body), ": " + text.strip() if status >= 400 and isinstance(text, str) else "")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or []):
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def client_ok(self):
        return aeragate.client_ok(self.client_address[0], Config.allow)

    def proxy(self):
        self.where = "-"
        if not self.client_ok():
            self.reply(403, "ecalgate: this address may not use the gateway\n")
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        if self.path.startswith("/"):
            self.reply(200, "ecalgate %s is running. Point ECal's gateway setting here.\n" % VERSION)
            return
        u = urlsplit(self.path)
        if u.scheme not in ("http", "https") or not u.hostname:
            self.reply(400, "ecalgate: expected a request like GET https://host/path\n")
            return
        port = u.port or (443 if u.scheme == "https" else 80)
        self.where = "%s://%s:%d" % (u.scheme, u.hostname, port)
        path = u.path or "/"
        if u.query:
            path += "?" + u.query
        headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_BY_HOP}
        headers["Host"] = u.netloc.rsplit("@", 1)[-1]
        headers["Accept-Encoding"] = "gzip, deflate"
        window = self.headers.get("X-Calendar-Window")
        for attempt in (1, 2):
            conn = UP.get(u.scheme, u.hostname, port)
            try:
                conn.request(self.command, path, body=body, headers=headers)
                resp = conn.getresponse()
                data = resp.read()
                break
            except ssl.SSLCertVerificationError as e:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(502, "ecalgate: %s has an untrusted certificate (%s)\n" % (u.hostname, e.verify_message))
                return
            except aeragate.HomeNetwork:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(403, "ecalgate: won't connect to the local network (%s)\n" % u.hostname)
                return
            except socket.gaierror:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(502, "ecalgate: can't find %s\n" % u.hostname)
                return
            except (http.client.RemoteDisconnected, BrokenPipeError, ConnectionResetError,
                    http.client.CannotSendRequest, http.client.ResponseNotReady):
                UP.drop(u.scheme, u.hostname, port)    # a kept connection went stale
                if attempt == 2:
                    self.reply(502, "ecalgate: %s closed the connection\n" % u.hostname)
                    return
            except (OSError, http.client.HTTPException) as e:
                UP.drop(u.scheme, u.hostname, port)
                self.reply(504 if isinstance(e, socket.timeout) else 502,
                           "ecalgate: can't reach %s (%s)\n" % (u.hostname, e))
                return
        enc = (resp.getheader("Content-Encoding") or "").lower()
        try:
            if enc == "gzip":
                data = gzip.decompress(data)
            elif enc == "deflate":
                try:
                    data = zlib.decompress(data)
                except zlib.error:
                    data = zlib.decompress(data, -zlib.MAX_WBITS)
        except (OSError, zlib.error):
            self.reply(502, "ecalgate: %s sent a broken compressed answer\n" % u.hostname)
            return
        ctype = resp.getheader("Content-Type") or ""
        before = len(data)
        if window and resp.status == 200 and ("calendar" in ctype.lower() or data[:15].upper().startswith(b"BEGIN:VCALENDAR")):
            data = trim_ics(data.decode("utf-8", "replace"), window).encode("utf-8")
        self.note(resp.status, len(data), " (trimmed from %d)" % before if len(data) != before else "")
        self.send_response(resp.status, resp.reason)
        for k, v in resp.getheaders():
            if k.lower() in HOP_BY_HOP or k.lower() in ("content-encoding", "set-cookie"):
                continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="HTTPS gateway for ECal on the Atari")
    ap.add_argument("--port", type=int, default=8180)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this Atari's address may use the gateway (may repeat)")
    ap.add_argument("--allow-local", action="store_true",
                    help="also forward to servers on the local network (a Radicale or Nextcloud at home)")
    ap.add_argument("--insecure", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    Config.allow = a.allow
    Config.allow_local = a.allow_local
    Config.verbose = a.verbose
    Config.insecure = a.insecure
    srv = ThreadingHTTPServer((a.bind, a.port), Handler)
    srv.daemon_threads = True
    aeragate.say("ecalgate %s listening on %s:%d" % (VERSION, a.bind, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
