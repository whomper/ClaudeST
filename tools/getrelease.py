#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Fetch another AERA program's files from its GitHub release, for a test
that needs that program (guidelines 1.2: a test uses what Erez has, the
other program's latest release, not a build of its main).

    getrelease.py APP DIR FILE...            the latest release
    getrelease.py APP@v1.8 DIR FILE...       that release
    getrelease.py --source APP DIR PATH...   files of the source at the
                                             latest release's tag

APP is the repository's name under github.com/whomper (EHypView,
EContact...). FILE is a file the release carries; with --source, PATH is
a file in the repository as it was tagged for the release, for a program
whose release carries no files of its own (AERA-Gateway's
gateway/eftpgate.py). The files land in DIR (by their names, without the
folders), and DIR/APP.VERSION says which release they came from (a rig
prints it, so a report names it). Standard Python only. The same file is
in AERA-Atari's common/tools and xl/tools.
"""
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

OWNER = "whomper"


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


def latest(app, name):
    """the tag of the latest release: GitHub answers
    .../releases/latest/download/NAME with a redirect to
    .../releases/download/vX.Y/NAME (the release pages themselves may be
    refused by a proxy that lets downloads through)"""
    url = "https://github.com/%s/%s/releases/latest/download/%s" % (OWNER, app, name)
    try:
        urllib.request.build_opener(_NoRedirect).open(urllib.request.Request(url, method="HEAD"))
    except urllib.error.HTTPError as e:
        loc = e.headers.get("Location", "")
        if e.code in (301, 302, 303, 307, 308) and "/releases/download/" in loc:
            return loc.split("/releases/download/", 1)[1].split("/", 1)[0]
        if e.code == 404:
            sys.exit("%s: no release with %s (%s)" % (app, name, e))
    except urllib.error.URLError:
        pass
    return highest_tag(app)


def highest_tag(app):
    """the highest vX.Y tag, through git (a cloud session's proxy refuses
    .../releases/latest but lets git through); the Release workflow makes
    a tag only with a release"""
    try:
        out = subprocess.run(["git", "ls-remote", "--tags", "https://github.com/%s/%s" % (OWNER, app)],
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("%s: no release found (%s)" % (app, e))
    tags = re.findall(r"refs/tags/(v(\d+)\.(\d+))$", out, re.M)
    if not tags:
        sys.exit("%s: no release found" % app)
    return max(tags, key=lambda t: (int(t[1]), int(t[2])))[0]


def fetch(spec, d, names, source=False):
    app, _, tag = spec.partition("@")
    tag = tag or (highest_tag(app) if source else latest(app, names[0]))
    os.makedirs(d, exist_ok=True)
    for n in names:
        if source:
            url = "https://raw.githubusercontent.com/%s/%s/%s/%s" % (OWNER, app, tag, n)
        else:
            url = "https://github.com/%s/%s/releases/download/%s/%s" % (OWNER, app, tag, n)
        try:
            with urllib.request.urlopen(url) as r:
                data = r.read()
        except urllib.error.HTTPError as e:
            sys.exit("%s %s: no %s (%s)" % (app, tag, n, e))
        with open(os.path.join(d, os.path.basename(n)), "wb") as f:
            f.write(data)
    with open(os.path.join(d, app.upper() + ".VERSION"), "w") as f:
        f.write(tag + "\n")
    print("%s %s: %s" % (app, tag, " ".join(names)))
    return tag


if __name__ == "__main__":
    args = sys.argv[1:]
    source = args[:1] == ["--source"]
    if source:
        args = args[1:]
    if len(args) < 3:
        sys.exit(__doc__)
    fetch(args[0], args[1], args[2:], source)
