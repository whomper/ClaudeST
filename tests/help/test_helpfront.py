#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""helpfront.py page: the front page replaces Main, fits the page, links
only to pages there are, and brings its pictures along; hcp (when on the
PATH, or HCP=) compiles it.

    python3 common/tests/help/test_helpfront.py
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, TOOLS)
import gemimg  # noqa: E402
import helpfront  # noqa: E402

STG = """@database "Test"
@author "AERA | Erez Yaary"
@$VER: TEST.HYP 1.2 (17.10.2026)
@width 75

@node "Main" "Test 1.2"
The old front page.
@endnode

@node "Installing" "Test: Installing"
Copy it.
@endnode

@node "Keys" "Test: Keys"
Keys.
@endnode
"""

SPEC = {
    "name": "Test", "banner": "EMAIL", "address": "github.com/whomper/Test",
    "welcome": ["Welcome to **Test**, a program with a rather long welcome that has to be "
                "wrapped over more than one line of the page, with a [[link to keys|Keys]]."],
    "window": {"picture": "docs/help/WINDOW.IMG", "intro": "Two panes:",
               "items": ["[[Installing]]: where it goes, and a long explanation of it.", "The other one."],
               "more": "More in [[Keys]]."},
    "diagram": {"title": "How Test reaches its server",
                "boxes": [["Your Atari", "Test, STinG"], ["Raspberry Pi", "testgate"], ["Server", "anywhere"]],
                "arrows": ["home net", "TLS"], "round": "[[Keys]]: the way round",
                "after": ["A note under the diagram."]},
    "contents": [["Start", ["Installing"]], ["Every day", ["Keys"]], ["More", ["[[Keys again|Keys]]"]]],
}


def main():
    fails = 0
    d = tempfile.mkdtemp()
    try:
        os.makedirs(os.path.join(d, "docs", "help"))
        gemimg.write_img(os.path.join(d, "docs", "help", "WINDOW.IMG"), [gemimg.TEAL] * (96 * 40), 96, 40)
        json.dump(SPEC, open(os.path.join(d, "docs", "front.json"), "w"))
        open(os.path.join(d, "in.stg"), "w").write(STG)
        out = os.path.join(d, "out.stg")
        helpfront.page(os.path.join(d, "docs", "front.json"), os.path.join(d, "in.stg"), out)
        stg = open(out, encoding="latin-1").read()
        main_ = re.search(r'@node "Main".*?@endnode', stg, re.S).group(0)
        nodes = set(re.findall(r'@node "([^"]+)"', stg))

        def check(ok, what):
            nonlocal fails
            print(("ok   " if ok else "FAIL ") + what)
            fails += not ok

        check("old front page" not in stg and '@node "Main" "Test 1.2"' in stg, "Main replaced, with the version")
        check(all(helpfront.vis(l) <= 75 for l in main_.split("\n")
                  if not l.startswith("@") or l.startswith("@{")), "every line within 75 characters")
        links = set(re.findall(r'LINK "([^"]+)"', main_))
        check(links and links <= nodes, "links go to pages there are: %s" % sorted(links))
        check(all(os.path.exists(os.path.join(d, n)) for n in ("BANNER.IMG", "WINDOW.IMG")),
              "the pictures next to the page")
        check("@image WINDOW.IMG 64 " in main_, "the picture on the right")
        i = main_.split("\n").index("Two panes:")
        check(main_.split("\n")[i:i + 5].count("") >= 1 and "@{B}How Test" in main_, "the diagram after it")
        hcp = os.environ.get("HCP") or shutil.which("hcp")
        if hcp:
            r = subprocess.run([hcp, "-o", os.path.join(d, "TEST.HYP"), out], capture_output=True, text=True)
            check(r.returncode == 0 and "error" not in (r.stdout + r.stderr).lower(), "hcp compiles it")
        else:
            print("skip hcp (not found)")
    finally:
        shutil.rmtree(d)
    print("%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
