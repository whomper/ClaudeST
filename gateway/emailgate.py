#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""emailgate - the Raspberry Pi mail gateway for EMail on the Atari.

Mail providers take only encrypted connections (TLS), and a 16 MHz Falcon
can't do modern TLS at a usable speed. So EMail talks plain IMAP, SMTP and
POP3 to the Pi on your home network, and emailgate passes each connection
on to your provider over TLS 1.2 or newer, with the provider's certificate
and name checked against the system's root certificates:

    Atari --plain, home network--> Pi :143 --TLS--> imap.gmail.com:993
                                      :587 --TLS--> smtp.gmail.com:465
                                      :110 --TLS--> pop.gmail.com:995

An SMTP server given with :587 (iCloud) is reached with STARTTLS: the
gateway says EHLO and STARTTLS itself, then hands the Atari the server's
greeting, as if it had come straight from the server.

Only addresses on the private networks (192.168.x.x, 10.x.x.x,
172.16-31.x.x) may use it, or with --allow only the Atari's. What goes
through is never written down: -v logs who connected where, not what was
said (the passwords go through too).

Only standard Python 3 is needed (Raspberry Pi OS has it), and
aeragate.py next to it. `aera-gateway install mail` sets it up as a
service.

    emailgate.py [--ports 143,587,110] [--allow 192.168.1.50] [-v]
                 IMAP_SERVER SMTP_SERVER[:587] [POP3_SERVER]
"""

import argparse
import socket
import ssl
import threading

import aeragate

VERSION = "1.1"

is_private = aeragate.is_private


class Config:
    allow = []              # client addresses allowed; empty = any on the private networks
    verbose = False
    timeout = 60            # connecting, and the TLS handshake
    idle = 30 * 60          # a connection with nothing said either way for this long is closed


def log(msg):
    if Config.verbose:
        aeragate.say(msg)


def client_ok(addr):
    return aeragate.client_ok(addr, Config.allow)


# what each protocol says to the Atari when the gateway can't help: it
# shows the text in its alert
def refusal(proto, text):
    return {"imap": "* BYE emailgate: %s\r\n", "pop3": "-ERR emailgate: %s\r\n",
            "smtp": "421 emailgate: %s\r\n"}[proto] % text


def tls_context():
    return aeragate.tls_context()


def read_reply(f):
    """an SMTP reply: "250-..." lines up to "250 ..."; (code, the lines)"""
    lines = []
    while True:
        line = f.readline(1024)
        if not line:
            raise OSError("the server closed the connection")
        lines.append(line)
        if len(line) < 4 or line[3:4] != b"-":
            try:
                return int(line[:3]), b"".join(lines)
            except ValueError:
                raise OSError("the server's answer isn't SMTP: %r" % line[:60])


def connect(host, port, starttls=False):
    """the provider, over TLS; and with STARTTLS (SMTP), its greeting"""
    raw = aeragate.connect(host, port, Config.timeout)
    try:
        greeting = b""
        if starttls:
            f = raw.makefile("rb")
            code, greeting = read_reply(f)
            if code != 220:
                raise OSError("the server says: %s" % greeting.decode("latin-1").strip())
            raw.sendall(b"EHLO emailgate\r\n")
            code, ehlo = read_reply(f)
            if code != 250 or b"STARTTLS" not in ehlo.upper():
                raise OSError("%s doesn't offer STARTTLS" % host)
            raw.sendall(b"STARTTLS\r\n")
            code, _ = read_reply(f)
            if code != 220:
                raise OSError("%s refused STARTTLS" % host)
            f.close()
        tls = tls_context().wrap_socket(raw, server_hostname=host)
        return tls, greeting
    except Exception:
        raw.close()
        raise


def relay(atari, server):
    """both ways, until one side closes or nothing is said for Config.idle"""
    return aeragate.relay(atari, server, Config.idle, Config.timeout)


def serve(proto, upstream, atari, addr):
    host, port, starttls = upstream
    who = "%s:%d" % addr[:2]
    try:
        if not client_ok(addr[0]):
            log("%s %s: not allowed" % (proto, who))
            atari.sendall(refusal(proto, "this address may not use the gateway").encode())
            return
        try:
            server, greeting = connect(host, port, starttls)
        except ssl.SSLCertVerificationError as e:
            log("%s %s -> %s:%d: certificate: %s" % (proto, who, host, port, e.verify_message))
            atari.sendall(refusal(proto, "%s's certificate doesn't check out (%s)" % (host, e.verify_message)).encode())
            return
        except (OSError, ssl.SSLError) as e:
            log("%s %s -> %s:%d: %s" % (proto, who, host, port, e))
            atari.sendall(refusal(proto, "can't reach %s:%d (%s)" % (host, port, e)).encode())
            return
        log("%s %s -> %s:%d, %s" % (proto, who, host, port, server.version()))
        try:
            if greeting:
                atari.sendall(greeting)
            why = relay(atari, server)
            log("%s %s: %s" % (proto, who, why))
        except (OSError, ssl.SSLError) as e:
            log("%s %s: %s" % (proto, who, e))
        finally:
            server.close()
    except OSError:
        pass
    finally:
        atari.close()


def listen(proto, bind, port, upstream):
    """upstream: (host, port, STARTTLS?)"""
    s = socket.create_server((bind, port))
    log("emailgate %s: %s:%d -> %s:%d" % (proto, bind, port, upstream[0], upstream[1]))

    def loop():
        while True:
            atari, addr = s.accept()
            threading.Thread(target=serve, args=(proto, upstream, atari, addr), daemon=True).start()
    t = threading.Thread(target=loop, daemon=True)
    t.start()
    return s, t


def server_arg(s, port):
    """HOST or HOST:PORT"""
    if s.count(":") == 1:
        host, p = s.split(":")
        return host, int(p)
    return s, port


def parser():
    """the command line (aera-gateway reads a service's settings with it)"""
    ap = argparse.ArgumentParser(description="mail gateway for EMail on the Atari: plain on the "
                                 "home network, TLS to the provider")
    ap.add_argument("imap", help="the IMAP server (port 993, TLS)")
    ap.add_argument("smtp", help="the SMTP server: port 465 (TLS), or SERVER:587 for STARTTLS")
    ap.add_argument("pop3", nargs="?", help="the POP3 server (port 995, TLS)")
    ap.add_argument("--ports", default="143,587,110",
                    help="the ports on the Pi for IMAP, SMTP and POP3 (default 143,587,110)")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this Atari's address may use the gateway (may repeat)")
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    Config.allow = a.allow
    Config.verbose = a.verbose
    ports = [int(p) for p in a.ports.split(",")] + [110, 110]
    smtp = server_arg(a.smtp, 465)
    targets = [("imap", ports[0], server_arg(a.imap, 993) + (False,)),
               ("smtp", ports[1], smtp + (smtp[1] == 587,))]      # 587: STARTTLS
    if a.pop3:
        targets.append(("pop3", ports[2], server_arg(a.pop3, 995) + (False,)))
    aeragate.say("emailgate %s: %s; %s" % (
        VERSION, ", ".join("%s on %d" % (p, n) for p, n, _ in targets),
        "only " + ", ".join(a.allow) if a.allow else "private-network addresses only"))
    threads = [listen(proto, a.bind, port, up)[1] for proto, port, up in targets]
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
