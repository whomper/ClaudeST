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
import json
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
        self.new_choice = None      # the model and effort for new chats (CHOOSE)
        self.chat_kind = "CHAT"     # or "CODE": chat_id is a Claude Code session
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
        """items: (id, label), (id, label, pinned) or (id, label, pinned,
        when); pinned ones go first. Items with a time are grouped under
        date headings (X lines) as on claude.ai: Pinned, Today, Yesterday,
        a date for the rest of the week, then Older."""
        self.list_kind = kind
        items = sorted(items, key=lambda it: not (len(it) > 2 and it[2]))
        dated = any(len(it) > 3 and it[3] for it in items)
        self.out("L", kind, title)
        if back:
            self.out("I", "..", "< All projects")
        rows, last = 0, None
        for it in items:
            pinned = len(it) > 2 and it[2]
            if dated:
                head = "Pinned" if pinned else backends.date_heading(it[3] if len(it) > 3 else None)
                if head and head != last:
                    if rows >= 298:
                        break
                    self.out("X", head)
                    rows += 1
                last = head or last
            if rows >= 299:                  # the Atari holds 300 rows
                break
            rows += 1
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
        elif self.list_kind == "CODE":
            self.send_list("CODE", "Code", self.be.list_code_sessions())
        else:
            self.send_list("CHATS", "Recents", self.be.list_chats())

    def online(self):
        """The command is done: clear its notice (an empty N), and S, the
        status line, which shows only the connection to claude.ai."""
        self.out("N", "")
        self.out("S", "Online: " + self.be.whoami())

    def notice(self, s):
        """N: a passing notice ("Loading...", "Model: Opus 5.5"). The Atari
        shows it in the title bar, or in the list while one loads."""
        self.out("N", s)

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
        self.notice("Loading chats...")
        self.send_list("CHATS", "Recents", self.be.list_chats())
        self.out("C", self.chat_id or "")
        if self.new_choice is None:
            self.new_choice = (self.be.model, self.be.effort)
        self.send_models()
        self.online()

    def cmd_list(self, kind="CHATS", *_):
        self.notice("Loading...")
        if kind == "PROJECTS":
            self.project_id = None
            self.send_list("PROJECTS", "Projects", self.be.list_projects())
        elif kind == "ARTIFACTS":
            self.notice("Looking for artifacts...")
            arts = self.be.list_artifacts(
                progress=lambda i, n: self.notice("Scanning chat %d/%d" % (i, n)))
            self.send_list("ARTIFACTS", "Artifacts", arts)
            if not arts:
                n = getattr(self.be, "last_scan", None)
                self.message("I", "No artifacts found%s." % (
                    " in your %d most recent chats" % n if n else ""))
        elif kind == "CODE":
            self.notice("Loading sessions...")
            sessions = self.be.list_code_sessions()
            self.send_list("CODE", "Code", sessions)
            if not sessions:
                self.message("I", "No Claude Code sessions found.")
        else:
            self.project_id = None
            self.send_list("CHATS", "Recents", self.be.list_chats())
        self.online()

    def cmd_open(self, kind, oid, *_):
        if kind == "PROJECT":
            self.notice("Loading project...")
            name, chats = self.be.project_chats(oid)
            self.project_id = oid
            self.send_list("PROJECT", name, chats, back=True)
            self.online()
            return
        if kind == "ARTIFACT":
            self.notice("Loading artifact...")
            title, body = self.be.get_artifact(oid)
            self.out("T", title)
            self.out("R")
            self.message("K", body)
            self.online()
            return
        if kind == "CODE":
            self.notice("Loading session...")
            title, msgs = self.be.get_code_session(oid)
            self.chat_id, self.chat_kind = oid, "CODE"
            choice = getattr(self.be, "chat_choice", None)    # the session's model
            if choice and choice[0]:
                self.be.use_chat_model(*choice)
            elif self.new_choice:
                self.be.choose(*self.new_choice)
            self.send_models()
            self.out("T", title)
            self.out("C", oid)
            self.out("R")
            if len(msgs) > HISTORY_MESSAGES:
                self.message("I", "%d earlier messages are not shown." % (len(msgs) - HISTORY_MESSAGES))
                msgs = msgs[-HISTORY_MESSAGES:]
            for role, body in msgs:
                self.message(role, body)
            self.online()
            return
        self.notice("Loading chat...")
        title, msgs = self.be.get_chat(oid)
        self.chat_id, self.chat_kind = oid, "CHAT"
        # each chat keeps its own model; chats without one get the choice
        # for new chats
        choice = getattr(self.be, "chat_choice", None)
        if choice and choice[0]:
            self.be.use_chat_model(*choice)
        elif self.new_choice:
            self.be.choose(*self.new_choice)
        self.send_models()
        self.out("T", title)
        self.out("C", oid)
        self.out("R")
        if len(msgs) > HISTORY_MESSAGES:
            self.message("I", "%d earlier messages are not shown." % (len(msgs) - HISTORY_MESSAGES))
            msgs = msgs[-HISTORY_MESSAGES:]
        for role, body in msgs:
            self.message(role, body)
        self.online()

    def cmd_new(self, quiet="", *_):
        """Start a new chat. QUIET: the Atari switched to another area and
        sets its own title; the next message still starts a new chat."""
        self.chat_id = None
        self.chat_kind = "CHAT"
        self.out("C", "")
        if self.new_choice and self.new_choice != (self.be.model, self.be.effort):
            self.be.choose(*self.new_choice)
            self.send_models()
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
        self.notice("Claude is thinking...")
        self.out("M", "A")
        fmt = Formatter()
        if self.chat_kind == "CODE":
            self.notice("Claude Code is working...")
            try:
                title = self.be.send_code(self.chat_id, text, lambda d: self.ops(fmt.feed(d)))
            finally:
                self.ops(fmt.finish())
                self.out("Z")
                self.out("Y", "0")
            if title:
                self.out("T", title)
            self.online()
            return
        try:
            chat_id, title = self.be.send(self.chat_id, self.project_id, text,
                                          lambda d: self.ops(fmt.feed(d)))
        finally:
            self.ops(fmt.finish())
            self.out("Z")
            self.out("Y", "0")
        self.chat_id = chat_id
        if new:
            self.be.remember_effort(chat_id, self.be.effort)
        if title:
            self.out("T", title)
        self.out("C", chat_id)
        if new and self.list_kind == "CHATS":
            self.send_list("CHATS", "Recents", self.be.list_chats())
        elif new and self.list_kind == "PROJECT" and self.project_id:
            name, chats = self.be.project_chats(self.project_id)
            self.send_list("PROJECT", name, chats, back=True)
        self.online()

    def cmd_account(self, *_):
        """The Account page: plan, usage and when it resets, account details."""
        self.notice("Loading account...")
        report = self.be.account_report()
        self.out("T", "Account")
        self.out("R")
        self.message("X", report)     # a page: no "Info" label above it
        self.online()

    def cmd_choose(self, model="", effort="", *_):
        model, effort = self.be.choose(model, effort)
        log.info("model %s, effort %s", model, effort or "-")
        if self.chat_kind == "CODE" and self.chat_id:
            # a Claude Code session: switch its model, if claude.ai lets us
            try:
                self.be.set_code_model(self.chat_id, model, effort)
            except Exception as e:
                log.info("session model: %s", e)
                self.message("I", "claude.ai didn't accept a model change for this "
                                  "session (%s)." % e)
        else:
            self.new_choice = (model, effort)     # also for the next new chats
            if self.chat_id:
                self.be.set_chat_model(self.chat_id, model, effort)
        self.send_models()
        label = dict(self.be.models()).get(model, model)
        log.info("model for chat %s: %s", self.chat_id or "(new)", label)

    def cmd_find(self, query="", *_):
        self.query = query
        self.notice("Searching...")
        self.send_list("SEARCH", "Search: " + query, self.be.search(query))
        self.online()

    # -- right-click menu actions --------------------------------------

    def _done(self, msg):
        self.refresh_list()
        self.online()
        self.notice(msg)        # "Renamed" etc., shown for a few seconds

    def cmd_rename(self, kind, item_id, name="", *_):
        name = name.strip()
        if not name:
            return
        self.notice("Renaming...")
        if kind == "CODE":
            self.be.rename_code(item_id, name)
        else:
            self.be.rename(kind, item_id, name)
        if kind in ("CHAT", "CODE") and item_id == self.chat_id:
            self.out("T", name)
        self._done("Renamed")

    def cmd_pin(self, kind, item_id, want="", *_):
        """want: "1" pin, "0" unpin, empty: toggle."""
        pinned = (want == "0") if want in ("0", "1") else self._is_pinned(kind, item_id)
        self.notice("Unpinning..." if pinned else "Pinning...")
        self.be.set_pinned(kind, item_id, not pinned)
        self._done("Unpinned" if pinned else "Pinned")

    def _is_pinned(self, kind, item_id):
        items = self.be.list_projects() if kind == "PROJECT" else self.be.list_chats(limit=500)
        return any(it[0] == item_id and len(it) > 2 and it[2] for it in items)

    def cmd_delete(self, kind, item_id, *_):
        self.notice("Deleting...")
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
        self.notice("Archiving...")
        if kind == "CODE":
            self.be.archive_code(item_id)
            if item_id == self.chat_id:
                self.cmd_new()
                self.out("R")
        else:
            self.be.archive_project(item_id)
        self._done("Archived")

    def cmd_pickproj(self, chat_id, *_):
        """The Atari wants to choose a project for "Move to project"."""
        self.notice("Loading projects...")
        projects = self.be.list_projects()
        self.out("Q")
        for it in projects[:20]:
            self.out("J", it[0], it[1] or "Untitled project")
        self.online()           # clear "Loading projects..." before the menu opens
        self.out("W")

    def cmd_newproj(self, chat_id, name="", *_):
        """"New project..." in the Move menu: create it, move the chat in."""
        name = name.strip()
        if not name:
            return
        self.notice("Creating project...")
        project_id = self.be.create_project(name)
        self.be.move_chat(chat_id, project_id)
        self._done("Moved to " + name)

    def cmd_move(self, chat_id, project_id, *_):
        self.notice("Moving...")
        self.be.move_chat(chat_id, project_id)
        self._done("Moved to project")

    # -- saving an artifact on the Atari --------------------------------

    def cmd_save(self, kind, item_id, *_):
        """The Atari wants to save an artifact: suggest a file name; it
        answers with FETCH once the user has picked where to save it."""
        self.out("F", self.be.artifact_name(item_id))

    def cmd_fetch(self, kind, item_id, *_):
        """Send the artifact as hex-encoded D lines, then G <size>. Text is
        converted to the Atari character set with CR/LF line ends."""
        name, text = self.be.artifact_file(item_id)
        if isinstance(text, bytes):     # a binary file (.docx, .pdf...) as is
            data = text
        else:
            data = b"\r\n".join(to_atari(line) for line in text.split("\n"))
        self.notice("Sending %s..." % name)
        for i in range(0, len(data), 90):
            self.link.write(b"D\t" + data[i:i + 90].hex().upper().encode() + b"\n")
        self.out("G", len(data))
        self.be.forget_artifact_data(item_id)
        self.online()

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
            self.out("N", "")
            # the status line says whether claude.ai can be reached at all
            if getattr(self.be, "real", True) is None:
                self.out("S", "Not signed in to claude.ai")
            else:
                self.out("S", "Online: " + self.be.whoami())

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
        key, org_id=args.org, artifact_scan=args.artifact_scan, state_dir=args.store),
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
    ap.add_argument("--probe-chat", metavar="CHAT_ID",
                    help="claude.ai: show the fields claude.ai keeps for one chat (not its "
                         "messages), then exit")
    ap.add_argument("--probe-code", action="store_true",
                    help="claude.ai: check that your Claude Code sessions can be listed "
                         "(titles and counts only), then exit")
    ap.add_argument("--artifact-scan", type=int, default=100, metavar="N",
                    help="claude.ai: how many recent chats to search for artifacts")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    if args.probe_chat:
        backend = make_backend(args)
        conv = backend._conversation(args.probe_chat)
        for k in sorted(conv):
            v = conv[k]
            if k == "chat_messages":
                print("chat_messages: %d messages" % len(v or []))
            elif isinstance(v, (dict, list)):
                print("%s: %s" % (k, json.dumps(v)[:300]))
            else:
                print("%s: %r" % (k, v if not isinstance(v, str) else v[:120]))
        return
    if args.probe_code:
        backend = make_backend(args)
        sessions = backend.list_code_sessions()
        print("%d Claude Code sessions (via %s)" % (len(sessions), getattr(backend, "_code_base", "?")))
        for sid, title in sessions[:10]:
            print("  %s  %s" % (sid, title))
        if sessions:
            sid = sessions[0][0]
            events = backend._code_events(sid, cap=400)
            from collections import Counter
            print("newest session: %d events, kinds %s" % (len(events), dict(Counter(
                backends._event_payload(e)[0] for e in events))))
            print("first event keys: %s" % sorted(events[0]) if events else "no events")
            info = backend._code("get", "/sessions/%s" % sid)
            print("newest session: model %r, effort found %r" % (
                backend._session_model(info), backends.find_effort(info) or backends.find_effort(
                    [backends._event_payload(e)[1] for e in events
                     if backends._event_payload(e)[0] == "system"])))
            print("session fields: %s; session_context fields: %s" % (
                sorted(info), sorted(info.get("session_context") or {})))
        return
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
