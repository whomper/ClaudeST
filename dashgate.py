#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""dashgate - the Gateway's dashboard, for a browser on the home network
(Erez, 10 Nov 2026).

One page, read-only, at http://GATEWAY:8888/, sent filled in (it reads the
same with a browser's scripts off: Erez, 10 Nov 2026): each service running or not
and since when, its ports, the connections today and this run, the Ataris
that came and where they went, the settings (never a password or a key)
and the computer's memory, disk, load and temperature. It refreshes
itself every few seconds. The same as JSON:

    GET /              the page (with /dash.css and /dash.js)
    GET /status.json   what the page shows

Where the numbers come from:
  - the gateways tell it, through aeragate (client_ok and lookup), one
    datagram each on its socket, /run/aera-dash/events.sock: "c CLIENT ok"
    or "c CLIENT no" when an Atari connects, "t CLIENT HOST" when it goes
    somewhere. It knows which gateway sent each by the sender's user, which
    the kernel gives it (SO_PASSCRED): any other user's are dropped;
  - ClaudeST's bridge, which doesn't use aeragate: its connections, as the
    kernel lists them (/proc/net/tcp);
  - systemd (on a Raspberry Pi) or aera-gateway-run (in a container): each
    service's state and its settings, from its command line.
The counts start again when dashgate starts (Erez, 10 Nov 2026).

Who may see it: the home network, or with --allow only those addresses
(the rule the gateways share, aeragate.client_ok). Standard Python 3 only.
"""

import argparse
import collections
import html
import http.server
import ipaddress
import json
import os
import pwd
import re
import shlex
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import aeragate  # noqa: E402

aeragate.DASH_SOCKET = ""           # (its own visitors aren't the Ataris')

VERSION = "1.1"
HERE = os.path.dirname(os.path.realpath(__file__))

# key, its user (and unit), its script, the apps, what it speaks, its ports
SERVICES = [
    ("mail", "emailgate", "emailgate.py", "EMail", "IMAP · SMTP · POP3 to TLS", [143, 587, 110]),
    ("dav", "ecalgate", "ecalgate.py", "ECal · EContact", "CalDAV · CardDAV · calendar feeds", [8180]),
    ("ftp", "eftpgate", "eftpgate.py", "EFTP", "FTP to FTPS · SFTP", [2121]),
    ("web", "webgate", "webgate.py", "EWeb", "HTTP to HTTPS · pictures · scripts", [8280]),
    ("pdf", "pdfgate", "pdfgate.py", "EConvert · EPDF", "PDFs converted", [8380]),
    ("translate", "translategate", "translategate.py", "EMail · EWeb · EPDF", "translation on this computer",
     [8480]),
    ("radio", "radiogate", "radiogate.py", "EPlay", "web radio as MP3", [8580]),
    ("claude", "claude-st", "claude_bridge.py", "ClaudeST", "STinG to claude.ai", [2323]),
]
BY_KEY = {s[0]: s for s in SERVICES}
FLAGS = {"-v", "--verbose", "--allow-local", "--browser", "--no-allow-local", "--no-verbose", "--no-browser"}
HOST_RE = re.compile(r"^[A-Za-z0-9._:\[\]-]{1,253}$")


class Config:
    port = 8888
    bind = "0.0.0.0"
    allow = []
    socket = "/run/aera-dash/events.sock"
    container = False                   # in aera-gateway-run's container, not under systemd
    units = "/etc/systemd/system"
    run_status = "/run/aera-gateway/services.json"
    models = "/opt/translategate/models"
    opt = HERE
    proc = "/proc"
    users = {s[1]: s[0] for s in SERVICES if s[0] != "claude"}   # who may tell it what
    verbose = False


def say(*words):
    aeragate.say("dashgate:", *words)


def slurp(path):
    with open(path) as f:
        return f.read()


# ---------------------------------------------------------------- the counts

class Stats:
    """what the gateways told, since dashgate started"""

    def __init__(self, now=time.time):
        self.now = now
        self.lock = threading.Lock()
        self.events = collections.deque(maxlen=200)
        self.total = collections.Counter()
        self.days = collections.defaultdict(collections.Counter)     # key -> {date: n}
        self.hours = collections.defaultdict(collections.Counter)    # key -> {hour: n}
        self.last = {}
        self.clients = {}

    def seen(self, key, client, ok):
        t = self.now()
        e = {"t": t, "client": client, "service": key, "to": "", "ok": ok}
        with self.lock:
            self.events.append(e)
            self.total[key] += 1
            self.days[key][time.strftime("%Y-%m-%d", time.localtime(t))] += 1
            self.hours[key][int(t // 3600)] += 1
            self.last[key] = e
            c = self.clients.setdefault(client, {"count": 0, "refused": 0, "services": set(), "last": 0})
            c["count"] += 1
            c["refused"] += 0 if ok else 1
            if ok:
                c["services"].add(key)
            c["last"] = t

    def went(self, key, client, host):
        """the latest connection of that client to that gateway went to host"""
        t = self.now()
        with self.lock:
            for e in reversed(self.events):
                if t - e["t"] > 120:
                    break
                if e["service"] == key and (client == "-" or e["client"] == client):
                    if not e["to"]:
                        e["to"] = host
                    elif e["to"] != host:
                        e2 = dict(e, to=host, t=t)
                        self.events.append(e2)
                        self.last[key] = e2
                    return

    def take(self, key, uid_name, line):
        """one datagram from a gateway"""
        parts = line.split(" ")
        if len(parts) != 3 or parts[0] not in ("c", "t"):
            return
        client = parts[1]
        if client != "-":
            try:
                client = str(ipaddress.ip_address(client))
            except ValueError:
                return
        if parts[0] == "c" and parts[2] in ("ok", "no"):
            self.seen(key, client, parts[2] == "ok")
        elif parts[0] == "t" and HOST_RE.match(parts[2]):
            self.went(key, client, parts[2].lower())

    def today(self, key):
        return self.days[key][time.strftime("%Y-%m-%d", time.localtime(self.now()))]

    def trend(self, key, n=8):
        h = int(self.now() // 3600)
        return [self.hours[key][h - i] for i in range(n - 1, -1, -1)]


def listen(stats, path):
    """the gateways' datagrams, each from its user"""
    try:
        os.unlink(path)
    except OSError:
        pass
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_PASSCRED, 1)
    s.bind(path)
    os.chmod(path, 0o666)           # (anyone may send; only the gateways' users count)
    names = {}

    def run():
        size = socket.CMSG_SPACE(struct.calcsize("3i"))
        while True:
            try:
                data, anc, _, _ = s.recvmsg(1024, size)
            except OSError:
                continue
            uid = None
            for level, kind, raw in anc:
                if level == socket.SOL_SOCKET and kind == socket.SCM_CREDENTIALS:
                    uid = struct.unpack("3i", raw[:12])[1]
            if uid is None:
                continue
            if uid not in names:
                try:
                    names[uid] = pwd.getpwuid(uid).pw_name
                except KeyError:
                    names[uid] = None
            key = Config.users.get(names[uid])
            if key:
                stats.take(key, names[uid], data.decode("utf-8", "replace").strip())
    threading.Thread(target=run, daemon=True).start()
    return s


# ---------------------------------------------------------------- the kernel's lists

def tcp_peers(proc=None):
    """{local port: [the peers' addresses]}, the established connections"""
    proc = proc or Config.proc
    out = collections.defaultdict(list)
    for name, v6 in (("tcp", False), ("tcp6", True)):
        try:
            lines = slurp(os.path.join(proc, "net", name)).splitlines()[1:]
        except OSError:
            continue
        for line in lines:
            f = line.split()
            if len(f) < 4 or f[3] != "01":
                continue
            try:
                lport = int(f[1].split(":")[1], 16)
                raw = bytes.fromhex(f[2].split(":")[0])
                if v6:
                    raw = b"".join(raw[i:i + 4][::-1] for i in range(0, 16, 4))
                    ip = ipaddress.ip_address(raw)
                    ip = ip.ipv4_mapped or ip
                else:
                    ip = ipaddress.ip_address(raw[::-1])
            except (ValueError, IndexError):
                continue
            out[lport].append((str(ip), f[2]))
    return out


class Claude:
    """ClaudeST's connections, counted as the kernel lists them"""

    def __init__(self, stats):
        self.stats, self.known = stats, set()

    def poll(self, port=2323, peers=None):
        peers = tcp_peers() if peers is None else peers
        now = set(peers.get(port, []))
        for ip, key in now - self.known:
            self.stats.seen("claude", ip, True)
        self.known = now


# ---------------------------------------------------------------- the services

def argv_after(words, script):
    for i, w in enumerate(words):
        if os.path.basename(w) == script:
            return words[i + 1:]
    return None


def unit_args(text, script):
    for line in (text or "").splitlines():
        if line.startswith("ExecStart="):
            try:
                words = shlex.split(line[len("ExecStart="):].lstrip("-@:+!"))
            except ValueError:
                return []
            return [w.replace("%%", "%").replace("$$", "$") for w in argv_after(words, script) or []]
    return []


def opts(args):
    """(options: {name: [values]}, positionals) of a command line"""
    o, pos, i = collections.defaultdict(list), [], 0
    while i < len(args):
        a = args[i]
        if a.startswith("-") and "=" in a:
            k, v = a.split("=", 1)
            o[k].append(v)
        elif a in FLAGS:
            o[a].append("")
        elif a.startswith("-"):
            o[a].append(args[i + 1] if i + 1 < len(args) else "")
            i += 1
        else:
            pos.append(a)
        i += 1
    return o, pos


def boot_time(proc=None):
    try:
        return time.time() - float(slurp(os.path.join(proc or Config.proc, "uptime")).split()[0])
    except (OSError, ValueError, IndexError):
        return None


def from_systemd(run=subprocess.run):
    """{key: {state, since, args}} of the services installed here"""
    out, units = {}, []
    for key, user, script, *_ in SERVICES:
        path = os.path.join(Config.units, user + ".service")
        try:
            text = slurp(path)
        except OSError:
            continue
        out[key] = {"state": "stopped", "since": None, "args": unit_args(text, script)}
        units.append((key, user))
    if units:
        try:
            r = run(["systemctl", "show", "-p", "ActiveState", "-p", "ActiveEnterTimestampMonotonic"] +
                    [u + ".service" for _, u in units], capture_output=True, text=True, timeout=10)
            blocks = r.stdout.strip().split("\n\n")
        except (OSError, subprocess.SubprocessError):
            blocks = []
        boot = boot_time()
        for (key, _), block in zip(units, blocks):
            v = dict(ln.split("=", 1) for ln in block.splitlines() if "=" in ln)
            state = v.get("ActiveState", "")
            out[key]["state"] = {"active": "running", "activating": "starting", "reloading": "running",
                                 "deactivating": "stopping"}.get(state, "stopped")
            mono = v.get("ActiveEnterTimestampMonotonic", "0")
            if out[key]["state"] == "running" and mono.isdigit() and int(mono) and boot:
                out[key]["since"] = boot + int(mono) / 1e6
    return out


def from_container():
    """{key: {state, since, args}} as aera-gateway-run writes them"""
    try:
        with open(Config.run_status) as f:
            d = json.load(f)
    except (OSError, ValueError):
        return {}, None
    return {k: v for k, v in d.get("services", {}).items() if k in BY_KEY}, d.get("started")


def languages():
    try:
        names = sorted(os.listdir(Config.models))
    except OSError:
        return []
    out = []
    for n in names:
        m = re.search(r"([a-z]{2,3})_([a-z]{2,3})", n)
        if m and os.path.isdir(os.path.join(Config.models, n)):
            out.append("%s → %s" % m.groups())
    return out


def settings(found):
    allow, local, mail, browser, backend = [], False, [], None, None
    for key, s in found.items():
        o, pos = opts(s.get("args") or [])
        for a in o.get("--allow", []):
            for x in a.split(","):
                if x and x not in allow:
                    allow.append(x)
        local = local or "--allow-local" in o
        if key == "mail":
            mail = pos
        if key == "web":
            browser = "--browser" in o
        if key == "claude" and o.get("--backend"):
            backend = o["--backend"][-1]
    return {"allow": allow, "allow_local": local, "mail": mail, "browser": browser,
            "languages": languages() if "translate" in found else [], "claude": backend}


def ports(key, args):
    o, _ = opts(args or [])
    if key == "mail" and o.get("--ports"):
        try:
            return [int(p) for p in o["--ports"][-1].split(",") if p]
        except ValueError:
            pass
    if o.get("--port"):
        try:
            return [int(o["--port"][-1])]
        except ValueError:
            pass
    return list(BY_KEY[key][5])


# ---------------------------------------------------------------- the computer

def computer():
    p = Config.proc
    out = {}
    try:
        m = dict(re.findall(r"^(\w+):\s+(\d+)", slurp(os.path.join(p, "meminfo")), re.M))
        out["mem_total"] = int(m["MemTotal"]) * 1024
        out["mem_used"] = out["mem_total"] - int(m.get("MemAvailable", m.get("MemFree", 0))) * 1024
    except (OSError, KeyError, ValueError):
        pass
    try:
        st = os.statvfs("/data" if Config.container and os.path.isdir("/data") else "/")
        out["disk_total"] = st.f_blocks * st.f_frsize
        out["disk_used"] = (st.f_blocks - st.f_bfree) * st.f_frsize
    except OSError:
        pass
    try:
        out["load"] = round(float(slurp(os.path.join(p, "loadavg")).split()[0]) * 100 /
                            (os.cpu_count() or 1))
    except (OSError, ValueError, IndexError):
        pass
    try:
        out["temp"] = round(int(slurp("/sys/class/thermal/thermal_zone0/temp")) / 1000)
    except (OSError, ValueError):
        pass
    boot = boot_time()
    if boot:
        out["up_since"] = boot
    return out


def model():
    try:
        return slurp("/proc/device-tree/model").strip("\0\n ")
    except OSError:
        return ""


def own_address(host_header=""):
    """the address the browser used, when it is one; else this computer's"""
    h = (host_header or "").rsplit(":", 1)[0].strip("[]") if host_header.count(":") <= 1 else \
        host_header.rsplit("]", 1)[0].strip("[")
    try:
        ip = ipaddress.ip_address(h)
        if not ip.is_loopback:
            return str(ip)
    except ValueError:
        pass
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))            # (no packet is sent)
        return s.getsockname()[0]
    except OSError:
        return h or ""
    finally:
        s.close()


def gateway_version():
    for path in (os.path.join(Config.opt, "VERSION"), os.path.join(HERE, "VERSION")):
        try:
            return slurp(path).strip()
        except OSError:
            pass
    try:
        m = re.search(r'^VERSION = "([0-9.]+)"', slurp(os.path.join(HERE, "aera-gateway")), re.M)
        return m.group(1) if m else ""
    except OSError:
        return ""


# ---------------------------------------------------------------- the status

def status(stats, host_header="", found=None, started=None, peers=None):
    if found is None:
        if Config.container:
            found, started = from_container()
        else:
            found = from_systemd()
    peers = tcp_peers() if peers is None else peers
    services, live_ips = [], set()
    for key, user, script, apps, speaks, _ in SERVICES:
        s = found.get(key)
        if s is None:
            services.append({"key": key, "name": user, "apps": apps, "speaks": speaks, "state": "not installed",
                             "ports": ports(key, []), "today": 0, "total": 0, "trend": [0] * 8, "live": 0,
                             "last": None, "since": None})
            continue
        ps = ports(key, s.get("args"))
        live = [ip for p in ps for ip, _ in peers.get(p, [])]
        live_ips.update(live)
        with stats.lock:
            last = dict(stats.last[key]) if key in stats.last else None
            services.append({"key": key, "name": user, "apps": apps, "speaks": speaks, "state": s.get("state"),
                             "since": s.get("since"), "ports": ps, "today": stats.today(key),
                             "total": stats.total[key], "trend": stats.trend(key), "live": len(live),
                             "last": last})
    with stats.lock:
        ataris = sorted(({"ip": ip, "count": c["count"], "refused": c["refused"],
                          "services": sorted(c["services"]), "last": c["last"], "live": ip in live_ips}
                         for ip, c in stats.clients.items() if ip != "-"),
                        key=lambda a: -a["last"])[:20]
        events = list(stats.events)[-50:][::-1]
    if not started:
        since = [s["since"] for s in services if s.get("since") and s["state"] == "running"]
        started = min(since) if since else None
    return {
        "gateway": {"version": gateway_version(), "dashgate": VERSION, "address": own_address(host_header),
                    "where": "container" if Config.container else "systemd", "model": model(),
                    "since": started, "now": time.time(), "port": Config.port},
        "services": services,
        "ataris": ataris,
        "events": events,
        "settings": settings(found),
        "computer": computer(),
    }


# ---------------------------------------------------------------- the web page
#
# The page comes filled in (Erez, 10 Nov 2026: EWeb shows it with its
# scripts off): dash.js only fetches it again every few seconds and puts
# the new parts in place, and picks out one service's activity. Without
# scripts it refreshes itself every 30 s (<noscript>).

# the SVGs' colours as attributes too, for a browser whose SVG doesn't take
# them from the page's CSS (EWeb): the light look's, which such a browser
# shows; the CSS gives the dark look's where it applies
ACCENT = "#127a6f"
SOFT = "#eef1f4"
STATE = {"running": ("ok", "Running"), "starting": ("warn", "Starting"), "stopping": ("warn", "Stopping"),
         "stopped": ("bad", "Stopped"), "not installed": ("off", "Not installed")}


def esc(x):
    return html.escape("" if x is None else str(x), quote=True)


def num(x):
    return "{:,}".format(int(x or 0))


def span(secs):
    if not secs or secs < 0:
        return "–"
    d, h, m = int(secs // 86400), int(secs % 86400 // 3600), int(secs % 3600 // 60)
    if d:
        return "%d %s %d h" % (d, "day" if d == 1 else "days", h)
    if h:
        return "%d h %d min" % (h, m)
    return "%d min" % m


def when(t, now=None):
    if not t:
        return ""
    lt, ln = time.localtime(t), time.localtime(now or time.time())
    hm = time.strftime("%H:%M:%S", lt)
    if lt[:3] == ln[:3]:
        return hm
    return "%d %s %s" % (lt.tm_mday, time.strftime("%b", lt), hm)


def size(b):
    b, units, i = float(b or 0), ["B", "KB", "MB", "GB", "TB"], 0
    while b >= 1024 and i < len(units) - 1:
        b /= 1024
        i += 1
    return ("%d" % round(b) if b >= 10 or i == 0 else "%.1f" % b) + " " + units[i]


def spark(values):
    top = max(values + [1])
    pts = " ".join("%.1f,%.1f" % (i * 96 / max(1, len(values) - 1), 26 - v / top * 24) for i, v in enumerate(values))
    return ('<svg class="spark-line" width="96" height="28" viewBox="0 0 96 28" aria-hidden="true">'
            '<polyline points="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round" '
            'stroke-linecap="round"></polyline></svg>' % (pts, ACCENT))


def bar(pct):
    pct = max(0, min(100, round(pct or 0)))
    return ('<svg class="bar" width="100%%" height="8" aria-hidden="true">'
            '<rect class="track" width="100%%" height="8" rx="4" fill="%s"></rect>'
            '<rect class="fill%s" width="%d%%" height="8" rx="4" fill="%s"></rect></svg>'
            % (SOFT, " hot" if pct >= 85 else "", pct, ACCENT))


def last_line(s, now):
    if s["state"] == "not installed":
        return "Not installed on this Gateway"
    live = " · %d connected now" % s["live"] if s["live"] else ""
    if not s["last"]:
        return ("%d connected now" % s["live"]) if s["live"] else "Nothing yet this run"
    e = s["last"]
    return "%s%s%s, %s%s" % ("Last: " if e["ok"] else "Refused: ", e["client"],
                              " → " + e["to"] if e["to"] else "", when(e["t"], now), live)


def part_overall(d):
    inst = [s for s in d["services"] if s["state"] != "not installed"]
    run = sum(1 for s in inst if s["state"] == "running")
    cls = "off" if not inst else "ok" if run == len(inst) else "warn" if run else "bad"
    text = "%d of %d services running" % (run, len(inst)) if inst else "No service installed"
    return '<span id="overall" class="pill %s">%s</span>' % (cls, esc(text))


def part_tiles(d):
    g = d["gateway"]
    total = sum(s["total"] for s in d["services"])
    today = sum(s["today"] for s in d["services"])
    tiles = [("Running for", span(g["now"] - g["since"]) if g["since"] else "–", "",
              "since " + when(g["since"], g["now"]) if g["since"] else ""),
             ("Connections this run", num(total), "", "%s today" % num(today)),
             ("Gateway address", g["address"] or "–", " mono",
              "in a Docker container" if g["where"] == "container" else g["model"]),
             ("Version", g["version"] or "–", "",
              "the Docker image" if g["where"] == "container" else "installed with aera-gateway")]
    return ('<section id="tiles" class="tiles" aria-label="This Gateway">' + "".join(
        '<div class="card tile"><div class="label">%s</div><div class="big%s">%s</div>'
        '<div class="small muted">%s</div></div>' % (esc(a), b2, esc(b), esc(c)) for a, b, b2, c in tiles) +
        '</section>')


def part_services(d):
    now, out = d["gateway"]["now"], []
    for s in d["services"]:
        cls, word = STATE.get(s["state"], ("off", s["state"]))
        title = ' title="since %s"' % esc(when(s["since"], now)) if s.get("since") and s["state"] == "running" else ""
        out.append(
            '<button type="button" class="svc%s" data-key="%s" aria-pressed="false">'
            '<span class="row"><span><span class="name">%s</span><span class="small muted">%s</span></span>'
            '<span class="chip %s"%s>%s</span></span>'
            '<span class="tags"><span class="tag mono">%s</span><span class="tag muted">%s</span></span>'
            '<span class="nums"><span class="num"><b>%s</b><span class="small muted">today</span></span>'
            '<span class="num"><b>%s</b><span class="small muted">this run</span></span>'
            '<span class="spark">%s</span></span>'
            '<span class="lastline">%s</span></button>' % (
                " absent" if s["state"] == "not installed" else "", esc(s["key"]), esc(s["key"]), esc(s["apps"]),
                cls, title, esc(word), esc(" · ".join(str(p) for p in s["ports"])), esc(s["speaks"]),
                num(s["today"]), num(s["total"]), spark(s.get("trend") or [0] * 8), esc(last_line(s, now))))
    return '<div class="grid" id="services">%s</div>' % "".join(out)


def part_events(d):
    now = d["gateway"]["now"]
    rows = ['<tr data-service="%s"><td class="mono muted">%s</td><td class="mono">%s</td><td class="strong">%s</td>'
            '<td class="mono muted">%s</td><td class="%s">%s</td></tr>' % (
                esc(e["service"]), esc(when(e["t"], now)), esc("" if e["client"] == "-" else e["client"]),
                esc(e["service"]), esc(e["to"] or "–"), "" if e["ok"] else "refused",
                "connected" if e["ok"] else "refused: not allowed") for e in d["events"]]
    rows.append('<tr class="none"%s><td colspan="5" class="muted">Nothing yet this run</td></tr>' %
                (" hidden" if rows else ""))
    return '<tbody id="events">%s</tbody>' % "".join(rows)


def part_ataris(d):
    now = d["gateway"]["now"]
    if not d["ataris"]:
        return '<div id="ataris" class="list"><div class="empty">None yet this run</div></div>'
    out = []
    for a in d["ataris"]:
        refused = a["refused"] and not a["services"]
        st = ("st-live", "Connected") if a["live"] else ("st-bad", "Refused") if refused else \
            ("st", "Seen " + when(a["last"], now))
        count = ("%s %s" % (num(a["refused"]), "try" if a["refused"] == 1 else "tries")) if refused else \
            "%s this run" % num(a["count"])
        out.append('<div class="atari"><div><div class="mono">%s</div><div class="small muted">%s</div></div>'
                   '<div class="r"><div class="%s">%s</div><div class="small muted">%s</div></div></div>' % (
                       esc(a["ip"]), esc(", ".join(a["services"]) or "nothing allowed"), st[0], esc(st[1]),
                       esc(count)))
    return '<div id="ataris" class="list">%s</div>' % "".join(out)


def part_settings(d):
    s, out = d["settings"], []
    have = {x["key"]: x["state"] != "not installed" for x in d["services"]}

    def add(k, v, mono=False):
        if v in (None, ""):
            return
        lines = isinstance(v, list)
        v = "\n".join(v) if lines else v
        out.append('<dt>%s</dt><dd class="%s%s">%s</dd>' % (esc(k), "mono" if mono else "",
                                                             " lines" if "\n" in v else "", esc(v)))
    if not any(have.values()):
        add("Services", "none installed")
    else:
        add("Who may connect", s["allow"] if s["allow"] else "the home network", bool(s["allow"]))
        add("Servers at home", "may be reached" if s["allow_local"] else "not reached")
        if have.get("mail"):
            add("Mail servers", s["mail"], True)
        if s["browser"] is not None:
            add("Browser", "on, for EWeb's snapshots" if s["browser"] else "off")
        if have.get("translate"):
            add("Translate", ", ".join(s["languages"]) or "no languages yet")
        if s["claude"]:
            add("ClaudeST", {"claudeai": "claude.ai", "api": "the Anthropic API"}.get(s["claude"], s["claude"]))
        add("The log", "who connected where, never contents")
    return '<dl id="settings">%s</dl>' % "".join(out)


def part_meters(d):
    c, out = d["computer"], []

    def meter(label, value, pct):
        out.append('<div><div class="meter-top"><span class="muted">%s</span><b>%s</b></div>%s</div>' % (
            esc(label), esc(value), bar(pct)))
    if c.get("mem_total"):
        meter("Memory", "%s of %s" % (size(c["mem_used"]), size(c["mem_total"])), c["mem_used"] * 100 / c["mem_total"])
    if c.get("disk_total"):
        meter("Disk", "%s of %s" % (size(c["disk_used"]), size(c["disk_total"])),
              c["disk_used"] * 100 / c["disk_total"])
    if "load" in c:
        meter("Processor", "%d%%" % c["load"] + ("  ·  %d °C" % c["temp"] if "temp" in c else ""), c["load"])
    if c.get("up_since"):
        out.append('<div class="small muted">The computer has been up for %s</div>' %
                   esc(span(d["gateway"]["now"] - c["up_since"])))
    return '<div id="meters" class="list">%s</div>' % "".join(out)


def page(d):
    return PAGE % {
        "accent": ACCENT, "overall": part_overall(d), "updated": esc("updated " + when(d["gateway"]["now"])),
        "tiles": part_tiles(d), "services": part_services(d), "events": part_events(d),
        "ataris": part_ataris(d), "settings": part_settings(d), "meters": part_meters(d)}


PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AERA-Gateway</title>
<noscript><meta http-equiv="refresh" content="30"></noscript>
<link rel="stylesheet" href="/dash.css">
<script src="/dash.js" defer></script>
</head>
<body>
<div class="wrap">
<header class="top">
  <div class="brand">
    <svg width="40" height="40" viewBox="0 0 40 40" aria-hidden="true"><rect x="1" y="1" width="38" height="38" rx="10"
      class="logo" fill="none" stroke="%(accent)s" stroke-width="2"></rect><path d="M11 28 L20 11 L29 28 M14.5 22 H25.5"
      class="logo-a" fill="none" stroke="%(accent)s" stroke-width="2.4" stroke-linecap="round"
      stroke-linejoin="round"></path></svg>
    <div><h1>AERA-Gateway</h1><div class="sub">The Atari's way to the modern internet</div></div>
  </div>
  <div class="tools">
    %(overall)s
    <span id="updated" class="mono small muted">%(updated)s</span>
    <button type="button" id="theme" class="btn js-only" aria-label="Switch between light and dark">
      <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"></circle><path
        d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"></path></svg>
      <span id="theme-word">Light</span>
    </button>
  </div>
</header>
<p id="lost" class="lost" hidden>The Gateway doesn't answer: showing what it said last.</p>
%(tiles)s
<div class="cols">
<main class="main">
  <section aria-labelledby="svc-h">
    <div class="head"><h2 id="svc-h">Services</h2><span class="small muted js-only">Select one to see only its
      activity</span></div>
    %(services)s
  </section>
  <section class="card table-card" aria-labelledby="act-h">
    <div class="table-head">
      <h2 id="act-h">Recent activity <span class="muted normal" id="filter-word">&middot; every service</span></h2>
      <button type="button" class="btn small-btn" id="clear" hidden>Show every service</button>
    </div>
    <div class="scroll"><table>
      <thead><tr><th scope="col">Time</th><th scope="col">Atari</th><th scope="col">Service</th>
        <th scope="col">To</th><th scope="col">Result</th></tr></thead>
      %(events)s
    </table></div>
  </section>
</main>
<aside class="side">
  <section class="card pad" aria-labelledby="atari-h"><h2 id="atari-h">Ataris</h2>%(ataris)s</section>
  <section class="card pad" aria-labelledby="cfg-h"><h2 id="cfg-h">Settings</h2>%(settings)s</section>
  <section class="card pad" aria-labelledby="sys-h"><h2 id="sys-h">The computer</h2>%(meters)s</section>
</aside>
</div>
<footer class="foot small muted">
  <span>Read-only. The counts start again when the Gateway starts. The same as JSON: <a href="/status.json"
    class="mono">/status.json</a></span>
  <span>AERA | Erez Yaary &middot; GPL</span>
</footer>
</div>
</body>
</html>
"""

CSS = """
:root{--bg:#0e1217;--card:#161c23;--soft:#1f2730;--line:#2a3440;--text:#e8edf2;--muted:#9aa7b4;
--ok:#5fd39a;--okbg:rgba(95,211,154,.14);--warn:#f0b45a;--warnbg:rgba(240,180,90,.15);
--bad:#ff8a7a;--badbg:rgba(255,138,122,.15);--off:#9aa7b4;--offbg:rgba(154,167,180,.12);--accent:#4cc3b5;
color-scheme:dark}
:root[data-theme=light]{--bg:#f4f6f8;--card:#fff;--soft:#eef1f4;--line:#dde3e9;--text:#16202a;--muted:#5a6773;
--ok:#167a4a;--okbg:#dff3e8;--warn:#8a5a00;--warnbg:#fbeed3;--bad:#b3261e;--badbg:#fbe1de;--off:#5a6773;
--offbg:#eef1f4;--accent:#127a6f;color-scheme:light}
@media (prefers-color-scheme:light){:root:not([data-theme=dark]){--bg:#f4f6f8;--card:#fff;--soft:#eef1f4;
--line:#dde3e9;--text:#16202a;--muted:#5a6773;--ok:#167a4a;--okbg:#dff3e8;--warn:#8a5a00;--warnbg:#fbeed3;
--bad:#b3261e;--badbg:#fbe1de;--off:#5a6773;--offbg:#eef1f4;--accent:#127a6f;color-scheme:light}}
*{box-sizing:border-box}
[hidden]{display:none!important}
html:not(.js) .js-only{display:none!important}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",
Roboto,"Helvetica Neue",Arial,sans-serif}
a{color:var(--accent)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace}
.small{font-size:12px}.muted{color:var(--muted)}.normal{font-weight:400}
.wrap{max-width:1280px;margin:0 auto;padding:28px 16px 48px;display:flex;flex-direction:column;gap:24px}
.top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:16px}
.brand{display:flex;align-items:center;gap:14px}
.logo{fill:none;stroke:var(--accent);stroke-width:2}
.logo-a{fill:none;stroke:var(--accent);stroke-width:2.4;stroke-linecap:round;stroke-linejoin:round}
h1{margin:0;font-size:22px;font-weight:700;letter-spacing:-.01em}
h2{margin:0;font-size:16px;font-weight:600}
.sub{color:var(--muted);font-size:13px}
.tools{display:flex;flex-wrap:wrap;align-items:center;gap:10px}
.pill{display:inline-flex;align-items:center;gap:8px;padding:6px 12px;border-radius:999px;font-weight:600;
font-size:13px;background:var(--offbg);color:var(--off)}
.pill::before,.chip::before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor}
.ok{background:var(--okbg);color:var(--ok)}.warn{background:var(--warnbg);color:var(--warn)}
.bad{background:var(--badbg);color:var(--bad)}.off{background:var(--offbg);color:var(--off)}
.btn{min-height:44px;padding:0 16px;border-radius:10px;border:1px solid var(--line);background:var(--card);
color:var(--text);cursor:pointer;display:inline-flex;align-items:center;gap:8px;font:inherit}
.small-btn{min-height:36px;padding:0 12px;background:var(--soft)}
.lost{margin:0;padding:10px 14px;border-radius:10px;background:var(--warnbg);color:var(--warn);font-weight:500}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px}
.pad{padding:16px 18px}.pad h2{margin-bottom:12px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}
.tile{padding:16px 18px}
.label{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.06em}
.big{font-size:26px;font-weight:700;margin-top:4px;overflow-wrap:anywhere}
.big.mono{font-size:22px;font-weight:500;margin-top:6px}
.cols{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
.main{flex:999 1 640px;min-width:0;display:flex;flex-direction:column;gap:24px}
.side{flex:1 1 300px;min-width:0;display:flex;flex-direction:column;gap:24px}
.head{display:flex;flex-wrap:wrap;align-items:baseline;justify-content:space-between;gap:12px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px}
.svc{text-align:left;cursor:pointer;background:var(--card);color:var(--text);border:1px solid var(--line);
border-radius:14px;padding:16px 18px;display:flex;flex-direction:column;gap:10px;font:inherit;min-height:44px}
.svc[aria-pressed=true]{border:2px solid var(--accent);padding:15px 17px}
.svc.absent{opacity:.6}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px;width:100%}
.name{font-size:16px;font-weight:700;margin-right:8px}
.chip{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-size:12px;
font-weight:600;white-space:nowrap}
.chip::before{width:7px;height:7px}
.tags{display:flex;flex-wrap:wrap;gap:6px}
.tag{font-size:12px;padding:2px 8px;border-radius:6px;background:var(--soft)}
.tag.muted{color:var(--muted)}
.nums{display:flex;gap:20px;width:100%;align-items:flex-end}
.num b{display:block;font-size:20px}
.spark{flex:1;display:flex;justify-content:flex-end}
.spark polyline{stroke:var(--accent)}
.lastline{color:var(--muted);font-size:12px;border-top:1px solid var(--line);padding-top:8px;width:100%;
overflow-wrap:anywhere}
.table-card{overflow:hidden}
.table-head{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;padding:14px 18px;
border-bottom:1px solid var(--line)}
.scroll{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:13px;min-width:560px}
th{color:var(--muted);text-align:left;font-weight:500;padding:10px 8px}
td{padding:9px 8px;border-top:1px solid var(--line);overflow-wrap:anywhere}
th:first-child,td:first-child{padding-left:18px}th:last-child,td:last-child{padding-right:18px}
td.strong{font-weight:600}
td.mono{white-space:nowrap;overflow-wrap:normal}
dd.lines{white-space:pre-line}
td.refused{color:var(--bad);font-weight:500}
.list{display:flex;flex-direction:column;gap:10px}
.atari{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 12px;border-radius:10px;
background:var(--soft)}
.atari .r{text-align:right}
.st-live{color:var(--ok);font-weight:600}.st-bad{color:var(--bad);font-weight:600}.st{color:var(--muted);font-weight:600}
dl{margin:0;display:grid;grid-template-columns:auto 1fr;column-gap:14px;row-gap:10px;font-size:13px}
dt{color:var(--muted)}dd{margin:0;overflow-wrap:anywhere}
.meter-top{display:flex;justify-content:space-between;font-size:13px;margin-bottom:6px}
.bar{display:block}.bar .track{fill:var(--soft)}.bar .fill{fill:var(--accent)}.bar .fill.hot{fill:var(--warn)}
.foot{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px}
.empty{color:var(--muted);padding:6px 0}
"""

JS = r"""
(function () {
  "use strict";
  // the page comes filled in; this fetches it again every few seconds and
  // puts the new parts in place (never as markup: DOMParser, then nodes),
  // picks out one service's activity, and keeps the light or dark look
  var pick = null, root = document.documentElement;
  var PARTS = ["overall", "updated", "tiles", "services", "events", "ataris", "settings", "meters"];
  root.className += " js";
  function $(id) { return document.getElementById(id); }

  function theme() {
    var t = root.getAttribute("data-theme");
    if (!t) t = window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
    return t;
  }
  try { var saved = localStorage.getItem("aera-dash-theme"); if (saved) root.setAttribute("data-theme", saved); } catch (e) {}

  function filter() {
    var cards = $("services").getElementsByTagName("button"), i, shown = 0, rows = $("events").rows;
    for (i = 0; i < cards.length; i++)
      cards[i].setAttribute("aria-pressed", cards[i].getAttribute("data-key") === pick ? "true" : "false");
    for (i = 0; i < rows.length; i++) {
      var key = rows[i].getAttribute("data-service");
      if (key === null) continue;
      rows[i].hidden = !!pick && key !== pick;
      if (!rows[i].hidden) shown++;
    }
    for (i = 0; i < rows.length; i++)
      if (rows[i].getAttribute("data-service") === null) rows[i].hidden = shown > 0;
    $("filter-word").textContent = "· " + (pick ? pick + " only" : "every service");
    $("clear").hidden = !pick;
  }

  function refresh() {
    var x = new XMLHttpRequest();
    x.open("GET", "/");
    x.timeout = 8000;
    x.onload = function () {
      if (x.status !== 200) return lost();
      var doc = new DOMParser().parseFromString(x.responseText, "text/html");
      for (var i = 0; i < PARTS.length; i++) {
        var now = $(PARTS[i]), next = doc.getElementById(PARTS[i]);
        if (now && next) now.parentNode.replaceChild(document.importNode(next, true), now);
      }
      $("lost").hidden = true;
      filter();
    };
    x.onerror = x.ontimeout = lost;
    x.send();
  }
  function lost() { $("lost").hidden = false; }

  document.addEventListener("DOMContentLoaded", function () {
    $("theme-word").textContent = theme() === "dark" ? "Light" : "Dark";
    $("theme").addEventListener("click", function () {
      var t = theme() === "dark" ? "light" : "dark";
      root.setAttribute("data-theme", t);
      try { localStorage.setItem("aera-dash-theme", t); } catch (e) {}
      $("theme-word").textContent = t === "dark" ? "Light" : "Dark";
    });
    document.addEventListener("click", function (ev) {
      var b = ev.target;
      while (b && b !== document && !(b.getAttribute && b.getAttribute("data-key"))) b = b.parentNode;
      if (b && b !== document) { pick = pick === b.getAttribute("data-key") ? null : b.getAttribute("data-key"); filter(); }
    });
    $("clear").addEventListener("click", function () { pick = null; filter(); });
    filter();
    setInterval(refresh, 4000);
  });
})();
"""

FILES = {"/dash.css": ("text/css; charset=utf-8", CSS), "/dash.js": ("text/javascript; charset=utf-8", JS)}


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "dashgate/" + VERSION
    protocol_version = "HTTP/1.1"
    stats = None

    def log_message(self, fmt, *args):
        if Config.verbose:
            say("%s %s" % (self.client_address[0], fmt % args))

    def send(self, status, ctype, body):
        body = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy",
                         "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; "
                         "img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        if not aeragate.client_ok(self.client_address[0], Config.allow):
            return self.send(403, "text/plain; charset=utf-8", "dashgate: not from this computer\n")
        path = self.path.split("?", 1)[0]
        if path == "/":
            return self.send(200, "text/html; charset=utf-8", page(status(self.stats, self.headers.get("Host", ""))))
        if path in FILES:
            return self.send(200, *FILES[path])
        if path == "/status.json":
            body = json.dumps(status(self.stats, self.headers.get("Host", "")), separators=(",", ":"))
            return self.send(200, "application/json", body)
        self.send(404, "text/plain; charset=utf-8", "dashgate: no such page\n")

    do_HEAD = do_GET


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def parser():
    ap = argparse.ArgumentParser(description="The Gateway's dashboard, for a browser on the home network")
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--allow", action="append", default=[],
                    help="only this address may see it (more: repeat); default: the home network")
    ap.add_argument("--socket", default="/run/aera-dash/events.sock", help="where the gateways tell it")
    ap.add_argument("--container", action="store_true", help="in aera-gateway-run's container, not under systemd")
    ap.add_argument("-v", "--verbose", action="store_true", help="log every request")
    return ap


def main(argv=None):
    a = parser().parse_args(argv)
    Config.port, Config.bind, Config.socket = a.port, a.bind, a.socket
    Config.allow = [x for v in a.allow for x in v.split(",") if x]
    Config.container, Config.verbose = a.container, a.verbose
    stats = Stats()
    Handler.stats = stats
    try:
        listen(stats, Config.socket)
    except OSError as e:
        say("can't listen on %s (%s): no counts" % (Config.socket, e.strerror or e))
    claude = Claude(stats)

    def poll():
        while True:
            try:
                claude.poll(ports("claude", (from_container()[0] if Config.container else {}).get(
                    "claude", {}).get("args"))[0])
            except Exception:       # (the dashboard goes on without ClaudeST's counts)
                pass
            time.sleep(3)
    threading.Thread(target=poll, daemon=True).start()
    srv = Server((Config.bind, Config.port), Handler)
    say("dashgate %s listening on %s:%d" % (VERSION, Config.bind, Config.port))
    srv.serve_forever()


if __name__ == "__main__":
    main()
