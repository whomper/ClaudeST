#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translatelangs - the languages translategate has: Argos Translate's
models, each language to and from English, put in and taken out.

`aera-gateway install translate` uses it as root; on the Gateway it is
also translategate-langs.service, a job of its own: an app's Languages
page asks translategate for a set of languages (POST /languages, only
when the page closes with OK; Erez, 7 Nov 2026), translategate writes the
request to /var/lib/translategate/request, systemd starts this job, which
downloads what is missing and removes what is no longer wanted, writing
its progress to /var/lib/translategate/job; translategate reads that for
GET /job and starts again with the new models when the job is done.

translategate itself never connects anywhere; this job connects only to
Argos Translate's index and downloads, over HTTPS (a test's own index may
be a file: TRANSLATELANGS_INDEX).

    translatelangs.py job        (translategate-langs.service)

Standard Python 3 only.
"""

import json
import os
import re
import shutil
import sys
import tempfile
import time
import urllib.request
import zipfile

INDEX = "https://raw.githubusercontent.com/argosopentech/argospm-index/main/index.json"
AGENT = "aera-gateway"                  # (Argos's server turns away Python's own User-Agent)
HERE = os.path.dirname(os.path.abspath(__file__))
TABLE = os.path.join(HERE, "translate-languages.tsv")
MODELS = "/opt/translategate/models"
STATE = "/var/lib/translategate"
MEMORY_MAX = 1536                       # MB translategate may take (its MemoryMax) on a 4 GB Pi
WORKING = 150                           # MB of that it needs beside the models
LOADED = 1.25                           # a model loaded takes about this much more than its download
CAP_MB = int((MEMORY_MAX - WORKING) / LOADED)   # the MB of models (as the table counts them) that fit


class Fail(Exception):
    """why a set of languages can't be had, in words the Atari can show"""


def table(path=TABLE):
    """[(code, name, MB)] in the table's order, English first"""
    out = []
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                code, name, mb = line.rstrip("\n").split("\t")
                out.append((code, name, int(mb)))
    except (OSError, ValueError):
        pass
    return out


def pairs(langs):
    """the models for langs: each to and from English"""
    return [p for x in sorted(set(langs) - {"en"}) for p in ((x, "en"), ("en", x))]


def have(models=MODELS):
    """the pairs whose model is there, as ("he", "en")"""
    out = set()
    try:
        names = os.listdir(models)
    except OSError:
        return out
    for d in names:
        m = re.fullmatch(r"([a-z]{2,3})_([a-z]{2,3})", d)
        if m and os.path.exists(os.path.join(models, d, "metadata.json")):
            out.add(m.groups())
    return out


def installed(models=MODELS):
    """the languages there both ways, English included"""
    got = have(models)
    return {"en"} | {a for a, b in got if b == "en" and ("en", a) in got}


def size_mb(langs, rows=None):
    sizes = {c: mb for c, _, mb in (rows if rows is not None else table())}
    return sum(sizes.get(c, 0) for c in set(langs) - {"en"})


def links(want, index=None):
    """{pair: https URL} of the newest model for each pair in want"""
    url = index or os.environ.get("TRANSLATELANGS_INDEX") or INDEX
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": AGENT}), timeout=60) as r:
            data = json.load(r)
    except (OSError, ValueError) as e:
        raise Fail("can't read Argos Translate's list of models (%s)" % type(e).__name__)
    newest = {}
    local = url.startswith("file:")
    for p in data:
        k = (p.get("from_code"), p.get("to_code"))
        ok = [u for u in p.get("links", []) if u.startswith("https://") or (local and u.startswith("file:"))]
        if k in want and ok:
            v = tuple(int(x) for x in re.findall(r"\d+", str(p.get("package_version", "0"))))
            if k not in newest or v > newest[k][0]:
                newest[k] = (v, ok[0])
    gone = [p for p in want if p not in newest]
    if gone:
        raise Fail("Argos Translate has no model for %s" % ", ".join("%s>%s" % p for p in gone))
    return {k: u for k, (_, u) in newest.items()}


def fetch(pair, url, models=MODELS, progress=lambda n: None):
    """one model, downloaded, checked and put in place as models/a_b"""
    a, b = pair
    os.makedirs(models, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix=".part-", dir=models)   # (the same disk: the last step is a rename)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": AGENT})
        with urllib.request.urlopen(req, timeout=600) as r, open(os.path.join(tmp, "m.zip"), "wb") as f:
            while True:
                piece = r.read(1 << 20)
                if not piece:
                    break
                f.write(piece)
                progress(len(piece))
        with zipfile.ZipFile(os.path.join(tmp, "m.zip")) as z:
            names = z.namelist()
            top = names[0].split("/")[0]
            need = ("%s/metadata.json" % top, "%s/sentencepiece.model" % top, "%s/model/model.bin" % top)
            if any(n not in names for n in need) or any(n.startswith("/") or ".." in n.split("/") for n in names):
                raise Fail("the %s>%s model isn't one translategate can use" % pair)
            z.extractall(tmp, [n for n in names if n.startswith(top + "/model/") or n in need])
        os.remove(os.path.join(tmp, "m.zip"))
        dst = os.path.join(models, "%s_%s" % pair)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        os.rename(os.path.join(tmp, top), dst)
    except zipfile.BadZipFile:
        raise Fail("the %s>%s model came broken" % pair)
    except OSError as e:
        if getattr(e, "errno", None) == 28:
            raise Fail("the Gateway's disk is full")
        raise Fail("the %s>%s model didn't download (%s)" % (a, b, type(e).__name__))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def apply(want, models=MODELS, status=lambda **k: None, index=None, fetch_one=None):
    """models for exactly the languages want (English always): the missing
    downloaded, the unwanted removed. status(done=, total=) as it goes.
    True if anything changed; Fail with why if it can't."""
    want = set(want) | {"en"}
    rows = table()
    known = {c for c, _, _ in rows}
    odd = sorted(want - known)
    if odd:
        raise Fail("no language %s" % ", ".join(odd)[:40])
    if size_mb(want, rows) > CAP_MB:
        raise Fail("those languages take %d MB; %d MB fit" % (size_mb(want, rows), CAP_MB))
    there = have(models)
    add = [p for p in pairs(want) if p not in there]
    drop = sorted(p for p in there if not (set(p) <= want))
    total = sum(mb for c, _, mb in rows if c in {x for p in add for x in p} - {"en"}) << 20
    if add:
        free = shutil.disk_usage(models if os.path.isdir(models) else os.path.dirname(models)).free
        if free < total * 2.2:          # the download and the model beside it
            raise Fail("the Gateway's disk has %d MB free; %d MB are needed" % (free >> 20, total * 2.2 / 2 ** 20))
        urls = links(add, index)
        done = [0]

        def progress(n):
            done[0] += n
            status(done=done[0], total=max(total, done[0]))
        for p in add:
            (fetch_one or fetch)(p, urls[p], models, progress)
    for p in drop:
        shutil.rmtree(os.path.join(models, "%s_%s" % p), ignore_errors=True)
    return bool(add or drop)


# ---------------------------------------------------------------- the job

def write_json(path, data):
    tmp = path + ".new"
    with open(tmp, "w") as f:
        json.dump(data, f)
    os.replace(tmp, path)


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def job(state=STATE, models=MODELS, index=None, fetch_one=None):
    """the request translategate wrote, done; its progress in state/job"""
    req = read_json(os.path.join(state, "request")) or {}
    jid = req.get("id", 0)
    out = os.path.join(state, "job")
    last = [0.0]

    def status(**k):
        if time.monotonic() - last[0] >= 1:
            last[0] = time.monotonic()
            write_json(out, dict(id=jid, state="running", **k))
    write_json(out, dict(id=jid, state="running", done=0, total=0))
    try:
        changed = apply(req.get("want", []), models, status, index, fetch_one)
        write_json(out, dict(id=jid, state="done", changed=changed))
    except Fail as e:
        write_json(out, dict(id=jid, state="failed", why=str(e), changed=True))
    except Exception as e:              # its words could name a path: its name only
        write_json(out, dict(id=jid, state="failed", why="the job failed (%s)" % type(e).__name__, changed=True))


if __name__ == "__main__":
    if sys.argv[1:] != ["job"]:
        sys.exit("usage: translatelangs.py job")
    job()
