#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""emailgate.py on this computer: an "Atari" talks plain to the gateway,
which talks TLS to servers made here with a certificate made here (and
trusted through SSL_CERT_FILE, as the Pi trusts the system's roots).

    python3 -m unittest discover -s gateway -p 'test_*.py'   (needs openssl)
"""
import os
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emailgate  # noqa: E402

TMP = tempfile.mkdtemp(prefix="emailgate-test.")


def make_cert(name, cn, san):
    cert, key = os.path.join(TMP, name + ".pem"), os.path.join(TMP, name + ".key")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-subj",
                    "/CN=" + cn, "-addext", "subjectAltName=" + san, "-keyout", key, "-out", cert],
                   check=True, capture_output=True)
    return cert, key


def server_ctx(cert, key):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    return ctx


class Server:
    """a server on 127.0.0.1 that runs talk(sock) for each connection"""

    def __init__(self, talk):
        self.sock = socket.create_server(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.talk = talk
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self):
        while True:
            try:
                c, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self.one, args=(c,), daemon=True).start()

    def one(self, c):
        try:
            self.talk(c)
        except (OSError, ssl.SSLError):
            pass
        finally:
            c.close()

    def close(self):
        self.sock.close()


def echo_lines(f, out):
    """what the Atari says comes back, marked"""
    for line in iter(lambda: f.readline(), b""):
        out(b"ECHO " + line)


class GateTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.cert, cls.key = make_cert("good", "localhost", "DNS:localhost")
        cls.other, cls.okey = make_cert("other", "localhost", "DNS:localhost")   # trusted by nobody
        os.environ["SSL_CERT_FILE"] = cls.cert
        good = server_ctx(cls.cert, cls.key)

        def imaps(c):                       # IMAP on 993: TLS from the start
            with good.wrap_socket(c, server_side=True) as t:
                t.sendall(b"* OK test IMAP ready\r\n")
                echo_lines(t.makefile("rb"), t.sendall)

        def smtp587(c):                     # SMTP on 587: STARTTLS first
            f = c.makefile("rb")
            c.sendall(b"220-smtp.test\r\n220 ESMTP ready\r\n")
            assert f.readline().startswith(b"EHLO")
            c.sendall(b"250-smtp.test\r\n250-PIPELINING\r\n250 STARTTLS\r\n")
            assert f.readline() == b"STARTTLS\r\n"
            c.sendall(b"220 go ahead\r\n")
            with good.wrap_socket(c, server_side=True) as t:
                g = t.makefile("rb")
                assert g.readline().startswith(b"EHLO atari")
                t.sendall(b"250-smtp.test, over TLS\r\n250 AUTH PLAIN\r\n")
                echo_lines(g, t.sendall)

        def untrusted(c):
            with server_ctx(cls.other, cls.okey).wrap_socket(c, server_side=True) as t:
                t.sendall(b"* OK nobody vouches for me\r\n")

        cls.imap = Server(imaps)
        cls.smtp = Server(smtp587)
        cls.bad = Server(untrusted)
        # bound but never listening: connections are refused, and while the
        # tests run no other socket can be given the port, neither a gate on
        # port 0 nor a connection as its own source port (it would reach itself)
        cls.closed = socket.socket()
        cls.closed.bind(("127.0.0.1", 0))
        cls.closed_port = cls.closed.getsockname()[1]
        cls.gates = []

    @classmethod
    def tearDownClass(cls):
        for s in (cls.imap, cls.smtp, cls.bad, cls.closed):
            s.close()
        for g in cls.gates:
            g.close()
        shutil.rmtree(TMP, ignore_errors=True)

    def setUp(self):
        emailgate.Config.allow = []
        emailgate.Config.timeout = 10

    def gate(self, proto, host, port, starttls=False):
        s, _ = emailgate.listen(proto, "127.0.0.1", 0, (host, port, starttls))
        self.gates.append(s)
        return s.getsockname()[1]

    def atari(self, port):
        c = socket.create_connection(("127.0.0.1", port), timeout=10)
        return c, c.makefile("rb")

    def test_imap_relayed_over_tls(self):
        c, f = self.atari(self.gate("imap", "localhost", self.imap.port))
        self.assertEqual(f.readline(), b"* OK test IMAP ready\r\n")
        c.sendall(b"a1 LOGIN dana secret\r\n")
        self.assertEqual(f.readline(), b"ECHO a1 LOGIN dana secret\r\n")
        big = b"x" * 200000 + b"\r\n"           # a big message, both ways
        c.sendall(big)
        got = f.readline()
        self.assertEqual(got, b"ECHO " + big)
        c.close()

    def test_smtp_starttls_greeting_handed_on(self):
        c, f = self.atari(self.gate("smtp", "localhost", self.smtp.port, starttls=True))
        self.assertEqual(f.readline(), b"220-smtp.test\r\n")       # the server's, from before TLS
        self.assertEqual(f.readline(), b"220 ESMTP ready\r\n")
        c.sendall(b"EHLO atari\r\n")
        self.assertEqual(f.readline(), b"250-smtp.test, over TLS\r\n")
        self.assertEqual(f.readline(), b"250 AUTH PLAIN\r\n")
        c.sendall(b"MAIL FROM:<dana@test.local>\r\n")
        self.assertEqual(f.readline(), b"ECHO MAIL FROM:<dana@test.local>\r\n")
        c.close()

    def test_untrusted_certificate_refused(self):
        c, f = self.atari(self.gate("imap", "localhost", self.bad.port))
        line = f.readline()
        self.assertTrue(line.startswith(b"* BYE emailgate: localhost's certificate doesn't check out"), line)
        self.assertNotIn(b"nobody vouches", f.read())
        c.close()

    def test_wrong_name_refused(self):
        # the certificate is for localhost, not 127.0.0.1
        c, f = self.atari(self.gate("imap", "127.0.0.1", self.imap.port))
        line = f.readline()
        self.assertTrue(line.startswith(b"* BYE emailgate: 127.0.0.1's certificate doesn't check out"), line)
        c.close()

    def test_unreachable_said_in_the_protocol(self):
        for proto, start in (("imap", b"* BYE emailgate: can't reach"), ("pop3", b"-ERR emailgate: can't reach"),
                             ("smtp", b"421 emailgate: can't reach")):
            c, f = self.atari(self.gate(proto, "localhost", self.closed_port))
            line = f.readline()
            self.assertTrue(line.startswith(start), line)
            c.close()

    def test_only_the_allowed_atari(self):
        emailgate.Config.allow = ["192.168.1.50"]
        c, f = self.atari(self.gate("imap", "localhost", self.imap.port))
        self.assertEqual(f.readline(), b"* BYE emailgate: this address may not use the gateway\r\n")
        c.close()
        emailgate.Config.allow = ["127.0.0.1"]
        c, f = self.atari(self.gate("imap", "localhost", self.imap.port))
        self.assertEqual(f.readline(), b"* OK test IMAP ready\r\n")
        c.close()

    def test_private_addresses(self):
        for a in ("192.168.1.50", "10.1.2.3", "172.16.0.1", "172.31.255.254", "127.0.0.1", "::ffff:192.168.1.2"):
            self.assertTrue(emailgate.is_private(a), a)
        for a in ("8.8.8.8", "172.32.0.1", "100.64.0.1", "2001:db8::1", "not an address"):
            self.assertFalse(emailgate.is_private(a), a)
        self.assertTrue(emailgate.client_ok("192.168.1.9"))
        self.assertFalse(emailgate.client_ok("203.0.113.7"))

    def test_tls_12_at_least(self):
        self.assertGreaterEqual(emailgate.tls_context().minimum_version, ssl.TLSVersion.TLSv1_2)
        self.assertTrue(emailgate.tls_context().check_hostname)
        self.assertEqual(emailgate.tls_context().verify_mode, ssl.CERT_REQUIRED)

    def test_server_arg(self):
        self.assertEqual(emailgate.server_arg("smtp.mail.me.com:587", 465), ("smtp.mail.me.com", 587))
        self.assertEqual(emailgate.server_arg("smtp.gmail.com", 465), ("smtp.gmail.com", 465))


if __name__ == "__main__":
    unittest.main()
