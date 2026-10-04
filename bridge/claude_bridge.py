#!/usr/bin/env python3
"""Claude ST bridge.

Runs on any modern computer and connects an Atari ST/STE/TT/Falcon running
CLAUDE.PRG to Claude. The Atari can't do modern TLS, so the bridge does
the HTTPS work and talks a small line protocol (see ../PROTOCOL.md) to
the Atari over a serial cable, a TCP socket (WiFi modems, emulators) or a
pair of files/FIFOs (Hatari's --rs232-in/--rs232-out).

Examples:
  CLAUDE_SESSION_KEY=sk-ant-sid01-... ./claude_bridge.py --serial /dev/ttyUSB0
  ANTHROPIC_API_KEY=... ./claude_bridge.py --backend api --tcp 0.0.0.0:2323
  ./claude_bridge.py --backend demo --pipe st_out.fifo st_in.fifo
"""
import argparse
import hashlib
import logging
import os
import socket
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from atari_text import Formatter, format_text, from_atari, to_atari  # noqa: E402
import backends  # noqa: E402

log = logging.getLogger("claude-st")

CHUNK = 160              # max text bytes per P line
MAX_ID = 39              # Claude ST keeps item ids up to this many characters
HISTORY_MESSAGES = 40    # how much of a long chat to send to the Atari


# ---------------------------------------------------------------------------
# links
# ---------------------------------------------------------------------------

class Link:
    def __init__(self):
        self.buf = b""

    def _read(self) -> bytes:
        raise NotImplementedError

    def write(self, data: bytes):
        raise NotImplementedError

    def readline(self) -> bytes:
        while b"\n" not in self.buf:
            chunk = self._read()
            if chunk is None:
                raise EOFError
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.rstrip(b"\r")


def find_serial_port():
    """Pick a serial port for --serial auto: a USB adapter first (by its
    stable /dev/serial/by-id name), then the Raspberry Pi's own UART."""
    import glob
    for pattern in ("/dev/serial/by-id/*", "/dev/ttyUSB*", "/dev/ttyACM*",
                    "/dev/serial0", "/dev/ttyAMA0"):
        found = sorted(glob.glob(pattern))
        if found:
            return found[0]
    sys.exit("--serial auto: no serial port found. Is the USB serial adapter plugged in?")


class SerialLink(Link):
    def __init__(self, dev, baud, rtscts):
        super().__init__()
        import serial
        self.error = serial.SerialException
        if dev == "auto":
            dev = find_serial_port()
        log.info("serial port %s at %d baud", dev, baud)
        self.port = serial.Serial(dev, baud, rtscts=rtscts, timeout=1)

    # an unplugged adapter ends the session; systemd restarts the bridge
    def _read(self):
        try:
            return self.port.read(256)
        except (self.error, OSError):
            return None

    def write(self, data):
        try:
            self.port.write(data)
            self.port.flush()
        except (self.error, OSError) as e:
            raise EOFError from e


class TcpLink(Link):
    """Serves one Atari at a time over TCP: Claude ST through STinG, a WiFi
    modem in transparent mode, or an emulator. Only addresses in `allow`
    may connect (when given). A new connection from the Atari replaces the
    old one, so a rebooted Atari never waits on a stale socket."""

    def __init__(self, hostport, allow=None):
        super().__init__()
        host, _, port = hostport.rpartition(":")
        self.allow = set(allow or [])
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((host or "0.0.0.0", int(port)))
        self.srv.listen(2)
        self.conn = None
        log.info("listening on %s:%d%s", *self.srv.getsockname(),
                 " for " + ", ".join(sorted(self.allow)) if self.allow else "")

    def _accept(self):
        conn, addr = self.srv.accept()
        if self.allow and addr[0] not in self.allow:
            log.warning("refused connection from %s (not in --allow)", addr[0])
            conn.close()
            return
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        if self.conn is not None:
            log.info("new connection from the Atari replaces the old one")
            self.conn.close()
        self.conn = conn
        self.buf = b""
        log.info("Atari connected from %s", addr[0])

    def _read(self):
        import select
        while True:
            socks = [self.srv] + ([self.conn] if self.conn else [])
            ready, _, _ = select.select(socks, [], [], 60)
            if self.srv in ready:
                self._accept()
                continue
            if self.conn and self.conn in ready:
                try:
                    data = self.conn.recv(4096)
                except OSError:
                    data = b""
                if data:
                    return data
                log.info("Atari disconnected")
                self.conn.close()
                self.conn = None
                self.buf = b""

    def write(self, data):
        if self.conn is None:
            return  # nobody to talk to; the Atari will say HELLO on reconnect
        try:
            self.conn.sendall(data)
        except OSError:
            self.conn.close()
            self.conn = None


class PipeLink(Link):
    """Read what the Atari sends from one file, write to another. With
    Hatari: hatari --rs232-out <in_path> --rs232-in <out_path>."""

    def __init__(self, in_path, out_path):
        super().__init__()
        for p in (in_path, out_path):
            if not os.path.exists(p):
                os.mkfifo(p)
        # open the write side first so Hatari's reader doesn't block us
        self.out = os.open(out_path, os.O_RDWR)
        self.inp = os.open(in_path, os.O_RDWR)

    def _read(self):
        return os.read(self.inp, 1024)

    def write(self, data):
        os.write(self.out, data)


# ---------------------------------------------------------------------------
# protocol
# ---------------------------------------------------------------------------

class Session:
    def __init__(self, link, backend):
        self.link = link
        self.be = backend
        self.chat_id = None
        self.project_id = None
        self.list_kind = "CHATS"
        self.query = ""
        self.long_ids = {}   # short stand-in -> real id, for ids the Atari can't hold

    # -- output --------------------------------------------------------

    def out(self, cmd, *fields):
        parts = [cmd.encode()] + [to_atari(str(f)) for f in fields]
        log.debug("-> %r", parts)
        self.link.write(b"\t".join(parts) + b"\n")

    def text(self, s):
        data = to_atari(s)
        for i in range(0, len(data), CHUNK):
            self.link.write(b"P\t" + data[i:i + CHUNK] + b"\n")

    def ops(self, ops):
        for op in ops:
            if op[0] == "P":
                if op[1]:
                    self.text(op[1])
            elif op[0] == "B":
                self.out("B")
            elif op[0] == "H":
                self.out("H", op[1])

    def message(self, role, body):
        self.out("M", role)
        self.ops(format_text(body))
        self.out("Z")

    def send_list(self, kind, title, items, back=False):
        """items: (id, label) or (id, label, pinned); pinned ones go first,
        like claude.ai's Starred section."""
        self.list_kind = kind
        items = sorted(items, key=lambda it: not (len(it) > 2 and it[2]))
        self.out("L", kind, title)
        if back:
            self.out("I", "..", "< All projects")
        for it in items[:299]:
            iid = self.short_id(it[0])
            if len(it) > 2 and it[2]:
                self.out("I", iid, it[1] or "Untitled", "P")
            else:
                self.out("I", iid, it[1] or "Untitled")
        self.out("E")

    def short_id(self, real):
        """Ids longer than the Atari keeps (artifact file paths, say) are
        sent as a short, stable stand-in and translated back in handle()."""
        real = str(real)
        if len(real) <= MAX_ID:
            return real
        short = "~" + hashlib.sha1(real.encode()).hexdigest()[:20]
        self.long_ids[short] = real
        return short

    def refresh_list(self):
        """Re-send whatever the sidebar is showing, after it changed."""
        if self.list_kind == "PROJECTS":
            self.send_list("PROJECTS", "Projects", self.be.list_projects())
        elif self.list_kind == "PROJECT" and self.project_id:
            name, chats = self.be.project_chats(self.project_id)
            self.send_list("PROJECT", name, chats, back=True)
        elif self.list_kind == "SEARCH":
            self.send_list("SEARCH", "Search: " + self.query, self.be.search(self.query))
        elif self.list_kind == "ARTIFACTS":
            pass
        else:
            self.send_list("CHATS", "Recents", self.be.list_chats())

    def status(self, s):
        self.out("S", s)

    # -- commands ------------------------------------------------------

    def send_models(self):
        """The model menu: O, then V <id> <label> per model, U <id> <label>
        per effort level of the current model, then K <model> <effort>."""
        self.out("O")
        for mid, label in self.be.models():
            self.out("V", mid, label)
        for eid, label in backends.efforts_for(self.be.model):
            self.out("U", eid, label)
        self.out("K", self.be.model, self.be.effort or "")

    def cmd_hello(self, *_):
        self.status("Loading chats...")
        self.send_list("CHATS", "Recents", self.be.list_chats())
        self.out("C", self.chat_id or "")
        self.send_models()
        self.status("Online: " + self.be.whoami())

    def cmd_list(self, kind="CHATS", *_):
        self.status("Loading...")
        if kind == "PROJECTS":
            self.project_id = None
            self.send_list("PROJECTS", "Projects", self.be.list_projects())
        elif kind == "ARTIFACTS":
            self.status("Looking for artifacts...")
            arts = self.be.list_artifacts(
                progress=lambda i, n: self.status("Looking in chat %d of %d..." % (i, n)))
            self.send_list("ARTIFACTS", "Artifacts", arts)
            if not arts:
                n = getattr(self.be, "last_scan", None)
                self.message("I", "No artifacts found%s." % (
                    " in your %d most recent chats" % n if n else ""))
        else:
            self.project_id = None
            self.send_list("CHATS", "Recents", self.be.list_chats())
        self.status("Online: " + self.be.whoami())

    def cmd_open(self, kind, oid, *_):
        if kind == "PROJECT":
            self.status("Loading project...")
            name, chats = self.be.project_chats(oid)
            self.project_id = oid
            self.send_list("PROJECT", name, chats, back=True)
            self.status("Project: " + name)
            return
        if kind == "ARTIFACT":
            self.status("Loading artifact...")
            title, body = self.be.get_artifact(oid)
            self.out("T", title)
            self.out("R")
            self.message("K", body)
            self.status("Online: " + self.be.whoami())
            return
        self.status("Loading chat...")
        title, msgs = self.be.get_chat(oid)
        self.chat_id = oid
        self.out("T", title)
        self.out("C", oid)
        self.out("R")
        if len(msgs) > HISTORY_MESSAGES:
            self.message("I", "%d earlier messages are not shown." % (len(msgs) - HISTORY_MESSAGES))
            msgs = msgs[-HISTORY_MESSAGES:]
        for role, body in msgs:
            self.message(role, body)
        self.status("Online: " + self.be.whoami())

    def cmd_new(self, quiet="", *_):
        """Start a new chat. QUIET: the Atari switched to another area and
        sets its own title; the next message still starts a new chat."""
        self.chat_id = None
        self.out("C", "")
        if quiet != "QUIET":
            self.out("T", "New chat")

    def cmd_send(self, text="", *_):
        text = text.strip()
        if not text:
            return
        new = self.chat_id is None
        if new:
            self.out("R")
        self.message("U", text)
        self.out("Y", "1")
        self.status("Claude is thinking...")
        self.out("M", "A")
        fmt = Formatter()
        try:
            chat_id, title = self.be.send(self.chat_id, self.project_id, text,
                                          lambda d: self.ops(fmt.feed(d)))
        finally:
            self.ops(fmt.finish())
            self.out("Z")
            self.out("Y", "0")
        self.chat_id = chat_id
        if title:
            self.out("T", title)
        self.out("C", chat_id)
        if new and self.list_kind == "CHATS":
            self.send_list("CHATS", "Recents", self.be.list_chats())
        elif new and self.list_kind == "PROJECT" and self.project_id:
            name, chats = self.be.project_chats(self.project_id)
            self.send_list("PROJECT", name, chats, back=True)
        self.status("Online: " + self.be.whoami())

    def cmd_choose(self, model="", effort="", *_):
        model, effort = self.be.choose(model, effort)
        log.info("model %s, effort %s", model, effort or "-")
        self.send_models()
        label = dict(self.be.models()).get(model, model)
        self.status("Model: %s%s" % (label, ", effort " + effort if effort else ""))

    def cmd_find(self, query="", *_):
        self.query = query
        self.status("Searching...")
        self.send_list("SEARCH", "Search: " + query, self.be.search(query))
        self.status("Online: " + self.be.whoami())

    # -- right-click menu actions --------------------------------------

    def _done(self, msg):
        self.refresh_list()
        self.status(msg)

    def cmd_rename(self, kind, item_id, name="", *_):
        name = name.strip()
        if not name:
            return
        self.status("Renaming...")
        self.be.rename(kind, item_id, name)
        if kind == "CHAT" and item_id == self.chat_id:
            self.out("T", name)
        self._done("Renamed")

    def cmd_pin(self, kind, item_id, want="", *_):
        """want: "1" pin, "0" unpin, empty: toggle."""
        pinned = (want == "0") if want in ("0", "1") else self._is_pinned(kind, item_id)
        self.status("Unpinning..." if pinned else "Pinning...")
        self.be.set_pinned(kind, item_id, not pinned)
        self._done("Unpinned" if pinned else "Pinned")

    def _is_pinned(self, kind, item_id):
        items = self.be.list_projects() if kind == "PROJECT" else self.be.list_chats(limit=500)
        return any(it[0] == item_id and len(it) > 2 and it[2] for it in items)

    def cmd_delete(self, kind, item_id, *_):
        self.status("Deleting...")
        self.be.delete(kind, item_id)
        if kind == "CHAT" and item_id == self.chat_id:
            self.chat_id = None
            self.out("R")
            self.out("C", "")
            self.out("T", "New chat")
        if kind == "PROJECT" and item_id == self.project_id:
            self.project_id = None
            self.list_kind = "PROJECTS"
        self._done("Deleted")

    def cmd_archive(self, kind, item_id, *_):
        self.status("Archiving...")
        self.be.archive_project(item_id)
        self._done("Archived")

    def cmd_pickproj(self, chat_id, *_):
        """The Atari wants to choose a project for "Move to project"."""
        self.status("Loading projects...")
        projects = self.be.list_projects()
        self.out("Q")
        for it in projects[:20]:
            self.out("J", it[0], it[1] or "Untitled project")
        self.out("W")
        self.status("Online: " + self.be.whoami())

    def cmd_move(self, chat_id, project_id, *_):
        self.status("Moving...")
        self.be.move_chat(chat_id, project_id)
        self._done("Moved to project")

    # -- saving an artifact on the Atari --------------------------------

    def cmd_save(self, kind, item_id, *_):
        """The Atari wants to save an artifact: suggest a file name; it
        answers with FETCH once the user has picked where to save it."""
        name, _ = self.be.artifact_file(item_id)
        self.out("F", name)

    def cmd_fetch(self, kind, item_id, *_):
        """Send the artifact as hex-encoded D lines, then G <size>. Text is
        converted to the Atari character set with CR/LF line ends."""
        name, text = self.be.artifact_file(item_id)
        if isinstance(text, bytes):     # a binary file (.docx, .pdf...) as is
            data = text
        else:
            data = b"\r\n".join(to_atari(line) for line in text.split("\n"))
        self.status("Sending %s..." % name)
        for i in range(0, len(data), 90):
            self.link.write(b"D\t" + data[i:i + 90].hex().upper().encode() + b"\n")
        self.out("G", len(data))
        self.status("Online: " + self.be.whoami())

    def cmd_bye(self, *_):
        log.info("Atari closed Claude ST")

    def handle(self, raw: bytes):
        fields = from_atari(raw).split("\t")
        cmd, args = fields[0].upper(), [self.long_ids.get(f, f) for f in fields[1:]]
        fn = getattr(self, "cmd_" + cmd.lower(), None)
        log.debug("<- %r", fields)
        if fn is None:
            return
        try:
            fn(*args)
        except EOFError:
            raise
        except Exception as e:  # report on the Atari, keep serving
            log.error("%s failed: %s", cmd, e)
            log.debug(traceback.format_exc())
            self.out("Y", "0")
            self.message("E", str(e) or e.__class__.__name__)
            self.status("Error - see chat")

    def run(self):
        while True:
            try:
                line = self.link.readline()
            except EOFError:
                return
            if line:
                self.handle(line)


def make_backend(args):
    if args.backend == "demo":
        return backends.DemoBackend()
    if args.backend == "api":
        return backends.ApiBackend(args.store, model=args.model)
    key = os.environ.get("CLAUDE_SESSION_KEY")
    if not key:
        # keep running so the Atari can show what's wrong
        log.error("CLAUDE_SESSION_KEY is not set")

        def no_key():
            raise RuntimeError("No claude.ai session key is set on the gateway. "
                               "Set CLAUDE_SESSION_KEY (on a Pi: sudo ./install.sh --set-key).")
        return backends.LazyBackend(no_key, "claude.ai (no key)")
    return backends.LazyBackend(lambda: backends.ClaudeAiBackend(
        key, org_id=args.org, artifact_scan=args.artifact_scan),
                                "claude.ai")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", choices=["claudeai", "api", "demo"], default="claudeai")
    link = ap.add_mutually_exclusive_group()
    link.add_argument("--serial", metavar="DEVICE", help="serial port, e.g. /dev/ttyUSB0 or COM3, or \"auto\"")
    link.add_argument("--tcp", metavar="HOST:PORT", help="listen for the Atari on a TCP port (STinG, WiFi modem)")
    link.add_argument("--pipe", nargs=2, metavar=("FROM_ST", "TO_ST"), help="files/FIFOs (Hatari)")
    ap.add_argument("--allow", action="append", metavar="IP",
                    help="with --tcp: only accept this Atari address (repeatable)")
    ap.add_argument("--baud", type=int, default=19200)
    ap.add_argument("--rtscts", action="store_true", help="hardware flow control")
    ap.add_argument("--org", help="claude.ai organization uuid (default: first chat org)")
    ap.add_argument("--model", default="claude-opus-5-5", help="model for --backend api")
    ap.add_argument("--store", default="~/.claude-st", help="history dir for --backend api")
    ap.add_argument("--probe", nargs="?", const="", metavar="WORD",
                    help="claude.ai: list what kinds of blocks your recent chats hold "
                         "(no content), to diagnose a missing artifact, then exit. With "
                         "WORD, also detail the chats with WORD in their title")
    ap.add_argument("--artifact-scan", type=int, default=100, metavar="N",
                    help="claude.ai: how many recent chats to search for artifacts")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    if not (args.probe is not None or args.serial or args.tcp or args.pipe):
        ap.error("one of --serial, --tcp or --pipe is required")
    backend = make_backend(args)
    if args.probe is not None:
        if args.backend != "claudeai":
            ap.error("--probe is for the claude.ai backend")
        for kind, count in sorted(backend.probe(args.artifact_scan, args.probe).items()):
            print("%6d  %s" % (count, kind))
        return
    if args.serial:
        link = SerialLink(args.serial, args.baud, args.rtscts)
    elif args.tcp:
        link = TcpLink(args.tcp, args.allow)
    else:
        link = PipeLink(*args.pipe)
    log.info("Claude ST bridge ready (%s backend)", backend.name)
    try:
        Session(link, backend).run()
    except KeyboardInterrupt:
        return
    except EOFError:
        pass
    sys.exit("link closed")


if __name__ == "__main__":
    main()
