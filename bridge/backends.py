"""Backends the bridge can serve to the Atari.

claudeai  - your claude.ai account (chats, projects, artifacts) through the
            same web endpoints the claude.ai site uses. Unofficial: needs your
            browser's sessionKey cookie and may break if claude.ai changes.
api       - the official Anthropic API; chats and projects are stored locally.
demo      - canned data, no network. For trying the Atari side out.
"""
import json
import logging
import os
import re
import time
import uuid
from urllib.parse import unquote

log = logging.getLogger("claude-st")

Item = tuple  # (id, label)


# The models offered on the Atari, newest first: (model id, short label)
MODELS = [
    ("claude-opus-5-5", "Opus 5.5"),
    ("claude-fable-5-1", "Fable 5.1"),
    ("claude-sonnet-5-5", "Sonnet 5.5"),
    ("claude-haiku-4-5", "Haiku 4.5"),
    ("claude-opus-5", "Opus 5"),
    ("claude-opus-4-8", "Opus 4.8"),
]
EFFORTS = [("low", "Low"), ("medium", "Medium"), ("high", "High"),
           ("xhigh", "Extra high"), ("max", "Max")]
# these take the server-side refusal fallback ("fallbacks": "default")
FALLBACK_MODELS = {"claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5"}


def efforts_for(model):
    """The effort levels a model accepts (none for Haiku 4.5)."""
    if not model or model == "default" or model.startswith("claude-haiku"):
        return []
    if model in ("claude-opus-4-6", "claude-sonnet-4-6"):
        return [e for e in EFFORTS if e[0] != "xhigh"]
    return EFFORTS


def default_effort(model):
    if not efforts_for(model):
        return ""
    return "medium" if model == "claude-opus-5-5" else "high"


class Backend:
    name = "?"
    model = "claude-opus-5-5"
    effort = "medium"

    def models(self):
        """-> [(id, label)] the Atari's model menu offers"""
        return MODELS

    def choose(self, model, effort=""):
        """Pick the model and effort for the next replies. An unknown model
        is ignored; an effort the model doesn't take becomes its default."""
        if model in dict(self.models()):
            self.model = model
        levels = [e[0] for e in efforts_for(self.model)]
        self.effort = effort if effort in levels else default_effort(self.model)
        return self.model, self.effort

    def whoami(self) -> str:
        return self.name

    def list_chats(self, limit=100):
        raise NotImplementedError

    def search(self, query):
        q = query.lower()
        return [c for c in self.list_chats(limit=500) if q in c[1].lower()]

    def list_projects(self):
        return []

    def project_chats(self, project_id):
        return "Project", []

    def get_chat(self, chat_id):
        """-> (title, [(role, text)]) with role 'U' or 'A'"""
        raise NotImplementedError

    def send(self, chat_id, project_id, text, on_delta):
        """Stream a reply. Returns (chat_id, title)."""
        raise NotImplementedError

    def list_artifacts(self, progress=None):
        return []

    def get_artifact(self, artifact_id):
        return "Artifact", ""

    def artifact_file(self, artifact_id):
        """-> (8.3 file name, text) for saving an artifact on the Atari"""
        title, body = self.get_artifact(artifact_id)
        return artifact_filename(title), body

    # sidebar item actions; kind is "CHAT" or "PROJECT"
    def rename(self, kind, item_id, name):
        raise NotImplementedError("Renaming isn't supported by the %s backend" % self.name)

    def set_pinned(self, kind, item_id, pinned):
        raise NotImplementedError("Pinning isn't supported by the %s backend" % self.name)

    def delete(self, kind, item_id):
        raise NotImplementedError("Deleting isn't supported by the %s backend" % self.name)

    def archive_project(self, project_id):
        raise NotImplementedError("Archiving isn't supported by the %s backend" % self.name)

    def move_chat(self, chat_id, project_id):
        raise NotImplementedError("Moving chats isn't supported by the %s backend" % self.name)


# ---------------------------------------------------------------------------
# claude.ai web account (unofficial)
# ---------------------------------------------------------------------------

def _http_session():
    """curl_cffi gets past claude.ai's browser checks far more reliably than
    plain requests, so prefer it when installed."""
    try:
        from curl_cffi import requests as creq
        return creq.Session(impersonate="chrome"), True
    except ImportError:
        import requests
        return requests.Session(), False


def _text_of(msg):
    """Flatten a claude.ai chat message into text, noting artifacts inline."""
    parts = []
    for block in msg.get("content") or []:
        t = block.get("type")
        if t == "text" and block.get("text"):
            parts.append(block["text"])
        elif t == "tool_use" and block.get("name") == "artifacts":
            inp = block.get("input") or {}
            title = inp.get("title") or inp.get("id") or "artifact"
            parts.append("[Artifact: %s - open it from Artifacts]" % title)
    if not parts and msg.get("text"):
        parts.append(msg["text"])
    return "\n\n".join(parts)


def _local_tz():
    """IANA name of the local time zone, as claude.ai expects."""
    if os.environ.get("TZ", "").count("/"):
        return os.environ["TZ"]
    try:
        target = os.path.realpath("/etc/localtime")
        if "zoneinfo/" in target:
            return target.split("zoneinfo/", 1)[1]
    except OSError:
        pass
    return "UTC"


# Artifacts appear in claude.ai conversations in several shapes:
#  - a tool call named "artifacts" (input: id, type, title, command, content)
#  - <antArtifact identifier=".." type=".." title=".."> ... </antArtifact>
#    inside an assistant's text (older chats)
#  - file tools, "create_file" (path, file_text) and "str_replace" (path,
#    old_str, new_str), used when Claude creates files
#  - "present_files" (filepaths): files Claude made some other way, e.g. a
#    .docx cover letter written by a script. Their bytes are fetched from
#    the conversation's file store when opened or saved.
_ANT_ARTIFACT = re.compile(r"<antArtifact\b([^>]*)>(.*?)</antArtifact>", re.S)
_ATTR = re.compile(r'(\w+)="([^"]*)"')
_FILE_LINK = re.compile(r"computer://(/mnt/user-data/outputs/[^)\s\"'>]+)")

_EXT_BY_TYPE = {
    "text/markdown": "MD", "text/html": "HTM", "image/svg+xml": "SVG",
    "application/vnd.ant.react": "JSX", "application/vnd.ant.mermaid": "MMD",
    "text/plain": "TXT",
}
_EXT_BY_LANG = {
    "python": "PY", "javascript": "JS", "typescript": "TS", "c": "C", "cpp": "CPP",
    "c++": "CPP", "java": "JAV", "html": "HTM", "css": "CSS", "json": "JSN",
    "shell": "SH", "bash": "SH", "sh": "SH", "sql": "SQL", "asm": "S", "assembly": "S",
    "basic": "BAS", "gfa": "LST", "markdown": "MD", "yaml": "YML", "xml": "XML",
    "rust": "RS", "go": "GO", "ruby": "RB", "php": "PHP", "pascal": "PAS",
}


_DOCX_PARA = re.compile(r"<w:p[ >].*?</w:p>", re.S)
_DOCX_TEXT = re.compile(r"<w:t(?: [^>]*)?>([^<]*)</w:t>|<w:(?:tab|br)/>")


def docx_text(data):
    """The plain text of a .docx file, one line per paragraph."""
    import html
    import io
    import zipfile
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        xml = z.read("word/document.xml").decode("utf-8", "replace")
    lines = []
    for para in _DOCX_PARA.findall(xml):
        lines.append("".join(m.group(1) if m.group(1) is not None else
                             ("\t" if "tab" in m.group(0) else "\n")
                             for m in _DOCX_TEXT.finditer(para)))
    return html.unescape("\n".join(lines))


_TEXT_EXT = {"txt", "md", "markdown", "html", "htm", "css", "js", "jsx", "ts", "tsx", "py", "c",
             "h", "cpp", "s", "asm", "json", "csv", "xml", "svg", "yaml", "yml", "sh", "sql",
             "bas", "lst", "rs", "go", "rb", "php", "pas", "java", "mmd", "tex", "ini", "inf"}


def artifact_filename(title, kind="", language="", path=""):
    """An 8.3 name for saving on the Atari, e.g. "SNAKE_GA.PY"."""
    stem, ext = "", ""
    if path:
        base = os.path.basename(path)
        stem, _, ext = base.rpartition(".") if "." in base else (base, "", "")
    if not ext:
        ext = _EXT_BY_LANG.get((language or "").lower()) or _EXT_BY_TYPE.get(kind, "TXT")
    stem = stem or title or "ARTIFACT"
    stem = re.sub(r"[^A-Za-z0-9_-]+", "_", stem).strip("_").upper()[:8] or "ARTIFACT"
    ext = re.sub(r"[^A-Za-z0-9]", "", ext).upper()[:3] or "TXT"
    return "%s.%s" % (stem, ext)


class ClaudeAiBackend(Backend):
    name = "claude.ai"
    BASE = "https://claude.ai/api"
    ROOT_PARENT = "00000000-0000-4000-8000-000000000000"

    def __init__(self, session_key, org_id=None, artifact_scan=100):
        self.http, self.impersonating = _http_session()
        self.http.headers.update({
            "Cookie": "sessionKey=" + session_key,
            "Origin": "https://claude.ai",
            "Referer": "https://claude.ai/",
            "Accept": "application/json",
            "anthropic-client-platform": "web_claude_ai",
        })
        if not self.impersonating:
            self.http.headers["User-Agent"] = (
                "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/126.0 Safari/537.36")
        self.artifact_scan = artifact_scan
        self.org = org_id or self._pick_org()
        self._artifacts = {}
        self._scanned = {}      # conversation uuid -> updated_at already scanned

    def _get(self, path, **params):
        r = self.http.get(self.BASE + path, params=params or None, timeout=60)
        if r.status_code in (401, 403):
            raise RuntimeError("claude.ai refused the session (HTTP %d). "
                               "Is CLAUDE_SESSION_KEY current?" % r.status_code)
        r.raise_for_status()
        return r.json()

    def _post(self, path, body, **kw):
        r = self.http.post(self.BASE + path, json=body, timeout=kw.pop("timeout", 60), **kw)
        if r.status_code in (401, 403):
            raise RuntimeError("claude.ai refused the request (HTTP %d)." % r.status_code)
        return r

    def _pick_org(self):
        orgs = self._get("/organizations")
        if not orgs:
            raise RuntimeError("no organizations on this claude.ai account")
        for o in orgs:
            if "chat" in (o.get("capabilities") or []):
                self.org_name = o.get("name", "")
                return o["uuid"]
        self.org_name = orgs[0].get("name", "")
        return orgs[0]["uuid"]

    def whoami(self):
        return "claude.ai"

    # claude.ai picks the account's default model unless the request names one
    model = "default"
    effort = ""

    def models(self):
        return [("default", "Default model")] + MODELS

    def _conversations(self, limit, offset=0):
        return self._get("/organizations/%s/chat_conversations" % self.org,
                         limit=limit, offset=offset)

    def _recent_conversations(self, n, page=50):
        """The n most recently updated chats, fetched a page at a time."""
        out, seen = [], set()
        while len(out) < n:
            want = min(page, n - len(out))
            batch = self._conversations(want, offset=len(out))
            fresh = [c for c in batch or [] if c.get("uuid") not in seen]
            seen.update(c.get("uuid") for c in fresh)
            out += fresh
            if len(fresh) < want:
                break           # the last page (or offset is not supported)
        return out

    def list_chats(self, limit=100):
        return [(c["uuid"], c.get("name") or "Untitled", bool(c.get("is_starred")))
                for c in self._conversations(limit)]

    def list_projects(self):
        projs = self._get("/organizations/%s/projects" % self.org)
        projs = [p for p in projs if not p.get("archived_at") and not p.get("is_archived")]
        return [(p["uuid"], p.get("name") or "Untitled project", bool(p.get("is_starred")))
                for p in projs]

    # The claude.ai web app's own (unofficial) endpoints for these actions.
    def _send(self, method, path, body=None):
        r = getattr(self.http, method)(self.BASE + path, json=body, timeout=60) if body is not None \
            else getattr(self.http, method)(self.BASE + path, timeout=60)
        if r.status_code >= 400:
            raise RuntimeError("claude.ai refused that (HTTP %d)." % r.status_code)
        return r

    def _item_path(self, kind, item_id):
        what = "projects" if kind == "PROJECT" else "chat_conversations"
        return "/organizations/%s/%s/%s" % (self.org, what, item_id)

    def rename(self, kind, item_id, name):
        self._send("put", self._item_path(kind, item_id), {"name": name})

    def set_pinned(self, kind, item_id, pinned):
        self._send("put", self._item_path(kind, item_id), {"is_starred": pinned})

    def delete(self, kind, item_id):
        self._send("delete", self._item_path(kind, item_id))

    def archive_project(self, project_id):
        self._send("put", self._item_path("PROJECT", project_id), {"is_archived": True})

    def move_chat(self, chat_id, project_id):
        try:
            self._send("put", self._item_path("CHAT", chat_id), {"project_uuid": project_id})
        except RuntimeError:
            self._send("post", "/organizations/%s/chat_conversations/move_many" % self.org,
                       {"conversation_uuids": [chat_id], "project_uuid": project_id})

    def project_chats(self, project_id):
        name = "Project"
        for proj in self.list_projects():
            if proj[0] == project_id:
                name = proj[1]
        try:
            convs = self._get("/organizations/%s/projects/%s/conversations" % (self.org, project_id))
        except Exception:
            convs = [c for c in self._conversations(500) if c.get("project_uuid") == project_id]
        return name, [(c["uuid"], c.get("name") or "Untitled", bool(c.get("is_starred")))
                      for c in convs]

    def _conversation(self, chat_id):
        return self._get("/organizations/%s/chat_conversations/%s" % (self.org, chat_id),
                         tree="True", rendering_mode="messages", render_all_tools="true")

    @staticmethod
    def _current_branch(conv):
        msgs = conv.get("chat_messages") or []
        by_id = {m["uuid"]: m for m in msgs}
        leaf = conv.get("current_leaf_message_uuid")
        if leaf not in by_id:
            return sorted(msgs, key=lambda m: m.get("index", 0))
        branch = []
        while leaf in by_id:
            branch.append(by_id[leaf])
            leaf = by_id[leaf].get("parent_message_uuid")
        return branch[::-1]

    def get_chat(self, chat_id):
        conv = self._conversation(chat_id)
        out = []
        for m in self._current_branch(conv):
            role = "U" if m.get("sender") == "human" else "A"
            out.append((role, _text_of(m)))
        self._harvest_artifacts(conv)
        return conv.get("name") or "Untitled", out

    def send(self, chat_id, project_id, text, on_delta):
        title = None
        if not chat_id:
            chat_id = str(uuid.uuid4())
            body = {"uuid": chat_id, "name": ""}
            if project_id:
                body["project_uuid"] = project_id
            r = self._post("/organizations/%s/chat_conversations" % self.org, body)
            r.raise_for_status()
            parent = self.ROOT_PARENT
            new = True
        else:
            conv = self._conversation(chat_id)
            parent = conv.get("current_leaf_message_uuid") or self.ROOT_PARENT
            title = conv.get("name")
            new = False

        body = {
            "prompt": text,
            "parent_message_uuid": parent,
            "timezone": _local_tz(),
            "attachments": [],
            "files": [],
            "rendering_mode": "messages",
        }
        if self.model and self.model != "default":
            body["model"] = self.model
        if self.effort:
            body["effort"] = self.effort      # unofficial: dropped if refused

        def post(body):
            return self._post("/organizations/%s/chat_conversations/%s/completion" % (self.org, chat_id),
                              body, stream=True, timeout=600,
                              headers={"Accept": "text/event-stream"})
        r = post(body)
        if r.status_code in (400, 422) and "effort" in body:
            del body["effort"]
            r = post(body)
            if r.status_code < 400:
                on_delta("(claude.ai did not take the effort setting, so this reply "
                         "uses the model's own.)\n\n")
        if r.status_code >= 400:
            raise RuntimeError("claude.ai completion failed: HTTP %d %s" % (r.status_code, r.text[:200]))
        for raw in r.iter_lines():
            if not raw:
                continue
            line = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else raw
            if not line.startswith("data:"):
                continue
            try:
                ev = json.loads(line[5:].strip())
            except ValueError:
                continue
            t = ev.get("type")
            if t == "completion" and ev.get("completion"):
                on_delta(ev["completion"])
            elif t == "content_block_delta":
                d = ev.get("delta") or {}
                if d.get("type") == "text_delta":
                    on_delta(d.get("text", ""))
            elif t == "content_block_start":
                cb = ev.get("content_block") or {}
                if cb.get("type") == "tool_use" and cb.get("name") == "artifacts":
                    on_delta("\n\n[Creating an artifact...]\n\n")
            elif t == "error":
                err = ev.get("error") or {}
                raise RuntimeError(err.get("message") or "claude.ai returned an error")

        if new:
            try:
                r = self._post("/organizations/%s/chat_conversations/%s/title" % (self.org, chat_id),
                               {"message_content": text, "recent_titles": []})
                if r.ok:
                    title = r.json().get("title")
            except Exception:
                pass
            if not title:
                try:
                    title = self._conversation(chat_id).get("name")
                except Exception:
                    pass
        return chat_id, title or text[:40]

    # Artifacts live inside conversations as "artifacts" tool calls; there
    # is no account-wide listing, so scan the most recent chats.
    def _harvest_artifacts(self, conv):
        cname = conv.get("name") or "Untitled"
        self._conv_uuid = conv.get("uuid", "")
        cid = self._conv_uuid[:8]
        msgs = sorted(conv.get("chat_messages") or [], key=lambda m: m.get("index", 0))
        for m in msgs:
            self._when = m.get("created_at") or conv.get("updated_at") or ""
            for block in m.get("content") or []:
                t = block.get("type")
                if t == "tool_use":
                    self._artifact_from_tool(cid, cname, block.get("name"), block.get("input") or {})
                elif t == "text" and block.get("text"):
                    self._artifacts_from_text(cid, cname, block["text"])
            if not m.get("content") and m.get("text"):
                self._artifacts_from_text(cid, cname, m["text"])

    def _art(self, key, title, cname):
        art = self._artifacts.setdefault(
            key, {"title": title, "content": "", "chat": cname, "type": "", "language": "",
                  "path": "", "when": "", "conv": "", "remote": False})
        art["when"] = max(art["when"], getattr(self, "_when", "") or "")
        art["conv"] = getattr(self, "_conv_uuid", "")
        return art

    def _artifact_from_tool(self, cid, cname, name, inp):
        if name == "artifacts":
            aid = inp.get("id")
            if not aid:
                return
            art = self._art(cid + ":" + aid, inp.get("title") or aid, cname)
            for f in ("title", "type", "language"):
                if inp.get(f):
                    art[f] = inp[f]
            cmd = inp.get("command", "create")
            if cmd in ("create", "rewrite") and "content" in inp:
                art["content"] = inp["content"]
            elif cmd == "update" and inp.get("old_str") is not None:
                art["content"] = art["content"].replace(inp["old_str"], inp.get("new_str", ""), 1)
        elif name in ("create_file", "str_replace") and inp.get("path"):
            path = inp["path"]
            art = self._art(cid + ":" + path, os.path.basename(path), cname)
            art["path"] = path
            if name == "create_file":
                art["content"] = inp.get("file_text", inp.get("content", ""))
            elif inp.get("old_str") is not None:
                art["content"] = art["content"].replace(inp["old_str"], inp.get("new_str", ""), 1)
        elif name == "present_files":
            paths = inp.get("filepaths") or inp.get("paths") or inp.get("files") or []
            for path in [paths] if isinstance(paths, str) else paths:
                if not isinstance(path, str):
                    continue
                art = self._art(cid + ":" + path, os.path.basename(path), cname)
                art["path"] = path
                if not art["content"]:
                    art["remote"] = True    # made by a script: fetch it when needed

    def _artifacts_from_text(self, cid, cname, text):
        # links to files Claude made: [View it](computer:///mnt/user-data/outputs/x.docx)
        for path in _FILE_LINK.findall(text):
            path = unquote(path)
            art = self._art(cid + ":" + path, os.path.basename(path), cname)
            art["path"] = path
            if not art["content"]:
                art["remote"] = True
        for attrs, body in _ANT_ARTIFACT.findall(text):
            a = dict(_ATTR.findall(attrs))
            aid = a.get("identifier") or a.get("title") or "artifact"
            art = self._art(cid + ":" + aid, a.get("title") or aid, cname)
            art.update({k: a[k] for k in ("type", "language", "title") if a.get(k)})
            art["content"] = body.strip("\n")

    def list_artifacts(self, progress=None):
        convs = self._recent_conversations(self.artifact_scan)
        todo = [c for c in convs
                if not (c.get("updated_at") and self._scanned.get(c["uuid"]) == c["updated_at"])]
        for i, c in enumerate(todo):
            if progress and len(todo) > 3:
                progress(i + 1, len(todo))
            try:
                self._harvest_artifacts(self._conversation(c["uuid"]))
                self._scanned[c["uuid"]] = c.get("updated_at") or ""
            except Exception as e:
                log.info("skipping chat %s: %s", c["uuid"], e)
        self.last_scan = len(convs)
        # newest first, by when Claude last wrote each artifact
        keys = sorted(self._artifacts, key=lambda k: self._artifacts[k]["when"], reverse=True)
        return [(k, self._artifacts[k]["title"]) for k in keys]

    def _download(self, a):
        """The bytes of a file Claude made in a conversation's file store."""
        from urllib.parse import quote
        last = None
        for path in ("/organizations/%s/conversations/%s/wiggle/download-file?path=%s",
                     "/organizations/%s/chat_conversations/%s/wiggle/download-file?path=%s"):
            r = self.http.get(self.BASE + path % (self.org, a["conv"], quote(a["path"], safe="")),
                              timeout=60)
            if r.status_code == 200:
                return r.content
            last = r.status_code
        raise RuntimeError("claude.ai would not hand over %s (HTTP %s)."
                           % (os.path.basename(a["path"]), last))

    def _artifact_bytes(self, a):
        if a["remote"] and "data" not in a:
            a["data"] = self._download(a)
        return a.get("data")

    def get_artifact(self, artifact_id):
        a = self._artifacts.get(artifact_id)
        if not a:
            return "Artifact", "(artifact not found - reopen the Artifacts list)"
        if a["remote"]:
            data = self._artifact_bytes(a)
            ext = a["path"].rsplit(".", 1)[-1].lower() if "." in a["path"] else ""
            if ext == "docx":
                return a["title"], "From chat: %s\n\n%s" % (a["chat"], docx_text(data))
            if ext in _TEXT_EXT:
                return a["title"], "From chat: %s\n\n```%s\n%s\n```" % (
                    a["chat"], ext, data.decode("utf-8", "replace"))
            return a["title"], ("From chat: %s\n\nThis is a %s file (%d bytes), which can't "
                                "be shown here. Right-click it and choose Save to disk."
                                % (a["chat"], ext.upper() or "binary", len(data)))
        return a["title"], "From chat: %s\n\n```%s\n%s\n```" % (a["chat"], a.get("language", ""), a["content"])

    def artifact_file(self, artifact_id):
        a = self._artifacts.get(artifact_id)
        if not a:
            raise RuntimeError("Artifact not found - reopen the Artifacts list.")
        name = artifact_filename(a["title"], a.get("type", ""), a.get("language", ""), a.get("path", ""))
        if a["remote"]:
            return name, self._artifact_bytes(a)    # bytes: saved as they are
        return name, a["content"]

    def probe(self, limit=100, word=None):
        """What the recent chats contain, without any content: for the
        bridge's --probe option, to see where a missing artifact went.
        With a word, also say which chats have it in their title, how
        recent they are and which tools and files they use."""
        from collections import Counter
        seen = Counter()
        convs = self._recent_conversations(limit)
        for pos, c in enumerate(convs, 1):
            conv = self._conversation(c["uuid"])
            if word and word.lower() in (c.get("name") or "").lower():
                print("chat #%d: %s (updated %s)" % (pos, c.get("name"), c.get("updated_at")))
                for m in conv.get("chat_messages") or []:
                    for block in m.get("content") or []:
                        if block.get("type") == "tool_use":
                            inp = block.get("input") or {}
                            print("    tool %s: %s %s" % (block.get("name"), sorted(inp),
                                  inp.get("path") or inp.get("filepaths") or inp.get("title") or ""))
            for m in conv.get("chat_messages") or []:
                for block in m.get("content") or []:
                    t = block.get("type")
                    seen[t + (":" + block.get("name") if t == "tool_use" else "")] += 1
                    if t == "text" and "<antArtifact" in (block.get("text") or ""):
                        seen["text:<antArtifact>"] += 1
                if not m.get("content") and "<antArtifact" in (m.get("text") or ""):
                    seen["text:<antArtifact>"] += 1
        return seen


class LazyBackend:
    """Builds the real backend on first use and retries on later commands if
    that fails (no network yet at boot, expired session key...). The error
    is shown on the Atari instead of crashing an unattended gateway."""

    def __init__(self, factory, name):
        self.factory = factory
        self.name = name
        self.real = None

    def _get(self):
        if self.real is None:
            self.real = self.factory()
        return self.real

    def whoami(self):
        return self.real.whoami() if self.real else self.name

    def __getattr__(self, attr):
        return getattr(self._get(), attr)


# ---------------------------------------------------------------------------
# Official Anthropic API, local history
# ---------------------------------------------------------------------------

ST_SYSTEM = (
    "You are Claude, talking to the user through Claude ST, a client running on "
    "an Atari ST/Falcon with a monochrome 80-column text display. Prefer plain "
    "prose and simple lists; avoid emoji, wide tables and images."
)


class ApiBackend(Backend):
    name = "Anthropic API"

    def __init__(self, store_dir, model="claude-opus-5-5"):
        import anthropic
        self.client = anthropic.Anthropic()
        self.model = model
        self.effort = default_effort(model)
        self.dir = os.path.expanduser(store_dir)
        os.makedirs(os.path.join(self.dir, "chats"), exist_ok=True)

    def _path(self, chat_id):
        if not re.fullmatch(r"[0-9a-f]{32}", chat_id or ""):
            raise ValueError("bad chat id")
        return os.path.join(self.dir, "chats", chat_id + ".json")

    def _load(self, chat_id):
        with open(self._path(chat_id)) as f:
            return json.load(f)

    def _save(self, chat):
        chat["updated"] = time.time()
        with open(self._path(chat["id"]), "w") as f:
            json.dump(chat, f, indent=1)

    def _all(self):
        chats = []
        d = os.path.join(self.dir, "chats")
        for fn in os.listdir(d):
            if fn.endswith(".json"):
                try:
                    with open(os.path.join(d, fn)) as f:
                        chats.append(json.load(f))
                except (OSError, ValueError):
                    pass
        return sorted(chats, key=lambda c: -c.get("updated", 0))

    def _projects(self):
        p = os.path.join(self.dir, "projects.json")
        if not os.path.exists(p):
            return []
        with open(p) as f:
            return json.load(f)

    def whoami(self):
        return "API " + dict(self.models()).get(self.model, self.model)

    def models(self):
        known = dict(MODELS)
        return MODELS if self.model in known else [(self.model, self.model[7:19])] + MODELS

    def _save_projects(self, projects):
        with open(os.path.join(self.dir, "projects.json"), "w") as f:
            json.dump(projects, f, indent=1)

    def list_chats(self, limit=100):
        return [(c["id"], c["title"], bool(c.get("pinned"))) for c in self._all()[:limit]]

    def list_projects(self):
        return [(p["id"], p["name"], bool(p.get("pinned")))
                for p in self._projects() if not p.get("archived")]

    def project_chats(self, project_id):
        name = next((p["name"] for p in self._projects() if p["id"] == project_id), "Project")
        return name, [(c["id"], c["title"], bool(c.get("pinned")))
                      for c in self._all() if c.get("project") == project_id]

    def _edit_project(self, project_id, **changes):
        projects = self._projects()
        for p in projects:
            if p["id"] == project_id:
                p.update(changes)
        self._save_projects(projects)

    def rename(self, kind, item_id, name):
        if kind == "PROJECT":
            return self._edit_project(item_id, name=name)
        chat = self._load(item_id)
        chat["title"] = name
        self._save(chat)

    def set_pinned(self, kind, item_id, pinned):
        if kind == "PROJECT":
            return self._edit_project(item_id, pinned=pinned)
        chat = self._load(item_id)
        chat["pinned"] = pinned
        self._save(chat)

    def delete(self, kind, item_id):
        if kind == "PROJECT":
            self._save_projects([p for p in self._projects() if p["id"] != item_id])
            for c in self._all():  # its chats stay, outside any project
                if c.get("project") == item_id:
                    c["project"] = None
                    self._save(c)
            return
        os.remove(self._path(item_id))

    def archive_project(self, project_id):
        self._edit_project(project_id, archived=True)

    def move_chat(self, chat_id, project_id):
        chat = self._load(chat_id)
        chat["project"] = project_id
        self._save(chat)

    def get_chat(self, chat_id):
        c = self._load(chat_id)
        return c["title"], [("U" if m["role"] == "user" else "A", m["content"]) for m in c["messages"]]

    def send(self, chat_id, project_id, text, on_delta):
        if chat_id:
            chat = self._load(chat_id)
        else:
            chat = {"id": uuid.uuid4().hex, "title": text.strip()[:40] or "New chat",
                    "project": project_id, "messages": []}
        system = ST_SYSTEM
        proj = next((p for p in self._projects() if p["id"] == chat.get("project")), None)
        if proj and proj.get("instructions"):
            system += "\n\nProject instructions:\n" + proj["instructions"]
        chat["messages"].append({"role": "user", "content": text})

        reply = []
        extra = {}
        if self.effort:
            extra["output_config"] = {"effort": self.effort}
        if self.model in FALLBACK_MODELS:
            extra.update(betas=["server-side-fallback-2026-07-01"], fallbacks="default")
        with self.client.beta.messages.stream(
            model=self.model,
            max_tokens=16000,
            system=system,
            messages=chat["messages"],
            **extra,
        ) as stream:
            for delta in stream.text_stream:
                reply.append(delta)
                on_delta(delta)
            final = stream.get_final_message()
        if final.stop_reason == "refusal":
            note = "\n\n(Claude declined to answer this request.)"
            reply.append(note)
            on_delta(note)
        chat["messages"].append({"role": "assistant", "content": "".join(reply)})
        self._save(chat)
        return chat["id"], chat["title"]

    _FENCE = re.compile(r"```(\w*)\n(.*?)```", re.S)

    def _artifact_index(self):
        out = []
        for c in self._all():
            n = 0
            for m in c["messages"]:
                if m["role"] != "assistant":
                    continue
                for lang, body in self._FENCE.findall(m["content"]):
                    if body.count("\n") < 4:
                        continue
                    n += 1
                    out.append(("%s.%d" % (c["id"], n), "%s %d: %s" % (lang or "code", n, c["title"]),
                                lang, body, c["title"]))
        return out

    def list_artifacts(self, progress=None):
        return [(a[0], a[1]) for a in self._artifact_index()]

    def get_artifact(self, artifact_id):
        for aid, label, lang, body, chat in self._artifact_index():
            if aid == artifact_id:
                return label, "From chat: %s\n\n```%s\n%s```" % (chat, lang, body)
        return "Artifact", "(not found)"

    def artifact_file(self, artifact_id):
        for aid, label, lang, body, chat in self._artifact_index():
            if aid == artifact_id:
                return artifact_filename(chat, language=lang), body
        raise RuntimeError("Artifact not found.")


# ---------------------------------------------------------------------------
# Demo
# ---------------------------------------------------------------------------

class DemoBackend(Backend):
    name = "demo"

    def __init__(self):
        self.chats = {
            "d1": {"title": "Atari Falcon DSP tricks", "project": "p1", "messages": [
                ("U", "What can the Falcon's DSP56001 do for audio?"),
                ("A", "Quite a lot. The **DSP56001** runs at 32 MHz and can:\n\n"
                      "- mix several channels of 16-bit audio in real time\n"
                      "- run MP3 decoding (as in *Aniplayer*)\n"
                      "- do effects like reverb and EQ\n\n"
                      "## Getting started\n\nLoad a program with `Dsp_LoadProg()` and "
                      "talk to it through the host port.")]},
            "d2": {"title": "Fix my GFA BASIC loop", "project": None, "messages": [
                ("U", "Why is my FOR loop so slow in GFA BASIC?"),
                ("A", "Use integer variables (`i%`) instead of floats, and compile it "
                      "with the GFA compiler:\n\n```\nFOR i%=1 TO 1000\n  PLOT i%,100\nNEXT i%\n```")]},
            "d3": {"title": "Ideas for a demoscene intro", "project": None, "messages": [
                ("U", "Give me three ideas for a 4KB intro."),
                ("A", "1. A rotozoomer with a Claude spark\n2. Raster bars synced to YM music\n"
                      "3. A sync-scroller greeting every ST in the house")]},
        }
        self.chats["d4"] = {"title": "שאלה על Atari Falcon", "project": None, "messages": [
            ("U", "מה זה Atari Falcon?"),
            ("A", "ה-Atari Falcon 030 הוא מחשב ביתי משנת 1992, עם מעבד Motorola 68030 "
                  "ומעבד אותות DSP56001 (במהירות 32 מגה-הרץ).\n\n"
                  "## יתרונות\n\n- סאונד של 16 ביט\n- גרפיקה של עד 65,536 צבעים\n\n"
                  "It was the last computer Atari made, אחרי ה-TT030.")]}
        self.projects = {"p1": {"name": "Falcon audio"}, "p2": {"name": "Demoscene"}}

    def list_chats(self, limit=100):
        return [(k, v["title"], v.get("pinned", False)) for k, v in self.chats.items()][:limit]

    def list_projects(self):
        return [(k, p["name"], p.get("pinned", False))
                for k, p in self.projects.items() if not p.get("archived")]

    def project_chats(self, project_id):
        return self.projects.get(project_id, {}).get("name", "Project"), [
            (k, v["title"], v.get("pinned", False))
            for k, v in self.chats.items() if v["project"] == project_id]

    def _item(self, kind, item_id):
        table = self.projects if kind == "PROJECT" else self.chats
        if item_id not in table:
            raise KeyError("no such %s" % kind.lower())
        return table[item_id]

    def rename(self, kind, item_id, name):
        self._item(kind, item_id)["name" if kind == "PROJECT" else "title"] = name

    def set_pinned(self, kind, item_id, pinned):
        self._item(kind, item_id)["pinned"] = pinned

    def delete(self, kind, item_id):
        self._item(kind, item_id)
        if kind == "PROJECT":
            del self.projects[item_id]
            for c in self.chats.values():
                if c["project"] == item_id:
                    c["project"] = None
        else:
            del self.chats[item_id]

    def archive_project(self, project_id):
        self._item("PROJECT", project_id)["archived"] = True

    def move_chat(self, chat_id, project_id):
        self._item("PROJECT", project_id)
        self._item("CHAT", chat_id)["project"] = project_id

    def get_chat(self, chat_id):
        c = self.chats[chat_id]
        return c["title"], list(c["messages"])

    def send(self, chat_id, project_id, text, on_delta):
        if not chat_id:
            chat_id = "d%d" % (len(self.chats) + 1)
            self.chats = {chat_id: {"title": text[:40], "project": project_id, "messages": []},
                          **self.chats}
        reply = ("You said: \"%s\".\n\nThis is the **demo** backend, so no real model "
                 "is answering (you picked %s%s). Run the bridge with `--backend claudeai` "
                 "to talk to your account.\n\n- Streaming works\n- Word wrap works\n"
                 % (text, dict(self.models()).get(self.model, self.model),
                    ", effort " + self.effort if self.effort else ""))
        for i in range(0, len(reply), 7):
            on_delta(reply[i:i + 7])
            time.sleep(0.03)
        c = self.chats[chat_id]
        c["messages"] += [("U", text), ("A", reply)]
        return chat_id, c["title"]

    def list_artifacts(self, progress=None):
        return [("a1", "falcon_mixer.s")]

    def artifact_file(self, artifact_id):
        return "FALCON_M.S", "; mix two channels\n    move.l  (a0)+,d0\n    add.l   (a1)+,d0\n    move.l  d0,(a2)+\n"

    def get_artifact(self, artifact_id):
        return "falcon_mixer.s", "```asm\n; mix two channels\n    move.l  (a0)+,d0\n    add.l   (a1)+,d0\n    move.l  d0,(a2)+\n```"
