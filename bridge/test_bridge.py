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
        self.assertEqual([c[:3] for c in be.list_chats()], [(ClaudeAiParsing.CONV["uuid"], "Snake game", True)])
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


class ScriptMadeFiles(unittest.TestCase):
    """Files Claude made with a script (a .docx cover letter): found through
    present_files or a computer:// link, fetched from the chat's file store."""

    @staticmethod
    def docx(*paras):
        import io
        import zipfile
        body = "".join('<w:p><w:r><w:t xml:space="preserve">%s</w:t></w:r></w:p>' % p for p in paras)
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("word/document.xml", "<w:document><w:body>%s</w:body></w:document>" % body)
        return buf.getvalue()

    def backend(self, convs):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org, be.artifact_scan, be._artifacts, be._scanned = "org", 100, {}, {}
        listing = [{"uuid": c["uuid"], "name": c["name"], "updated_at": c["updated_at"]}
                   for c in convs]
        by_id = {c["uuid"]: c for c in convs}
        be.offsets = []

        def get(path, **params):
            if path.endswith("/chat_conversations"):
                be.offsets.append(params.get("offset", 0))
                o = params.get("offset", 0)
                return listing[o:o + params["limit"]]
            return by_id[path.rsplit("/", 1)[1]]
        be._get = get
        letter = self.docx("Dear hiring manager,", "Erez &amp; co")

        class Resp:
            status_code, content = 200, letter
        be.http = type("H", (), {"get": lambda self, url, timeout=0: Resp()})()
        return be

    def conv(self, uid, name, when, content):
        return {"uuid": uid, "name": name, "updated_at": when, "chat_messages": [
            {"index": 0, "sender": "assistant", "created_at": when, "content": content}]}

    def test_cover_letters_found_newest_first(self):
        convs = [
            self.conv("aaaaaaaa-1", "Old snake", "2026-01-01", [
                {"type": "tool_use", "name": "artifacts",
                 "input": {"id": "s", "title": "Snake", "content": "x"}}]),
            self.conv("bbbbbbbb-2", "Job hunt", "2026-09-01", [
                {"type": "tool_use", "name": "present_files",
                 "input": {"filepaths": ["/mnt/user-data/outputs/Cover_Letter_Acme.docx"]}}]),
            self.conv("cccccccc-3", "Job hunt 2", "2026-09-20", [
                {"type": "text", "text": "[View your letter](computer:///mnt/user-data/outputs/"
                                         "Cover%20Letter%20Globex.docx)"}]),
        ]
        be = self.backend(convs)
        arts = be.list_artifacts()
        self.assertEqual([a[1] for a in arts],
                         ["Cover Letter Globex.docx", "Cover_Letter_Acme.docx", "Snake"])
        title, body = be.get_artifact(arts[0][0])
        self.assertIn("Dear hiring manager,\nErez & co", body)
        name, data = be.artifact_file(arts[1][0])
        self.assertEqual(name, "COVER_LE.DOC")
        self.assertTrue(data.startswith(b"PK"))     # saved as the real .docx

    def test_scan_goes_past_the_first_page(self):
        convs = [self.conv("%08d-x" % i, "c%d" % i, "2026-01-01", []) for i in range(120)]
        be = self.backend(convs)
        be.list_artifacts()
        self.assertEqual(be.offsets, [0, 50])       # 50 + 50 = the 100 newest chats
        self.assertEqual(be.last_scan, 100)

    def test_binary_files_reach_the_atari_unchanged(self):
        class Binary(DemoBackend):
            def artifact_file(self, aid):
                return "LETTER.DOC", b"PK\x03\x04\n\xff"
        link = FakeLink()
        Session(link, Binary()).handle(b"FETCH\tARTIFACT\ta1")
        data = bytes.fromhex("".join(l[1].decode() for l in link.lines() if l[0] == b"D"))
        self.assertEqual(data, b"PK\x03\x04\n\xff")


class ModelChoice(unittest.TestCase):
    def test_hello_offers_models_and_effort(self):
        link = FakeLink()
        Session(link, DemoBackend()).handle(b"HELLO\t1\t1.1")
        lines = link.lines()
        self.assertIn([b"V", b"claude-opus-5-5", b"Opus 5.5"], lines)
        self.assertIn([b"U", b"xhigh", b"Extra high"], lines)
        self.assertIn([b"K", b"claude-opus-5-5", b"medium"], lines)
        self.assertLess(lines.index([b"O"]), lines.index([b"K", b"claude-opus-5-5", b"medium"]))

    def test_choose_model_and_effort(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.handle(b"CHOOSE\tclaude-sonnet-5-5\tmax")
        self.assertIn([b"K", b"claude-sonnet-5-5", b"max"], link.lines())
        link.sent = b""
        s.handle(b"CHOOSE\tclaude-haiku-4-5\tmax")      # Haiku takes no effort
        lines = link.lines()
        self.assertIn([b"K", b"claude-haiku-4-5", b""], lines)
        self.assertFalse([l for l in lines if l[0] == b"U"])
        link.sent = b""
        s.handle(b"CHOOSE\tno-such-model\tlow")          # unknown: keeps Haiku
        self.assertIn([b"K", b"claude-haiku-4-5", b""], link.lines())
        link.sent = b""
        s.handle(b"CHOOSE\tclaude-opus-5-5\tbogus")      # bad effort: the default
        self.assertIn([b"K", b"claude-opus-5-5", b"medium"], link.lines())
        link.sent = b""
        s.handle(b"SEND\thi")
        reply = b"".join(l[1] for l in link.lines() if l[0] == b"P")
        self.assertIn(b"picked Opus 5.5, effort medium", reply)

    def test_api_request_carries_model_and_effort(self):
        from backends import ApiBackend
        be = ApiBackend.__new__(ApiBackend)
        seen = {}

        class Stream:
            text_stream = ["ok"]

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

            def get_final_message(self):
                return type("M", (), {"stop_reason": "end_turn"})()

        def stream(**kw):
            seen.update(kw)
            return Stream()
        be.client = type("C", (), {})()
        be.client.beta = type("B", (), {})()
        be.client.beta.messages = type("Ms", (), {"stream": staticmethod(stream)})()
        be._projects = lambda: []
        be._save = lambda chat: None
        be.model, be.effort = "claude-opus-5-5", "medium"
        be.choose("claude-fable-5-1", "xhigh")
        be.send(None, None, "hi", lambda d: None)
        self.assertEqual(seen["model"], "claude-fable-5-1")
        self.assertEqual(seen["output_config"], {"effort": "xhigh"})
        self.assertEqual(seen["fallbacks"], "default")
        be.choose("claude-haiku-4-5")
        seen.clear()
        be.send(None, None, "hi", lambda d: None)
        self.assertNotIn("output_config", seen)
        self.assertNotIn("fallbacks", seen)

    def test_claude_ai_defaults_to_the_account_model(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        self.assertEqual(be.models()[0], ("default", "Default model"))
        self.assertEqual(be.choose("default", "high"), ("default", ""))
        self.assertEqual(be.choose("claude-opus-5-5", "high"), ("claude-opus-5-5", "high"))


class PerChatModel(unittest.TestCase):
    def test_model_labels(self):
        from backends import model_label
        self.assertEqual(model_label("claude-opus-5-5"), "Opus 5.5")
        self.assertEqual(model_label("claude-sonnet-4-20250514"), "Sonnet 4")
        self.assertEqual(model_label("claude-3-5-sonnet-20241022"), "3.5 Sonnet")
        self.assertEqual(model_label("claude-opus-4-1-20250805"), "Opus 4.1")

    def test_each_chat_shows_its_own_model(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.handle(b"HELLO\t1\t1.3")
        s.handle(b"CHOOSE\tclaude-fable-5-1\thigh")     # the choice for new chats
        link.sent = b""
        s.handle(b"OPEN\tCHAT\td2")                     # an older Sonnet 4 chat
        lines = link.lines()
        self.assertIn([b"V", b"claude-sonnet-4-20250514", b"Sonnet 4"], lines)
        self.assertIn([b"K", b"claude-sonnet-4-20250514", b""], lines)
        self.assertFalse([l for l in lines if l[0] == b"U"])      # no effort for it
        self.assertLess(lines.index([b"K", b"claude-sonnet-4-20250514", b""]),
                        lines.index([b"R"]))
        link.sent = b""
        s.handle(b"OPEN\tCHAT\td1")                     # no model recorded
        self.assertIn([b"K", b"claude-fable-5-1", b"high"], link.lines())
        s.handle(b"CHOOSE\tclaude-sonnet-5-5\tlow")     # changes this chat...
        s.handle(b"OPEN\tCHAT\td2")
        link.sent = b""
        s.handle(b"NEW")                                 # ...and new chats
        self.assertIn([b"K", b"claude-sonnet-5-5", b"low"], link.lines())
        s.handle(b"CHOOSE\tclaude-opus-5-5\tmax")
        link.sent = b""
        s.handle(b"OPEN\tCHAT\td1")                     # d1 kept its own choice
        self.assertIn([b"K", b"claude-sonnet-5-5", b"low"], link.lines())

    def test_api_chats_keep_their_model(self):
        import tempfile
        from backends import ApiBackend
        be = ApiBackend.__new__(ApiBackend)
        be.dir = tempfile.mkdtemp()
        os.makedirs(os.path.join(be.dir, "chats"))
        be.model, be.effort = "claude-opus-5-5", "medium"
        chat = {"id": "a" * 32, "title": "t", "messages": []}
        be._save(chat)
        be.get_chat(chat["id"])
        self.assertIsNone(be.chat_choice)
        be.set_chat_model(chat["id"], "claude-haiku-4-5", "")
        be.get_chat(chat["id"])
        self.assertEqual(be.chat_choice, ("claude-haiku-4-5", ""))

    def test_claude_ai_reads_the_chat_model(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be._artifacts = {}
        be._conversation = lambda cid: dict(ClaudeAiParsing.CONV, model="claude-opus-4-8")
        be.get_chat("x")
        self.assertEqual(be.chat_choice, ("claude-opus-4-8", ""))


class StatusLine(unittest.TestCase):
    def test_status_line_only_shows_the_connection(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        for cmd in (b"HELLO\t1\t1.5", b"LIST\tARTIFACTS", b"LIST\tPROJECTS",
                    b"OPEN\tCHAT\td1", b"CHOOSE\tclaude-sonnet-5-5\thigh", b"ACCOUNT",
                    b"SEND\thi", b"RENAME\tCHAT\td1\tNew name", b"FIND\tgfa"):
            s.handle(cmd)
        lines = link.lines()
        self.assertTrue(all(l[1].startswith(b"Online") for l in lines if l[0] == b"S"))
        notices = [l[1] for l in lines if l[0] == b"N"]
        self.assertIn(b"Loading chat...", notices)
        self.assertFalse([n for n in notices if b"Sonnet" in n])   # not for a model change
        # each command ends with its notice cleared, except a result like "Renamed"
        self.assertEqual(lines[-2:], [[b"N", b""], [b"S", b"Online: demo"]])


class EffortFromClaudeAi(unittest.TestCase):
    """Each chat and Code session shows the effort claude.ai has for it."""

    def test_models_that_take_effort(self):
        from backends import efforts_for, model_label
        for m in ("claude-sonnet-5", "claude-opus-4-7", "claude-opus-5-5[1m]",
                  "claude-sonnet-5-20260101", "claude-opus-5"):
            self.assertEqual(len(efforts_for(m)), 5, m)
        self.assertEqual(efforts_for("claude-haiku-4-5"), [])
        self.assertEqual(model_label("claude-opus-5-5[1m]"), "Opus 5.5")
        self.assertEqual(model_label("claude-sonnet-5"), "Sonnet 5")

    def test_code_session_effort_is_read(self):
        be, _ = CodeSessions().backend()
        orig = be.http.get

        def get(url, **kw):
            r = orig(url, **kw)
            if url.endswith("/sessions/session_b"):
                r._d = {"title": "New", "session_context": {"model": "claude-opus-5-5[1m]",
                                                            "effort": "high"}}
            return r
        be.http.get = get
        link = FakeLink()
        s = Session(link, be)
        s.handle(b"OPEN\tCODE\tsession_b")
        self.assertIn([b"K", b"claude-opus-5-5", b"high"], link.lines())    # not Med

    def test_sonnet_5_session_offers_effort(self):
        be, _ = CodeSessions().backend()
        orig = be.http.get

        def get(url, **kw):
            r = orig(url, **kw)
            if url.endswith("/sessions/session_b"):
                r._d = {"title": "New", "session_context": {"model": "claude-sonnet-5"}}
            return r
        be.http.get = get
        link = FakeLink()
        Session(link, be).handle(b"OPEN\tCODE\tsession_b")
        lines = link.lines()
        self.assertIn([b"V", b"claude-sonnet-5", b"Sonnet 5"], lines)
        self.assertIn([b"U", b"max", b"Max"], lines)
        self.assertIn([b"K", b"claude-sonnet-5", b"high"], lines)

    def test_chat_effort_is_read_and_not_carried_over(self):
        from backends import ChoiceMemory, ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be._artifacts, be.memory = {}, ChoiceMemory()
        convs = {"c1": dict(ClaudeAiParsing.CONV, model="claude-opus-5-5",
                            settings={"effort": "high"}),
                 "c2": dict(ClaudeAiParsing.CONV, model="claude-opus-5-5")}
        be._conversation = lambda cid: convs[cid]
        be.list_chats = lambda limit=100: []
        link = FakeLink()
        s = Session(link, be)
        s.handle(b"OPEN\tCHAT\tc1")
        self.assertIn([b"K", b"claude-opus-5-5", b"high"], link.lines())
        link.sent = b""
        s.handle(b"OPEN\tCHAT\tc2")                # nothing recorded: Opus 5.5's default
        self.assertIn([b"K", b"claude-opus-5-5", b"medium"], link.lines())


class CompletionConflict(unittest.TestCase):
    """HTTP 409 from claude.ai's completion: reload the chat and retry."""

    def backend(self, codes):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org, be.model, be.effort = "o1", "claude-opus-5-5", "high"
        leaves = iter(["m1", "m2", "m3", "m4", "m5"])
        be._conversation = lambda cid: {"current_leaf_message_uuid": next(leaves), "name": "Chat"}
        sent = []

        class R:
            def __init__(self, code):
                self.status_code, self.text = code, ""     # streamed: nothing read yet

            def iter_content(self):
                return iter([b'{"error": ', b'"conflict"}'] if self.status_code == 409 else [])

            def iter_lines(self):
                return iter([b'data: {"type": "completion", "completion": "Hi"}'])

        def post(path, body, **kw):
            sent.append(dict(body))
            return R(codes.pop(0))
        be._post = post
        return be, sent

    def test_retries_with_the_latest_message(self):
        import backends
        be, sent = self.backend([409, 200, 200])
        orig, backends.time.sleep = backends.time.sleep, lambda s: None
        try:
            got = []
            be.send("c1", None, "hello", got.append)
        finally:
            backends.time.sleep = orig
        self.assertEqual(got, ["Hi"])
        self.assertEqual([b["parent_message_uuid"] for b in sent[:2]], ["m1", "m2"])

    def test_upgraded_chat_is_not_retried(self):
        import backends
        be, sent = self.backend([409, 409, 409, 409])
        upgraded = ('{"type":"error","error":{"type":"invalid_request_error","message":"This chat '
                    'is available in the new Claude experience.","details":{"error_code":'
                    '"conversation_upgraded"}}}')
        backends._error_text, orig = (lambda r, limit=400: upgraded), backends._error_text
        try:
            with self.assertRaises(RuntimeError) as cm:
                be.send("c1", None, "hello", lambda d: None)
        finally:
            backends._error_text = orig
        self.assertIn("new chat system", str(cm.exception))
        self.assertEqual(len(sent), 1)               # no retries, no rate limit

    def test_gives_up_with_the_reason(self):
        import backends
        be, sent = self.backend([409, 409, 409, 409])
        orig, backends.time.sleep = backends.time.sleep, lambda s: None
        try:
            with self.assertRaises(RuntimeError) as cm:
                be.send("c1", None, "hello", lambda d: None)
        finally:
            backends.time.sleep = orig
        self.assertIn("HTTP 409: {\"error\": \"conflict\"}", str(cm.exception))
        self.assertNotIn("model", sent[-1])          # the last try without model/effort


class MovedChats(unittest.TestCase):
    """Chats claude.ai moved to its new system (workspace_upgraded)."""

    def backend(self):
        from backends import ChoiceMemory, ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org, be._artifacts, be.memory = "o1", {}, ChoiceMemory()
        self.convs = {
            "old": dict(ClaudeAiParsing.CONV, uuid="old", model="claude-opus-5-5", workspace_upgraded=True,
                        settings={"effort_level": "medium", "enabled_web_search": True}),
            "fine": dict(ClaudeAiParsing.CONV, uuid="fine", model="claude-opus-5-5")}
        be._conversation = lambda cid: self.convs[cid]
        be.list_chats = lambda limit=100: []
        self.sent = []
        be._send = lambda method, path, body=None: self.sent.append((method, path, body))
        be.send = lambda *a: (_ for _ in ()).throw(AssertionError("must not send"))
        return be

    def test_moved_chat_is_read_only(self):
        link = FakeLink()
        s = Session(link, self.backend())
        s.handle(b"OPEN\tCHAT\told")
        self.assertIn(b"moved this chat", link.sent)
        self.assertIn([b"K", b"claude-opus-5-5", b"medium"], link.lines())   # effort_level
        link.sent = b""
        s.handle(b"SEND\thello")                   # nothing goes to claude.ai
        self.assertIn(b"moved this chat", link.sent)
        self.assertNotIn([b"M", b"E"], link.lines())
        s.handle(b"OPEN\tCHAT\tfine")
        self.assertFalse(s.readonly)

    def test_moved_chat_continues_in_its_session(self):
        be = self.backend()
        # the single-chat reply has no session id; the v2 chat list has
        be._get = lambda path, **kw: {"data": [
            {"uuid": "fine"}, {"uuid": "old", "workspace_session_id": "cse_1"}],
            "has_more": False} if path.endswith("chat_conversations_v2") else {}
        posted = []
        events = [{"id": "e1", "type": "user", "message": {"role": "user", "content": "later"}},
                  {"id": "e2", "type": "assistant",
                   "message": {"role": "assistant", "content": [{"type": "text", "text": "ok"}]}}]
        be._code_events = lambda sid, after=None, cap=2000: (
            [] if after else events) if sid == "cse_1" else []
        be.send_code = lambda sid, text, on_delta, **kw: (posted.append((sid, text)), on_delta("Hi"))[1]
        del be.send
        link = FakeLink()
        s = Session(link, be)
        s.handle(b"OPEN\tCHAT\told")
        self.assertFalse(s.readonly)
        self.assertNotIn(b"moved this chat", link.sent)
        self.assertIn(b"later", link.sent)            # what was said after the move
        s.handle(b"SEND\thello")
        self.assertEqual(posted, [("cse_1", "hello")])
        self.assertIn(b"Hi", link.sent)

    def test_moved_chat_stops_polling_once_answered(self):
        import backends
        be = self.backend()
        del be.send
        polls, clock = [], [0.0]
        reply = [{"id": "e9", "type": "assistant",
                  "message": {"role": "assistant", "content": [{"type": "text", "text": "Hi"}]}}]
        def events(sid, after=None, cap=2000):
            polls.append(after)
            return reply if len(polls) == 2 else []   # one reply, then nothing
        be._code_events = events
        be._code = lambda method, path, body=None, **kw: {"title": "t"}
        orig_sleep, orig_time = backends.time.sleep, backends.time.time
        backends.time.sleep = lambda s: clock.__setitem__(0, clock[0] + s)
        backends.time.time = lambda: clock[0]
        try:
            got = []
            be.send_moved("old", "cse_1", "hello", got.append)
        finally:
            backends.time.sleep, backends.time.time = orig_sleep, orig_time
        self.assertEqual(got, ["Hi"])
        self.assertLess(len(polls), 8)              # not 15 minutes of polling
        self.assertLess(clock[0], 30)

    def test_merge_moved(self):
        from backends import merge_moved
        a, b, c = ("U", "a"), ("A", "b"), ("U", "c")
        self.assertEqual(merge_moved([a, b], []), [a, b])
        self.assertEqual(merge_moved([a, b], [a, b, c]), [a, b, c])   # whole chat
        self.assertEqual(merge_moved([a, b], [c]), [a, b, c])         # only the new part
        self.assertEqual(merge_moved([a, b], [b, c]), [a, b, c])      # overlapping

    def test_effort_is_saved_in_the_chat_settings(self):
        be = self.backend()
        be.set_chat_model("old", "claude-opus-5-5", "high")
        method, path, body = self.sent[-1]
        self.assertEqual((method, path), ("put", "/organizations/o1/chat_conversations/old"))
        self.assertEqual(body["settings"], {"effort_level": "high", "enabled_web_search": True})


class DateHeadings(unittest.TestCase):
    """Chat lists grouped like claude.ai's: Pinned, Today, Yesterday, the
    rest of the week by date, then Older."""

    def test_headings(self):
        from datetime import datetime, timedelta, timezone
        from backends import date_heading
        now = datetime(2026, 10, 7, 15, 0, tzinfo=timezone.utc)
        local = now.astimezone()

        def ago(days):
            return (local - timedelta(days=days)).replace(hour=12).isoformat()
        self.assertEqual(date_heading(ago(0), now), "Today")
        self.assertEqual(date_heading(ago(1), now), "Yesterday")
        d = (local - timedelta(days=6))
        self.assertEqual(date_heading(ago(6), now), "%s %d" % (d.strftime("%b"), d.day))
        self.assertEqual(date_heading(ago(7), now), "Older")
        self.assertEqual(date_heading(ago(400), now), "Older")
        self.assertIsNone(date_heading(None, now))
        self.assertIsNone(date_heading("not a date", now))

    def test_list_lines(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.be.chats["d3"]["pinned"] = True
        s.handle(b"LIST\tCHATS")
        rows = [l for l in link.lines() if l[0] in (b"X", b"I")]
        kinds = [(l[0], l[1]) for l in rows]
        self.assertEqual(kinds[:2], [(b"X", b"Pinned"), (b"I", b"d3")])
        self.assertEqual(kinds[2:4], [(b"X", b"Today"), (b"I", b"d1")])
        self.assertEqual(kinds[4:6], [(b"X", b"Yesterday"), (b"I", b"d2")])
        self.assertEqual(kinds[6:], [(b"X", b"Older"), (b"I", b"d4")])

    def test_projects_list_has_no_headings(self):
        link = FakeLink()
        Session(link, DemoBackend()).handle(b"LIST\tPROJECTS")
        self.assertFalse([l for l in link.lines() if l[0] == b"X"])


class NewProject(unittest.TestCase):
    def test_new_project_from_the_move_menu(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.handle(b"NEWPROJ\td2\tGFA tips")
        pid = [k for k, v in s.be.projects.items() if v["name"] == "GFA tips"]
        self.assertEqual(len(pid), 1)
        self.assertEqual(s.be.chats["d2"]["project"], pid[0])
        self.assertNotIn([b"M", b"E"], link.lines())
        self.assertIn(b"Moved to GFA tips", link.sent)

    def test_claude_ai_creates_the_project(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org = "o1"
        sent = []

        class R:
            status_code = 200

            def json(self):
                return {"uuid": "proj-123", "name": "GFA tips"}

        be._send = lambda method, path, body=None: sent.append((method, path, body)) or R()
        self.assertEqual(be.create_project("GFA tips"), "proj-123")
        self.assertEqual(sent[0][:2], ("post", "/organizations/o1/projects"))
        self.assertEqual(sent[0][2]["name"], "GFA tips")

    def test_api_backend_creates_the_project(self):
        import tempfile
        from backends import ApiBackend
        be = ApiBackend.__new__(ApiBackend)
        be.dir = tempfile.mkdtemp()
        os.makedirs(os.path.join(be.dir, "chats"))
        pid = be.create_project("Retro")
        self.assertEqual(be.list_projects(), [(pid, "Retro", False)])


class LongRunningBridge(unittest.TestCase):
    """A bridge that runs for months: nothing stale, nothing piling up."""

    def test_artifacts_of_deleted_chats_are_dropped(self):
        t = ScriptMadeFiles()
        convs = [t.conv("aaaaaaaa-1", "Old snake", "2026-01-01", [
                     {"type": "tool_use", "name": "artifacts",
                      "input": {"id": "s", "title": "Snake", "content": "x"}}]),
                 t.conv("bbbbbbbb-2", "Job hunt", "2026-09-01", [
                     {"type": "tool_use", "name": "present_files",
                      "input": {"filepaths": ["/mnt/user-data/outputs/Letter.docx"]}}])]
        be = t.backend(convs)
        self.assertEqual(len(be.list_artifacts()), 2)
        be2 = t.backend(convs[1:])              # "Old snake" deleted on claude.ai
        be2._artifacts, be2._scanned = be._artifacts, be._scanned
        self.assertEqual([a[1] for a in be2.list_artifacts()], ["Letter.docx"])
        self.assertEqual(list(be2._scanned), ["bbbbbbbb-2"])

    def test_downloaded_files_are_not_kept(self):
        t = ScriptMadeFiles()
        be = t.backend([t.conv("bbbbbbbb-2", "Job hunt", "2026-09-01", [
            {"type": "tool_use", "name": "present_files",
             "input": {"filepaths": ["/mnt/user-data/outputs/Letter.docx"]}}])])
        downloads = []
        get = be.http.get
        be.http = type("H", (), {"get": lambda self, url, timeout=0: downloads.append(url) or get(url)})()
        (aid, _), = be.list_artifacts()
        be.get_artifact(aid)                    # open: shown, then let go
        self.assertNotIn("data", be._artifacts[aid])
        link = FakeLink()
        s = Session(link, be)
        s.handle(b"SAVE\tARTIFACT\t" + s.short_id(aid).encode())
        self.assertEqual(len(downloads), 1)     # the name needs no download
        s.handle(b"FETCH\tARTIFACT\t" + s.short_id(aid).encode())
        self.assertEqual(len(downloads), 2)
        self.assertNotIn("data", be._artifacts[aid])
        self.assertIn([b"G", str(len(t.docx("Dear hiring manager,", "Erez &amp; co"))).encode()],
                      link.lines())

    def test_effort_per_chat_survives_a_restart(self):
        import tempfile
        from backends import ChoiceMemory, ClaudeAiBackend
        path = os.path.join(tempfile.mkdtemp(), "chat-effort.json")

        def backend():
            be = ClaudeAiBackend.__new__(ClaudeAiBackend)
            be._artifacts, be.memory = {}, ChoiceMemory(path)
            be._send = lambda *a, **k: None
            be._conversation = lambda cid: dict(ClaudeAiParsing.CONV, model="claude-opus-5-5")
            return be
        be = backend()
        be.set_chat_model("c1", "claude-opus-5-5", "max")
        be = backend()                          # the bridge restarted
        be.get_chat("c1")
        self.assertEqual(be.chat_choice, ("claude-opus-5-5", "max"))
        be.get_chat("c2")                       # nothing remembered: current effort
        self.assertEqual(be.chat_choice, ("claude-opus-5-5", ""))

    def test_effort_memory_is_bounded(self):
        from backends import ChoiceMemory
        m = ChoiceMemory()
        m.LIMIT = 3
        for i in range(5):
            m.set("c%d" % i, "high")
        self.assertEqual(list(m.efforts), ["c2", "c3", "c4"])


class CodeSessions(unittest.TestCase):
    EVENTS = [
        {"id": "e1", "data": {"type": "system", "subtype": "init"}},
        {"id": "e2", "data": {"type": "user", "message": {"role": "user", "content": "fix the build"}}},
        {"id": "e3", "data": {"type": "assistant", "message": {"content": [
            {"type": "text", "text": "Looking."},
            {"type": "tool_use", "name": "Bash", "input": {"command": "make  -C st"}}]}}},
        {"id": "e4", "data": {"type": "user", "message": {"content": [
            {"type": "tool_result", "content": "ok"}]}}},
        {"id": "e5", "data": {"type": "assistant", "message": {"content": [
            {"type": "text", "text": "It builds now."}]}}},
        {"id": "e6", "data": {"type": "result", "subtype": "success"}},
    ]

    def test_events_become_messages(self):
        from backends import code_messages
        self.assertEqual(code_messages(self.EVENTS), [
            ("U", "fix the build"),
            ("A", "Looking.\n\n[Bash: make -C st]\n\nIt builds now.")])

    def backend(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org = "o1"
        calls = []
        sessions = [{"id": "session_a", "title": "Old", "updated_at": "2026-09-01"},
                    {"id": "session_b", "title": "New", "updated_at": "2026-10-01"},
                    {"id": "session_c", "title": "Gone", "session_status": "archived"}]
        events = list(self.EVENTS[:2])

        class R:
            def __init__(self, code, data):
                self.status_code, self._d, self.content, self.text = code, data, b"x", ""

            def json(self):
                return self._d

        def call(method):
            def f(url, **kw):
                calls.append((method, url, kw))
                if not url.startswith("https://claude.ai/v1"):
                    return R(404, {})
                path = url[len("https://claude.ai/v1"):]
                if path == "/sessions":
                    return R(200, {"data": sessions})
                if path.endswith("/events") and method == "post":
                    events.extend(self.EVENTS[2:])      # Claude answers
                    return R(200, {})
                if path.endswith("/events"):
                    after = (kw.get("params") or {}).get("after_id")
                    ids = [e["id"] for e in events]
                    rest = events[ids.index(after) + 1:] if after else events
                    return R(200, {"data": rest, "has_more": False})
                return R(200, {"id": path.split("/")[-1], "title": "New",
                               "session_context": {"model": "claude-opus-5-5[1m]"}})
            return f
        be.http = type("H", (), {m: staticmethod(call(m)) for m in ("get", "post", "patch")})()
        return be, calls

    def test_list_open_and_send(self):
        be, calls = self.backend()
        self.assertEqual([s[:2] for s in be.list_code_sessions()], [("session_b", "New"), ("session_a", "Old")])
        self.assertEqual(calls[0][2]["headers"]["anthropic-beta"], "ccr-byoc-2025-07-29")
        title, msgs = be.get_code_session("session_b")
        self.assertEqual((title, msgs), ("New", [("U", "fix the build")]))
        self.assertEqual(be.chat_choice, ("claude-opus-5-5", ""))
        got = []
        be.send_code("session_b", "and the tests?", got.append, poll=0)
        self.assertEqual(got, ["Looking.\n\n[Bash: make -C st]\n\nIt builds now."])
        sent = [c for c in calls if c[0] == "post"][0][2]["json"]["events"][0]
        self.assertEqual(sent["message"], {"role": "user", "content": "and the tests?"})

    def test_code_over_the_protocol(self):
        link = FakeLink()
        s = Session(link, DemoBackend())
        s.handle(b"LIST\tCODE")
        lines = link.lines()
        self.assertIn([b"L", b"CODE", b"Code"], lines)
        self.assertIn([b"I", b"session_d1", b"Port the bridge to MicroPython"], lines)
        link.sent = b""
        s.handle(b"HELLO\t1\t1.8")                     # new chats: Opus 5.5, medium
        s.handle(b"OPEN\tCODE\tsession_d1")             # runs on Sonnet 5.5
        self.assertIn([b"K", b"claude-sonnet-5-5", b"high"], link.lines())   # its default
        s.handle(b"CHOOSE\tclaude-opus-5-5\tmax")         # switch the session
        self.assertEqual(s.be.code["session_d1"]["model"], "claude-opus-5-5")
        link.sent = b""
        s.handle(b"OPEN\tCODE\tsession_d2")              # no model: the new-chat choice
        self.assertIn([b"K", b"claude-opus-5-5", b"medium"], link.lines())
        self.assertIn([b"T", b"Fix the 68000 store-merging crash"], link.lines())
        link.sent = b""
        s.handle(b"SEND\tthanks")
        self.assertIn(b"no session ran", link.sent)
        self.assertNotIn(b"L\tCHATS", link.sent)     # not a new chat
        s.handle(b"RENAME\tCODE\tsession_d2\tStore merging")
        self.assertIn([b"T", b"Store merging"], link.lines())
        link.sent = b""
        s.handle(b"NEW")
        s.handle(b"SEND\thello")                      # back to a normal new chat
        self.assertIn([b"C", b"d5"], link.lines())


class AccountPage(unittest.TestCase):
    def test_usage_bar_and_reset_time(self):
        from datetime import datetime, timezone
        from backends import reset_text, usage_bar
        self.assertEqual(usage_bar(25, 8), "[##------] 25%")
        self.assertEqual(usage_bar(140, 4), "[####] 100%")
        now = datetime(2026, 10, 4, 12, 0, tzinfo=timezone.utc)
        self.assertTrue(reset_text("2026-10-04T14:14:00Z", now).startswith("resets in 2 h 14 min ("))
        self.assertTrue(reset_text("2026-10-07T15:00:00+00:00", now).startswith("resets in 3 d 3 h ("))

    def test_claude_ai_plan_usage_and_account(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org = "o1"

        def get(path, **params):
            if path == "/account":
                return {"full_name": "Erez Yaary", "email_address": "erez@example.com",
                        "created_at": "2023-03-14T10:00:00Z", "memberships": [
                            {"organization": {"uuid": "o0", "name": "Other"}},
                            {"organization": {"uuid": "o1", "name": "Erez's org",
                                              "capabilities": ["chat", "claude_max"],
                                              "rate_limit_tier": "default_claude_max_20x"}}]}
            if path == "/organizations/o1/usage":
                return {"five_hour": {"utilization": 42.0, "resets_at": "2099-01-01T00:00:00Z"},
                        "seven_day": {"utilization": 10, "resets_at": None},
                        "seven_day_opus": None,
                        "iguana_necktie": {"utilization": 3, "resets_at": "2099-01-01T00:00:00Z"},
                        "extra_usage": {"is_enabled": False, "utilization": None}}
            raise AssertionError(path)
        be._get = get
        page = be.account_report()
        self.assertIn("## Plan\nMax (20x usage)", page)
        self.assertIn("Current session (5 hours)\n  [########------------] 42% used\n  resets in ", page)
        self.assertIn("  resets in ", page)
        self.assertIn("used\n\nCloud session credits", page)
        self.assertIn("\n\nThis week, all models\n  [##------------------] 10% used", page)
        self.assertNotIn("Opus", page)
        self.assertNotIn("guana", page)          # shown under its real name
        self.assertIn("Cloud session credits\n  [#-------------------] 3% used\n  expires in ", page)
        self.assertIn("Name: Erez Yaary", page)
        self.assertIn("Email: erez@example.com", page)
        self.assertIn("Organization: Erez's org", page)
        self.assertIn("Member since: 2023-03-14", page)

    def test_used_up_credit_is_hidden(self):
        from backends import ClaudeAiBackend
        be = ClaudeAiBackend.__new__(ClaudeAiBackend)
        be.org = "o1"
        for usage in ({"iguana_necktie": {"utilization": 100, "resets_at": "2099-01-01T00:00:00Z"}},
                      {"iguana_necktie": {"utilization": 50, "resets_at": "2020-01-01T00:00:00Z"}}):
            be._get = lambda path, usage=usage, **p: (
                {"memberships": []} if path == "/account" else usage)
            self.assertNotIn("Cloud session credits", be.account_report())

    def test_plan_names(self):
        from backends import plan_name
        self.assertEqual(plan_name(["chat", "claude_pro"]), "Pro")
        self.assertEqual(plan_name(["chat"]), "Free")
        self.assertEqual(plan_name(["chat", "claude_max"], "default_claude_max_5x"), "Max (5x usage)")
        self.assertEqual(plan_name(["chat", "raven"]), "Team")

    def test_account_over_the_protocol(self):
        link = FakeLink()
        Session(link, DemoBackend()).handle(b"ACCOUNT")
        lines = link.lines()
        self.assertIn([b"T", b"Account"], lines)
        self.assertIn([b"M", b"X"], lines)        # no "Info" label
        self.assertIn([b"H", b"Plan"], lines)
        self.assertIn([b"H", b"Usage"], lines)
        self.assertIn([b"H", b"Account"], lines)
        self.assertNotIn([b"M", b"E"], lines)


class LongIds(unittest.TestCase):
    """Artifact ids can be long file paths; the Atari keeps 39 characters."""

    def test_long_artifact_ids_round_trip(self):
        class LongPaths(DemoBackend):
            PATH = "abcdef12:/mnt/user-data/outputs/a_rather_long_report_name.html"

            def list_artifacts(self, progress=None):
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

            def list_artifacts(self, progress=None):
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
        self.assertEqual([c[:3] for c in be.list_chats()], [("a" * 32, "Renamed", True)])
        be.move_chat("a" * 32, "retro")
        self.assertEqual([c[:3] for c in be.project_chats("retro")[1]], [("a" * 32, "Renamed", True)])
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
