#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""eftpgate's tests: an SFTP server (paramiko) and an FTPS server
(pyftpdlib, with a CA made for the test) on this computer, eftpgate in
front of them, and a plain FTP client (ftplib, and EFTP's own core in
build/ftp-cli when it is built) talking to eftpgate as the Atari does.

    python3 -m unittest discover -s gateway -p 'test_*.py'
    (pip install paramiko pyftpdlib pyopenssl; openssl on the PATH)
"""
import argparse
import ftplib
import io
import os
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock

import paramiko
from OpenSSL import SSL
from OpenSSL._util import lib as ssl_lib
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import TLS_FTPHandler
from pyftpdlib.handlers.ftps.data import TLS_DTPHandler
from pyftpdlib.ioloop import IOLoop
from pyftpdlib.servers import FTPServer
import logging

import eftpgate

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, "..", "build", "ftp-cli")
logging.getLogger("pyftpdlib").addHandler(logging.NullHandler())   # quiet: no "[I ..." lines
logging.getLogger("pyftpdlib").propagate = False
logging.getLogger("paramiko").setLevel(logging.CRITICAL)


# ---------------------------------------------------------------- an SFTP server

class StubServer(paramiko.ServerInterface):
    def check_auth_password(self, user, password):
        return paramiko.AUTH_SUCCESSFUL if (user, password) == ("atari", "falcon") else paramiko.AUTH_FAILED

    def get_allowed_auths(self, user):
        return "password"

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED


def set_attr(path, attr):
    """paramiko's SFTPServer.set_file_attr empties a file it truncates;
    OpenSSH keeps what is below the new size"""
    if attr._flags & attr.FLAG_SIZE:
        os.truncate(path, attr.st_size)
        attr._flags &= ~attr.FLAG_SIZE
    paramiko.SFTPServer.set_file_attr(path, attr)


class StubHandle(paramiko.SFTPHandle):
    def stat(self):
        try:
            return paramiko.SFTPAttributes.from_stat(os.fstat(self.readfile.fileno()))
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)

    def chattr(self, attr):
        try:
            set_attr(self.filename, attr)
            return paramiko.SFTP_OK
        except OSError as e:
            return paramiko.SFTPServer.convert_errno(e.errno)


class StubSFTP(paramiko.SFTPServerInterface):
    root = None

    def real(self, path):
        return os.path.join(self.root, self.canonicalize(path).lstrip("/"))

    @staticmethod
    def err(e):
        return paramiko.SFTPServer.convert_errno(e.errno)

    def list_folder(self, path):
        try:
            out = []
            for name in os.listdir(self.real(path)):
                a = paramiko.SFTPAttributes.from_stat(os.lstat(os.path.join(self.real(path), name)))
                a.filename = name
                out.append(a)
            return out
        except OSError as e:
            return self.err(e)

    def stat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(os.stat(self.real(path)))
        except OSError as e:
            return self.err(e)

    def lstat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(os.lstat(self.real(path)))
        except OSError as e:
            return self.err(e)

    def open(self, path, flags, attr):
        real = self.real(path)
        try:
            fd = os.open(real, flags, 0o644)
        except OSError as e:
            return self.err(e)
        mode = "ab" if flags & os.O_APPEND else "r+b" if flags & os.O_RDWR else \
            "wb" if flags & os.O_WRONLY else "rb"
        f = os.fdopen(fd, mode)
        h = StubHandle(flags)
        h.filename = real
        h.readfile = f
        h.writefile = f
        return h

    def _do(self, fn, *a):
        try:
            fn(*a)
            return paramiko.SFTP_OK
        except OSError as e:
            return self.err(e)

    def remove(self, path):
        return self._do(os.remove, self.real(path))

    def rename(self, a, b):
        return self._do(os.rename, self.real(a), self.real(b))

    def posix_rename(self, a, b):
        return self._do(os.rename, self.real(a), self.real(b))

    def mkdir(self, path, attr):
        return self._do(os.mkdir, self.real(path))

    def rmdir(self, path):
        return self._do(os.rmdir, self.real(path))

    def chattr(self, path, attr):
        try:
            set_attr(self.real(path), attr)
            return paramiko.SFTP_OK
        except OSError as e:
            return self.err(e)


def sftp_server(root, key):
    StubSFTP.root = root
    ls = socket.socket()
    ls.bind(("127.0.0.1", 0))
    ls.listen(5)

    def serve():
        while True:
            try:
                sock, _ = ls.accept()
            except OSError:
                return
            t = paramiko.Transport(sock)
            t.add_server_key(key)
            t.set_subsystem_handler("sftp", paramiko.SFTPServer, StubSFTP)
            try:
                t.start_server(server=StubServer())
            except Exception:
                pass
    threading.Thread(target=serve, daemon=True).start()
    return ls, ls.getsockname()[1]


# ---------------------------------------------------------------- an FTPS server

def sh(work, *cmd):
    subprocess.run(cmd, check=True, capture_output=True, cwd=work)


def make_certs(work):
    sh(work, "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "30",
       "-subj", "/CN=eftpgate test CA", "-keyout", "ca.key", "-out", "ca.pem",
       "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    sh(work, "openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost",
       "-keyout", "srv.key", "-out", "srv.csr")
    with open(os.path.join(work, "srv.ext"), "w") as f:
        f.write("subjectAltName=DNS:localhost\n")
    sh(work, "openssl", "x509", "-req", "-in", "srv.csr", "-CA", "ca.pem", "-CAkey", "ca.key",
       "-CAcreateserial", "-days", "30", "-out", "srv.crt", "-extfile", "srv.ext")
    with open(os.path.join(work, "srv.chain"), "w") as out:
        for part in ("srv.crt", "srv.key"):
            with open(os.path.join(work, part)) as f:
                out.write(f.read())
    return os.path.join(work, "srv.chain"), os.path.join(work, "ca.pem")


def ftps_server(home, chain, implicit=False):
    auth = DummyAuthorizer()
    auth.add_user("atari", "falcon", home, perm="elradfmwMT")
    ctx = SSL.Context(SSL.TLS_SERVER_METHOD)
    ctx.use_certificate_chain_file(chain)
    ctx.use_privatekey_file(chain)
    ctx.set_session_id(b"eftpgate-test")
    ctx.set_max_proto_version(SSL.TLS1_2_VERSION)   # session IDs, as vsftpd checks them
    stats = {"new": 0, "reused": 0}

    class D(TLS_DTPHandler):
        def handle_ssl_established(self):
            stats["reused" if ssl_lib.SSL_session_reused(self.socket._ssl) else "new"] += 1
            super().handle_ssl_established()

    class H(TLS_FTPHandler):
        ssl_context = ctx
        tls_control_required = True
        tls_data_required = True
        dtp_handler = D

        def __init__(self, conn, srv, ioloop=None):
            super().__init__(conn, srv, ioloop)
            if implicit and self.connected:
                self.secure_connection(self.ssl_context)
    H.authorizer = auth
    s = FTPServer(("127.0.0.1", 0), H, ioloop=IOLoop())    # one loop each: two servers run at once
    threading.Thread(target=s.serve_forever, kwargs={"timeout": 0.2}, daemon=True).start()
    return s, s.socket.getsockname()[1], stats


# ---------------------------------------------------------------- eftpgate itself

def gateway(state, allow=None, allow_local=True, cafile=None, pasv=None):
    a = argparse.Namespace(allow=allow, allow_local=allow_local, state=state, pasv_ports=pasv, cafile=cafile)
    srv = eftpgate.Server(("127.0.0.1", 0), eftpgate.Session)
    srv.cfg = eftpgate.Config(a)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


class Base(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="eftpgate-")
        cls.home = os.path.join(cls.work, "home")
        os.makedirs(os.path.join(cls.home, "pub"))
        cls.data = bytes(range(256)) * 300 + b"end"
        with open(os.path.join(cls.home, "pub", "demo.bin"), "wb") as f:
            f.write(cls.data)
        with open(os.path.join(cls.home, "pub", "readme.txt"), "wb") as f:
            f.write(b"line one\nline two\n" * 3000)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def atari(self, port, user, pw="falcon"):
        f = ftplib.FTP()
        f.connect("127.0.0.1", port, timeout=30)
        f.said = f.login(user, pw)
        return f

    def get(self, f, name, rest=None, text=False):
        out = io.BytesIO()
        if text:            # TYPE A, and the bytes as they come (retrbinary would say TYPE I)
            f.voidcmd("TYPE A")
            with f.transfercmd("RETR " + name, rest) as c:
                while True:
                    b = c.recv(8192)
                    if not b:
                        break
                    out.write(b)
            f.voidresp()
            f.voidcmd("TYPE I")
        else:
            f.retrbinary("RETR " + name, out.write, rest=rest)
        return out.getvalue()

    def put_text(self, f, name, data):
        f.voidcmd("TYPE A")
        with f.transfercmd("STOR " + name) as c:
            for i in range(0, len(data), 4093):     # CR and LF apart now and then
                c.sendall(data[i:i + 4093])
        f.voidresp()
        f.voidcmd("TYPE I")


class SFTPTests(Base):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.key = paramiko.ECDSAKey.generate()
        cls.ls, cls.sport = sftp_server(cls.home, cls.key)
        cls.state = os.path.join(cls.work, "state")
        cls.gw, cls.port = gateway(cls.state, pasv="30600-30602")     # as install.sh sets it, smaller
        cls.user = "sftp:atari@127.0.0.1:%d" % cls.sport

    @classmethod
    def tearDownClass(cls):
        cls.gw.shutdown()
        cls.ls.close()
        super().tearDownClass()

    def test_login_and_list(self):
        f = self.atari(self.port, self.user)
        self.assertIn("SFTP to 127.0.0.1", f.said)
        names = {n: facts for n, facts in f.mlsd("/pub")}
        self.assertEqual(int(names["demo.bin"]["size"]), len(self.data))
        self.assertEqual(names["demo.bin"]["type"], "file")
        self.assertTrue(names["demo.bin"]["modify"].isdigit())
        lines = []
        f.retrlines("LIST /pub", lines.append)
        self.assertTrue(any(l.startswith("-rw") and l.endswith(" demo.bin") for l in lines), lines)
        f.quit()

    def test_one_look_up_and_the_answer_is_used(self):
        # the server's name is looked up once, checked, and connected to
        real = socket.getaddrinfo
        asked = []

        def gai(host, *a, **k):
            if host.endswith(".example"):
                asked.append(host)
            return real("127.0.0.1", *a, **k)
        with mock.patch("socket.getaddrinfo", gai):
            f = self.atari(self.port, "sftp:atari@sftp.example:%d" % self.sport)
            self.assertIn("SFTP to sftp.example", f.said)
            f.quit()
        self.assertEqual(asked, ["sftp.example"])

    def test_wrong_password(self):
        with self.assertRaises(ftplib.error_perm) as e:
            self.atari(self.port, self.user, "atari")
        self.assertTrue(str(e.exception).startswith("530"))

    def test_bad_login_form(self):
        with self.assertRaises(ftplib.error_perm):
            self.atari(self.port, "atari")

    def test_download_and_resume(self):
        f = self.atari(self.port, self.user)
        f.cwd("pub")
        self.assertEqual(f.pwd(), "/pub")
        self.assertEqual(self.get(f, "demo.bin"), self.data)
        self.assertEqual(self.get(f, "demo.bin", rest=1000), self.data[1000:])
        self.assertEqual(f.size("demo.bin"), len(self.data))
        f.quit()

    def test_text_line_ends(self):
        f = self.atari(self.port, self.user)
        got = self.get(f, "/pub/readme.txt", text=True)
        self.assertEqual(got, b"line one\r\nline two\r\n" * 3000)
        self.put_text(f, "/pub/note.txt", b"hello\r\nworld\r\n" * 5000)
        self.assertEqual(open(os.path.join(self.home, "pub", "note.txt"), "rb").read(), b"hello\nworld\n" * 5000)
        f.quit()

    def test_upload_resume_and_folders(self):
        f = self.atari(self.port, self.user)
        f.mkd("/up")
        f.storbinary("STOR /up/a.bin", io.BytesIO(self.data[:5000]))
        f.storbinary("STOR /up/a.bin", io.BytesIO(self.data[5000:]), rest=5000)
        self.assertEqual(open(os.path.join(self.home, "up", "a.bin"), "rb").read(), self.data)
        f.sendcmd("MFMT 20200102030405 /up/a.bin")
        self.assertEqual(f.sendcmd("MDTM /up/a.bin"), "213 20200102030405")
        f.sendcmd("SITE CHMOD 600 /up/a.bin")
        self.assertEqual(os.stat(os.path.join(self.home, "up", "a.bin")).st_mode & 0o777, 0o600)
        f.rename("/up/a.bin", "/up/b.bin")
        self.assertTrue(os.path.exists(os.path.join(self.home, "up", "b.bin")))
        f.delete("/up/b.bin")
        f.rmd("/up")
        self.assertFalse(os.path.exists(os.path.join(self.home, "up")))
        with self.assertRaises(ftplib.error_perm):
            f.cwd("/nowhere")
        f.quit()

    def test_host_key_remembered(self):
        self.atari(self.port, self.user).quit()
        kh = open(os.path.join(self.state, "known_hosts")).read()
        self.assertIn("[127.0.0.1]:%d" % self.sport, kh)
        # the same server with another key: refused
        ls, sport = sftp_server(self.home, paramiko.ECDSAKey.generate())
        try:
            # pretend it is the one we know: copy the known key to its port
            keys = paramiko.HostKeys(os.path.join(self.state, "known_hosts"))
            keys.add("[127.0.0.1]:%d" % sport, self.key.get_name(), self.key)
            keys.save(os.path.join(self.state, "known_hosts"))
            with self.assertRaises(ftplib.error_perm) as e:
                self.atari(self.port, "sftp:atari@127.0.0.1:%d" % sport)
            self.assertIn("key has changed", str(e.exception))
        finally:
            ls.close()

    @unittest.skipUnless(os.path.exists(CLI), "build/ftp-cli isn't built")
    def test_eftp_core(self):
        down = os.path.join(self.work, "down")
        os.makedirs(down, exist_ok=True)
        r = subprocess.run([CLI, "127.0.0.1", str(self.port), self.user, "falcon", "get", "/pub", down],
                           capture_output=True, text=True, timeout=60, cwd=self.work)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(open(os.path.join(down, "PUB", "DEMO.BIN"), "rb").read(), self.data)
        self.assertEqual(open(os.path.join(down, "PUB", "README.TXT"), "rb").read(),
                         b"line one\r\nline two\r\n" * 3000)


class FTPSTests(Base):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.chain, cls.ca = make_certs(cls.work)
        cls.srv, cls.sport, cls.stats = ftps_server(cls.home, cls.chain)
        cls.isrv, cls.isport, _ = ftps_server(cls.home, cls.chain, implicit=True)
        cls.gw, cls.port = gateway(os.path.join(cls.work, "state"), cafile=cls.ca)

    @classmethod
    def tearDownClass(cls):
        cls.gw.shutdown()
        cls.srv.close_all()
        cls.isrv.close_all()
        super().tearDownClass()

    def test_explicit(self):
        f = self.atari(self.port, "ftps:atari@localhost:%d" % self.sport)
        names = {n for n, _ in f.mlsd("/pub")}
        self.assertIn("demo.bin", names)
        self.assertEqual(self.get(f, "/pub/demo.bin"), self.data)
        self.assertEqual(self.get(f, "/pub/demo.bin", rest=777), self.data[777:])
        f.storbinary("STOR /pub/up.bin", io.BytesIO(self.data[::-1]))
        self.assertEqual(open(os.path.join(self.home, "pub", "up.bin"), "rb").read(), self.data[::-1])
        f.quit()
        self.assertGreater(self.stats["reused"], 0)
        self.assertEqual(self.stats["new"], 0, "every data connection resumes the control's TLS session")

    def test_implicit(self):
        f = self.atari(self.port, "ftps-implicit:atari@localhost:%d" % self.isport)
        self.assertEqual(self.get(f, "/pub/demo.bin"), self.data)
        f.quit()

    def test_certificate_checked(self):
        gw, port = gateway(os.path.join(self.work, "state2"))      # the system's roots only
        try:
            with self.assertRaises(ftplib.error_perm) as e:
                self.atari(port, "ftps:atari@localhost:%d" % self.sport)
            self.assertIn("certificate", str(e.exception))
        finally:
            gw.shutdown()

    @unittest.skipUnless(os.path.exists(CLI), "build/ftp-cli isn't built")
    def test_eftp_core(self):
        r = subprocess.run([CLI, "127.0.0.1", str(self.port), "ftps:atari@localhost:%d" % self.sport, "falcon",
                            "ls", "/pub"], capture_output=True, text=True, timeout=60, cwd=self.work)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("demo.bin", r.stdout)


class AccessTests(Base):
    def test_only_the_home_network(self):
        gw, port = gateway(os.path.join(self.work, "s"), allow=["10.9.9.9"])
        try:
            with self.assertRaises(ftplib.error_temp):
                f = ftplib.FTP()
                f.connect("127.0.0.1", port, timeout=10)
        finally:
            gw.shutdown()

    def test_home_servers_need_allow_local(self):
        gw, port = gateway(os.path.join(self.work, "s"), allow_local=False)
        try:
            with self.assertRaises(ftplib.error_perm) as e:
                self.atari(port, "sftp:atari@127.0.0.1:22")
            self.assertIn("allow-local", str(e.exception))
        finally:
            gw.shutdown()

    def test_parse_login(self):
        self.assertEqual(eftpgate.parse_login("sftp:erez@my.server.net"), ("sftp", "erez", "my.server.net", 22))
        self.assertEqual(eftpgate.parse_login("ftps:me@home.org@ftp.x.org:2121"),
                         ("ftps", "me@home.org", "ftp.x.org", 2121))
        self.assertEqual(eftpgate.parse_login("FTPS-IMPLICIT:a@b"), ("ftps-implicit", "a", "b", 990))
        self.assertIsNone(eftpgate.parse_login("atari"))

    def test_list_parsing(self):
        now = time.time()
        e = eftpgate.parse_list("-rw-r--r--   1 ftp  ftp     12345 Jan 02  2020 demo file.bin", now)
        self.assertEqual((e.name, e.size, e.isdir, e.mode), ("demo file.bin", 12345, False, 0o644))
        e = eftpgate.parse_list("drwxr-xr-x 2 ftp ftp 4096 Mar 3 12:00 pub", now)
        self.assertTrue(e.isdir)
        e = eftpgate.parse_list("01-02-20  03:04PM       <DIR>          Games", now)
        self.assertTrue(e.isdir and e.name == "Games")


if __name__ == "__main__":
    unittest.main()
