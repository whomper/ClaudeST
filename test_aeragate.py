#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""aeragate's rules: who may use a gateway, where it may go, TLS.

    python3 -m unittest gateway/test_aeragate.py
"""
import os
import socket
import ssl
import sys
import threading
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aeragate  # noqa: E402


def fake_getaddrinfo(answers):
    """a resolver that gives each name its answers, and counts the asking"""
    asked = []

    def getaddrinfo(host, port, *a, **k):
        asked.append(host)
        if host not in answers:
            raise socket.gaierror(socket.EAI_NONAME, "Name or service not known")
        out = []
        for ip in answers[host]:
            fam = socket.AF_INET6 if ":" in ip else socket.AF_INET
            sa = (ip, port, 0, 0) if fam == socket.AF_INET6 else (ip, port)
            out.append((fam, socket.SOCK_STREAM, 6, "", sa))
        return out
    return getaddrinfo, asked


class AddressTest(unittest.TestCase):
    def test_home_network(self):
        for a in ("192.168.1.50", "10.1.2.3", "172.16.0.1", "172.31.255.254", "127.0.0.1", "169.254.1.1",
                  "::1", "fd00::5", "fe80::1", "fe80::1%eth0", "::ffff:192.168.1.2"):
            self.assertTrue(aeragate.is_private(a), a)
        for a in ("8.8.8.8", "172.32.0.1", "100.64.0.1", "2001:db8::1", "::ffff:8.8.8.8", "not an address", ""):
            self.assertFalse(aeragate.is_private(a), a)

    def test_leads_home(self):
        for a in ("192.168.1.1", "0.0.0.0", "0.1.2.3", "::", "127.0.0.1", "nonsense"):
            self.assertTrue(aeragate.leads_home(a), a)
        for a in ("8.8.8.8", "2a00:1450::1"):
            self.assertFalse(aeragate.leads_home(a), a)

    def test_who_may_use_it(self):
        self.assertTrue(aeragate.client_ok("192.168.1.9", []))
        self.assertFalse(aeragate.client_ok("203.0.113.7", []))
        self.assertTrue(aeragate.client_ok("192.168.1.50", ["192.168.1.50"]))
        self.assertTrue(aeragate.client_ok("::ffff:192.168.1.50", ["192.168.1.50"]))
        self.assertFalse(aeragate.client_ok("192.168.1.51", ["192.168.1.50"]))


class LookupTest(unittest.TestCase):
    def test_home_refused_unless_allowed(self):
        gai, _ = fake_getaddrinfo({"nas.example": ["93.184.216.34", "192.168.1.5"]})
        with mock.patch("socket.getaddrinfo", gai):
            with self.assertRaises(aeragate.HomeNetwork):
                aeragate.lookup("nas.example", 21, allow_local=False)
            self.assertEqual(len(aeragate.lookup("nas.example", 21, allow_local=True)), 2)

    def test_not_found(self):
        gai, _ = fake_getaddrinfo({})
        with mock.patch("socket.getaddrinfo", gai):
            with self.assertRaises(socket.gaierror):
                aeragate.lookup("nowhere.example", 80)

    def test_connects_where_it_checked(self):
        # the name is looked up once; the connection goes to that answer
        srv = socket.create_server(("127.0.0.1", 0))
        self.addCleanup(srv.close)
        port = srv.getsockname()[1]
        gai, asked = fake_getaddrinfo({"here.example": ["127.0.0.1"]})
        with mock.patch("socket.getaddrinfo", gai), \
                mock.patch("socket.create_connection", side_effect=AssertionError("a second look-up")):
            c = aeragate.connect("here.example", port, 5)
        c.close()
        self.assertEqual(asked, ["here.example"])

    def test_next_address_when_one_fails(self):
        srv = socket.create_server(("127.0.0.1", 0))
        self.addCleanup(srv.close)
        dead = socket.socket()                 # bound, never listening: refused
        dead.bind(("127.0.0.1", 0))
        self.addCleanup(dead.close)
        addrs = [(socket.AF_INET, dead.getsockname()), (socket.AF_INET, srv.getsockname())]
        aeragate.dial(addrs, 5).close()
        with self.assertRaises(ConnectionRefusedError):
            aeragate.dial(addrs[:1], 5)


class TlsTest(unittest.TestCase):
    def test_tls_12_and_checks(self):
        ctx = aeragate.tls_context()
        self.assertGreaterEqual(ctx.minimum_version, ssl.TLSVersion.TLSv1_2)
        self.assertTrue(ctx.check_hostname)
        self.assertEqual(ctx.verify_mode, ssl.CERT_REQUIRED)


class RelayTest(unittest.TestCase):
    def test_both_ways_then_closed(self):
        a1, a2 = socket.socketpair()
        b1, b2 = socket.socketpair()
        result = []
        t = threading.Thread(target=lambda: result.append(aeragate.relay(a2, b1, 10, 10)))
        t.start()
        a1.sendall(b"x" * 100000)
        got = b""
        while len(got) < 100000:
            got += b2.recv(65536)
        b2.sendall(b"back")
        self.assertEqual(a1.recv(10), b"back")
        a1.close()
        t.join(10)
        self.assertEqual(result, ["closed"])
        for s in (a2, b1, b2):
            s.close()

    def test_idle(self):
        a1, a2 = socket.socketpair()
        b1, b2 = socket.socketpair()
        self.assertEqual(aeragate.relay(a2, b1, 0.2, 1), "idle")
        for s in (a1, a2, b1, b2):
            s.close()


if __name__ == "__main__":
    unittest.main()
