# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""dashgate: the counts, who may tell it, the services' states and
settings, and the page."""

import getpass
import http.client
import json
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashgate  # noqa: E402
import aeragate  # noqa: E402


def wait_for(cond, seconds=3):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


class StatsTest(unittest.TestCase):
    def test_counts(self):
        st = dashgate.Stats()
        st.take("web", "webgate", "c 192.168.1.31 ok")
        st.take("web", "webgate", "t 192.168.1.31 news.example.com")
        st.take("web", "webgate", "c 192.168.1.45 no")
        st.take("web", "webgate", "c not-an-address ok")
        st.take("web", "webgate", "t 192.168.1.31 <script>")
        st.take("web", "webgate", "x y z")
        self.assertEqual(st.total["web"], 2)
        self.assertEqual(st.today("web"), 2)
        self.assertEqual(st.trend("web")[-1], 2)
        self.assertEqual([(e["client"], e["to"], e["ok"]) for e in st.events],
                         [("192.168.1.31", "news.example.com", True), ("192.168.1.45", "", False)])
        self.assertEqual(st.clients["192.168.1.45"]["refused"], 1)
        self.assertEqual(st.clients["192.168.1.31"]["services"], {"web"})

    def test_a_second_place(self):
        """one connection that goes to two servers: both shown"""
        st = dashgate.Stats()
        st.take("mail", "emailgate", "c 192.168.1.31 ok")
        st.take("mail", "emailgate", "t - imap.example.com")
        st.take("mail", "emailgate", "t - smtp.example.com")
        self.assertEqual([e["to"] for e in st.events], ["imap.example.com", "smtp.example.com"])
        self.assertEqual(st.total["mail"], 1)
        self.assertEqual(st.last["mail"]["to"], "smtp.example.com")

    def test_claude_from_the_kernel(self):
        st = dashgate.Stats()
        c = dashgate.Claude(st)
        c.poll(2323, {2323: [("192.168.1.32", "A")]})
        c.poll(2323, {2323: [("192.168.1.32", "A")]})
        c.poll(2323, {2323: [("192.168.1.32", "A"), ("192.168.1.32", "B")]})
        self.assertEqual(st.total["claude"], 2)


class SocketTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "events.sock")
        self.users = dashgate.Config.users
        self.stats = dashgate.Stats()

    def tearDown(self):
        dashgate.Config.users = self.users
        aeragate.DASH_SOCKET = ""

    def test_the_gateways_tell_it(self):
        """aeragate.client_ok and lookup, from a gateway's user"""
        dashgate.Config.users = {getpass.getuser(): "radio"}
        s = dashgate.listen(self.stats, self.path)
        self.addCleanup(s.close)
        aeragate.DASH_SOCKET = self.path
        self.assertTrue(aeragate.client_ok("192.168.1.31", []))
        self.assertFalse(aeragate.client_ok("203.0.113.9", []))
        aeragate.lookup("localhost", 80)
        self.assertTrue(wait_for(lambda: len(self.stats.events) == 2 and self.stats.events[1]["to"]))
        self.assertEqual([(e["client"], e["ok"]) for e in self.stats.events],
                         [("192.168.1.31", True), ("203.0.113.9", False)])
        self.assertEqual(self.stats.events[1]["to"], "localhost")

    def test_others_dropped(self):
        """a user that isn't a gateway's isn't counted"""
        dashgate.Config.users = {"emailgate-not-me": "mail"}
        s = dashgate.listen(self.stats, self.path)
        self.addCleanup(s.close)
        aeragate.DASH_SOCKET = self.path
        aeragate.client_ok("192.168.1.31", [])
        time.sleep(0.3)
        self.assertEqual(len(self.stats.events), 0)

    def test_nobody_listening(self):
        aeragate.DASH_SOCKET = self.path + ".none"
        self.assertTrue(aeragate.client_ok("192.168.1.31", []))


class ServicesTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.old = dashgate.Config.units, dashgate.Config.proc, dashgate.Config.models
        dashgate.Config.units = self.dir
        dashgate.Config.proc = self.dir
        dashgate.Config.models = os.path.join(self.dir, "models")
        os.makedirs(os.path.join(self.dir, "net"))
        os.makedirs(os.path.join(self.dir, "models", "translate-de_en-1_9"))
        with open(os.path.join(self.dir, "uptime"), "w") as f:
            f.write("1000.0 900.0\n")

    def tearDown(self):
        dashgate.Config.units, dashgate.Config.proc, dashgate.Config.models = self.old

    def unit(self, name, line):
        with open(os.path.join(self.dir, name + ".service"), "w") as f:
            f.write("[Service]\nUser=%s\nExecStart=%s\n" % (name, line))

    def test_from_systemd(self):
        self.unit("emailgate", "/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --allow 192.168.1.31 "
                               "imap.example.com smtp.example.com:587")
        self.unit("webgate", "/usr/bin/python3 /opt/aera-gateway/webgate.py --port 8280 --allow 192.168.1.31,"
                             "192.168.1.32 --allow-local --browser")
        self.unit("translategate", "/opt/translategate/venv/bin/python3 /opt/aera-gateway/translategate.py "
                                   "--port 8480")
        calls = []

        class R:
            stdout = ("ActiveState=active\nActiveEnterTimestampMonotonic=400000000\n\n"
                      "ActiveState=failed\nActiveEnterTimestampMonotonic=0\n\n"
                      "ActiveState=activating\nActiveEnterTimestampMonotonic=0\n")

        def run(cmd, **kw):
            calls.append(cmd)
            return R()
        found = dashgate.from_systemd(run)
        self.assertEqual(calls[0][-3:], ["emailgate.service", "webgate.service", "translategate.service"])
        self.assertEqual({k: v["state"] for k, v in found.items()},
                         {"mail": "running", "web": "stopped", "translate": "starting"})
        self.assertAlmostEqual(found["mail"]["since"], time.time() - 600, delta=5)
        s = dashgate.settings(found)
        self.assertEqual(s["allow"], ["192.168.1.31", "192.168.1.32"])
        self.assertTrue(s["allow_local"])
        self.assertTrue(s["browser"])
        self.assertEqual(s["mail"], ["imap.example.com", "smtp.example.com:587"])
        self.assertEqual(s["languages"], ["de → en"])

    def test_connected_now(self):
        """established connections, from the kernel's list; listening ones aren't"""
        with open(os.path.join(self.dir, "net", "tcp"), "w") as f:
            f.write("  sl  local_address rem_address   st\n"
                    "   0: 00000000:2050 00000000:0000 0A 0 0\n"
                    "   1: 1401A8C0:2050 1F01A8C0:C350 01 0 0\n")
        with open(os.path.join(self.dir, "net", "tcp6"), "w") as f:
            f.write("  sl  local_address rem_address   st\n"
                    "   0: 0000000000000000FFFF00001401A8C0:0913 0000000000000000FFFF00002001A8C0:C351 01 0 0\n")
        peers = dashgate.tcp_peers()
        self.assertEqual([ip for ip, _ in peers[8272]], ["192.168.1.31"])
        self.assertEqual([ip for ip, _ in peers[2323]], ["192.168.1.32"])
        self.assertNotIn(0x2050 + 1, peers)

    def test_status(self):
        st = dashgate.Stats()
        st.take("dav", "ecalgate", "c 192.168.1.31 ok")
        found = {"dav": {"state": "running", "since": time.time() - 90, "args": ["--port", "8181"]}}
        d = dashgate.status(st, "192.168.1.20:8888", found, None, {8181: [("192.168.1.31", "x")]})
        dav = [s for s in d["services"] if s["key"] == "dav"][0]
        self.assertEqual((dav["ports"], dav["total"], dav["live"]), ([8181], 1, 1))
        self.assertEqual([s["state"] for s in d["services"] if s["key"] == "mail"], ["not installed"])
        self.assertEqual(d["gateway"]["address"], "192.168.1.20")
        self.assertTrue(d["ataris"][0]["live"])
        self.assertAlmostEqual(d["gateway"]["since"], time.time() - 90, delta=5)
        json.dumps(d)


class PageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dashgate.Handler.stats = dashgate.Stats()
        cls.srv = dashgate.Server(("127.0.0.1", 0), dashgate.Handler)
        cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.old = dashgate.Config.units
        dashgate.Config.units = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()
        dashgate.Config.units = cls.old

    def setUp(self):
        dashgate.Config.allow = []

    def get(self, path):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        c.close()
        return r.status, r, body

    def test_page(self):
        status, r, body = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn(b"AERA-Gateway", body)
        self.assertIn("script-src 'self'", r.getheader("Content-Security-Policy"))
        self.assertEqual(r.getheader("Cache-Control"), "no-store")
        for path in ("/dash.css", "/dash.js"):
            self.assertEqual(self.get(path)[0], 200)
        self.assertNotIn("innerHTML", dashgate.JS)
        self.assertNotIn("https://", dashgate.PAGE + dashgate.CSS + dashgate.JS)    # (nothing from the internet)

    def test_filled_in(self):
        """the page reads the same with scripts off: everything is in it"""
        st = dashgate.Handler.stats
        st.take("web", "webgate", "c 192.168.1.31 ok")
        st.take("web", "webgate", "t 192.168.1.31 news.example.com")
        st.take("web", "webgate", "c 192.168.1.45 no")
        status, _, body = self.get("/")
        page = body.decode()
        for want in ('data-service="web"', "news.example.com", "192.168.1.31", "refused: not allowed",
                     "Connections this run", "Not installed", "1 try", '<meta http-equiv="refresh" content="30">',
                     'fill="none" stroke="#127a6f"', 'fill="#eef1f4"'):
            self.assertIn(want, page)
        self.assertNotIn("%(", page)
        d = {"gateway": {"now": 0, "since": None, "address": "", "where": "systemd", "model": "", "version": "1"},
             "services": [{"key": "web", "apps": "<b>", "speaks": "&", "state": "running", "since": None,
                           "ports": [1], "today": 0, "total": 0, "trend": [0] * 8, "live": 0, "last": None}],
             "events": [], "ataris": [], "computer": {},
             "settings": {"allow": [], "allow_local": False, "mail": [], "browser": None, "languages": [],
                          "claude": None}}
        out = dashgate.page(d)
        self.assertIn("&lt;b&gt;", out)
        self.assertNotIn("<b>&", out)
        self.assertIn("Nothing yet this run", out)

    def test_json(self):
        status, r, body = self.get("/status.json")
        self.assertEqual(status, 200)
        d = json.loads(body)
        self.assertEqual(sorted(d), ["ataris", "computer", "events", "gateway", "services", "settings"])
        self.assertEqual([s["key"] for s in d["services"]],
                         ["mail", "dav", "ftp", "web", "pdf", "translate", "radio", "claude"])

    def test_who_may_see_it(self):
        dashgate.Config.allow = ["192.168.1.40"]
        status, _, body = self.get("/status.json")
        self.assertEqual((status, body), (403, b"dashgate: not from this computer\n"))

    def test_no_such_page(self):
        self.assertEqual(self.get("/etc/passwd")[0], 404)


if __name__ == "__main__":
    unittest.main()
