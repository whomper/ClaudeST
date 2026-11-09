#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translategate - text translated on the Pi, for the Atari apps.

An app with its Translate setting On sends UTF-8 text; the Pi translates it
with a local engine (no text leaves the home network) and sends it back.
The protocol is AERA-Atari's standards/TRANSLATION-SERVICE.md; the client
is common/src/translate.c:

    GET  /status                    translategate 1 / engine NAME VERSION / ready
    GET  /languages                 CODE<TAB>NAME, one line a language
    POST /translate?to=CODE[&from=CODE]   the text as the body (8 KB at most)

  and for an app's Languages page (standards/TRANSLATION-SERVICE.md):

    GET  /languages?all=1           cap_mb N / used_mb N / CODE NAME MB installed|available
    POST /languages                 the languages wanted ("he,de"), sent on OK:
                                    202 "job N" (X-Translate-Job), a job installs and removes
    GET  /job                       job N / running + bytes D of T | done | failed: why

  answered by 200 with the translation (X-Translate-From: the language
  used), or, already in the target language, by 200 with X-Translate-Same: 1
  and no body; refused with one line "translategate: why" and 400, 413,
  422 (the pair isn't installed), 503 (loading or busy) or 504 (over 60 s).

The text is shaped here, once, for every app: blank-line paragraph breaks
come back exactly; the lines of a paragraph are joined and translated as
sentences, unless they are all short (an address, a list, a poem), when
each line is translated and kept; URLs, e-mail addresses, numbers with
units and quote markers ("> ") are kept out of the engine's way and put
back as they were.

The engine is a plug-in (ENGINES): it stays loaded between requests and is
asked one thing, translate(texts, src, dst). "stand-in" needs nothing and
shows the shaping (tests, CI); "argos" is Argos Translate's models run
by CTranslate2, in the venv `aera-gateway install translate` makes.

The rules are aeragate's: the home network only, or --allow; no connection
onwards. Nothing is kept: no text, no cache, no session. The log names who
asked for which pair, the sizes and the time, never a word of the text,
and neither does any error reply. A closed connection stops the job.

Standard Python 3 only, but for the argos engine's ctranslate2 and
sentencepiece in its own venv (Erez, 7 Nov 2026).
"""

import argparse
import json
import os
import re
import select
import socket
import socketserver
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl, urlsplit

import aeragate
import translatelangs

VERSION = "1.0"
PROTOCOL = "1"


class Config:
    allow = []
    max_bytes = 8192            # a request's body
    local_bytes = 256 << 10     # one on the local socket: a whole page or text, for webgate and pdfgate
    local_seconds = 600
    seconds = 60                # a request, then 504
    short = 300                 # a body this small has a worker of its own (a subject, a few words)
    workers = 2                 # requests translated at once, the short one's included
    verbose = False
    engine = None
    models = "/opt/translategate/models"
    state = "/var/lib/translategate"    # the languages' job: request (ours) and job (translatelangs')
    start_job = None                    # tests: run the job here (on the Gateway systemd starts it)
    restart = staticmethod(lambda: os._exit(0))     # systemd starts translategate again


CODE = re.compile(r"[a-z]{2,3}(-[A-Za-z0-9]{2,4})?")


class Stop(Exception):
    """the connection closed or the time ran out: nothing more is done"""


# ---------------------------------------------------------------- engines

class StandIn:
    """No model: a stand-in for the tests and CI. "Translating" puts the
    target code before the text and capitals its Latin letters, and, like
    some real engines, loses brackets, so what isn't protected shows."""
    name, version = "stand-in", "1"
    NAMES = {"en": "English", "he": "Hebrew", "de": "German"}

    def __init__(self, delay=0.0, load=0.0):
        self.delay = delay      # seconds a text takes (the tests' slow engine)
        self.ready_at = time.monotonic() + load

    def ready(self):
        return time.monotonic() >= self.ready_at

    def languages(self):
        return sorted(self.NAMES.items())

    def pairs(self):
        return {(a, b) for a in self.NAMES for b in self.NAMES if a != b}

    def detect(self, text):
        hebrew = sum(1 for c in text if "֐" <= c <= "׿")
        latin = sum(1 for c in text if c.isascii() and c.isalpha())
        if hebrew > latin:
            return "he"
        return "de" if re.search(r"\b(der|die|und|nicht|ist)\b", text, re.I) else "en"

    def translate(self, texts, src, dst, check=lambda: None):
        out = []
        for t in texts:
            check()
            if self.delay:
                end = time.monotonic() + self.delay
                while time.monotonic() < end:
                    check()
                    time.sleep(min(0.05, max(0.0, end - time.monotonic())))
            out.append("%s: %s" % (dst, re.sub(r"[\[\]{}<>()]", "", t).upper()) if t.strip() else t)
        return out


SENTENCE_END = re.compile(r"[.!?\u2026]+[\"'\u201d\u2019)\]]*\s+(?=[A-Z\u00c0-\u00de0-9\u0590-\u05ff\"'\u201c(\[])")
STOPWORDS = {
    "en": "the and of to is in that it you for with this are be not have",
    "de": "der die das und ist nicht ich sie mit den ein eine zu von auf",
    "fr": "le la les et est pas je vous une des du que qui pour dans",
    "es": "el la los las y es no que de en un una por con para",
    "it": "il la che e di non un una per sono con mi ti gli",
    "nl": "de het een en is niet ik je van op dat met voor zijn",
    "pt": "o a os as e que de do da em um uma para com nao",
}
SCRIPTS = (("he", "\u0590", "\u05ff"), ("ar", "\u0600", "\u06ff"), ("ru", "\u0400", "\u04ff"),
           ("el", "\u0370", "\u03ff"), ("ja", "\u3040", "\u30ff"), ("zh", "\u4e00", "\u9fff"))


def sentences(text):
    """a paragraph's sentences, for an engine that takes one at a time"""
    out, at = [], 0
    for m in SENTENCE_END.finditer(text):
        out.append(text[at:m.end()].strip())
        at = m.end()
    out.append(text[at:].strip())
    return [s for s in out if s]


def detect(text, codes):
    """the language of text, among codes: by its letters, then, for the
    Latin ones, by its commonest words"""
    counts = {code: sum(1 for c in text if lo <= c <= hi) for code, lo, hi in SCRIPTS}
    latin = sum(1 for c in text if c.isalpha() and c < "\u0250")
    code, n = max(counts.items(), key=lambda kv: kv[1])
    if n > latin and code in codes:
        return code
    words = re.findall(r"[a-z]+", text.lower())
    best, score = None, -1
    for c in codes:
        if c in STOPWORDS:
            stop = set(STOPWORDS[c].split())
            k = sum(1 for w in words if w in stop)
            if k > score or (k == score and c == "en"):
                best, score = c, k
    return best or ("en" if "en" in codes else sorted(codes)[0])


REPEAT = re.compile(r"(?<!\S)(\S+)(?:\s+\1)+(?!\S)")


def unrepeat(src, out):
    """a word said again and again where the source said it once: once
    (small models do this with a name or a word alone: "Dana" -> "Dana Dana")"""
    return out if REPEAT.search(src) else REPEAT.sub(r"\1", out)


PHRASES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "translate-phrases.tsv")


def load_phrases(path=PHRASES):
    """translate-phrases.tsv: {(from, to): {phrase in lower case: translation}}"""
    out = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                parts = line.rstrip("\n").split("\t")
                if len(parts) == 4:
                    out.setdefault((parts[0], parts[1]), {})[parts[2].strip().lower()] = parts[3].strip()
    except OSError:
        pass
    return out


CLOSINGS = load_phrases()
SHORT_WORDS = 4
CLOSING_PUNCT = re.compile(r"^(.*?)([\s,.!:;\u2026]*)$", re.S)


def closing(src, dst, text):
    """the closing's words, its own punctuation after them, or None"""
    words, punct = CLOSING_PUNCT.match(text).groups()
    got = CLOSINGS.get((src, dst), {}).get(words.strip().lower())
    return got + punct if got else None


def short(text):
    """a few words with no sentence end (a subject, a name, words picked
    out): the models do these far better as a sentence, so a full stop is
    put after them, and taken off the answer again"""
    return len(text.split()) <= SHORT_WORDS and not re.search(r"[.!?\u2026:;,]$", text)


class Argos:
    """Argos Translate's models (OpenNMT, one a pair) run by CTranslate2,
    from the venv `aera-gateway install translate` made: loaded once, in
    the background (503 until then), kept loaded between requests"""
    name = "argos"
    BEAM = 4
    BATCH = 8               # sentences the engine is given at once, between looks at the socket

    def __init__(self, models="/opt/translategate/models", threads=2, workers=2):
        self.version = "?"
        self.pair = {}
        self.names = {}
        self.loaded = False
        threading.Thread(target=self.load, args=(models, threads, workers), daemon=True).start()

    def load(self, models, threads, workers):
        import glob
        import json
        import os
        try:
            import ctranslate2
            import sentencepiece
            versions = set()
            for meta in sorted(glob.glob(os.path.join(models, "*", "metadata.json"))):
                d = os.path.dirname(meta)
                with open(meta, encoding="utf-8") as f:
                    m = json.load(f)
                tr = ctranslate2.Translator(os.path.join(d, "model"), device="cpu",
                                            inter_threads=workers, intra_threads=threads)
                sp = sentencepiece.SentencePieceProcessor(model_file=os.path.join(d, "sentencepiece.model"))
                self.pair[(m["from_code"], m["to_code"])] = (tr, sp)
                self.names[m["from_code"]], self.names[m["to_code"]] = m["from_name"], m["to_name"]
                versions.add(str(m.get("package_version", "?")))
            self.version = "/".join(sorted(versions)) or "none"
            aeragate.say("translategate: argos %s, CTranslate2 %s, %d pairs: %s" % (
                self.version, ctranslate2.__version__, len(self.pair),
                " ".join("%s>%s" % p for p in sorted(self.pair))))
            if self.pair:
                self.loaded = True
        except Exception as e:
            aeragate.say("translategate: the engine didn't load (%s)" % type(e).__name__)

    def ready(self):
        return self.loaded

    def languages(self):
        return sorted(self.names.items())

    def pairs(self):
        """the installed pairs, and those through English (he>en>de)"""
        direct = set(self.pair)
        return direct | {(a, b) for a, x in direct for y, b in direct if x == "en" == y and a != b}

    def detect(self, text):
        # among every language detect() knows, not only the installed
        # sources: German with en,he installed is German (422, the pair
        # isn't installed), never "already English"
        return detect(text, set(STOPWORDS) | {code for code, _, _ in SCRIPTS} | {a for a, _ in self.pair})

    def translate(self, texts, src, dst, check=lambda: None):
        if (src, dst) not in self.pair:         # through English
            return self.translate(self.translate(texts, src, "en", check), "en", dst, check)
        tr, sp = self.pair[(src, dst)]
        out = []
        for t in texts:
            check()
            ss = sentences(t)
            done = [closing(src, dst, x) for x in ss]
            todo = [i for i, x in enumerate(done) if x is None]
            for at in range(0, len(todo), self.BATCH):
                batch = todo[at:at + self.BATCH]
                feed = [ss[i] + "." if short(ss[i]) else ss[i] for i in batch]
                toks = [sp.encode(x, out_type=str)[:400] for x in feed]
                r = tr.translate_batch(toks, beam_size=self.BEAM,
                                       max_decoding_length=min(512, 2 * max(len(x) for x in toks) + 16))
                for i, x in zip(batch, r):
                    got = unrepeat(ss[i], sp.decode(x.hypotheses[0]))
                    if short(ss[i]):
                        got = re.sub(r"[.\u3002]$", "", got.rstrip())
                    done[i] = got
                check()
            out.append(" ".join(done))
        return out


ENGINES = {"stand-in": StandIn, "argos": Argos}


# ---------------------------------------------------------------- the text's shape

SHORT_LINE = 40
PARA_BREAK = re.compile(r"(\r?\n(?:[ \t]*\r?\n)+)")
QUOTE = re.compile(r"^((?:[ \t]*>)+[ \t]?)")
KEEP = re.compile(
    r"(?:https?|ftp)://[^\s<>\"']+"
    r"|www\.[^\s<>\"']+"
    r"|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}"
    r"|[$€£₪]\s?\d+(?:[.,]\d+)*"
    r"|\d+(?:[.,:]\d+)*\s?(?:%|°[CF]?|[kKMGT]?B\b|[kmcm]?m\b|km/h|kg\b|g\b|mg\b|l\b|ml\b"
    r"|h\b|min\b|s\b|ms\b|[kMG]?Hz\b|[kKM]?W\b|V\b|A\b|mAh\b|EUR\b|USD\b|ILS\b|NIS\b|GBP\b"
    r"|[€₪$£]|ש״ח|ש\"ח|שקל(?:ים)?)")
URL_TAIL = ".,;:!?)]}'\""
# what stands for a kept piece while the engine works: letters and digits only
# (brackets and markup are what engines lose), matched again whatever the
# engine did to its case or spacing
MARK = "ZQX%dQZ"
MARK_BACK = re.compile(r"Z\s*Q\s*X\s*(\d+)\s*Q\s*Z", re.I)


def protect(text):
    """(text with each kept piece as a mark, the pieces)"""
    kept = []

    def one(m):
        s = m.group(0)
        tail = ""
        if s[:1].isalpha() and ("://" in s or s.lower().startswith("www.")):
            while s and s[-1] in URL_TAIL:
                pair = {")": "(", "]": "[", "}": "{"}.get(s[-1])
                if pair and s.count(pair) >= s.count(s[-1]):
                    break               # a bracket the URL opened: its own
                tail = s[-1] + tail
                s = s[:-1]
        kept.append(s)
        return MARK % (len(kept) - 1) + tail
    return KEEP.sub(one, text), kept


def restore(text, kept):
    """the marks back as the pieces; a piece the engine lost goes at the end"""
    used = set()

    def one(m):
        i = int(m.group(1))
        if i >= len(kept) or i in used:
            return ""
        used.add(i)
        return kept[i]
    text = MARK_BACK.sub(one, text)
    lost = [kept[i] for i in range(len(kept)) if i not in used]
    return (text.rstrip() + " " + " ".join(lost)) if lost else text


def plan(text):
    """the request as (pieces, rebuild): the pieces for the engine, and a
    function that puts their translations back into the request's shape"""
    parts = PARA_BREAK.split(text)      # paragraph, break, paragraph, ...
    pieces, slots = [], []

    def piece(s):
        lead = s[:len(s) - len(s.lstrip())]
        trail = s[len(s.rstrip()):]
        core = s.strip()
        if not core:
            return lambda got: s
        body, kept = protect(core)
        if not re.search(r"\w", MARK_BACK.sub("", body)):
            return lambda got: s        # nothing to translate: only kept pieces
        n = len(pieces)
        pieces.append(body)
        return lambda got: lead + restore(got[n], kept) + trail

    for i, part in enumerate(parts):
        if i % 2:                       # a paragraph break: back as it was
            slots.append(lambda got, part=part: part)
            continue
        # the text's own line ends before and after: back as they were
        head = re.match(r"[\r\n]*", part).group(0)
        tail = re.search(r"[\r\n]*\Z", part[len(head):]).group(0)
        slots.append(lambda got, head=head: head)
        part = part[len(head):len(part) - len(tail)]
        if part:
            shape(part, piece, slots)
        slots.append(lambda got, tail=tail: tail)

    def rebuild(got):
        return "".join(s(got) for s in slots)
    return pieces, rebuild


def shape(part, piece, slots):
    """one paragraph (no line ends before or after it) into slots"""
    lines = re.split(r"(\r?\n)", part)
    texts, ends = lines[0::2], lines[1::2] + [""]
    quotes = [QUOTE.match(t) for t in texts]
    prefix = quotes[0].group(1) if quotes[0] else ""
    same_quote = all((q.group(1) if q else "") == prefix for q in quotes)
    inner = [t[len(prefix):] for t in texts] if same_quote else texts
    if len(texts) > 1 and same_quote and any(len(t.strip()) >= SHORT_LINE for t in inner):
        # a paragraph of running text: one piece, answered on one line
        joined = " ".join(t.strip() for t in inner if t.strip())
        f = piece(joined)
        slots.append(lambda got, f=f, prefix=prefix: prefix + f(got))
    else:
        # short lines (or one line): each translated, the lines kept
        fs = []
        for t, end in zip(texts, ends):
            q = QUOTE.match(t)
            pre = q.group(1) if q else ""
            fs.append((pre, piece(t[len(pre):]), end))
        slots.append(lambda got, fs=fs: "".join(pre + f(got) + end for pre, f, end in fs))


# ---------------------------------------------------------------- the service

class Lanes:
    """Config.workers at once; one of them only for short requests, so a
    subject is answered while a long letter is being translated"""

    def __init__(self, workers):
        self.cond = threading.Condition()
        self.workers = max(2, workers)
        self.busy = 0
        self.short_busy = False

    def take(self, short, deadline, check):
        with self.cond:
            while True:
                if self.busy < self.workers - 1:
                    self.busy += 1
                    return "any"
                if short and not self.short_busy:
                    self.short_busy = True
                    return "short"
                left = deadline - time.monotonic()
                if left <= 0:
                    raise Stop("time")
                self.cond.wait(min(left, 0.2))
                check()

    def give(self, lane):
        with self.cond:
            if lane == "short":
                self.short_busy = False
            else:
                self.busy -= 1
            self.cond.notify_all()


LANES = Lanes(Config.workers)


def closed(sock):
    """has the app closed the connection? (it sends nothing after its body)"""
    try:
        r, _, _ = select.select([sock], [], [], 0)
        if not r:
            return False
        return sock.recv(1, socket.MSG_PEEK) == b""
    except (OSError, ValueError):
        return True


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "translategate/" + VERSION
    sys_version = ""
    timeout = 30                        # reading the request

    # Python's own lines name the request; ours name only sizes
    def log_message(self, fmt, *args):
        pass

    def send_error(self, code, message=None, explain=None):
        self.reply(code if code in (400, 413) else 400, "bad request")

    def note(self, text):
        aeragate.say("translategate: %s %s" % (self.who(), text))

    def who(self):
        return self.client_address[0]

    def max_bytes(self):
        return Config.max_bytes

    def seconds(self):
        return Config.seconds

    def reply(self, status, why, headers=(), body=None):
        data = body if body is not None else ("translategate: %s\n" % why).encode()
        self.close_connection = True
        try:
            self.send_response_only(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            for k, v in headers:
                self.send_header(k, v)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)
            self.wfile.flush()
        except OSError:
            pass

    def refuse(self, status, why, n=0, pair="", headers=()):
        self.reply(status, why, headers=headers)
        self.note("%s%d, %d bytes (%s)" % (pair + " " if pair else "", status, n, why))

    def allowed(self):
        if aeragate.client_ok(self.client_address[0], Config.allow):
            return True
        self.refuse(403, "not from this computer")
        return False

    def do_GET(self):
        if not self.allowed():
            return
        eng = Config.engine
        path = urlsplit(self.path).path
        if path == "/status":
            text = "translategate %s\nengine %s %s\n%s\n" % (
                PROTOCOL, eng.name, eng.version, "ready" if eng.ready() else "loading")
        elif path == "/languages" and urlsplit(self.path).query == "all=1":
            # the Languages page: every language that can be had, its MB,
            # whether it is here; the room for them (translatelangs.CAP_MB)
            here = translatelangs.installed(Config.models)
            rows = translatelangs.table()
            text = "cap_mb %d\nused_mb %d\n" % (translatelangs.CAP_MB, translatelangs.size_mb(here, rows))
            text += "".join("%s\t%s\t%d\t%s\n" % (c, n, mb, "installed" if c in here else "available")
                            for c, n, mb in rows)
        elif path == "/job":
            jid, state, job = Jobs.current()
            text = "job %d\n" % jid
            if state == "running":
                text += "running\nbytes %d of %d\n" % (job.get("done", 0), job.get("total", 0))
            elif state == "failed":
                text += "failed: %s\n" % job.get("why", "")
            else:
                text += state + "\n"
        elif path == "/languages":
            text = "".join("%s\t%s\n" % kv for kv in eng.languages()) if eng.ready() else ""
            if not eng.ready():
                self.refuse(503, "the engine is loading")
                return
        else:
            self.refuse(404, "no such page")
            return
        self.reply(200, "", body=text.encode())
        if Config.verbose:
            self.note("%s" % path)

    def do_POST(self):
        if not self.allowed():
            return
        url = urlsplit(self.path)
        if url.path == "/languages":
            self.languages()
            return
        if url.path != "/translate":
            self.refuse(404, "no such page")
            return
        q = {}
        for k, v in parse_qsl(url.query, keep_blank_values=True):
            if k not in ("to", "from") or k in q:
                self.refuse(400, "no such option")
                return
            q[k] = v
        to, src = q.get("to", ""), q.get("from", "")
        if not CODE.fullmatch(to) or (src and not CODE.fullmatch(src)):
            self.refuse(400, "a language code isn't right")
            return
        size = self.headers.get("Content-Length", "")
        if not size.isdigit():
            self.refuse(400, "no Content-Length")
            return
        size = int(size)
        if size > self.max_bytes():
            self.refuse(413, "the text is over %d KB" % (self.max_bytes() >> 10), size)
            return
        data = self.rfile.read(size)
        if len(data) < size:
            self.note("dropped, %d bytes" % len(data))
            return
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            self.refuse(400, "the text isn't UTF-8", size)
            return
        del data
        eng = Config.engine
        if not eng.ready():
            self.refuse(503, "the engine is loading", size)
            return
        t0 = time.monotonic()
        used = src or eng.detect(text)
        pair = "%s>%s" % (used, to)
        if used == to:
            self.reply(200, "", headers=[("X-Translate-From", used), ("X-Translate-Same", "1")], body=b"")
            self.note("%s same, %d bytes" % (pair, size))
            return
        langs = dict(eng.languages())
        if used not in langs or to not in langs or (used, to) not in eng.pairs():
            # (X-Translate-From: the language found, so the app can name it)
            self.refuse(422, "that pair of languages isn't installed", size, pair,
                        headers=[("X-Translate-From", used)])
            return
        deadline = t0 + self.seconds()
        sock = self.connection

        def check():
            if closed(sock):
                raise Stop("dropped")
            if time.monotonic() > deadline:
                raise Stop("time")
        try:
            pieces, rebuild = plan(text)
            lane = LANES.take(size < Config.short, deadline, check)
            try:
                got = eng.translate(pieces, used, to, check) if pieces else []
            finally:
                LANES.give(lane)
            check()
            out = rebuild(got).encode("utf-8")
        except Stop as e:
            if str(e) == "dropped":
                self.note("%s dropped, %d bytes" % (pair, size))
            else:
                self.refuse(504, "it took over %d s" % self.seconds(), size, pair)
            return
        except Exception as e:          # the engine's words could hold the text: its name only
            self.refuse(500, "the engine failed", size, pair + " (%s)" % type(e).__name__)
            return
        if len(out) > 4 * size + 64:
            self.refuse(422, "the translation came out too long", size, pair)
            return
        self.reply(200, "", headers=[("X-Translate-From", used)], body=out)
        self.note("%s 200, %d bytes in, %d out, %.1f s" % (pair, size, len(out), time.monotonic() - t0))


def _languages(self):
    """POST /languages: the set of languages wanted (codes, by commas or
    lines), sent once when the Languages page closes with OK: installed
    and removed by a job; answered at once with its number"""
    size = self.headers.get("Content-Length", "")
    if not size.isdigit() or int(size) > 4096:
        self.refuse(400, "no Content-Length" if not size.isdigit() else "too long")
        return
    try:
        codes = set(re.split(r"[\s,]+", self.rfile.read(int(size)).decode("ascii").strip())) - {""}
    except UnicodeDecodeError:
        codes = {"?"}
    rows = translatelangs.table()
    known = {c for c, _, _ in rows}
    if not all(CODE.fullmatch(c) and c in known for c in codes):
        self.refuse(400, "a language code isn't right")
        return
    want = codes | {"en"}
    need = translatelangs.size_mb(want, rows)
    if need > translatelangs.CAP_MB:
        self.refuse(422, "those languages take %d MB; %d MB fit" % (need, translatelangs.CAP_MB))
        return
    if want == translatelangs.installed(Config.models):
        self.reply(200, "", body=b"job 0\ndone\n")
        return
    try:
        jid = Jobs.start(want)
    except OSError as e:
        self.refuse(503, "can't start the job (%s)" % type(e).__name__)
        return
    if jid is None:
        self.refuse(503, "a languages job is running")
        return
    self.reply(202, "", headers=[("X-Translate-Job", str(jid))], body=("job %d\n" % jid).encode())
    self.note("languages job %d: %s" % (jid, ",".join(sorted(want))))


class Jobs:
    """the languages' job: one at a time, started by POST /languages,
    done by translatelangs (translategate-langs.service); when it ends
    with the models changed, translategate starts again with them"""
    lock = threading.Lock()
    started = None

    @staticmethod
    def read(name):
        try:
            with open(os.path.join(Config.state, name)) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    @classmethod
    def current(cls):
        """(id, state, the job's dict)"""
        req, job = cls.read("request") or {}, cls.read("job") or {}
        rid, jid = req.get("id", 0), job.get("id", 0)
        if rid > jid:
            return rid, "running", {"done": 0, "total": 0}
        return jid, job.get("state", "none") if jid else "none", job

    @classmethod
    def start(cls, want):
        with cls.lock:
            jid, state, _ = cls.current()
            if state == "running":
                return None
            jid += 1
            path = os.path.join(Config.state, "request")
            with open(path + ".new", "w") as f:
                json.dump({"id": jid, "want": sorted(want)}, f)
            os.replace(path + ".new", path)
            cls.started = jid
        if Config.start_job:
            Config.start_job()
        return jid

    @classmethod
    def watch(cls):
        while True:
            time.sleep(2)
            cls.check()

    @classmethod
    def check(cls):
        if cls.started is None:
            return
        jid, state, job = cls.current()
        if jid == cls.started and state in ("done", "failed"):
            cls.started = None
            if job.get("changed"):
                aeragate.say("translategate: the languages changed; starting again with them")
                Config.restart()


Handler.languages = _languages


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # never the traceback: its lines and values could quote the text
        who = client_address[0] if isinstance(client_address, tuple) else "local"
        aeragate.say("translategate: %s error (%s)" % (who, sys.exc_info()[0].__name__))


class LocalHandler(Handler):
    """the local socket: the Gateway's own gateways (webgate, pdfgate,
    through translateclient), on the same machine. The same /translate,
    /status and /languages; a whole page or text at once (it is shaped
    and translated here a paragraph at a time), with longer to do it;
    nothing of the Languages page"""

    def allowed(self):
        if self.command == "POST" and urlsplit(self.path).path != "/translate" or \
                urlsplit(self.path).path == "/job" or "all=1" in urlsplit(self.path).query:
            self.refuse(404, "no such page")
            return False
        return True                     # who may connect is the socket's group and mode

    def who(self):
        return "local"

    def max_bytes(self):
        return Config.local_bytes

    def seconds(self):
        return Config.local_seconds


class LocalServer(socketserver.ThreadingUnixStreamServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        aeragate.say("translategate: local error (%s)" % sys.exc_info()[0].__name__)


def serve_local(path):
    """the local socket at path, for the gateways' users only (0660; the
    group is the unit's, aera-translate)"""
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    old = os.umask(0o117)
    try:
        srv = LocalServer(path, LocalHandler)
    finally:
        os.umask(old)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    aeragate.say("translategate: local socket %s" % path)
    return srv


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="text translated on the Pi for the Atari apps")
    ap.add_argument("--port", type=int, default=8480)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this Atari's address may use the gateway (may repeat)")
    ap.add_argument("--engine", choices=sorted(ENGINES), default="stand-in")
    ap.add_argument("--models", default="/opt/translategate/models", help="argos: its models, one folder a pair")
    ap.add_argument("--state", default="/var/lib/translategate", help="the languages' job's files")
    ap.add_argument("--workers", type=int, default=2, help="requests at once (one kept for short ones)")
    ap.add_argument("--time", type=int, default=60, help="seconds a request may take")
    ap.add_argument("--local", help="a Unix socket for the Gateway's own gateways (translateclient), "
                    "e.g. /run/translategate/local.sock")
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    global LANES
    Config.allow = a.allow
    Config.seconds = a.time
    Config.workers = a.workers
    Config.verbose = a.verbose
    LANES = Lanes(a.workers)
    Config.models, Config.state = a.models, a.state
    if Jobs.current()[1] == "running":  # a job translategate's start cut short: picked up again
        Jobs.started = Jobs.current()[0]
    threading.Thread(target=Jobs.watch, daemon=True).start()
    srv = Server((a.bind, a.port), Handler)
    if a.local:
        serve_local(a.local)
    aeragate.say("translategate %s listening on %s:%d" % (VERSION, a.bind, srv.server_address[1]))
    Config.engine = Argos(a.models, workers=Config.workers) if a.engine == "argos" else StandIn()
    if a.engine != "argos":             # (argos says its own once it has loaded)
        aeragate.say("translategate: engine %s %s" % (Config.engine.name, Config.engine.version))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
