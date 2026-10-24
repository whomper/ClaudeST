#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""aera-gateway on a stand-in Pi: its files in a folder of their own, its
commands (systemctl, ufw, apt-get...) played by the test. The services'
units, settings, the take-over of the apps' own set-ups and the way back
when a new gateway doesn't start.

    python3 -m unittest gateway/test_installer.py
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import os
import shutil
import subprocess
import tempfile
import unittest
import unittest.mock

HERE = os.path.dirname(os.path.abspath(__file__))
_loader = importlib.machinery.SourceFileLoader("aera_gateway", os.path.join(HERE, "aera-gateway"))
_spec = importlib.util.spec_from_loader("aera_gateway", _loader)
ag = importlib.util.module_from_spec(_spec)
_loader.exec_module(ag)


class FakePi(ag.Pi):
    def __init__(self, root):
        super().__init__(root)
        self.cmds = []
        self.users = set()
        self.active = set()
        self.ports = set()
        self.broken = False         # the next gateway started doesn't answer
        self.ufw = False
        self.rules = []
        self.answers = []
        self.busy = set()           # ports another program has
        self.programs = set()       # installed beyond python3 (chromium, Xvfb...)
        self.no_modules = set()     # Python modules that aren't there

    def have(self, prog):
        return prog == "python3" or (prog == "ufw" and self.ufw is not None)

    def which(self, *progs):
        return next(("/usr/bin/" + p for p in progs if p in self.programs), None)

    def is_root(self):
        return True

    def tty(self):
        return bool(self.answers)

    def ask(self, prompt, secret=False):
        return self.answers.pop(0) if self.answers else ""

    def sleep(self, s):
        pass

    def own(self, fd, p, owner):
        self.cmds.append(("chown", owner, p))

    def started(self, name):
        unit = self.read(ag.unit_path(name)) or ""
        self.active.discard(name)
        if name == "claude-st":
            self.active.add(name)
            return
        script, args = ag.exec_args(unit, name + ".py")
        if script is None:                  # not a gateway (stunnel4)
            return
        if self.broken and script.startswith(ag.OPT) or not os.path.exists(self.path(script)):
            return
        if name == "emailgate":
            ports = args[args.index("--ports") + 1].split(",")
        else:
            ports = [args[args.index("--port") + 1]]
        if self.busy & set(ports):
            return
        self.active.add(name)
        self.ports |= set(ports)

    def run(self, *cmd, check=True, out="show", errors=True, input=None):
        self.cmds.append(cmd)
        rc, text = 0, ""
        c = list(cmd)
        if c[0] == "id":
            rc = 0 if c[1] in self.users else 1
        elif c[0] == "useradd":
            self.users.add(c[-1])
        elif c[:2] == ["systemctl", "restart"]:
            self.started(c[2].replace(".service", ""))
        elif c[:3] == ["systemctl", "disable", "--now"]:
            self.active.discard(c[3].replace(".service", ""))
        elif c[:2] == ["systemctl", "is-active"]:
            name = c[-1]
            rc = 0 if name in self.active else 3
            text = "active\n" if rc == 0 else "inactive\n"
        elif c[0] == "ss":
            port = c[-1].split(":")[-1].strip(" )")
            text = "LISTEN 0 5 0.0.0.0:%s\n" % port if port in self.ports | self.busy else ""
        elif c[:2] == ["ufw", "status"]:
            text = "Status: active\n" if self.ufw else "Status: inactive\n"
        elif c[:2] == ["ufw", "allow"]:
            self.rules.append((c[3], c[7]))
        elif c[:2] == ["hostname", "-I"]:
            text = "192.168.1.10 fd00::10\n"
        elif c[:3] == ["python3", "-m", "venv"]:
            os.makedirs(os.path.join(c[3], "bin"), exist_ok=True)
            with open(os.path.join(c[3], "bin", "python"), "w"):
                pass
            os.chmod(os.path.join(c[3], "bin", "python"), 0o755)
        elif c[0] == "ls":
            rc = 2
        elif c[0] == "make":            # aerapens' build: a stand-in for what it makes
            with open(os.path.join(c[c.index("-C") + 1], "aerapens"), "wb") as f:
                f.write(b"\x7fELF\x02\x01\x01\0" + bytes(range(256)))       # a program's bytes, not text
        elif c[:2] == ["python3", "-c"] and c[2].startswith("import "):
            rc = 1 if c[2][7:] in self.no_modules else 0
        if check and rc:
            raise ag.Failed("%s failed" % " ".join(cmd))
        return subprocess.CompletedProcess(cmd, rc, text if out == "keep" else None)


OLD_MAIL = """[Unit]
Description=EMail gateway for the Atari (TLS to the mail provider)
After=network-online.target
Wants=network-online.target

[Service]
User=emailgate
ExecStart=/usr/bin/python3 /usr/local/bin/emailgate.py -v --ports 2143,2587,2110 --allow 192.168.1.50 \
imap.mail.me.com smtp.mail.me.com:587
Restart=always
RestartSec=5
# the mail ports are below 1024: that one privilege, and no other
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
"""

OLD_DAV = """[Unit]
Description=ECal gateway for the Atari (HTTPS for CalDAV and subscriptions)
After=network-online.target
Wants=network-online.target

[Service]
User=ecalgate
ExecStart=/usr/bin/python3 /usr/local/bin/ecalgate.py --port 8180 --allow-local
Environment=HTTPS_PROXY=http://192.168.1.2:3128
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
"""

OLD_FTP = """[Unit]
Description=EFTP gateway for the Atari (FTPS and SFTP)
After=network-online.target
Wants=network-online.target

[Service]
User=eftpgate
ExecStart=/usr/bin/python3 /usr/local/bin/eftpgate.py --port 2121 --pasv-ports 30000-30099 --allow 192.168.1.50 \
--cafile /etc/ssl/nas-ca.pem -v
StateDirectory=eftpgate
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
"""


class Base(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="aera-gateway-test.")
        self.addCleanup(shutil.rmtree, self.root, True)
        self.pi = FakePi(self.root)
        self.pi.write("/etc/ssl/certs/ca-certificates.crt", "roots")

    def run_ag(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = ag.main(list(argv), self.pi)
        self.out, self.err = out.getvalue(), err.getvalue()
        return rc

    def unit(self, name):
        return self.pi.read(ag.unit_path(name))

    def execstart(self, name):
        return [line for line in self.unit(name).splitlines() if line.startswith("ExecStart=")][0]


class WriteTest(unittest.TestCase):
    """A file with keys in it is never open to others, not even while
    it is being written."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)
        self.umask = os.umask(0)
        self.addCleanup(os.umask, self.umask)

    def test_never_wider_than_mode(self):
        seen = []

        class Watched(ag.Pi):
            def own(self, fd, p, owner):
                seen.append(os.fstat(fd).st_mode & 0o777)

        pi = Watched(self.root)
        full = pi.path("/etc/claude-st/claude-st.env")
        os.makedirs(os.path.dirname(full))
        with open(full + ".aera-new", "w") as f:         # left over, open to all
            f.write("old\n")
        os.chmod(full + ".aera-new", 0o666)
        replace = os.replace

        def replacing(a, b):
            seen.append(os.stat(a).st_mode & 0o777)
            replace(a, b)
        with unittest.mock.patch.object(ag.os, "replace", replacing):
            pi.write("/etc/claude-st/claude-st.env", "CLAUDE_SESSION_KEY=sk\n", 0o640, "root:claude-st")
        self.assertEqual(seen, [0o600, 0o640])
        self.assertEqual(os.stat(full).st_mode & 0o777, 0o640)
        with open(full) as f:
            self.assertEqual(f.read(), "CLAUDE_SESSION_KEY=sk\n")
        self.assertFalse(os.path.exists(full + ".aera-new"))

    def test_mode_not_the_umask(self):
        os.umask(0o077)
        pi = ag.Pi(self.root)
        pi.write("/opt/x/run.py", "#\n", 0o755)
        self.assertEqual(os.stat(pi.path("/opt/x/run.py")).st_mode & 0o777, 0o755)


class WordsTest(unittest.TestCase):
    def test_round_trip(self):
        args = ["--cafile", "/etc/ssl/my nas/ca.pem", "100%", "$HOME", 'say "hi"', "a\\b"]
        line = "ExecStart=/usr/bin/python3 /opt/aera-gateway/eftpgate.py " + " ".join(map(ag.unit_word, args))
        self.assertEqual(ag.exec_args(line, "eftpgate.py"), ("/opt/aera-gateway/eftpgate.py", args))
        self.assertIn("100%%", line)
        self.assertIn("$$HOME", line)

    def test_allow_list(self):
        self.assertEqual(ag.allow_list(["192.168.1.5,192.168.1.6", "192.168.1.7"]),
                         ["192.168.1.5", "192.168.1.6", "192.168.1.7"])
        self.assertEqual(ag.allow_list(["192.168.1.5", "any"]), [])


class MailTest(Base):
    def test_fresh(self):
        self.assertEqual(self.run_ag("install", "mail", "imap.gmail.com", "smtp.gmail.com", "pop.gmail.com"), 0,
                         self.err)
        self.assertEqual(self.execstart("emailgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --ports 143,587,110 "
                         "imap.gmail.com smtp.gmail.com pop.gmail.com")
        self.assertIn("AmbientCapabilities=CAP_NET_BIND_SERVICE", self.unit("emailgate"))
        for f in ag.FILES:
            self.assertTrue(self.pi.exists("/opt/aera-gateway/" + f), f)
        self.assertIn("emailgate", self.pi.users)
        self.assertIn("Incoming:  IMAP   Server: 192.168.1.10   Port: 143", self.out)
        self.assertIn("(or POP3        Server: 192.168.1.10   Port: 110)", self.out)
        self.assertIn("Outgoing (SMTP)   Server: 192.168.1.10   Port: 587", self.out)

    def test_servers_needed(self):
        self.assertEqual(self.run_ag("install", "mail"), 2)
        self.assertIn("IMAP_SERVER SMTP_SERVER", self.err)
        self.assertIsNone(self.unit("emailgate"))

    def test_settings_kept_and_changed(self):
        self.run_ag("install", "mail", "--allow", "192.168.1.50", "imap.gmail.com", "smtp.gmail.com")
        self.assertEqual(self.run_ag("install", "mail", "--ports", "2143,2587,2110"), 0, self.err)
        self.assertEqual(self.execstart("emailgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --ports 2143,2587,2110 "
                         "--allow 192.168.1.50 imap.gmail.com smtp.gmail.com")
        self.assertIn("for 192.168.1.50 only", self.out)
        self.assertNotIn("POP3", self.out)
        self.assertNotIn("CAP_NET_BIND_SERVICE", self.unit("emailgate"))     # no low port: no privilege
        self.run_ag("install", "mail", "--allow", "any")
        self.assertNotIn("--allow", self.execstart("emailgate"))

    def stunnel(self):
        """EMail 1.x's set-up, and what systemctl stop stunnel4 could see"""
        self.pi.write(ag.Mail.STUNNEL, "pid = /run/stunnel4/atari-mail.pid\n[imap]\naccept = 0.0.0.0:143\n")
        self.pi.write("/run/stunnel4/atari-mail.pid", "4242\n")
        seen, run = [], self.pi.run

        def watched(*cmd, **kw):
            if cmd[:3] == ("systemctl", "stop", "stunnel4"):
                seen.append(self.pi.exists(ag.Mail.STUNNEL))
            return run(*cmd, **kw)
        self.pi.run = watched
        return seen

    def test_stunnel_goes(self):
        seen = self.stunnel()
        self.assertEqual(self.run_ag("install", "mail", "imap.mail.me.com", "smtp.mail.me.com:587"), 0, self.err)
        self.assertEqual(seen, [True])          # stopped while its .conf still said how
        self.assertIn(("kill", "4242"), self.pi.cmds)
        self.assertFalse(self.pi.exists(ag.Mail.STUNNEL))
        self.assertFalse(self.pi.exists(ag.Mail.STUNNEL + ".aera-old"))
        self.assertIn(("systemctl", "disable", "--now", "stunnel4"), self.pi.cmds)
        self.assertIn("and stopped stunnel", self.out)

    def test_stunnel_settings_kept(self):
        # EMail 1.x on its own ports, iCloud's STARTTLS, POP3, and --allow's ufw rules
        self.pi.write(ag.Mail.STUNNEL, "pid = /run/stunnel-atari-mail.pid\n"
                      "[imap]\nclient = yes\naccept = 0.0.0.0:1143\nconnect = imap.mail.me.com:993\n"
                      "[smtp]\nclient = yes\naccept = 0.0.0.0:1025\nconnect = smtp.mail.me.com:587\n"
                      "protocol = smtp\n"
                      "[pop3]\nclient = yes\naccept = 0.0.0.0:1110\nconnect = pop.example.com:995\n")
        status = self.pi.out

        def out(*cmd):
            if cmd[:3] == ("ufw", "show", "added"):         # whether ufw is on or not
                return "Added user rules (see 'ufw status' for running firewall):\n" \
                       "ufw allow from 192.168.1.50 to any port 1143 proto tcp\nufw deny 1143/tcp\n" \
                       "ufw deny 1025/tcp\nufw deny 1110/tcp\n"
            return status(*cmd)
        self.pi.out = out
        self.assertEqual(self.run_ag("install", "mail", "--allow", "192.168.1.50"), 0, self.err)
        self.assertEqual(self.execstart("emailgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --ports 1143,1025,1110 "
                         "--allow 192.168.1.50 imap.mail.me.com smtp.mail.me.com:587 pop.example.com")
        for port in ("1143", "1025", "1110"):
            self.assertIn(("ufw", "delete", "deny", port + "/tcp"), self.pi.cmds)
        self.assertNotIn("change them in EMail", self.out)

        # the same, given other ports: EMail must be told
        self.setUp()
        self.pi.write(ag.Mail.STUNNEL, "[imap]\naccept = 0.0.0.0:1143\nconnect = imap.gmail.com:993\n"
                      "[smtp]\naccept = 0.0.0.0:1025\nconnect = smtp.gmail.com:465\n")
        self.assertEqual(self.run_ag("install", "mail", "--ports", "143,587"), 0, self.err)
        self.assertIn("--ports 143,587,110 imap.gmail.com smtp.gmail.com\n",
                      self.execstart("emailgate") + "\n")
        self.assertIn("was on ports 1143, 1025; emailgate is on 143, 587", self.out)

    def test_stunnel_back(self):
        # emailgate doesn't start: EMail 1.x's set-up is as it was
        self.stunnel()
        self.pi.broken = True
        self.assertEqual(self.run_ag("install", "mail", "imap.mail.me.com", "smtp.mail.me.com:587"), 1)
        self.assertTrue(self.pi.exists(ag.Mail.STUNNEL))
        self.assertFalse(self.pi.exists(ag.Mail.STUNNEL + ".aera-old"))
        self.assertIn(("systemctl", "restart", "stunnel4"), self.pi.cmds)
        self.assertNotIn(("systemctl", "disable", "--now", "stunnel4"), self.pi.cmds)
        self.assertIn("is back", self.out)

    def test_firewall(self):
        self.pi.ufw = True
        self.run_ag("install", "mail", "imap.gmail.com", "smtp.gmail.com")
        self.assertEqual(sorted(self.pi.rules), sorted((f, p) for f in ag.HOME_NETS for p in ("143", "587")))


class TakeOverTest(Base):
    """A Pi set up by the apps' own install.sh"""

    def setUp(self):
        super().setUp()
        for name, unit in (("emailgate", OLD_MAIL), ("ecalgate", OLD_DAV), ("eftpgate", OLD_FTP)):
            self.pi.write(ag.unit_path(name), unit)
            self.pi.write("/usr/local/bin/%s.py" % name, "# the app's copy\n", 0o755)
            self.pi.users.add(name)
            self.pi.started(name)
        self.pi.write("/var/lib/eftpgate/known_hosts", "[sftp.example]:22 ssh-ed25519 AAAA\n", 0o600)

    def test_update_keeps_everything(self):
        self.assertEqual(self.run_ag("update"), 0, self.err)
        self.assertEqual(self.execstart("emailgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --ports 2143,2587,2110 "
                         "--allow 192.168.1.50 imap.mail.me.com smtp.mail.me.com:587")
        self.assertEqual(self.execstart("ecalgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/ecalgate.py --port 8180 --allow-local")
        self.assertIn("Environment=HTTPS_PROXY=http://192.168.1.2:3128", self.unit("ecalgate"))
        self.assertEqual(self.execstart("eftpgate"),
                         "ExecStart=/usr/bin/python3 /opt/aera-gateway/eftpgate.py --port 2121 --pasv-ports "
                         "30000-30099 --allow 192.168.1.50 --cafile /etc/ssl/nas-ca.pem -v")
        self.assertIn("StateDirectory=eftpgate", self.unit("eftpgate"))
        for name in ("emailgate", "ecalgate", "eftpgate"):
            self.assertFalse(self.pi.exists("/usr/local/bin/%s.py" % name), name)
            self.assertIn(name, self.pi.active)
        self.assertEqual(self.pi.read("/var/lib/eftpgate/known_hosts"), "[sftp.example]:22 ssh-ed25519 AAAA\n")
        self.assertFalse(any(c[0] == "useradd" for c in self.pi.cmds))
        self.run_ag("status")
        self.assertNotIn("take", self.out)

    def test_status_before(self):
        self.run_ag("status")
        self.assertIn("app's own install.sh", self.out)
        self.assertIn("port 2143 2587; only 192.168.1.50; imap.mail.me.com smtp.mail.me.com:587", self.out)

    def test_old_one_back_when_the_new_one_fails(self):
        self.pi.broken = True
        self.assertEqual(self.run_ag("install", "dav", "--port", "8181"), 1)
        self.assertIn("ecalgate didn't start. The one before is running again.", self.err)
        self.assertEqual(self.unit("ecalgate"), OLD_DAV)
        self.assertTrue(self.pi.exists("/usr/local/bin/ecalgate.py"))
        self.assertFalse(self.pi.exists("/opt/aera-gateway"))
        self.assertIn("ecalgate", self.pi.active)

    def test_update_goes_back_too(self):
        self.pi.broken = False
        self.run_ag("update")
        before = self.pi.read("/opt/aera-gateway/aeragate.py")
        self.pi.write("/opt/aera-gateway/aeragate.py", "# the one running\n")
        unit = self.unit("ecalgate")
        self.pi.broken = True
        self.assertEqual(self.run_ag("install", "dav", "--no-allow-local"), 1)
        self.assertEqual(self.unit("ecalgate"), unit)
        self.assertEqual(self.pi.read("/opt/aera-gateway/aeragate.py"), "# the one running\n")
        self.assertNotEqual(before, "# the one running\n")

    def test_remove(self):
        self.run_ag("update")
        self.assertEqual(self.run_ag("remove", "ftp"), 0)
        self.assertIn("SSH keys it remembered stay", self.out)
        self.assertIsNone(self.unit("eftpgate"))
        self.assertTrue(self.pi.exists("/var/lib/eftpgate/known_hosts"))
        self.assertTrue(self.pi.exists("/opt/aera-gateway"))
        self.run_ag("remove", "mail")
        self.run_ag("remove", "cal")
        self.assertFalse(self.pi.exists("/opt/aera-gateway"))


class DavFtpTest(Base):
    def test_dav_settings(self):
        self.assertEqual(self.run_ag("install", "dav"), 0, self.err)
        self.assertEqual(self.execstart("ecalgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/ecalgate.py "
                                                     "--port 8180")
        self.assertIn("Pi gateway:  192.168.1.10", self.out)
        self.run_ag("install", "ecal", "--allow-local", "--allow", "192.168.1.50,192.168.1.51")
        self.run_ag("install", "econtact", "--port", "8181", "-v")
        self.assertEqual(self.execstart("ecalgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/ecalgate.py "
                                                     "--port 8181 --allow 192.168.1.50 --allow 192.168.1.51 "
                                                     "--allow-local -v")
        self.run_ag("install", "dav", "--no-allow-local", "--no-verbose", "--allow", "any")
        self.assertEqual(self.execstart("ecalgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/ecalgate.py "
                                                     "--port 8181")

    def test_ftp_fresh_and_firewall(self):
        self.pi.ufw = True
        self.assertEqual(self.run_ag("install", "ftp", "--allow", "192.168.1.50"), 0, self.err)
        self.assertEqual(self.execstart("eftpgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/eftpgate.py "
                                                     "--port 2121 --pasv-ports 30000-30099 --allow 192.168.1.50")
        self.assertEqual(self.pi.rules, [("192.168.1.50", "2121"), ("192.168.1.50", "30000:30099")])
        self.assertIn("Options > Settings > Secure", self.out)

    def test_web(self):
        self.pi.ufw = True
        self.assertEqual(self.run_ag("install", "eweb", "--allow", "192.168.1.50"), 0, self.err)
        self.assertEqual(self.execstart("webgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/webgate.py "
                                                    "--port 8280 --allow 192.168.1.50")
        self.assertEqual(self.pi.rules, [("192.168.1.50", "8280")])
        self.assertIn("In EWeb on the Atari, Settings > Secure:", self.out)
        self.assertIn("Pi gateway:  192.168.1.10", self.out)
        self.assertIn("Port:        8280", self.out)
        self.assertTrue(self.pi.exists("/opt/aera-gateway/webgate.py"))
        self.assertNotIn(("apt-get", "install", "-y", "-qq", "python3-pil"), self.pi.cmds)  # Pillow there
        self.pi.no_modules = {"PIL"}
        self.run_ag("install", "web")
        self.assertIn(("apt-get", "install", "-y", "-qq", "python3-pil"), self.pi.cmds)
        self.pi.no_modules = set()
        self.run_ag("install", "web", "--allow-local", "-v")
        self.assertEqual(self.execstart("webgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/webgate.py "
                                                    "--port 8280 --allow 192.168.1.50 --allow-local -v")

    def test_web_browser(self):
        self.pi.write("/proc/meminfo", "MemTotal:        3884136 kB\nMemFree: 1 kB\n")
        self.assertEqual(self.run_ag("install", "web", "--browser"), 0, self.err)
        self.assertEqual(self.execstart("webgate"), "ExecStart=/usr/bin/python3 /opt/aera-gateway/webgate.py "
                                                    "--port 8280 --browser")
        self.assertIn("Environment=DISPLAY=:99", self.unit("webgate"))
        installed = [c for c in self.pi.cmds if c[:2] == ("apt-get", "install")][-1]
        for pkg in ("xvfb", "xdotool"):
            self.assertIn(pkg, installed)
        screen, browser = self.unit("webgate-screen"), self.unit("webgate-browser")
        self.assertIn(":99 -screen 0 1024x768x24 -nolisten tcp", screen)
        for want in ("--proxy-server=http://127.0.0.1:8281", "--load-extension=/opt/aera-gateway/webgate-ext",
                     "--user-data-dir=/var/lib/webgate/browser", "JoinsNamespaceOf=webgate.service",
                     "User=webgate", "--kiosk", '"--proxy-bypass-list=<-loopback>;127.0.0.1:8281"',
                     '"--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1"', "--disable-quic",
                     "--force-webrtc-ip-handling-policy=disable_non_proxied_udp"):
            self.assertIn(want, browser)
        self.assertTrue(self.pi.exists("/opt/aera-gateway/webgate-ext/manifest.json"))
        self.assertIn(("systemctl", "restart", "webgate-browser.service"), self.pi.cmds)
        self.assertIn("the Pi's browser", self.out)
        # an update keeps it, and a new extension starts afresh
        self.pi.write("/var/lib/webgate/browser/Default/Service Worker/x", "old")
        self.pi.write("/var/lib/webgate/browser/Default/Cookies", "logins")
        self.assertEqual(self.run_ag("update"), 0, self.err)
        self.assertIn("--browser", self.execstart("webgate"))
        self.assertFalse(self.pi.exists("/var/lib/webgate/browser/Default/Service Worker"))
        self.assertTrue(self.pi.exists("/var/lib/webgate/browser/Default/Cookies"))
        # off again, and gone with the service
        self.assertEqual(self.run_ag("install", "web", "--no-browser"), 0, self.err)
        self.assertNotIn("--browser", self.execstart("webgate"))
        self.assertNotIn("DISPLAY", self.unit("webgate"))
        self.assertIsNone(self.unit("webgate-browser"))
        self.assertIsNone(self.unit("webgate-screen"))
        self.run_ag("install", "web", "--browser")
        self.assertEqual(self.run_ag("remove", "web"), 0, self.err)
        self.assertIsNone(self.unit("webgate-browser"))

    @unittest.skipUnless(os.path.isdir(os.path.join(ag.Web.PENS_SRC, "common")),
                         "needs AERA-Gateway's pens/ (an app's copy of gateway/ has none)")
    def test_web_pens(self):
        self.pi.programs = {"make"}          # no cc yet
        self.assertEqual(self.run_ag("install", "web"), 0, self.err)
        self.assertIn(("apt-get", "install", "-y", "-qq", "gcc"), self.pi.cmds)
        self.assertIn("Installing gcc, to build aerapens", self.out)
        make = [c for c in self.pi.cmds if c[0] == "make"][-1]
        self.assertEqual(make[-1], "PENS_BUILD=v" + ag.VERSION)
        self.assertFalse(make[3].startswith(os.path.dirname(ag.HERE)))      # never in the copy itself
        self.assertTrue(self.pi.exists("/opt/aera-gateway-pens/aerapens"))
        sock, svc = self.pi.read("/etc/systemd/system/webgate-pens.socket"), self.unit("webgate-pens@")
        for want in ("ListenStream=/run/webgate-pens.sock", "Accept=yes", "SocketUser=webgate", "SocketMode=0600"):
            self.assertIn(want, sock)
        for want in ("ExecStart=/opt/aera-gateway-pens/aerapens", "StandardInput=socket", "DynamicUser=yes",
                     "LimitAS=64M", "LimitCPU=2", "RuntimeMaxSec=5", "PrivateNetwork=yes",
                     "RestrictAddressFamilies=AF_UNIX"):
            self.assertIn(want, svc)
        # the socket before webgate, so that webgate's first look finds aerapens
        cmds = [c for c in self.pi.cmds if c[:2] == ("systemctl", "restart")]
        self.assertLess(cmds.index(("systemctl", "restart", "webgate-pens.socket")),
                        cmds.index(("systemctl", "restart", "webgate.service")))
        self.assertEqual(self.run_ag("remove", "web"), 0, self.err)
        self.assertIsNone(self.pi.read("/etc/systemd/system/webgate-pens.socket"))
        self.assertFalse(self.pi.exists("/opt/aera-gateway-pens"))
        self.assertFalse(any(c[:2] == ("apt-get", "remove") for c in self.pi.cmds))   # gcc stays

    def test_web_browser_needs_memory(self):
        self.pi.write("/proc/meminfo", "MemTotal:         948304 kB\n")
        self.assertEqual(self.run_ag("install", "web", "--browser"), 1)
        self.assertIn("needs a Pi with 2 GB of memory or more; this one has 926 MB", self.err)
        self.assertIsNone(self.unit("webgate"))
        self.assertFalse(any(c[:2] == ("apt-get", "install") for c in self.pi.cmds))
        self.assertEqual(self.run_ag("install", "web"), 0, self.err)        # without it, as before

    def test_cafile_must_exist(self):
        self.assertEqual(self.run_ag("install", "ftp", "--cafile", "/no/such/ca.pem"), 2)

    def test_port_taken(self):
        self.pi.busy.add("8180")
        self.assertEqual(self.run_ag("install", "dav"), 1)
        self.assertIn("port 8180 is taken by another program", self.err)
        self.assertIsNone(self.unit("ecalgate"))

    def test_unknown(self):
        self.assertEqual(self.run_ag("install", "troll"), 2)
        self.assertIn("no such service", self.err)


class ClaudeTest(Base):
    def setUp(self):
        super().setUp()
        self.src = os.path.join(self.root, "atari_claude")
        os.makedirs(os.path.join(self.src, "bridge"))
        os.makedirs(os.path.join(self.src, "pi"))
        for f in ("claude_bridge.py", "backends.py", "atari_text.py", "test_bridge.py"):
            with open(os.path.join(self.src, "bridge", f), "w") as fh:
                fh.write("# %s\n" % f)
        with open(os.path.join(self.src, "bridge", "requirements.txt"), "w") as fh:
            fh.write("pyserial\n")
        with open(os.path.join(self.src, "pi", "claude-st.service"), "w") as fh:
            fh.write("[Service]\nExecStart=/opt/claude-st/venv/bin/python ...\n")
        self.conf = ag.Claude.CONF

    def test_install_all(self):
        rc = self.run_ag("install", "all", "--allow", "192.168.1.50", "--from", self.src,
                         "--mail", "imap.mail.me.com", "smtp.mail.me.com:587")
        self.assertEqual(rc, 0, self.err)
        for name in ("emailgate", "ecalgate", "eftpgate", "webgate"):
            self.assertIn("--allow 192.168.1.50", self.execstart(name), name)
        self.assertIn("imap.mail.me.com smtp.mail.me.com:587", self.execstart("emailgate"))
        self.assertIn("--tcp 0.0.0.0:2323 --allow 192.168.1.50", self.pi.read(self.conf))
        self.assertIn("Installed: mail, dav, ftp, web, claude", self.out)
        # again, without --mail or --allow: everything kept as it is
        self.assertEqual(self.run_ag("install", "all", "--from", self.src), 0, self.err)
        self.assertIn("--allow 192.168.1.50", self.execstart("ecalgate"))
        self.assertIn("imap.mail.me.com", self.execstart("emailgate"))
        self.assertIn("--allow 192.168.1.50", self.pi.read(self.conf))

    def test_install_all_keeps_a_serial_cable(self):
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--serial", "/dev/ttyUSB0"), 0)
        self.assertEqual(self.run_ag("install", "all", "--allow", "192.168.1.50", "--from", self.src), 0, self.err)
        self.assertIn("--serial /dev/ttyUSB0", self.pi.read(self.conf))

    def test_install_all_without_mail_and_a_failure(self):
        self.pi.busy.add("2121")             # ftp's port is taken
        rc = self.run_ag("install", "all", "--allow", "192.168.1.50", "--from", self.src)
        self.assertEqual(rc, 1)
        self.assertIn("Not installed: mail (name your provider's servers", self.out)
        self.assertIn("Didn't install: ftp", self.err)
        self.assertIsNone(self.unit("emailgate"))
        self.assertIsNotNone(self.unit("webgate"))          # the others went on
        self.assertIsNotNone(self.unit("claude-st"))
        self.assertEqual(self.run_ag("install", "all", "--mail", "imap.example"), 2)

    def test_network(self):
        rc = self.run_ag("install", "claude", "--from", self.src, "--network", "--atari", "192.168.1.20",
                         "--backend", "demo")
        self.assertEqual(rc, 0, self.err)
        # as pi/install.sh left it: the defaults, CLAUDE_ST_ARGS moved to the end
        self.assertEqual(self.pi.read(self.conf), ag.Claude.DEFAULT_CONF.replace(
            "CLAUDE_ST_ARGS=--backend claudeai --serial auto --baud 19200\n", "") +
            "CLAUDE_ST_ARGS=--backend demo --tcp 0.0.0.0:2323 --allow 192.168.1.20\n")
        self.assertEqual(os.stat(self.pi.path(self.conf)).st_mode & 0o777, 0o640)
        self.assertIn(("chown", "root:claude-st", self.conf), self.pi.cmds)
        self.assertEqual(sorted(os.listdir(self.pi.path("/opt/claude-st/bridge"))),
                         ["atari_text.py", "backends.py", "claude_bridge.py", "requirements.txt"])
        self.assertEqual(self.unit("claude-st"), "[Service]\nExecStart=/opt/claude-st/venv/bin/python ...\n")
        self.assertIn(("usermod", "-aG", "dialout", "claude-st"), self.pi.cmds)
        self.assertIn(("useradd", "--system", "--home-dir", "/var/lib/claude-st", "--shell", "/usr/sbin/nologin",
                       "claude-st"), self.pi.cmds)
        self.assertIn("Options > Settings > Network:  192.168.1.10, port 2323", self.out)

        # again: what isn't given stays
        self.run_ag("install", "claude", "--from", self.src, "--listen-port", "2424")
        self.assertTrue(self.pi.read(self.conf).endswith(
            "CLAUDE_ST_ARGS=--backend demo --tcp 0.0.0.0:2424 --allow 192.168.1.20\n"))
        self.run_ag("install", "claude", "--from", self.src, "--atari", "any")
        self.assertTrue(self.pi.read(self.conf).endswith("CLAUDE_ST_ARGS=--backend demo --tcp 0.0.0.0:2424\n"))
        self.run_ag("install", "claude", "--from", self.src)
        self.assertTrue(self.pi.read(self.conf).endswith("CLAUDE_ST_ARGS=--backend demo --tcp 0.0.0.0:2424\n"))

    def test_serial_and_key(self):
        self.pi.answers = ["sk-ant-sid01-test"]
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--port", "/dev/ttyUSB0",
                                     "--baud", "9600"), 0, self.err)
        self.assertIn("CLAUDE_ST_ARGS=--backend claudeai --serial /dev/ttyUSB0 --baud 9600\n", self.pi.read(self.conf))
        self.assertIn("CLAUDE_SESSION_KEY=sk-ant-sid01-test\n", self.pi.read(self.conf))
        self.assertNotIn("sk-ant", self.out + self.err)
        self.assertIn("(no USB serial adapters plugged in)", self.out)
        self.pi.answers = ["sk-ant-sid01-new"]
        self.assertEqual(self.run_ag("set-key"), 0)
        self.assertEqual(self.pi.read(self.conf).count("CLAUDE_SESSION_KEY="), 1)
        self.assertIn("CLAUDE_SESSION_KEY=sk-ant-sid01-new\n", self.pi.read(self.conf))
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--set-key"), 1)   # nothing typed
        self.assertEqual(self.run_ag("set-key", "--help"), 2)                  # never asks for a key
        self.assertIn("it takes nothing more", self.err)

    def test_bad_settings(self):
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--network"), 2)
        self.assertIn("needs the Atari's IP address", self.err)
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--atari", "atari.local"), 2)
        self.assertEqual(self.run_ag("install", "claude", "--from", self.src, "--backend", "gpt"), 2)
        self.assertEqual(self.run_ag("install", "claude", "--from", self.root), 1)
        self.assertIn("isn't a ClaudeST copy", self.err)
        self.assertFalse(any(c[0] == "apt-get" for c in self.pi.cmds))

    def test_uninstall_keeps_settings(self):
        self.run_ag("install", "claude", "--from", self.src, "--backend", "demo")
        self.assertEqual(self.run_ag("install", "claude", "--uninstall"), 0)
        self.assertFalse(self.pi.exists("/opt/claude-st"))
        self.assertIsNone(self.unit("claude-st"))
        self.assertTrue(self.pi.exists(self.conf))


if __name__ == "__main__":
    unittest.main()
