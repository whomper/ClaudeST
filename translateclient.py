#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translateclient - translategate for the Gateway's own gateways.

webgate and pdfgate translate on the Gateway (Erez, 8 Nov 2026) through
translategate's local socket: the same machine, one engine with its
models loaded once, translategate the only process that has them. The
words are POST /translate's (AERA-Atari standards/TRANSLATION-SERVICE.md):
the text shaped the same way, the same refusals.

    translate(text, to, src=None) -> (translation, from, same)
    translate_parts(parts, to, src=None, seconds=30) -> ([translations], from, done)

It is also aerapdf's translator (EPDF's docs/PI-GATEWAY.md, "aerapdf and
the translator"), run by pdfgate's aerapdf once per page:

    translateclient.py --to CODE  < paragraphs  > paragraphs

the page's paragraphs in UTF-8, an empty line between them, translated
in the same order and number (those the time didn't reach as they came);
first on its error output "from CODE", or "same CODE" when the page is in
CODE already (and nothing out), then "done N of M" for a page done in
part. Exit 0 done; 2 the pair isn't installed (422), 3 busy, loading or
no translategate (503), 4 over the time (504), 1 anything else. Its time
for a page is PAGE_SECONDS, and never past TRANSLATECLIENT_UNTIL (pdfgate
sets it from the conversion's own time: pages after it come as they
were). TRANSLATECLIENT_NOTE, a file of pdfgate's for this conversion:
the language last seen, so a refusal can say which pair.
(TRANSLATECLIENT_SOCKET: another socket, a test's.)

Refused(status, line) is raised with translategate's words ("that pair
of languages isn't installed", 503 while it loads, 504 over its time);
Unreachable when there is no translategate. Nothing is kept or logged
here. Standard Python 3 only.
"""

import os
import re
import socket
import sys
import time

SOCKET = "/run/translategate/local.sock"
GRACE = 15                          # seconds a piece begun in time may run past it
PAGE_SECONDS = 60                   # a PDF page's time (aerapdf's translator)


class Refused(Exception):
    """translategate's refusal: status (400, 413, 422, 503, 504, 500) and
    its one line, "translategate: why", as the Atari would show it"""

    def __init__(self, status, line, used=None):
        super().__init__("%d %s" % (status, line))
        self.status, self.line, self.used = status, line, used


class Unreachable(Exception):
    """no translategate on this machine (not installed, or starting)"""


class TimedOut(Unreachable):
    """translategate didn't answer in the time given (the connection is
    closed, which stops its job)"""


def ask(method, path, body=b"", sock=SOCKET, timeout=620):
    """(status, headers in lower case, body) for one request"""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(sock)
    except OSError as e:
        s.close()
        raise Unreachable("translategate isn't there (%s)" % type(e).__name__)
    try:
        head = "%s %s HTTP/1.0\r\nHost: translategate\r\n" % (method, path)
        if method == "POST":
            head += "Content-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\n" % len(body)
        s.sendall(head.encode() + b"\r\n" + body)
        got = b""
        while True:
            b = s.recv(65536)
            if not b:
                break
            got += b
    except socket.timeout:
        raise TimedOut("translategate didn't answer in %d s" % timeout)
    except OSError as e:
        raise Unreachable("translategate stopped answering (%s)" % type(e).__name__)
    finally:
        s.close()
    top, _, data = got.partition(b"\r\n\r\n")
    lines = top.decode("latin-1").split("\r\n")
    try:
        status = int(lines[0].split()[1])
    except (IndexError, ValueError):
        raise Unreachable("that isn't translategate")
    hd = {}
    for line in lines[1:]:
        k, _, v = line.partition(":")
        hd[k.strip().lower()] = v.strip()
    return status, hd, data


def translate(text, to, src=None, sock=SOCKET, timeout=620):
    """text (str) into the language to: (translation, the language it was
    in, same). same: it was in that language already, and translation is
    text as it came"""
    path = "/translate?to=%s" % to + ("&from=%s" % src if src else "")
    status, hd, data = ask("POST", path, text.encode("utf-8"), sock, timeout)
    used = hd.get("x-translate-from")
    if status != 200:
        raise Refused(status, data.decode("utf-8", "replace").strip(), used)
    if hd.get("x-translate-same") == "1":
        return text, used, True
    return data.decode("utf-8"), used, False


def translate_parts(parts, to, src=None, seconds=30, sock=SOCKET, piece=2048, stop=lambda: False):
    """a page's or a text's parts (paragraphs, in order) translated as
    far as seconds allow: (translations of the first done parts, the
    language, done). Parts go a few at a time, about piece bytes, so the
    first ones come back whatever the rest would take; the caller leaves
    parts[done:] as they were and says how much was translated (Erez,
    8 Nov 2026: time is the limit). A piece still at work when the time
    is up is dropped (TimedOut for the first: nothing was done). stop():
    True when the answer is no longer wanted (asked between pieces)"""
    out, used, end = [], src, time.monotonic() + seconds
    i = 0
    while i < len(parts) and time.monotonic() < end and not stop():
        j, size = i, 0
        while j < len(parts) and (j == i or size + len(parts[j].encode("utf-8")) <= piece):
            size += len(parts[j].encode("utf-8")) + 2
            j += 1
        wait = max(1.0, end - time.monotonic()) + GRACE
        try:
            text, used_now, same = translate("\n\n".join(parts[i:j]), to, used, sock, wait)
            got = parts[i:j] if same else text.split("\n\n")
            if len(got) != j - i:       # (the shaping keeps paragraphs; if not, one at a time)
                got = [translate(p, to, used_now, sock, wait)[0] for p in parts[i:j]]
        except TimedOut:
            if not out:
                raise
            break
        used = used or used_now
        out += got
        i = j
    return out, used, len(out)


# ------------------------------------------------- aerapdf's translator

def note(path, used=None):
    """the language last seen in this conversion (pdfgate's file): read, or
    written when used is given"""
    if not path:
        return None
    try:
        if used:
            with open(path, "w") as f:
                f.write(used + "\n")
            return used
        with open(path) as f:
            got = f.read().strip()
        return got if re.fullmatch(r"[a-z]{2,3}", got) else None
    except OSError:
        return None


def page(argv=None, stdin=None, stdout=None, stderr=None, env=None, sock=SOCKET, now=time.time):
    """one PDF page for aerapdf: the exit status"""
    argv = sys.argv[1:] if argv is None else argv
    stdin = stdin or sys.stdin.buffer
    stdout, stderr = stdout or sys.stdout.buffer, stderr or sys.stderr
    env = os.environ if env is None else env
    if len(argv) != 2 or argv[0] != "--to" or not re.fullmatch(r"[a-z]{2,3}", argv[1]):
        stderr.write("usage: translateclient.py --to CODE  < paragraphs  > paragraphs\n")
        return 1
    to = argv[1]
    sock = env.get("TRANSLATECLIENT_SOCKET") or sock     # (a test's translategate)
    try:
        text = stdin.read().decode("utf-8")
    except UnicodeDecodeError:
        return 1
    parts = [p.strip() for p in re.split(r"\n[ \t\r]*\n", text) if p.strip()]
    kept = env.get("TRANSLATECLIENT_NOTE")
    seen = note(kept)
    seconds = PAGE_SECONDS
    try:
        until = float(env.get("TRANSLATECLIENT_UNTIL", ""))
        seconds = min(seconds, until - now())
    except ValueError:
        pass
    if not parts:
        stderr.write("same %s\n" % to)
        return 0
    if seconds <= 0:
        # (the conversion's time is nearly gone: this page as it was)
        stdout.write("\n\n".join(parts).encode("utf-8") + b"\n")
        stderr.write("from %s\ndone 0 of %d\n" % (seen or "und", len(parts)))
        return 0
    try:
        out, used, done = translate_parts(parts, to, seconds=seconds, sock=sock)
    except Refused as e:
        note(kept, e.used)
        return {422: 2, 503: 3, 504: 4}.get(e.status, 1)
    except TimedOut:
        return 4
    except Unreachable:
        return 3
    if not done:
        return 4
    note(kept, used)
    if used == to:
        stderr.write("same %s\n" % to)
        return 0
    stdout.write("\n\n".join(out + parts[done:]).encode("utf-8") + b"\n")
    stderr.write("from %s\n" % used)
    if done < len(parts):
        stderr.write("done %d of %d\n" % (done, len(parts)))
    return 0


if __name__ == "__main__":
    sys.exit(page())
