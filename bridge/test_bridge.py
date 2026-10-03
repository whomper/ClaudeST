"""Offline tests for the bridge: charset, Markdown formatting and the
protocol session against the demo backend. Run: python3 -m unittest"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from atari_text import Formatter, format_text, from_atari, to_atari  # noqa: E402
from backends import DemoBackend  # noqa: E402
from claude_bridge import Link, Session  # noqa: E402


class FakeLink(Link):
    def __init__(self):
        super().__init__()
        self.sent = b""

    def write(self, data):
        self.sent += data

    def lines(self):
        return [l.split(b"\t") for l in self.sent.split(b"\n") if l]


class Charset(unittest.TestCase):
    def test_roundtrip_latin(self):
        s = "Café über Øl ß"
        self.assertEqual(from_atari(to_atari(s)), s)

    def test_substitutions(self):
        self.assertEqual(to_atari("“hi” — ok…"), b'"hi" -- ok...')

    def test_no_control_chars(self):
        out = to_atari("a\tb\nc\x1b🎉")
        self.assertNotIn(b"\n", out)
        self.assertNotIn(b"\t", out)
        self.assertEqual(out, b"a    bc")


class Markdown(unittest.TestCase):
    def test_blocks(self):
        ops = format_text("# Head\n**bold** *it* a_b\n- item\n```c\nx;\n```\n")
        self.assertEqual(ops, [
            ("H", "Head"),
            ("P", "bold it a_b"), ("B",),
            ("P", "- item"), ("B",),
            ("P", "[c]"), ("B",), ("P", "  x;"), ("B",), ("P", "[end]"), ("B",),
        ])

    def test_streaming_matches_whole(self):
        text = "Hello **world**, see [docs](https://example.com).\n- one\n- two\nEnd"
        f = Formatter()
        ops = []
        for ch in text:
            ops += f.feed(ch)
        ops += f.finish()
        joined = "".join(o[1] if o[0] == "P" else "\n" for o in ops)
        whole = "".join(o[1] if o[0] == "P" else "\n" for o in format_text(text))
        self.assertEqual(joined, whole)
        self.assertIn("docs <https://example.com>", joined)


class Protocol(unittest.TestCase):
    def setUp(self):
        self.link = FakeLink()
        self.s = Session(self.link, DemoBackend())

    def test_hello_lists_chats(self):
        self.s.handle(b"HELLO\t1\t1.0")
        lines = self.link.lines()
        self.assertIn([b"L", b"CHATS", b"Recents"], lines)
        self.assertEqual(sum(1 for l in lines if l[0] == b"I"), 4)
        self.assertEqual(lines[-1][0], b"S")

    def test_open_chat(self):
        self.s.handle(b"OPEN\tCHAT\td1")
        lines = self.link.lines()
        self.assertIn([b"T", b"Atari Falcon DSP tricks"], lines)
        self.assertIn([b"M", b"U"], lines)
        self.assertIn([b"M", b"A"], lines)
        self.assertIn([b"H", b"Getting started"], lines)

    def test_send_new_chat_streams_and_refreshes(self):
        self.s.handle(b"NEW")
        self.s.handle(b"SEND\tHi there")
        lines = self.link.lines()
        self.assertIn([b"Y", b"1"], lines)
        self.assertIn([b"Y", b"0"], lines)
        self.assertGreater(sum(1 for l in lines if l[0] == b"P"), 5)
        self.assertEqual(self.s.chat_id, "d5")
        self.assertIn([b"C", b"d5"], lines)

    def test_quiet_new_keeps_the_atari_title_and_starts_fresh(self):
        self.s.handle(b"OPEN\tCHAT\td1")
        self.link.sent = b""
        self.s.handle(b"NEW\tQUIET")
        self.assertEqual(self.link.lines(), [[b"C", b""]])
        self.assertIsNone(self.s.chat_id)

    def test_new_chat_inside_an_open_project(self):
        self.s.handle(b"OPEN\tPROJECT\tp1")
        self.s.handle(b"NEW\tQUIET")
        self.s.handle(b"SEND\thello")
        name, chats = self.s.be.project_chats("p1")
        self.assertIn("hello", [c[1] for c in chats])

    def test_project_and_back(self):
        self.s.handle(b"OPEN\tPROJECT\tp1")
        lines = self.link.lines()
        self.assertIn([b"L", b"PROJECT", b"Falcon audio"], lines)
        self.assertIn([b"I", b"..", b"< All projects"], lines)

    def test_search(self):
        self.s.handle(b"FIND\tgfa")
        items = [l for l in self.link.lines() if l[0] == b"I"]
        self.assertEqual(items, [[b"I", b"d2", b"Fix my GFA BASIC loop"]])

    def test_errors_are_shown_not_raised(self):
        self.s.handle(b"OPEN\tCHAT\tnope")
        lines = self.link.lines()
        self.assertIn([b"M", b"E"], lines)

    def test_hebrew_reaches_the_atari_as_st_charset(self):
        self.s.handle(b"OPEN\tCHAT\td4")
        sent = self.link.sent
        # "מה זה" (mem, he, space, zayin, he) in Atari codes, in logical order:
        # the Atari lays it out right to left itself
        self.assertIn(b"\xce\xc6 \xc8\xc6", sent)
        self.assertIn(b"\xc6-Atari Falcon 030", sent)  # "ה-Atari": maqaf-style hyphen kept

    def test_atari_charset_input(self):
        self.s.handle(b"FIND\t\x81ber")  # "über" typed on the ST
        self.assertIn([b"L", b"SEARCH", b"Search: \x81ber"], self.link.lines())

    def test_long_text_is_chunked(self):
        self.s.message("A", "x" * 1000)
        for l in self.link.lines():
            self.assertLess(len(b"\t".join(l)), 200)


class ItemActions(unittest.TestCase):
    """The right-click menu commands, against the demo backend."""

    def setUp(self):
        self.link = FakeLink()
        self.s = Session(self.link, DemoBackend())
        self.s.handle(b"HELLO\t1\t1.0")
        self.link.sent = b""

    def items(self):
        return [l for l in self.link.lines() if l[0] == b"I"]

    def test_pin_moves_item_to_top_with_flag(self):
        self.s.handle(b"PIN\tCHAT\td3\t1")
        self.assertEqual(self.items()[0], [b"I", b"d3", b"Ideas for a demoscene intro", b"P"])
        self.link.sent = b""
        self.s.handle(b"PIN\tCHAT\td3\t0")
        self.assertNotIn(b"\tP", self.link.sent)

    def test_pin_toggles_without_value(self):
        self.s.handle(b"PIN\tCHAT\td2")
        self.assertIn([b"I", b"d2", b"Fix my GFA BASIC loop", b"P"], self.items())

    def test_rename_open_chat_updates_title(self):
        self.s.handle(b"OPEN\tCHAT\td1")
        self.link.sent = b""
        self.s.handle(b"RENAME\tCHAT\td1\tDSP notes")
        lines = self.link.lines()
        self.assertIn([b"T", b"DSP notes"], lines)
        self.assertIn([b"I", b"d1", b"DSP notes"], lines)

    def test_delete_open_chat_clears_pane(self):
        self.s.handle(b"OPEN\tCHAT\td2")
        self.link.sent = b""
        self.s.handle(b"DELETE\tCHAT\td2")
        lines = self.link.lines()
        self.assertIn([b"R"], lines)
        self.assertIn([b"T", b"New chat"], lines)
        self.assertNotIn(b"d2", b"".join(l[1] for l in self.items()))
        self.assertIsNone(self.s.chat_id)

    def test_move_to_project_picker(self):
        self.s.handle(b"PICKPROJ\td2")
        lines = self.link.lines()
        self.assertEqual(lines[1], [b"Q"])
        self.assertIn([b"J", b"p2", b"Demoscene"], lines)
        self.assertIn([b"W"], lines)
        self.s.handle(b"MOVE\td2\tp2")
        self.link.sent = b""
        self.s.handle(b"OPEN\tPROJECT\tp2")
        self.assertIn([b"I", b"d2", b"Fix my GFA BASIC loop"], self.items())

    def test_project_rename_archive_delete(self):
        self.s.handle(b"LIST\tPROJECTS")
        self.s.handle(b"RENAME\tPROJECT\tp1\tFalcon sound")
        self.assertIn([b"I", b"p1", b"Falcon sound"], self.items())
        self.link.sent = b""
        self.s.handle(b"ARCHIVE\tPROJECT\tp1")
        self.assertEqual([l[1] for l in self.items()], [b"p2"])
        self.link.sent = b""
        self.s.handle(b"DELETE\tPROJECT\tp2")
        self.assertEqual(self.items(), [])

    def test_errors_reach_the_atari(self):
        self.s.handle(b"RENAME\tCHAT\tnope\tx")
        self.assertIn([b"M", b"E"], self.link.lines())


class ClaudeAiLists(unittest.TestCase):
    """The claude.ai backend's list handling, with the HTTP calls stubbed."""

    def backend(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org = "org"
        be.artifact_scan = 15
        be._artifacts = {}
        be._scanned = {}
        conv = ClaudeAiParsing.CONV
        chats = [{"uuid": conv["uuid"], "name": "Snake game", "is_starred": True,
                  "project_uuid": "p1"}]
        projects = [{"uuid": "p1", "name": "Games", "is_starred": False}]

        def get(path, **params):
            if path.endswith("/chat_conversations"):
                return chats
            if path.endswith("/projects"):
                return projects
            if "/projects/" in path:
                return chats
            return conv
        be._get = get
        return be

    def test_artifacts_list_from_pinned_chats(self):
        # list_chats returns (id, title, pinned); this used to crash
        be = self.backend()
        self.assertEqual(be.list_chats(), [(ClaudeAiParsing.CONV["uuid"], "Snake game", True)])
        arts = be.list_artifacts()
        self.assertEqual([a[1] for a in arts], ["Snake"])
        title, body = be.get_artifact(arts[0][0])
        self.assertEqual(title, "Snake")
        self.assertIn("let x = 1;", body)

    def test_artifacts_over_the_protocol(self):
        link = FakeLink()
        Session(link, self.backend()).handle(b"LIST\tARTIFACTS")
        self.assertNotIn([b"M", b"E"], link.lines())
        self.assertIn([b"L", b"ARTIFACTS", b"Artifacts"], link.lines())

    def test_project_chats_name(self):
        name, chats = self.backend().project_chats("p1")
        self.assertEqual(name, "Games")
        self.assertEqual(len(chats), 1)


class ArtifactFormats(unittest.TestCase):
    """The three ways artifacts appear in claude.ai conversations."""

    def harvest(self, messages):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be._artifacts = {}
        be._harvest_artifacts({"uuid": "abcdef12-x", "name": "Chat", "chat_messages": messages})
        return be

    def test_ant_artifact_tags_in_text(self):
        be = self.harvest([{"sender": "assistant", "content": [{"type": "text", "text":
            'Here:\n<antArtifact identifier="snake" type="application/vnd.ant.code" '
            'language="python" title="Snake game">\nprint(1)\n</antArtifact>\nDone.'}]}])
        (key, art), = be._artifacts.items()
        self.assertEqual(art["title"], "Snake game")
        self.assertEqual(art["content"], "print(1)")
        self.assertEqual(be.artifact_file(key)[0], "SNAKE_GA.PY")

    def test_old_messages_with_only_text(self):
        be = self.harvest([{"sender": "assistant", "text":
            '<antArtifact identifier="n" type="text/markdown" title="Notes"># Hi</antArtifact>'}])
        self.assertEqual(be.artifact_file(next(iter(be._artifacts)))[0], "NOTES.MD")

    def test_file_tools(self):
        be = self.harvest([{"sender": "assistant", "content": [
            {"type": "tool_use", "name": "create_file",
             "input": {"path": "/mnt/user-data/outputs/report.html", "file_text": "<p>old</p>"}},
            {"type": "tool_use", "name": "str_replace",
             "input": {"path": "/mnt/user-data/outputs/report.html", "old_str": "old", "new_str": "new"}}]}])
        name, text = be.artifact_file(next(iter(be._artifacts)))
        self.assertEqual((name, text), ("REPORT.HTM", "<p>new</p>"))


class LongIds(unittest.TestCase):
    """Artifact ids can be long file paths; the Atari keeps 39 characters."""

    def test_long_artifact_ids_round_trip(self):
        class LongPaths(DemoBackend):
            PATH = "abcdef12:/mnt/user-data/outputs/a_rather_long_report_name.html"

            def list_artifacts(self):
                return [("a1", "short one"), (self.PATH, "report")]

            def get_artifact(self, aid):
                if aid != self.PATH:
                    raise RuntimeError("not found: " + aid)
                return "report", "<p>hi</p>"
        link = FakeLink()
        s = Session(link, LongPaths())
        s.handle(b"LIST\tARTIFACTS")
        ids = [l[1] for l in link.lines() if l[0] == b"I"]
        self.assertEqual(ids[0], b"a1")             # short ids are untouched
        self.assertLessEqual(len(ids[1]), 39)
        link.sent = b""
        s.handle(b"OPEN\tARTIFACT\t" + ids[1])
        self.assertIn([b"T", b"report"], link.lines())
        self.assertNotIn([b"M", b"E"], link.lines())


class SavingArtifacts(unittest.TestCase):
    def test_save_and_fetch(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.handle(b"SAVE\tARTIFACT\ta1")
        self.assertEqual(link.lines(), [[b"F", b"FALCON_M.S"]])
        link.sent = b""
        s.handle(b"FETCH\tARTIFACT\ta1")
        lines = link.lines()
        data = bytes.fromhex("".join(l[1].decode() for l in lines if l[0] == b"D"))
        self.assertTrue(data.startswith(b"; mix two channels\r\n    move.l"))
        self.assertIn([b"G", str(len(data)).encode()], lines)
        for l in lines:
            self.assertLess(len(b"\t".join(l)), 200)

    def test_empty_artifact_list_says_so(self):
        class NoArtifacts(DemoBackend):
            last_scan = 40

            def list_artifacts(self):
                return []
        link = FakeLink()
        Session(link, NoArtifacts()).handle(b"LIST\tARTIFACTS")
        self.assertIn(b"No artifacts found in your 40 most recent chats.", link.sent)


class ApiBackendActions(unittest.TestCase):
    """Local storage side of the API backend (no network needed)."""

    def test_rename_pin_move_delete(self):
        import tempfile
        from backends import ApiBackend
        be = ApiBackend.__new__(ApiBackend)
        be.dir = tempfile.mkdtemp()
        os.makedirs(os.path.join(be.dir, "chats"))
        be._save_projects([{"id": "retro", "name": "Retro"}])
        be._save({"id": "a" * 32, "title": "First", "project": None, "messages": []})
        be.rename("CHAT", "a" * 32, "Renamed")
        be.set_pinned("CHAT", "a" * 32, True)
        self.assertEqual(be.list_chats(), [("a" * 32, "Renamed", True)])
        be.move_chat("a" * 32, "retro")
        self.assertEqual(be.project_chats("retro")[1], [("a" * 32, "Renamed", True)])
        be.archive_project("retro")
        self.assertEqual(be.list_projects(), [])
        be.delete("PROJECT", "retro")
        self.assertIsNone(be._load("a" * 32)["project"])
        be.delete("CHAT", "a" * 32)
        self.assertEqual(be.list_chats(), [])


class Gateway(unittest.TestCase):
    def test_backend_failure_reaches_the_atari_and_retries(self):
        from backends import LazyBackend
        calls = []

        def factory():
            calls.append(1)
            if len(calls) == 1:
                raise RuntimeError("claude.ai refused the session (HTTP 403).")
            return DemoBackend()

        link = FakeLink()
        s = Session(link, LazyBackend(factory, "claude.ai"))
        s.handle(b"HELLO\t1\t1.0")
        self.assertIn([b"M", b"E"], link.lines())
        self.assertIn(b"refused the session", link.sent)
        link.sent = b""
        s.handle(b"HELLO\t1\t1.0")  # e.g. after the key was fixed
        self.assertIn([b"L", b"CHATS", b"Recents"], link.lines())

    def test_write_failure_ends_the_session(self):
        class Dead(FakeLink):
            def write(self, data):
                raise EOFError

        s = Session(Dead(), DemoBackend())
        with self.assertRaises(EOFError):
            s.handle(b"HELLO\t1\t1.0")


class TcpGateway(unittest.TestCase):
    """The --tcp link as Claude ST uses it over STinG."""

    def setUp(self):
        import threading
        from claude_bridge import TcpLink
        self.link = TcpLink("127.0.0.1:0", allow=["127.0.0.1"])
        self.port = self.link.srv.getsockname()[1]
        self.session = Session(self.link, DemoBackend())
        threading.Thread(target=self.session.run, daemon=True).start()

    def connect(self):
        import socket
        c = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        c.settimeout(5)
        return c

    def read_until(self, c, marker):
        data = b""
        while marker not in data:
            chunk = c.recv(4096)
            if not chunk:
                break
            data += chunk
        return data

    def test_hello_over_tcp(self):
        c = self.connect()
        c.sendall(b"HELLO\t1\t1.0\n")
        self.assertIn(b"L\tCHATS\tRecents", self.read_until(c, b"S\tOnline"))
        c.close()

    def test_reconnect_replaces_stale_connection(self):
        old = self.connect()
        old.sendall(b"HELLO\t1\t1.0\n")
        self.read_until(old, b"S\tOnline")
        new = self.connect()  # e.g. the Atari rebooted; old socket never closed
        new.sendall(b"HELLO\t1\t1.0\n")
        self.assertIn(b"S\tOnline", self.read_until(new, b"S\tOnline"))
        old.close()
        new.close()

    def test_allow_list(self):
        from claude_bridge import TcpLink
        link = TcpLink("127.0.0.1:0", allow=["192.168.1.20"])
        import socket
        import threading
        t = threading.Thread(target=link._accept, daemon=True)
        t.start()
        c = socket.create_connection(("127.0.0.1", link.srv.getsockname()[1]), timeout=5)
        t.join(5)
        self.assertIsNone(link.conn)
        c.settimeout(5)
        self.assertEqual(c.recv(10), b"")  # closed on us
        c.close()


class ClaudeAiParsing(unittest.TestCase):
    """The claude.ai backend can't be exercised offline, but its parsing can."""

    CONV = {
        "uuid": "c0ffee00-0000-0000-0000-000000000000",
        "name": "Snake game",
        "current_leaf_message_uuid": "m3",
        "chat_messages": [
            {"uuid": "m1", "sender": "human", "parent_message_uuid": "root",
             "content": [{"type": "text", "text": "make snake"}]},
            {"uuid": "m2", "sender": "assistant", "parent_message_uuid": "m1",
             "content": [{"type": "text", "text": "Here you go."},
                         {"type": "tool_use", "name": "artifacts",
                          "input": {"id": "snake", "command": "create", "title": "Snake",
                                    "content": "let x = 1;"}}]},
            {"uuid": "m2b", "sender": "assistant", "parent_message_uuid": "m1",
             "content": [{"type": "text", "text": "an abandoned branch"}]},
            {"uuid": "m3", "sender": "human", "parent_message_uuid": "m2",
             "content": [{"type": "text", "text": "thanks"}]},
        ],
    }

    def test_branch_and_artifacts(self):
        from backends import ClaudeAiBackend, _text_of
        branch = ClaudeAiBackend._current_branch(self.CONV)
        self.assertEqual([m["uuid"] for m in branch], ["m1", "m2", "m3"])
        self.assertIn("[Artifact: Snake", _text_of(branch[1]))

        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be._artifacts = {}
        be._harvest_artifacts(self.CONV)
        conv = dict(self.CONV, chat_messages=[{"content": [
            {"type": "tool_use", "name": "artifacts",
             "input": {"id": "snake", "command": "update", "old_str": "1", "new_str": "2"}}]}])
        be._harvest_artifacts(conv)
        (key, art), = be._artifacts.items()
        self.assertEqual(art["title"], "Snake")
        self.assertEqual(art["content"], "let x = 2;")


if __name__ == "__main__":
    unittest.main()
