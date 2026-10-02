"""Backends the bridge can serve to the Atari.

claudeai  - your claude.ai account (chats, projects, artifacts) through the
            same web endpoints the claude.ai site uses. Unofficial: needs your
            browser's sessionKey cookie and may break if claude.ai changes.
api       - the official Anthropic API; chats and projects are stored locally.
demo      - canned data, no network. For trying the Atari side out.
"""
import json
import os
import re
import time
import uuid

Item = tuple  # (id, label)


class Backend:
    name = "?"

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

    def list_artifacts(self):
        return []

    def get_artifact(self, artifact_id):
        return "Artifact", ""


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


class ClaudeAiBackend(Backend):
    name = "claude.ai"
    BASE = "https://claude.ai/api"
    ROOT_PARENT = "00000000-0000-4000-8000-000000000000"

    def __init__(self, session_key, org_id=None, artifact_scan=15):
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

    def _conversations(self, limit):
        return self._get("/organizations/%s/chat_conversations" % self.org, limit=limit)

    def list_chats(self, limit=100):
        return [(c["uuid"], c.get("name") or "Untitled") for c in self._conversations(limit)]

    def list_projects(self):
        projs = self._get("/organizations/%s/projects" % self.org)
        projs = [p for p in projs if not p.get("archived_at")]
        return [(p["uuid"], p.get("name") or "Untitled project") for p in projs]

    def project_chats(self, project_id):
        name = "Project"
        for pid, label in self.list_projects():
            if pid == project_id:
                name = label
        try:
            convs = self._get("/organizations/%s/projects/%s/conversations" % (self.org, project_id))
        except Exception:
            convs = [c for c in self._conversations(500) if c.get("project_uuid") == project_id]
        return name, [(c["uuid"], c.get("name") or "Untitled") for c in convs]

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
        r = self._post("/organizations/%s/chat_conversations/%s/completion" % (self.org, chat_id),
                       body, stream=True, timeout=600,
                       headers={"Accept": "text/event-stream"})
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
        for m in conv.get("chat_messages") or []:
            for block in m.get("content") or []:
                if block.get("type") != "tool_use" or block.get("name") != "artifacts":
                    continue
                inp = block.get("input") or {}
                aid = inp.get("id")
                if not aid:
                    continue
                cmd = inp.get("command", "create")
                key = conv["uuid"][:8] + ":" + aid
                art = self._artifacts.get(key, {"title": aid, "content": "", "chat": cname})
                if inp.get("title"):
                    art["title"] = inp["title"]
                if cmd in ("create", "rewrite") and "content" in inp:
                    art["content"] = inp["content"]
                elif cmd == "update" and inp.get("old_str") is not None:
                    art["content"] = art["content"].replace(inp["old_str"], inp.get("new_str", ""), 1)
                self._artifacts[key] = art

    def list_artifacts(self):
        for cid, _ in self.list_chats(limit=self.artifact_scan):
            try:
                self._harvest_artifacts(self._conversation(cid))
            except Exception:
                continue
        return [(k, a["title"]) for k, a in self._artifacts.items()]

    def get_artifact(self, artifact_id):
        a = self._artifacts.get(artifact_id)
        if not a:
            return "Artifact", "(artifact not found - reopen the Artifacts list)"
        return a["title"], "From chat: %s\n\n```\n%s\n```" % (a["chat"], a["content"])


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
        return "API " + self.model

    def list_chats(self, limit=100):
        return [(c["id"], c["title"]) for c in self._all()[:limit]]

    def list_projects(self):
        return [(p["id"], p["name"]) for p in self._projects()]

    def project_chats(self, project_id):
        name = next((p["name"] for p in self._projects() if p["id"] == project_id), "Project")
        return name, [(c["id"], c["title"]) for c in self._all() if c.get("project") == project_id]

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
        with self.client.beta.messages.stream(
            model=self.model,
            max_tokens=16000,
            system=system,
            messages=chat["messages"],
            betas=["server-side-fallback-2026-07-01"],
            fallbacks="default",
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

    def list_artifacts(self):
        return [(a[0], a[1]) for a in self._artifact_index()]

    def get_artifact(self, artifact_id):
        for aid, label, lang, body, chat in self._artifact_index():
            if aid == artifact_id:
                return label, "From chat: %s\n\n```%s\n%s```" % (chat, lang, body)
        return "Artifact", "(not found)"


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
        self.projects = {"p1": "Falcon audio"}

    def list_chats(self, limit=100):
        return [(k, v["title"]) for k, v in self.chats.items()][:limit]

    def list_projects(self):
        return list(self.projects.items())

    def project_chats(self, project_id):
        return self.projects.get(project_id, "Project"), [
            (k, v["title"]) for k, v in self.chats.items() if v["project"] == project_id]

    def get_chat(self, chat_id):
        c = self.chats[chat_id]
        return c["title"], list(c["messages"])

    def send(self, chat_id, project_id, text, on_delta):
        if not chat_id:
            chat_id = "d%d" % (len(self.chats) + 1)
            self.chats = {chat_id: {"title": text[:40], "project": project_id, "messages": []},
                          **self.chats}
        reply = ("You said: \"%s\".\n\nThis is the **demo** backend, so no real model "
                 "is answering. Run the bridge with `--backend claudeai` to talk to your "
                 "account.\n\n- Streaming works\n- Word wrap works\n" % text)
        for i in range(0, len(reply), 7):
            on_delta(reply[i:i + 7])
            time.sleep(0.03)
        c = self.chats[chat_id]
        c["messages"] += [("U", text), ("A", reply)]
        return chat_id, c["title"]

    def list_artifacts(self):
        return [("a1", "falcon_mixer.s")]

    def get_artifact(self, artifact_id):
        return "falcon_mixer.s", "```asm\n; mix two channels\n    move.l  (a0)+,d0\n    add.l   (a1)+,d0\n    move.l  d0,(a2)+\n```"
