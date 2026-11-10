#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A stand-in for AERA-Gateway's translategate, for the Languages dialog's rig (run.py): the three
requests of the Languages page, answered as gateway/translategate.py (AERA-Gateway, 7 Nov 2026) does:

    GET  /languages?all=1   cap_mb N / used_mb N / CODE<TAB>NAME<TAB>MB<TAB>installed|available
    POST /languages         the wanted set ("he,de"): 202 "job N" (X-Translate-Job), 200 "job 0 / done" when
                            it is the set already there, 400 a code that isn't one, 422 over the room,
                            503 while a job runs; refusals are one line "translategate: why"
    GET  /job               job N / running + bytes D of T | done | failed: why | none
    POST /translate?to=CODE the text (UTF-8, paragraphs between blank lines) "translated": as many
                            paragraphs back, each a fixed sentence in CODE, so an app's rig can look at
                            how it draws the language. Hebrew (he) gets a right-to-left sentence with a
                            Latin run and a number in it ("Atari 2.13"), the bidi cases; any other
                            installed language gets "CODE: " and the text in capitals. 200 with
                            X-Translate-From (he for a text with Hebrew letters, else en), 200 with
                            X-Translate-Same: 1 and no body when it is already in CODE, 422 when CODE
                            (or the pair) isn't installed. No engine: the stand-in's own words only.

The table is gateway/translate-languages.tsv's (50 rows, the MB both models take to download). The job
is played by the clock: RATE MB a second, then the set wanted is the set installed. Everything it was
asked is kept in `log` (method, path, body), so a test can say "no POST was made" or "one POST, he,de".

An app's own rig uses it as its translategate on the Atari's port 8480 (EMail's
tests/hatari/langs_test.py, and translate_test.py for a right-to-left look):

    sys.path.insert(0, os.path.join(ROOT, "common", "tests", "langs"))
    import fakegate
    g = fakegate.Gate(port=8480, installed=("he",))   (Hebrew installed: /translate?to=he answers in it)
    g.start(); ... boot the app with FAKESTNG.PRG, translate into Hebrew, take the screenshot ...; g.stop()

    import fakegate
    g = fakegate.Gate(port=0, cap_mb=600)       (port 0: a free one, g.port)
    g.start() ... g.stop()
    g.delay_list = 30        (seconds the list takes: a stop is tried against it)
    g.post_status = 503      (the next POSTs are refused with this status)
"""
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROWS = [
    ("en", "English", 0),
    ("ar", "Arabic", 162),
    ("az", "Azerbaijani", 234),
    ("bg", "Bulgarian", 138),
    ("bn", "Bengali", 135),
    ("ca", "Catalan", 141),
    ("cs", "Czech", 132),
    ("da", "Danish", 130),
    ("de", "German", 287),
    ("el", "Greek", 135),
    ("eo", "Esperanto", 233),
    ("es", "Spanish", 355),
    ("et", "Estonian", 298),
    ("eu", "Basque", 144),
    ("fa", "Persian", 231),
    ("fi", "Finnish", 132),
    ("fr", "French", 126),
    ("ga", "Irish", 185),
    ("gl", "Galician", 145),
    ("he", "Hebrew", 230),
    ("hi", "Hindi", 199),
    ("hu", "Hungarian", 133),
    ("id", "Indonesian", 130),
    ("it", "Italian", 167),
    ("ja", "Japanese", 227),
    ("ko", "Korean", 229),
    ("ky", "Kyrgyz", 295),
    ("lt", "Lithuanian", 133),
    ("lv", "Latvian", 133),
    ("ms", "Malay", 132),
    ("nb", "Norwegian", 127),
    ("nl", "Dutch", 135),
    ("pb", "Portuguese (Brazil)", 129),
    ("pl", "Polish", 133),
    ("pt", "Portuguese", 129),
    ("ro", "Romanian", 135),
    ("ru", "Russian", 336),
    ("sk", "Slovak", 136),
    ("sl", "Slovenian", 135),
    ("sq", "Albanian", 134),
    ("sv", "Swedish", 193),
    ("sw", "Swahili", 144),
    ("th", "Thai", 132),
    ("tl", "Tagalog", 134),
    ("tr", "Turkish", 234),
    ("uk", "Ukrainian", 187),
    ("ur", "Urdu", 132),
    ("vi", "Vietnamese", 127),
    ("zh", "Chinese", 138),
    ("zt", "Chinese (traditional)", 139),
]
CODE = re.compile(r"[a-z]{2,3}(-[A-Za-z0-9]{2,4})?")

# /translate's words, the stand-in's own (no engine): a long sentence where the
# answer may be long, a short one for a word or two (the apps take at most 4 n + 64
# bytes back for n sent)
SAID = {
    "he": ("\u05e9\u05dc\u05d5\u05dd \u05dc\u05db\u05d5\u05dc\u05dd, \u05d6\u05d4\u05d5 "
           "\u05ea\u05e8\u05d2\u05d5\u05dd \u05dc\u05e2\u05d1\u05e8\u05d9\u05ea \u05e9\u05dc "
           "Atari 2.13 \u05e2\u05dc \u05d4\u05de\u05e1\u05da.",
           "\u05e9\u05dc\u05d5\u05dd, Atari 2.13"),
}


def said(code, text):
    """text (paragraphs between blank lines) "translated" into code by the stand-in: as many
    paragraphs, all the long sentence if the answer may be that long, else all the short one"""
    paras = text.split("\n\n")
    room = 4 * len(text.encode()) + 64
    if code not in SAID:
        return "\n\n".join("%s: %s" % (code, p.upper()) for p in paras)
    for t in SAID[code]:
        out = "\n\n".join([t] * len(paras))
        if len(out.encode()) <= room:
            return out
    return "\n\n".join([SAID[code][1][:1]] * len(paras))


def has_hebrew(text):
    return any("\u05d0" <= ch <= "\u05ea" for ch in text)


class Gate:
    def __init__(self, port=18480, cap_mb=600, installed=("he",), rate_mb=40.0, host="127.0.0.1"):
        self.host, self.port, self.cap_mb, self.rate = host, port, cap_mb, rate_mb
        self.installed = set(installed) | {"en"}
        self.size = {c: mb for c, _, mb in ROWS}
        self.log = []                   # (method, path, body)
        self.job_id = 0
        self.job = None                 # {"id", "want", "t0", "total", "state", "why"}
        self.delay_list = 0
        self.post_status = 0            # a status to refuse every POST with
        self.fail_job = ""              # a reason: the next job fails with it
        self.lock = threading.Lock()
        gate = self

        class H(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *a):
                pass

            def reply(self, status, why="", headers=(), body=None):
                data = body if body is not None else ("translategate: %s\n" % why).encode()
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
                    pass                    # the Atari went away (a stop)

            def do_GET(self):
                gate.log.append(("GET", self.path, b""))
                path, _, query = self.path.partition("?")
                if path == "/languages" and query == "all=1":
                    if gate.delay_list:
                        t_end = time.time() + gate.delay_list
                        while time.time() < t_end and not gate.stopping:
                            time.sleep(0.1)
                    text = "cap_mb %d\nused_mb %d\n" % (gate.cap_mb, gate.used_mb())
                    text += "".join("%s\t%s\t%d\t%s\n" % (c, n, mb, "installed" if c in gate.installed else "available")
                                    for c, n, mb in ROWS)
                    self.reply(200, body=text.encode())
                elif path == "/job":
                    self.reply(200, body=gate.job_text().encode())
                else:
                    self.reply(404, "no such page")

            def translate(self, query, body):
                args = dict(x.partition("=")[::2] for x in query.split("&") if x)
                to = args.get("to", "")
                text = body.decode("utf-8", "replace")
                src = args.get("from") or ("he" if has_hebrew(text) else "en")
                if not CODE.fullmatch(to):
                    self.reply(400, "to= isn't a language code")
                    return
                if to not in gate.installed or src not in gate.installed:
                    self.reply(422, "%s to %s isn't installed" % (src, to))
                    return
                if src == to:
                    self.reply(200, headers=[("X-Translate-Same", "1")], body=b"")
                    return
                out = said(to, text)
                self.reply(200, headers=[("X-Translate-From", src)], body=out.encode())

            def do_POST(self):
                size = self.headers.get("Content-Length", "")
                body = self.rfile.read(int(size)) if size.isdigit() else b""
                gate.log.append(("POST", self.path, body))
                path, _, query = self.path.partition("?")
                if path == "/translate":
                    self.translate(query, body)
                    return
                if self.path != "/languages":
                    self.reply(404, "no such page")
                    return
                if gate.post_status:
                    self.reply(gate.post_status, "refused for the test")
                    return
                if not size.isdigit() or int(size) > 4096:
                    self.reply(400, "no Content-Length" if not size.isdigit() else "too long")
                    return
                codes = set(re.split(r"[\s,]+", body.decode("ascii", "replace").strip())) - {""}
                if not all(CODE.fullmatch(c) and c in gate.size for c in codes):
                    self.reply(400, "a language code isn't right")
                    return
                want = codes | {"en"}
                need = sum(gate.size[c] for c in want if c != "en")
                if need > gate.cap_mb:
                    self.reply(422, "those languages take %d MB; %d MB fit" % (need, gate.cap_mb))
                    return
                if want == gate.installed:
                    self.reply(200, body=b"job 0\ndone\n")
                    return
                jid = gate.start_job(want)
                if jid is None:
                    self.reply(503, "a languages job is running")
                    return
                self.reply(202, headers=[("X-Translate-Job", str(jid))], body=("job %d\n" % jid).encode())

        self.stopping = False
        self.server = ThreadingHTTPServer((host, port), H)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]       # (port 0: a free one)

    def used_mb(self):
        return sum(self.size[c] for c in self.installed if c != "en")

    def start_job(self, want):
        with self.lock:
            self.settle()
            if self.job and self.job["state"] == "running":
                return None
            self.job_id += 1
            add = sum(self.size[c] for c in want - self.installed)
            self.job = {"id": self.job_id, "want": set(want), "t0": time.time(), "total": add * 1048576,
                        "state": "running", "why": self.fail_job}
            self.fail_job = ""
            return self.job_id

    def settle(self):
        """a running job that has had its time is done (or failed): the installed set follows"""
        j = self.job
        if j and j["state"] == "running":
            secs = max(j["total"] / 1048576.0 / self.rate, 1.0)
            if time.time() - j["t0"] >= secs:
                if j["why"]:
                    j["state"] = "failed"
                else:
                    j["state"] = "done"
                    self.installed = set(j["want"])

    def job_text(self):
        with self.lock:
            self.settle()
            j = self.job
            if not j:
                return "job 0\nnone\n"
            if j["state"] == "running":
                secs = max(j["total"] / 1048576.0 / self.rate, 1.0)
                done = int(j["total"] * min((time.time() - j["t0"]) / secs, 1.0))
                return "job %d\nrunning\nbytes %d of %d\n" % (j["id"], done, j["total"])
            if j["state"] == "failed":
                return "job %d\nfailed: %s\n" % (j["id"], j["why"])
            return "job %d\ndone\n" % j["id"]

    def posts(self):
        return [b for m, _, b in self.log if m == "POST"]

    def start(self):
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def stop(self):
        self.stopping = True
        self.server.shutdown()
        self.server.server_close()


if __name__ == "__main__":
    import sys
    g = Gate(port=int(sys.argv[1]) if len(sys.argv) > 1 else 18480)
    print("fakegate on %s:%d" % (g.host, g.port))
    g.server.serve_forever()
