#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translategate.py on this computer, with the stand-in engine: the
protocol (AERA-Atari standards/TRANSLATION-SERVICE.md), the text's shape,
the limits and refusals, a dropped job, a short request beside a long one,
and that no word of the text is ever logged or sent back in a refusal.

    python3 -m unittest discover -s gateway -p 'test_*.py'
"""
import contextlib
import io
import os
import socket
import sys
import threading
import time
import unittest
import unittest.mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translategate as tg  # noqa: E402

SECRET = "Pomegranate"      # a word that must never be logged


class Log(io.StringIO):
    """stderr while a test runs: everything aeragate.say() and Python wrote"""


class Gate:
    def __init__(self, engine=None, seconds=60, allow=None):
        tg.Config.engine = engine or tg.StandIn()
        tg.Config.seconds = seconds
        tg.Config.allow = allow or []
        tg.LANES = tg.Lanes(2)
        self.srv = tg.Server(("127.0.0.1", 0), tg.Handler)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def close(self):
        self.srv.shutdown()
        self.srv.server_close()

    def raw(self, data, wait=True):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        s.sendall(data)
        if not wait:
            return s
        out = b""
        while True:
            b = s.recv(65536)
            if not b:
                break
            out += b
        s.close()
        return out

    def ask(self, path, body=None, headers=None):
        """(status, headers dict, body bytes)"""
        if body is None:
            req = "GET %s HTTP/1.0\r\nHost: pi\r\n\r\n" % path
            data = req.encode()
        else:
            b = body.encode() if isinstance(body, str) else body
            hs = headers if headers is not None else {"Content-Length": str(len(b))}
            req = "POST %s HTTP/1.0\r\nHost: pi\r\nContent-Type: text/plain; charset=utf-8\r\n" % path
            req += "".join("%s: %s\r\n" % kv for kv in hs.items()) + "\r\n"
            data = req.encode() + b
        out = self.raw(data)
        head, _, rest = out.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        status = int(lines[0].split()[1])
        hd = {}
        for l in lines[1:]:
            k, _, v = l.partition(":")
            hd[k.strip().lower()] = v.strip()
        return status, hd, rest


class Base(unittest.TestCase):
    engine = None

    def setUp(self):
        self.log = Log()
        self.redirect = contextlib.redirect_stderr(self.log)
        self.redirect.__enter__()
        self.gate = Gate(self.engine() if self.engine else None)

    def tearDown(self):
        self.gate.close()
        self.redirect.__exit__(None, None, None)
        # the rule of rules: nothing of the text, in anything logged
        self.assertNotIn(SECRET.lower(), self.log.getvalue().lower())

    def tr(self, text, to="en", src=None):
        q = "/translate?to=%s" % to + ("&from=%s" % src if src else "")
        st, hd, body = self.gate.ask(q, text)
        return st, hd, body.decode("utf-8")


class ProtocolTest(Base):
    def test_status(self):
        st, _, body = self.gate.ask("/status")
        self.assertEqual(st, 200)
        self.assertEqual(body, b"translategate 1\nengine stand-in 1\nready\n")

    def test_languages(self):
        st, _, body = self.gate.ask("/languages")
        self.assertEqual(st, 200)
        self.assertEqual(body.decode().splitlines(), ["de\tGerman", "en\tEnglish", "he\tHebrew"])

    def test_translate(self):
        st, hd, body = self.tr("Hello %s." % SECRET, src="de")
        self.assertEqual(st, 200)
        self.assertEqual(hd["x-translate-from"], "de")
        self.assertEqual(hd["content-type"], "text/plain; charset=utf-8")
        self.assertEqual(int(hd["content-length"]), len(body.encode()))
        self.assertEqual(body, "en: HELLO %s." % SECRET.upper())
        self.assertIn("de>en 200,", self.log.getvalue())

    def test_detect(self):
        st, hd, body = self.tr("שלום, מה שלומך היום? %s" % SECRET)
        self.assertEqual((st, hd["x-translate-from"]), (200, "he"))
        self.assertTrue(body.startswith("en: שלום, מה"))
        st, _, body = self.tr("(see https://example.com/a(b)) and [www.example.com].", src="de")
        self.assertEqual(body, "en: SEE https://example.com/a(b) AND www.example.com.")

    def test_same(self):
        st, hd, body = self.tr("Good morning, %s." % SECRET)
        self.assertEqual(st, 200)
        self.assertEqual((hd["x-translate-from"], hd["x-translate-same"]), ("en", "1"))
        self.assertEqual((body, hd["content-length"]), ("", "0"))
        st, hd, body = self.tr("Good morning.", src="he", to="he")
        self.assertEqual((st, hd.get("x-translate-same"), body), (200, "1", ""))

    def test_refusals(self):
        def one_line(st, body, code):
            self.assertEqual(st, code)
            self.assertTrue(body.startswith(b"translategate: ") and body.count(b"\n") == 1, body)
            self.assertNotIn(SECRET.encode(), body)
        st, _, b = self.gate.ask("/translate?to=EN", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en&x=1", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?from=de", SECRET)
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", SECRET, headers={})
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", b"\xff\xfe" + SECRET.encode())
        one_line(st, b, 400)
        st, _, b = self.gate.ask("/translate?to=en", SECRET * 1000)
        one_line(st, b, 413)
        st, _, b = self.gate.ask("/translate?to=fr", "Hallo %s, das ist nicht gut." % SECRET)
        one_line(st, b, 422)
        st, _, b = self.gate.ask("/nothing")
        one_line(st, b, 404)

    def test_limit_exactly(self):
        text = ("word " * 2000)[:8192]
        st, _, _ = self.tr(text, src="de")
        self.assertEqual(st, 200)

    def test_not_from_home(self):
        tg.Config.allow = ["192.168.1.99"]
        st, _, b = self.gate.ask("/status")
        self.assertEqual(st, 403)


class ShapeTest(Base):
    def test_paragraphs_kept(self):
        text = "One %s.\n\nTwo.\r\n\r\n\n  \nThree.\n" % SECRET
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: ONE %s.\n\nen: TWO.\r\n\r\n\n  \nen: THREE.\n" % SECRET.upper())

    def test_lines_joined(self):
        text = ("This is a letter that was wrapped at about seventy-two columns by\n"
                "the mailer, so its lines must be joined before they are translated.\n\nBye.")
        st, _, body = self.tr(text, src="de")
        first, rest = body.split("\n\n")
        self.assertEqual(first, "en: THIS IS A LETTER THAT WAS WRAPPED AT ABOUT SEVENTY-TWO COLUMNS BY "
                                "THE MAILER, SO ITS LINES MUST BE JOINED BEFORE THEY ARE TRANSLATED.")
        self.assertEqual(rest, "en: BYE.")

    def test_short_lines_kept(self):
        text = "Erez Yaary\n12 Long Street\nSmalltown\n\nRoses are red,\nviolets are blue"
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: EREZ YAARY\nen: 12 LONG STREET\nen: SMALLTOWN\n\n"
                               "en: ROSES ARE RED,\nen: VIOLETS ARE BLUE")

    def test_quotes(self):
        text = ("> I wrote this a long time ago and it was wrapped by the mailer at\n"
                "> seventy-two columns, as letters are.\n\n>> short\n>> lines\n")
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "> en: I WROTE THIS A LONG TIME AGO AND IT WAS WRAPPED BY THE MAILER AT "
                               "SEVENTY-TWO COLUMNS, AS LETTERS ARE.\n\n>> en: SHORT\n>> en: LINES\n")

    def test_kept_pieces(self):
        text = ("See https://example.com/a(b)?x=[1], write to some.one@example.org, "
                "it costs $12.50 or 45 € and weighs 3.5 kg at 20%.")
        st, _, body = self.tr(text, src="de")
        self.assertEqual(body, "en: SEE https://example.com/a(b)?x=[1], WRITE TO some.one@example.org, "
                               "IT COSTS $12.50 OR 45 € AND WEIGHS 3.5 kg AT 20%.")

    def test_only_kept(self):
        st, _, body = self.tr("https://example.com/\n", src="de")
        self.assertEqual(body, "https://example.com/\n")

    def test_restore_lost_mark(self):
        text, kept = tg.protect("mail a@b.org now")
        self.assertEqual(text, "mail ZQX0QZ now")
        self.assertEqual(tg.restore("MAIL zqx 0 qz NOW", kept), "MAIL a@b.org NOW")
        self.assertEqual(tg.restore("MAIL NOW", kept), "MAIL NOW a@b.org")
        self.assertEqual(tg.restore("ZQX0QZ ZQX0QZ ZQX7QZ", kept), "a@b.org  ")

    def test_mark_survives_engine(self):
        # the stand-in drops brackets and changes case, as real engines may
        out = tg.StandIn().translate([tg.MARK % 3], "de", "en")[0]
        self.assertEqual(tg.MARK_BACK.search(out).group(1), "3")


class FakeModel:
    """CTranslate2 and SentencePiece as far as Argos.translate uses them:
    the "translation" is the source in capitals, a word alone said twice
    (as the small models do)"""
    def __init__(self):
        self.fed = []

    def encode(self, s, out_type=str):
        return s.split()

    def decode(self, toks):
        return " ".join(toks)

    def translate_batch(self, toks, **kw):
        class Hyp:
            def __init__(self, h):
                self.hypotheses = [h]
        self.fed += [" ".join(t) for t in toks]
        return [Hyp([w.upper() for w in t] * (2 if len(t) == 1 else 1)) for t in toks]


class ArgosShapeTest(unittest.TestCase):
    def setUp(self):
        self.eng = tg.Argos.__new__(tg.Argos)
        self.model = FakeModel()
        self.eng.pair = {("he", "en"): (self.model, self.model), ("en", "he"): (self.model, self.model)}

    def test_closings(self):
        self.assertEqual(tg.closing("he", "en", "בברכה,"), "Best regards,")
        self.assertEqual(tg.closing("en", "he", "Kind Regards"), "בברכה")
        self.assertIsNone(tg.closing("en", "he", "Kind regards to your mother"))
        self.assertEqual(self.eng.translate(["בברכה,", "Thanks!"], "he", "en"), ["Best regards,", "THANKS!"])
        self.assertEqual(self.model.fed, ["Thanks!"])

    def test_through_english(self):
        m = self.model
        self.eng.pair = {("he", "en"): (m, m), ("en", "he"): (m, m), ("de", "en"): (m, m), ("en", "de"): (m, m)}
        self.assertIn(("he", "de"), self.eng.pairs())
        self.assertNotIn(("he", "he"), self.eng.pairs())
        self.assertEqual(self.eng.translate(["two words here."], "he", "de"), ["TWO WORDS HERE."])
        self.assertEqual(self.model.fed, ["two words here.", "TWO WORDS HERE."])     # he>en, then en>de

    def test_short_gets_a_full_stop(self):
        self.assertEqual(self.eng.translate(["Dana", "new order", "One. Two words here."], "en", "he"),
                         ["DANA", "NEW ORDER", "ONE. TWO WORDS HERE."])
        self.assertEqual(self.model.fed, ["Dana.", "new order.", "One.", "Two words here."])
        self.assertTrue(tg.short("Re: the old Falcon"))
        self.assertFalse(tg.short("Thanks,"))
        self.assertFalse(tg.short("one two three four five"))


class Slow(tg.StandIn):
    def __init__(self):
        super().__init__(delay=0.6)


class JobTest(Base):
    engine = Slow

    def test_dropped(self):
        body = ("Long text %s. " % SECRET * 5 + "\n\n") * 4
        b = body.encode()
        s = self.gate.raw(b"POST /translate?to=en&from=de HTTP/1.0\r\nContent-Length: %d\r\n\r\n" % len(b) + b,
                          wait=False)
        time.sleep(0.3)
        s.close()
        for _ in range(50):
            if "dropped" in self.log.getvalue():
                break
            time.sleep(0.1)
        line = [l for l in self.log.getvalue().splitlines() if "dropped" in l]
        self.assertEqual(line, ["translategate: 127.0.0.1 de>en dropped, %d bytes" % len(b)])

    def test_time_limit(self):
        tg.Config.seconds = 1
        st, _, body = self.tr("One.\n\nTwo.\n\nThree.", src="de")
        self.assertEqual((st, body), (504, "translategate: it took over 1 s\n"))

    def test_short_beside_long(self):
        long_text = ("Paragraph %s.\n\n" % SECRET) * 6       # 6 x 0.6 s
        got = {}

        def long_one():
            got["long"] = self.tr(long_text, src="de")
        t = threading.Thread(target=long_one)
        t.start()
        time.sleep(0.2)
        t0 = time.monotonic()
        st, _, body = self.tr("Subject line", src="de")
        took = time.monotonic() - t0
        self.assertEqual((st, body), (200, "en: SUBJECT LINE"))
        self.assertLess(took, 1.5)
        t.join()
        self.assertEqual(got["long"][0], 200)


class EnHe(tg.StandIn):
    """the default install: English and Hebrew only"""
    NAMES = {"en": "English", "he": "Hebrew"}


class NotInstalledTest(Base):
    engine = EnHe

    def test_german_is_not_already_english(self):
        st, hd, body = self.tr("Guten Tag, ich hoffe, es geht Ihnen gut. Die Rechnung ist nicht korrekt, %s." % SECRET)
        self.assertEqual((st, body), (422, "translategate: that pair of languages isn't installed\n"))
        self.assertEqual(hd["x-translate-from"], "de")
        self.assertNotIn("x-translate-same", hd)

    def test_argos_detects_beyond_its_pairs(self):
        eng = tg.Argos.__new__(tg.Argos)
        eng.pair = {("en", "he"): None, ("he", "en"): None}
        s = "Guten Tag, ich hoffe, es geht Ihnen gut. Wir haben die Rechnung erhalten und sie ist nicht korrekt."
        self.assertEqual(eng.detect(s), "de")
        self.assertEqual(eng.detect("Thanks"), "en")
        self.assertEqual(eng.detect("I hope this letter finds you well and that the old Falcon still runs."), "en")
        self.assertEqual(eng.detect("שלום, מה שלומך?"), "he")


class LanguagesTest(Base):
    """the Languages page's three requests, against a fake download"""

    def setUp(self):
        super().setUp()
        import tempfile
        import translatelangs as tl
        self.tl = tl
        self.models = tempfile.mkdtemp(prefix="tg-models.")
        self.state = tempfile.mkdtemp(prefix="tg-state.")
        tg.Config.models, tg.Config.state = self.models, self.state
        tg.Jobs.started = None
        self.restarts = []
        tg.Config.restart = lambda: self.restarts.append(1)
        self.fetched = []
        self.fail = None

        def fetch_one(pair, url, models, progress):
            if self.fail:
                raise tl.Fail(self.fail)
            self.fetched.append(pair)
            d = os.path.join(models, "%s_%s" % pair)
            os.makedirs(d)
            open(os.path.join(d, "metadata.json"), "w").close()
            progress(1 << 20)

        def links(want, index=None):
            return {p: "https://example.invalid/%s_%s" % p for p in want}
        self.patch = unittest.mock.patch.object(tl, "links", links)
        self.patch.start()
        tg.Config.start_job = lambda: tl.job(self.state, self.models, fetch_one=fetch_one)
        for p in (("he", "en"), ("en", "he")):          # en,he installed
            os.makedirs(os.path.join(self.models, "%s_%s" % p))
            open(os.path.join(self.models, "%s_%s" % p, "metadata.json"), "w").close()

    def tearDown(self):
        self.patch.stop()
        tg.Config.start_job = None
        super().tearDown()

    def page(self):
        st, _, body = self.gate.ask("/languages?all=1")
        self.assertEqual(st, 200)
        lines = body.decode().splitlines()
        rows = {l.split("\t")[0]: l.split("\t") for l in lines[2:]}
        return lines[0], lines[1], rows

    def post(self, body):
        return self.gate.ask("/languages", body)

    def test_page(self):
        cap, used, rows = self.page()
        self.assertEqual(cap, "cap_mb %d" % self.tl.CAP_MB)
        self.assertEqual(used, "used_mb 230")
        self.assertEqual(rows["en"], ["en", "English", "0", "installed"])
        self.assertEqual(rows["he"], ["he", "Hebrew", "230", "installed"])
        self.assertEqual(rows["de"], ["de", "German", "287", "available"])
        # the plain list for the apps' Translate into, as before
        st, _, body = self.gate.ask("/languages")
        self.assertEqual(body.decode().splitlines()[0], "de\tGerman")

    def test_install_and_remove(self):
        st, hd, body = self.post("de")                  # he unticked, de ticked: one request
        self.assertEqual((st, body), (202, b"job 1\n"))
        self.assertEqual(hd["x-translate-job"], "1")
        st, _, body = self.gate.ask("/job")
        self.assertEqual(body, b"job 1\ndone\n")
        self.assertEqual(sorted(self.fetched), [("de", "en"), ("en", "de")])
        self.assertEqual(self.tl.installed(self.models), {"en", "de"})
        tg.Jobs.check()
        self.assertEqual(self.restarts, [1])            # translategate starts again with the models
        _, used, rows = self.page()
        self.assertEqual((used, rows["he"][3], rows["de"][3]), ("used_mb 287", "available", "installed"))
        st, _, body = self.post("de,en")                # nothing to change
        self.assertEqual((st, body), (200, b"job 0\ndone\n"))
        self.assertIn("languages job 1: de,en", self.log.getvalue())

    def test_refusals(self):
        st, _, body = self.post("de,ru,es,et")          # 287+336+355+298
        self.assertEqual((st, body), (422, ("translategate: those languages take 1276 MB; %d MB fit\n"
                                            % self.tl.CAP_MB).encode()))
        st, _, body = self.post("de,klingon")
        self.assertEqual((st, body), (400, b"translategate: a language code isn't right\n"))
        self.assertEqual(self.fetched, [])
        st, _, body = self.gate.ask("/job")
        self.assertEqual(body, b"job 0\nnone\n")

    def test_failed(self):
        self.fail = "the Gateway's disk is full"
        st, _, _ = self.post("he,de")
        self.assertEqual(st, 202)
        st, _, body = self.gate.ask("/job")
        self.assertEqual(body, b"job 1\nfailed: the Gateway's disk is full\n")
        self.assertEqual(self.tl.installed(self.models), {"en", "he"})

    def test_one_job_at_a_time(self):
        tg.Config.start_job = None                      # the job not started yet
        self.assertEqual(self.post("he,de")[0], 202)
        st, _, body = self.gate.ask("/job")
        self.assertEqual(body, b"job 1\nrunning\nbytes 0 of 0\n")
        st, _, body = self.post("he,fr")
        self.assertEqual((st, body), (503, b"translategate: a languages job is running\n"))


class LocalTest(unittest.TestCase):
    """the local socket and translateclient, as webgate and pdfgate use them"""
    engine = None

    def setUp(self):
        import tempfile
        import translateclient as tc
        self.tc = tc
        self.log = Log()
        self.redirect = contextlib.redirect_stderr(self.log)
        self.redirect.__enter__()
        tg.Config.engine = (self.engine or tg.StandIn)()
        tg.Config.local_seconds = 600
        tg.LANES = tg.Lanes(2)
        d = tempfile.mkdtemp(prefix="tg-local.")
        self.sock = os.path.join(d, "local.sock")
        self.srv = tg.serve_local(self.sock)

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.redirect.__exit__(None, None, None)
        self.assertNotIn(SECRET.lower(), self.log.getvalue().lower())

    def test_translate(self):
        self.assertEqual(os.stat(self.sock).st_mode & 0o777, 0o660)
        text, used, same = self.tc.translate("Hallo %s, das ist gut.\n\nZwei." % SECRET, "en", sock=self.sock)
        self.assertEqual((text, used, same), ("en: HALLO %s, DAS IST GUT.\n\nen: ZWEI." % SECRET.upper(), "de", False))
        text, used, same = self.tc.translate("Good morning, %s." % SECRET, "en", sock=self.sock)
        self.assertEqual((text, used, same), ("Good morning, %s." % SECRET, "en", True))
        self.assertIn("translategate: local de>en 200,", self.log.getvalue())

    def test_a_whole_page(self):
        page = "\n\n".join("Paragraph %d of the %s page." % (i, SECRET) for i in range(600))   # ~20 KB
        self.assertGreater(len(page), 8192)
        text, used, _ = self.tc.translate(page, "en", "de", sock=self.sock)
        self.assertEqual(text.count("\n\n"), 599)

    def page(self, text, to, until=None, note=None, sock=None):
        """translateclient as aerapdf runs it: (exit, stdout, stderr)"""
        out, err = io.BytesIO(), io.StringIO()
        env = {}
        if until is not None:
            env["TRANSLATECLIENT_UNTIL"] = "%.3f" % until
        if note:
            env["TRANSLATECLIENT_NOTE"] = note
        rc = self.tc.page(["--to", to], io.BytesIO(text.encode()), out, err, env, sock or self.sock)
        return rc, out.getvalue().decode(), err.getvalue()

    def test_page(self):
        """aerapdf's translator: paragraphs in, the same number out"""
        import tempfile
        note = os.path.join(tempfile.mkdtemp(prefix="tg-note."), "language")
        rc, out, err = self.page("Hallo %s, das ist gut.\n\n \nZwei.\n" % SECRET, "en", note=note)
        self.assertEqual((rc, out, err), (0, "en: HALLO %s, DAS IST GUT.\n\nen: ZWEI.\n" % SECRET.upper(), "from de\n"))
        self.assertEqual(open(note).read(), "de\n")
        self.assertEqual(self.page("Good morning, %s." % SECRET, "en"), (0, "", "same en\n"))
        self.assertEqual(self.page("", "en"), (0, "", "same en\n"))
        # refused: 2 for a pair not installed (the language noted), 3 for no translategate
        rc, out, err = self.page("Hallo %s, das ist nicht gut." % SECRET, "fr", note=note)
        self.assertEqual((rc, out), (2, ""))
        self.assertEqual(open(note).read(), "de\n")
        self.assertEqual(self.page("Hallo.", "en", sock=self.sock + ".none")[0], 3)
        self.assertEqual(self.page("Hallo.", "EN")[0], 1)
        # the conversion's time gone: the page as it was, the language last seen
        rc, out, err = self.page("Eins.\n\nZwei.", "en", until=time.time() - 1, note=note)
        self.assertEqual((rc, out, err), (0, "Eins.\n\nZwei.\n", "from de\ndone 0 of 2\n"))

    def test_refused(self):
        with self.assertRaises(self.tc.Refused) as e:
            self.tc.translate("Hallo %s, das ist nicht gut." % SECRET, "fr", sock=self.sock)
        self.assertEqual((e.exception.status, e.exception.line, e.exception.used),
                         (422, "translategate: that pair of languages isn't installed", "de"))
        # the Languages page isn't the local socket's
        st, _, body = self.tc.ask("POST", "/languages", b"de", sock=self.sock)
        self.assertEqual((st, body), (404, b"translategate: no such page\n"))
        st, _, _ = self.tc.ask("GET", "/job", sock=self.sock)
        self.assertEqual(st, 404)
        with self.assertRaises(self.tc.Unreachable):
            self.tc.translate("x", "en", sock=self.sock + ".none")


class LocalSlowTest(LocalTest):
    engine = Slow

    def test_parts_as_far_as_time_allows(self):
        parts = ["Part %d about %s." % (i, SECRET) for i in range(40)]
        out, used, done = self.tc.translate_parts(parts, "en", "de", seconds=1, sock=self.sock, piece=60)
        self.assertEqual(used, "de")
        self.assertTrue(0 < done < 40, done)
        self.assertEqual(out, ["en: PART %d ABOUT %s." % (i, SECRET.upper()) for i in range(done)])

    def test_page_in_part(self):
        """a page the time doesn't cover: what was done, the rest as it was"""
        long = " und".join(["%s ist gut"] * 90)        # (a paragraph a piece: a second each)
        paras = [("Teil %d: " % i) + long % ((SECRET,) * 90) + "." for i in range(20)]
        rc, out, err = self.page("\n\n".join(paras), "en", until=time.time() + 2)
        got = out.rstrip("\n").split("\n\n")
        self.assertEqual((rc, len(got)), (0, 20))
        self.assertEqual(err.splitlines()[0], "from de")
        n = int(err.splitlines()[1].split()[1])
        self.assertEqual(err.splitlines()[1], "done %d of 20" % n)
        self.assertTrue(0 < n < 20, n)
        self.assertTrue(got[0].startswith("en: TEIL 0: %s IST GUT" % SECRET.upper()), got[0][:40])
        self.assertEqual(got[n:], paras[n:])

    def test_a_piece_over_the_time(self):
        """the first piece not back in its time: 4, and the job stopped"""
        old = self.tc.GRACE
        self.tc.GRACE = 0
        try:
            with self.assertRaises(self.tc.TimedOut):
                self.tc.translate_parts(["Ein Teil ist %s." % SECRET] * 8, "en", "de", seconds=0.2,
                                        sock=self.sock)
        finally:
            self.tc.GRACE = old

    test_translate = test_a_whole_page = test_refused = test_page = None


class LoadingTest(Base):
    engine = staticmethod(lambda: tg.StandIn(load=30))

    def test_loading(self):
        st, _, body = self.gate.ask("/status")
        self.assertEqual(body, b"translategate 1\nengine stand-in 1\nloading\n")
        st, _, body = self.tr("Hello %s" % SECRET, src="de")
        self.assertEqual((st, body), (503, "translategate: the engine is loading\n"))


class Broken(tg.StandIn):
    def translate(self, texts, src, dst, check=lambda: None):
        raise ValueError("cannot translate %r" % texts)


class CrashTest(Base):
    engine = Broken

    def test_crash_says_nothing(self):
        st, _, body = self.tr("Hello %s" % SECRET, src="de")
        self.assertEqual((st, body), (500, "translategate: the engine failed\n"))
        self.assertIn("(ValueError)", self.log.getvalue())


if __name__ == "__main__":
    unittest.main()
