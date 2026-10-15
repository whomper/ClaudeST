# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""aeragate - the rules the AERA gateways on the Pi share.

emailgate, ecalgate and eftpgate each speak their own protocol to the
Atari. What they promise is the same, and lives here once:

  - who may use a gateway: the home network only (192.168.x.x, 10.x.x.x,
    172.16-31.x.x and the Pi itself), or with --allow only the Atari;
  - where a gateway may go: not back into the home network unless
    --allow-local says so, and only to the address that was checked,
    never to a second look-up of the name (a name whose answer changes
    in between can't lead it home);
  - TLS 1.2 or newer, with the server's certificate and name checked
    against the Pi's root certificates, or a --cafile;
  - the log says who connected where, never what went through;
  - bytes passed both ways until one side closes or both fall silent.

Standard Python 3 only. The gateways find it next to themselves.
"""

import ipaddress
import select
import socket
import ssl
import sys
import threading

VERSION = "1.1"

# the home network: the private ranges, link-local, and the Pi itself
HOME = [ipaddress.ip_network(n) for n in
        ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.0/8", "169.254.0.0/16",
         "fc00::/7", "fe80::/10")]
THIS_HOST = ipaddress.ip_network("0.0.0.0/8")       # 0.0.0.0 reaches the Pi itself

_DEFAULT_TIMEOUT = getattr(socket, "_GLOBAL_DEFAULT_TIMEOUT", object())
_LOG = threading.Lock()


def _ip(addr):
    try:
        ip = ipaddress.ip_address(str(addr).split("%")[0])
    except ValueError:
        return None
    if ip.version == 6 and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    return ip


def is_private(addr):
    """Is addr on the home network (or the Pi itself)?"""
    ip = _ip(addr)
    return ip is not None and (ip.is_loopback or any(ip in n for n in HOME))


def leads_home(addr):
    """Would a connection to addr reach the home network or the Pi?
    Anything that isn't an address counts as yes."""
    ip = _ip(addr)
    return ip is None or is_private(addr) or ip.is_unspecified or ip in THIS_HOST


def client_ok(addr, allow):
    """May addr use the gateway? allow: the Ataris' addresses (--allow);
    empty lets in anyone on the home network."""
    if allow:
        ip = _ip(addr)
        return addr in allow or (ip is not None and str(ip) in allow)
    return is_private(addr)


class HomeNetwork(OSError):
    """The server's name leads into the home network, and --allow-local
    wasn't given."""


def lookup(host, port, allow_local=True):
    """host's addresses, each checked, for dial(): socket.gaierror when
    there are none, HomeNetwork when one leads home and allow_local isn't
    set."""
    addrs = []
    for family, _, _, _, sa in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM):
        if (family, sa) not in addrs:
            addrs.append((family, sa))
    if not allow_local:
        for _, sa in addrs:
            if leads_home(sa[0]):
                raise HomeNetwork("%s is on the home network (%s)" % (host, sa[0]))
    return addrs


def dial(addrs, timeout=_DEFAULT_TIMEOUT):
    """A TCP connection to the first of the addresses lookup() checked
    that answers; the first failure if none does."""
    first = None
    for family, sa in addrs:
        s = socket.socket(family, socket.SOCK_STREAM)
        try:
            if timeout is not _DEFAULT_TIMEOUT:
                s.settimeout(timeout)
            s.connect(sa)
            return s
        except OSError as e:
            s.close()
            first = first or e
    raise first or OSError("no address to connect to")


def connect(host, port, timeout=_DEFAULT_TIMEOUT, allow_local=True):
    """lookup() and dial() in one, as socket.create_connection()"""
    return dial(lookup(host, port, allow_local), timeout)


def tls_context(cafile=None):
    """TLS 1.2 or newer, the server's certificate and name checked against
    the system's root certificates (or cafile's)."""
    ctx = ssl.create_default_context(cafile=cafile)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    return ctx


def say(*words):
    """One line in the journal. Who connected where, and what came of it;
    never what was said (passwords go through the gateways)."""
    with _LOG:
        print(*words, file=sys.stderr, flush=True)


def send_all(sock, data, timeout):
    """data to a non-blocking socket, TLS or not"""
    view = memoryview(data)
    while view:
        try:
            n = sock.send(view)
            view = view[n:]
        except (ssl.SSLWantWriteError, BlockingIOError):
            select.select([], [sock], [], timeout)
        except ssl.SSLWantReadError:
            select.select([sock], [], [], timeout)


def relay(a, b, idle, timeout):
    """Bytes both ways between a and b (either may be TLS) until one side
    closes ("closed") or neither says anything for idle seconds ("idle")."""
    a.setblocking(False)
    b.setblocking(False)
    while True:
        # TLS keeps what it has decrypted to itself: select() can't see it
        ready = [s for s in (a, b) if isinstance(s, ssl.SSLSocket) and s.pending()] or \
            select.select([a, b], [], [], idle)[0]
        if not ready:
            return "idle"
        for s in ready:
            other = b if s is a else a
            try:
                data = s.recv(65536)
            except (ssl.SSLWantReadError, ssl.SSLWantWriteError, BlockingIOError):
                continue
            if not data:
                return "closed"
            send_all(other, data, timeout)
