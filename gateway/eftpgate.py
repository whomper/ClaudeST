#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""eftpgate - the Raspberry Pi gateway for EFTP on the Atari.

Servers on the internet want FTPS (FTP over TLS) or SFTP (SSH), which an
8 MHz ST can't do. So EFTP speaks plain FTP to the Pi on your home
network, and eftpgate speaks FTPS or SFTP to the server:

    Atari (EFTP) --plain FTP, home LAN--> Pi :2121 (eftpgate) --FTPS--> ftp.example.org
                                                              --SFTP--> my.server.net

The Atari names the server in its login, the way FTP proxies have always
done it:

    USER sftp:erez@my.server.net:22        (sftp, ftps, ftps-implicit)
    PASS the server's own password

and eftpgate logs in there with them. To the Atari it is an ordinary,
well-behaved FTP server: MLSD listings with exact sizes and UTC times,
SIZE, MDTM, MFMT, REST (resuming both ways), UTF-8 names, passive mode.
On the way it does what a small Atari would find hard: TLS and SSH,
checking the server (certificates against the Pi's root certificates;
SSH host keys remembered the first time and checked every time after),
and text files' line ends for SFTP servers.

Standard Python 3, plus paramiko for SFTP (Raspberry Pi OS:
python3-paramiko), and aeragate.py next to it. `aera-gateway install ftp`
sets it up as a service. Falcon mode doesn't need it for FTPS: the Falcon
does TLS itself.

    eftpgate.py [--port 2121] [--allow 192.168.1.50] [--allow-local]
                [--state /var/lib/eftpgate] [-v]
"""

import argparse
import datetime
import ftplib
import os
import posixpath
import re
import socket
import socketserver
import ssl
import stat as statmod
import time

import aeragate

VERSION = "1.1"
VERBOSE = False


def log(*a):
    aeragate.say("eftpgate:", *a)


def vlog(*a):
    if VERBOSE:
        log(*a)


class GateError(Exception):
    """an error to send to the Atari: code and words"""

    def __init__(self, code, text):
        super().__init__(text)
        self.code = code
        self.text = text


# ---------------------------------------------------------------- entries

class Entry:
    __slots__ = ("name", "isdir", "islink", "size", "mtime", "mode")

    def __init__(self, name, isdir=False, islink=False, size=0, mtime=None, mode=None):
        self.name = name
        self.isdir = isdir
        self.islink = islink
        self.size = size
        self.mtime = mtime          # seconds since 1970, UTC, or None
        self.mode = mode            # Unix permission bits, or None


def mlsd_line(e):
    facts = ["type=%s" % ("dir" if e.isdir else "OS.unix=symlink" if e.islink else "file")]
    if not e.isdir:
        facts.append("size=%d" % e.size)
    if e.mtime is not None:
        facts.append("modify=" + time.strftime("%Y%m%d%H%M%S", time.gmtime(e.mtime)))
    if e.mode is not None:
        facts.append("UNIX.mode=0%o" % (e.mode & 0o7777))
    return ";".join(facts) + "; " + e.name


def list_line(e, now):
    mode = e.mode if e.mode is not None else (0o755 if e.isdir else 0o644)
    perms = "".join(c if mode & (0o400 >> i) else "-" for i, c in enumerate("rwxrwxrwx"))
    kind = "d" if e.isdir else "l" if e.islink else "-"
    t = time.gmtime(e.mtime or 0)
    when = time.strftime("%b %d %H:%M" if now - (e.mtime or 0) < 180 * 86400 else "%b %d  %Y", t)
    return "%s%s 1 owner group %12d %s %s" % (kind, perms, 0 if e.isdir else e.size, when, e.name)


# ---------------------------------------------------------------- LIST parsing (FTPS servers without MLSD)

MONTHS = {m: i + 1 for i, m in enumerate("jan feb mar apr may jun jul aug sep oct nov dec".split())}


def parse_list(line, now):
    """one line of a Unix or DOS LIST answer -> Entry, or None"""
    m = re.match(r"(\d\d)-(\d\d)-(\d\d(?:\d\d)?)\s+(\d\d):(\d\d)([AP]M)?\s+(<DIR>|\d+)\s+(.+)$", line)
    if m:
        mo, d, y, h, mi, ap, size, name = m.groups()
        y = int(y) + (2000 if len(y) == 2 and int(y) < 70 else 1900 if len(y) == 2 else 0)
        h = int(h) % 12 + (12 if ap == "PM" else 0) if ap else int(h)
        t = datetime.datetime(y, int(mo), int(d), h, int(mi), tzinfo=datetime.timezone.utc).timestamp()
        return Entry(name, size == "<DIR>", False, 0 if size == "<DIR>" else int(size), t)
    parts = line.split(None, 8)
    if len(parts) < 9 or parts[0][0] not in "-dlbcps" or len(parts[0]) < 10:
        # no group column: try with eight
        parts = line.split(None, 7)
        if len(parts) < 8 or parts[0][0] not in "-dlbcps":
            return None
        parts.insert(3, "")
    perm, size, mon, day, tm, name = parts[0], parts[4], parts[5], parts[6], parts[7], parts[8]
    if mon.lower()[:3] not in MONTHS or not size.isdigit():
        return None
    try:
        if ":" in tm:
            hh, mm = tm.split(":")
            year = time.gmtime(now).tm_year
            t = datetime.datetime(year, MONTHS[mon.lower()[:3]], int(day), int(hh), int(mm),
                                  tzinfo=datetime.timezone.utc).timestamp()
            if t > now + 2 * 86400:
                t = datetime.datetime(year - 1, MONTHS[mon.lower()[:3]], int(day), int(hh), int(mm),
                                      tzinfo=datetime.timezone.utc).timestamp()
        else:
            t = datetime.datetime(int(tm), MONTHS[mon.lower()[:3]], int(day),
                                  tzinfo=datetime.timezone.utc).timestamp()
    except ValueError:
        t = None
    if perm[0] == "l" and " -> " in name:
        name = name.split(" -> ")[0]
    if name in (".", ".."):
        return None
    mode = sum(0o400 >> i for i, c in enumerate(perm[1:10]) if c not in "-")
    return Entry(name, perm[0] == "d", perm[0] == "l", int(size), t, mode)


# ---------------------------------------------------------------- the servers behind

class Backend:
    """what eftpgate needs from the real server; paths are absolute"""
    text_newlines = False       # convert line ends for text itself (SFTP)

    def home(self): raise NotImplementedError
    def isdir(self, path): raise NotImplementedError
    def listdir(self, path): raise NotImplementedError
    def open_read(self, path, offset, text): raise NotImplementedError
    def open_write(self, path, offset, text): raise NotImplementedError
    def size(self, path): raise NotImplementedError
    def mtime(self, path): raise NotImplementedError
    def set_mtime(self, path, t): raise NotImplementedError
    def mkdir(self, path): raise NotImplementedError
    def rmdir(self, path): raise NotImplementedError
    def remove(self, path): raise NotImplementedError
    def rename(self, a, b): raise NotImplementedError
    def chmod(self, path, mode): raise NotImplementedError
    def noop(self): pass
    def close(self): pass
    def describe(self): return ""


class SFTPBackend(Backend):
    text_newlines = True

    def __init__(self, host, port, user, password, known_hosts, addrs):
        try:
            import paramiko
        except ImportError:
            raise GateError(530, "SFTP needs paramiko on the Pi: sudo apt install python3-paramiko")
        self.paramiko = paramiko
        try:
            sock = aeragate.dial(addrs, 20)
        except OSError as e:
            raise GateError(530, "can't reach %s:%d: %s" % (host, port, e.strerror or e))
        t = paramiko.Transport(sock)
        try:
            t.start_client(timeout=20)
        except Exception as e:
            t.close()
            raise GateError(530, "%s:%d doesn't speak SSH: %s" % (host, port, e))
        self.check_host_key(t, host, port, known_hosts)
        try:
            t.auth_password(user, password)
        except paramiko.BadAuthenticationType as e:
            if "keyboard-interactive" not in (e.allowed_types or []):
                t.close()
                raise GateError(530, "%s wants another kind of login (%s)" % (host, ",".join(e.allowed_types)))
            try:
                t.auth_interactive(user, lambda title, instr, prompts: [password] * len(prompts))
            except paramiko.AuthenticationException:
                t.close()
                raise GateError(530, "login incorrect")
        except paramiko.AuthenticationException:
            t.close()
            raise GateError(530, "login incorrect")
        t.set_keepalive(60)
        self.t = t
        self.sftp = paramiko.SFTPClient.from_transport(t)
        self.desc = "SFTP to %s, %s" % (host, t.remote_cipher)

    def check_host_key(self, t, host, port, path):
        """the server's key the first time is remembered; a different one
        later is refused (someone in between, or a reinstalled server)"""
        paramiko = self.paramiko
        key = t.get_remote_server_key()
        name = host if port == 22 else "[%s]:%d" % (host, port)
        keys = paramiko.HostKeys()
        if os.path.exists(path):
            try:
                keys.load(path)
            except (IOError, paramiko.SSHException):
                pass
        known = keys.lookup(name)
        if known is not None and key.get_name() in known:
            if known[key.get_name()] != key:
                t.close()
                raise GateError(530, "%s's SSH key has changed: if that is right, remove it from %s on the Pi"
                                % (host, path))
            return
        keys.add(name, key.get_name(), key)
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            keys.save(path)
            log("remembered the SSH key of %s (%s)" % (name, key.get_name()))
        except OSError as e:
            log("can't save %s: %s" % (path, e))

    def home(self):
        return self.sftp.normalize(".")

    def isdir(self, path):
        try:
            return statmod.S_ISDIR(self.sftp.stat(path).st_mode)
        except IOError:
            return False

    def listdir(self, path):
        out = []
        for a in self.sftp.listdir_attr(path):
            mode = a.st_mode or 0
            isdir = statmod.S_ISDIR(mode)
            islink = statmod.S_ISLNK(mode)
            if islink:
                try:
                    isdir = statmod.S_ISDIR(self.sftp.stat(posixpath.join(path, a.filename)).st_mode)
                    islink = False
                except IOError:
                    pass
            out.append(Entry(a.filename, isdir, islink, a.st_size or 0, a.st_mtime, mode & 0o7777))
        return out

    def open_read(self, path, offset, text):
        f = self.sftp.open(path, "rb")
        if offset:
            f.seek(offset)
        f.prefetch()
        return f

    def open_write(self, path, offset, text):
        if offset:
            f = self.sftp.open(path, "r+b")
            f.truncate(offset)
            f.seek(offset)
        else:
            f = self.sftp.open(path, "wb")
        f.set_pipelined(True)
        return f

    def size(self, path):
        a = self.sftp.stat(path)
        if statmod.S_ISDIR(a.st_mode):
            raise GateError(550, "%s is a folder" % path)
        return a.st_size

    def mtime(self, path):
        return self.sftp.stat(path).st_mtime

    def set_mtime(self, path, t):
        self.sftp.utime(path, (t, t))

    def mkdir(self, path):
        self.sftp.mkdir(path)

    def rmdir(self, path):
        self.sftp.rmdir(path)

    def remove(self, path):
        self.sftp.remove(path)

    def rename(self, a, b):
        try:
            self.sftp.posix_rename(a, b)
        except IOError:
            self.sftp.rename(a, b)

    def chmod(self, path, mode):
        self.sftp.chmod(path, mode)

    def noop(self):
        self.sftp.stat(".")

    def close(self):
        try:
            self.sftp.close()
            self.t.close()
        except Exception:
            pass

    def describe(self):
        return self.desc


class ReusingFTP_TLS(ftplib.FTP_TLS):
    """ftplib's FTPS, with the data connections resuming the control
    connection's TLS session: vsftpd (require_ssl_reuse), FileZilla
    Server and others refuse data connections that don't"""

    addrs = None            # the server's addresses, as aeragate.lookup() checked them

    def dial(self):
        """the control connection, to the address that was checked rather
        than to a second look-up of the name (data connections go to the
        address this one reached)"""
        if self.addrs:
            return aeragate.dial(self.addrs, self.timeout)
        return socket.create_connection((self.host, self.port), self.timeout)

    def connect(self, host="", port=0, timeout=-999, source_address=None):
        if host:
            self.host = host
        if port:
            self.port = port
        if timeout != -999:
            self.timeout = timeout
        self.sock = self.dial()
        self.af = self.sock.family
        self.file = self.sock.makefile("r", encoding=self.encoding)
        self.welcome = self.getresp()
        return self.welcome

    def ntransfercmd(self, cmd, rest=None):
        conn, size = ftplib.FTP.ntransfercmd(self, cmd, rest)
        if self._prot_p:
            conn = self.context.wrap_socket(conn, server_hostname=self.host, session=self.sock.session)
        return conn, size


class ImplicitFTP_TLS(ReusingFTP_TLS):
    """FTPS from the first byte (port 990): the socket is wrapped before
    the server's greeting"""

    def connect(self, host="", port=0, timeout=-999, source_address=None):
        if host:
            self.host = host
        if port:
            self.port = port
        if timeout != -999:
            self.timeout = timeout
        self.sock = self.dial()
        self.af = self.sock.family
        self.sock = self.context.wrap_socket(self.sock, server_hostname=self.host)
        self.file = self.sock.makefile("r", encoding=self.encoding)
        self.welcome = self.getresp()
        return self.welcome


class FTPSBackend(Backend):
    def __init__(self, host, port, user, password, implicit, cafile=None, addrs=None):
        ctx = aeragate.tls_context(cafile)
        cls = ImplicitFTP_TLS if implicit else ReusingFTP_TLS
        f = cls(context=ctx, timeout=30, encoding="utf-8")
        f.addrs = addrs
        try:
            f.connect(host, port)
            if implicit:
                f.login(user, password, secure=False)
            else:
                f.login(user, password)        # AUTH TLS first
            f.prot_p()
        except ssl.SSLCertVerificationError as e:
            raise GateError(530, "%s's certificate didn't check out: %s" % (host, e.verify_message))
        except ftplib.error_perm as e:
            raise GateError(530, str(e)[4:] or "login incorrect")
        except (OSError, ftplib.Error, EOFError) as e:
            raise GateError(530, "can't connect to %s:%d: %s" % (host, port, e))
        self.f = f
        self.mlsd = "MLST" in self.features()
        try:
            f.sendcmd("OPTS UTF8 ON")
        except ftplib.Error:
            pass
        f.voidcmd("TYPE I")
        self.ascii = False
        self.desc = "FTPS to %s, %s" % (host, f.sock.version())

    def features(self):
        try:
            return self.f.sendcmd("FEAT").upper()
        except ftplib.Error:
            return ""

    def type(self, text):
        if text != self.ascii:
            self.f.voidcmd("TYPE A" if text else "TYPE I")
            self.ascii = text

    def home(self):
        return self.f.pwd()

    def isdir(self, path):
        here = self.f.pwd()
        try:
            self.f.cwd(path)
            self.f.cwd(here)
            return True
        except ftplib.error_perm:
            return False

    def listdir(self, path):
        out = []
        self.ascii = True           # ftplib's mlsd and retrlines say TYPE A themselves
        if self.mlsd:
            for name, facts in self.f.mlsd(path, ["type", "size", "modify", "unix.mode"]):
                t = facts.get("type", "").lower()
                if t in ("cdir", "pdir"):
                    continue
                mod = facts.get("modify")
                mtime = None
                if mod:
                    mtime = datetime.datetime.strptime(mod[:14], "%Y%m%d%H%M%S").replace(
                        tzinfo=datetime.timezone.utc).timestamp()
                mode = facts.get("unix.mode")
                out.append(Entry(name, t == "dir", "link" in t, int(facts.get("size", 0) or 0), mtime,
                                 int(mode, 8) if mode else None))
            return out
        lines = []
        self.f.retrlines("LIST " + path, lines.append)
        now = time.time()
        for line in lines:
            e = parse_list(line, now)
            if e:
                out.append(e)
        return out

    def open_read(self, path, offset, text):
        self.type(text)
        conn = self.f.transfercmd("RETR " + path, offset or None)
        return _FTPDataFile(self.f, conn)

    def open_write(self, path, offset, text):
        self.type(text)
        conn = self.f.transfercmd("STOR " + path, offset or None)
        return _FTPDataFile(self.f, conn)

    def size(self, path):
        self.type(False)
        n = self.f.size(path)
        if n is None:
            raise GateError(550, "no size for %s" % path)
        return n

    def mtime(self, path):
        r = self.f.sendcmd("MDTM " + path)
        return datetime.datetime.strptime(r[4:18], "%Y%m%d%H%M%S").replace(
            tzinfo=datetime.timezone.utc).timestamp()

    def set_mtime(self, path, t):
        self.f.sendcmd("MFMT %s %s" % (time.strftime("%Y%m%d%H%M%S", time.gmtime(t)), path))

    def mkdir(self, path):
        self.f.mkd(path)

    def rmdir(self, path):
        self.f.rmd(path)

    def remove(self, path):
        self.f.delete(path)

    def rename(self, a, b):
        self.f.rename(a, b)

    def chmod(self, path, mode):
        self.f.sendcmd("SITE CHMOD %o %s" % (mode, path))

    def noop(self):
        self.f.voidcmd("NOOP")

    def close(self):
        try:
            self.f.quit()
        except Exception:
            try:
                self.f.close()
            except Exception:
                pass

    def describe(self):
        return self.desc


class _FTPDataFile:
    """a data connection of the server behind, as a file"""

    def __init__(self, ftp, conn):
        self.ftp, self.conn = ftp, conn

    def read(self, n):
        return self.conn.recv(n)

    def write(self, data):
        self.conn.sendall(data)

    def close(self, ok=True):
        try:
            if ok and isinstance(self.conn, ssl.SSLSocket):
                self.conn.unwrap()
        except (OSError, ValueError):
            pass
        self.conn.close()
        try:
            self.ftp.voidresp()
        except ftplib.Error as e:
            if ok:
                raise GateError(451, str(e))


# ---------------------------------------------------------------- the Atari's side

def parse_login(spec):
    """'sftp:erez@my.server.net:22' -> (proto, user, host, port)"""
    m = re.match(r"(sftp|ftps|ftps-implicit|ftpes):(.*)@([^@:]+|\[[^]]+\])(?::(\d+))?$", spec, re.I)
    if not m:
        return None
    proto, user, host, port = m.groups()
    proto = proto.lower()
    if proto == "ftpes":
        proto = "ftps"
    host = host.strip("[]")
    port = int(port) if port else {"sftp": 22, "ftps": 21, "ftps-implicit": 990}[proto]
    return proto, user, host, port


private = aeragate.is_private


class Session(socketserver.StreamRequestHandler):
    """one Atari's FTP conversation"""
    timeout = 900          # an idle line goes after 15 minutes

    def setup(self):
        super().setup()
        self.back = None
        self.spec = None
        self.cwd = "/"
        self.text = False
        self.rest = 0
        self.rnfr = None
        self.pasv = None
        self.peer = self.client_address[0]

    # ---- the line
    def reply(self, code, text):
        lines = str(text).split("\n")
        out = "".join("%d-%s\r\n" % (code, l) for l in lines[:-1]) + "%d %s\r\n" % (code, lines[-1])
        vlog(self.peer, "<", out.strip())
        self.wfile.write(out.encode("utf-8", "replace"))

    def handle(self):
        cfg = self.server.cfg
        if not cfg.allowed(self.peer):
            self.reply(421, "eftpgate answers only the home network")
            return
        self.reply(220, "eftpgate %s: log in as sftp:user@host, ftps:user@host or ftps-implicit:user@host"
                   % VERSION)
        try:
            while True:
                raw = self.rfile.readline(4096)
                if not raw:
                    break
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                cmd, _, arg = line.partition(" ")
                cmd = cmd.upper()
                vlog(self.peer, ">", "PASS ****" if cmd == "PASS" else line)
                fn = getattr(self, "do_" + cmd, None)
                if fn is None:
                    self.reply(502, "%s isn't something eftpgate does" % cmd)
                    continue
                if self.back is None and cmd not in ("USER", "PASS", "QUIT", "FEAT", "SYST", "NOOP", "OPTS", "AUTH"):
                    self.reply(530, "log in first")
                    continue
                try:
                    if fn(arg) == "quit":
                        break
                except GateError as e:
                    self.reply(e.code, e.text)
                except (IOError, OSError, ftplib.Error) as e:
                    self.reply(550, self.words(e))
                except (ConnectionError, socket.timeout):
                    raise
                except Exception as e:          # paramiko's SSHException and the like
                    log("%s: %s: %r" % (self.peer, cmd, e))
                    self.reply(451, "the server went wrong: %s" % e)
        except (ConnectionError, socket.timeout, OSError):
            pass
        finally:
            self.close_pasv()
            if self.back:
                self.back.close()
                log("%s: logged out" % self.peer)

    @staticmethod
    def words(e):
        if isinstance(e, ftplib.Error):
            return str(e)[4:] or str(e)
        if isinstance(e, IOError) and e.errno == 2:
            return "no such file or folder"
        if isinstance(e, IOError) and e.errno == 13:
            return "permission denied"
        return str(getattr(e, "strerror", None) or e)

    def path(self, arg):
        p = arg.strip() or "."
        return posixpath.normpath(posixpath.join(self.cwd, p)).replace("//", "/")

    # ---- logging in
    def do_USER(self, arg):
        if self.back:
            self.back.close()
            self.back = None
        self.spec = parse_login(arg.strip())
        if not self.spec:
            self.reply(530, "log in as sftp:user@host[:port], ftps:user@host or ftps-implicit:user@host")
            return
        self.reply(331, "the password for %s on %s" % (self.spec[1], self.spec[2]))

    def do_PASS(self, arg):
        if not self.spec:
            self.reply(503, "USER first")
            return
        proto, user, host, port = self.spec
        cfg = self.server.cfg
        try:
            addrs = aeragate.lookup(host, port, cfg.allow_local)
        except socket.gaierror:
            raise GateError(530, "can't find %s" % host)
        except aeragate.HomeNetwork:
            raise GateError(530, "%s is on the home network: start eftpgate with --allow-local to reach it" % host)
        if proto == "sftp":
            self.back = SFTPBackend(host, port, user, arg, cfg.known_hosts, addrs)
        else:
            self.back = FTPSBackend(host, port, user or "anonymous", arg, proto == "ftps-implicit", cfg.cafile,
                                    addrs)
        self.cwd = self.back.home() or "/"
        log("%s: logged in to %s (%s)" % (self.peer, host, self.back.describe()))
        self.reply(230, "logged in to %s: %s" % (host, self.back.describe()))

    def do_AUTH(self, arg):
        self.reply(504, "no TLS here: eftpgate does it for you, towards the server")

    def do_QUIT(self, arg):
        self.reply(221, "goodbye")
        return "quit"

    def do_NOOP(self, arg):
        if self.back:
            self.back.noop()       # the server behind stays awake too
        self.reply(200, "still here")

    def do_SYST(self, arg):
        self.reply(215, "UNIX Type: L8 (eftpgate)")

    def do_FEAT(self, arg):
        self.reply(211, "Features:\n MLSD\n MLST type*;size*;modify*;UNIX.mode*;\n SIZE\n MDTM\n MFMT\n"
                        " REST STREAM\n UTF8\n EPSV\n PASV\nEnd")

    def do_OPTS(self, arg):
        self.reply(200, "UTF-8 it is")

    def do_TYPE(self, arg):
        t = arg.strip().upper()[:1]
        if t not in ("A", "I", "L"):
            self.reply(504, "TYPE A or I")
            return
        self.text = t == "A"
        self.reply(200, "TYPE %s" % ("A" if self.text else "I"))

    def do_MODE(self, arg):
        self.reply(200 if arg.strip().upper() == "S" else 504, "stream mode")

    def do_STRU(self, arg):
        self.reply(200 if arg.strip().upper() == "F" else 504, "file structure")

    # ---- folders
    def do_PWD(self, arg):
        self.reply(257, '"%s" is the current folder' % self.cwd.replace('"', '""'))

    do_XPWD = do_PWD

    def do_CWD(self, arg):
        p = self.path(arg)
        if not self.back.isdir(p):
            raise GateError(550, "%s: no such folder" % p)
        self.cwd = p
        self.reply(250, '"%s" is the current folder' % p)

    def do_CDUP(self, arg):
        return self.do_CWD("..")

    def do_MKD(self, arg):
        p = self.path(arg)
        self.back.mkdir(p)
        self.reply(257, '"%s" made' % p.replace('"', '""'))

    def do_RMD(self, arg):
        self.back.rmdir(self.path(arg))
        self.reply(250, "folder deleted")

    def do_DELE(self, arg):
        self.back.remove(self.path(arg))
        self.reply(250, "deleted")

    def do_RNFR(self, arg):
        self.rnfr = self.path(arg)
        self.reply(350, "and the new name?")

    def do_RNTO(self, arg):
        if not self.rnfr:
            raise GateError(503, "RNFR first")
        a, self.rnfr = self.rnfr, None
        self.back.rename(a, self.path(arg))
        self.reply(250, "renamed")

    def do_SITE(self, arg):
        parts = arg.split(None, 2)
        if len(parts) == 3 and parts[0].upper() == "CHMOD":
            self.back.chmod(self.path(parts[2]), int(parts[1], 8))
            self.reply(200, "permissions changed")
        else:
            self.reply(504, "SITE CHMOD only")

    def do_SIZE(self, arg):
        self.reply(213, str(self.back.size(self.path(arg))))

    def do_MDTM(self, arg):
        self.reply(213, time.strftime("%Y%m%d%H%M%S", time.gmtime(self.back.mtime(self.path(arg)))))

    def do_MFMT(self, arg):
        stamp, _, name = arg.partition(" ")
        t = datetime.datetime.strptime(stamp[:14], "%Y%m%d%H%M%S").replace(
            tzinfo=datetime.timezone.utc).timestamp()
        self.back.set_mtime(self.path(name), t)
        self.reply(213, "Modify=%s; %s" % (stamp[:14], name))

    def do_REST(self, arg):
        try:
            self.rest = int(arg.strip())
        except ValueError:
            raise GateError(501, "REST needs a number")
        self.reply(350, "from byte %d" % self.rest)

    # ---- data connections (passive only)
    def close_pasv(self):
        if self.pasv:
            try:
                self.pasv.close()
            except OSError:
                pass
            self.pasv = None

    def listen(self):
        self.close_pasv()
        here = self.connection.getsockname()[0]
        s = socket.socket(socket.AF_INET6 if ":" in here else socket.AF_INET)
        # the last data connection on a port waits a minute (TIME_WAIT): a new one may listen there
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        lo, hi = self.server.cfg.pasv_ports
        for port in ([0] if not lo else range(lo, hi + 1)):
            try:
                s.bind((here, port))
                break
            except OSError:
                continue
        else:
            raise GateError(425, "no free port for the data connection")
        s.listen(1)
        s.settimeout(30)
        self.pasv = s
        return here, s.getsockname()[1]

    def do_EPSV(self, arg):
        here, port = self.listen()
        self.reply(229, "Entering Extended Passive Mode (|||%d|)" % port)

    def do_PASV(self, arg):
        here, port = self.listen()
        if ":" in here:
            here = here.split(":")[-1] if "." in here else "0.0.0.0"
        self.reply(227, "Entering Passive Mode (%s,%d,%d)" % (here.replace(".", ","), port >> 8, port & 255))

    def do_PORT(self, arg):
        self.reply(502, "passive mode only (PASV, EPSV)")

    do_EPRT = do_PORT

    def data(self):
        if not self.pasv:
            raise GateError(425, "PASV or EPSV first")
        try:
            c, addr = self.pasv.accept()
        except socket.timeout:
            raise GateError(425, "the data connection didn't come")
        finally:
            self.close_pasv()
        if addr[0] != self.peer and not (private(addr[0]) and private(self.peer)):
            c.close()
            raise GateError(425, "the data connection came from somewhere else")
        c.settimeout(120)
        return c

    def send_lines(self, lines):
        self.reply(150, "the listing")
        c = self.data()
        try:
            c.sendall(("".join(l + "\r\n" for l in lines)).encode("utf-8", "replace"))
        finally:
            c.close()
        self.reply(226, "done")

    def listing(self, arg, fmt):
        args = [a for a in arg.split() if not a.startswith("-")]
        p = self.path(args[0] if args else ".")
        entries = self.back.listdir(p)
        now = time.time()
        self.send_lines([fmt(e, now) for e in entries])

    def do_MLSD(self, arg):
        self.listing(arg, lambda e, now: mlsd_line(e))

    def do_LIST(self, arg):
        self.listing(arg, list_line)

    def do_NLST(self, arg):
        self.listing(arg, lambda e, now: e.name)

    def do_MLST(self, arg):
        p = self.path(arg)
        parent, name = posixpath.split(p)
        for e in self.back.listdir(parent or "/"):
            if e.name == name:
                self.reply(250, "Listing %s\n %s\nEnd" % (p, mlsd_line(e)))
                return
        raise GateError(550, "%s: not there" % p)

    def do_RETR(self, arg):
        p = self.path(arg)
        rest, self.rest = self.rest, 0
        conv = self.text and self.back.text_newlines
        # converted text can't seek: read from the start, skip what the Atari has
        f = self.back.open_read(p, 0 if conv else rest, self.text)
        skip = rest if conv else 0
        ok = False
        try:
            self.reply(150, "sending %s" % p)
            c = self.data()
            last_cr = False
            try:
                while True:
                    buf = f.read(32768)
                    if not buf:
                        break
                    if conv:      # an SFTP server's text: LF -> CR LF, as FTP's TYPE A wants
                        out = re.sub(rb"(?<!\r)\n", b"\r\n", buf)
                        if last_cr and buf.startswith(b"\n"):
                            out = out[1:]     # its CR came in the block before
                        last_cr = buf.endswith(b"\r")
                        buf = out
                        if skip:
                            n = min(skip, len(buf))
                            buf, skip = buf[n:], skip - n
                    if buf:
                        c.sendall(buf)
                ok = True
            except OSError:       # the Atari stopped it
                ok = False
            finally:
                c.close()
        finally:
            if isinstance(f, _FTPDataFile):
                f.close(ok)
            else:
                f.close()
        self.reply(226 if ok else 426, "sent" if ok else "the transfer stopped")

    def receive(self, arg, append):
        p = self.path(arg)
        rest, self.rest = self.rest, 0
        if append:
            try:
                rest = self.back.size(p)
            except (GateError, IOError, OSError, ftplib.Error):
                rest = 0
        conv = self.text and self.back.text_newlines
        f = self.back.open_write(p, rest, self.text)
        ok = False
        try:
            self.reply(150, "ready for %s" % p)
            c = self.data()
            cr = False
            try:
                while True:
                    buf = c.recv(32768)
                    if not buf:
                        break
                    if conv:      # CR LF -> LF for an SFTP server's text
                        if cr:
                            buf = b"\r" + buf
                        cr = buf.endswith(b"\r")
                        if cr:
                            buf = buf[:-1]
                        buf = buf.replace(b"\r\n", b"\n")
                    if buf:
                        f.write(buf)
                if cr:
                    f.write(b"\r")
                ok = True
            except OSError:
                ok = False
            finally:
                c.close()
        finally:
            if isinstance(f, _FTPDataFile):
                f.close(ok)
            else:
                f.close()
        self.reply(226 if ok else 426, "received" if ok else "the transfer stopped")

    def do_STOR(self, arg):
        self.receive(arg, False)

    def do_APPE(self, arg):
        self.receive(arg, True)

    def do_ABOR(self, arg):
        self.reply(226, "nothing going on")


# ---------------------------------------------------------------- the server

class Config:
    def __init__(self, a):
        self.allow = a.allow
        self.allow_local = a.allow_local
        self.known_hosts = os.path.join(a.state, "known_hosts")
        self.cafile = a.cafile
        lo, _, hi = (a.pasv_ports or "").partition("-")
        self.pasv_ports = (int(lo), int(hi or lo)) if lo else (0, 0)

    def allowed(self, ip):
        return aeragate.client_ok(ip, self.allow)


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="the Raspberry Pi gateway for EFTP: plain FTP in, FTPS or SFTP out")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", help="only this Atari's address (may be given more than once)")
    ap.add_argument("--allow-local", action="store_true", help="also reach servers on the home network")
    ap.add_argument("--state", default=os.environ.get("STATE_DIRECTORY", "/var/lib/eftpgate"),
                    help="where the SSH host keys are remembered")
    ap.add_argument("--cafile", help="check FTPS servers against these root certificates (PEM) "
                    "instead of the Pi's own, e.g. a NAS's self-made CA")
    ap.add_argument("--pasv-ports", help="data ports to use, e.g. 30000-30100 (default: any)")
    ap.add_argument("-v", action="store_true", help="log every command")
    return ap


def main(argv=None):
    global VERBOSE
    a = parser().parse_args(argv)
    VERBOSE = a.v
    srv = Server((a.bind, a.port), Session)
    srv.cfg = Config(a)
    log("eftpgate %s listening on port %d" % (VERSION, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
