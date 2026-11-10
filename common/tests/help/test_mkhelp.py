#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkhelp.py: a guide's pages, links, tables, listings and pictures; every
line within the page; the Atari character set; the same file each time;
the options; then helpfront.py page over it, and hcp (when on the PATH,
or HCP=) compiles the two.

    python3 common/tests/help/test_mkhelp.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
MKHELP = os.path.join(TOOLS, "mkhelp.py")

GUIDE = """# Test - user guide

Welcome. See [Keyboard](#keyboard) and [the settings](#settings-and-files); \
[a site](https://example.com) is only its text. An @ stays one.

![The window](docs/help/window.png)

## Keyboard

| Key | What it does |
|---|---|
| Space | Play or pause, the longest description of all here, long enough that it has to be wrapped onto a second line |
| ⇧-Left | The previous file |
| `a|b` | A bar in code |

- **Bold** item, and a long list item that goes on past the page's width so that it hangs under itself as it should
  with its continuation.
- ↑ and ↓ move — «quoted» and café.

## Settings and files

```
C:\\GUIDES\\TEST.HYP        the guide, where the viewer finds it first, a description that is too long for one line
short
```

### A heading inside

Text.
"""


def run(*args):
    return subprocess.run([sys.executable, "-I", MKHELP] + list(args), check=True)


def main():
    d = tempfile.mkdtemp(prefix="mkhelp")
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(("ok    " if ok else "FAIL  ") + what)
        fails += not ok

    try:
        guide = os.path.join(d, "GUIDE.md")
        ver = os.path.join(d, "VERSION")
        with open(guide, "w", encoding="utf-8") as f:
            f.write(GUIDE)
        with open(ver, "w") as f:
            f.write("1.2\n")
        out = os.path.join(d, "TEST.STG")
        run(guide, ver, out)
        raw = open(out, "rb").read()
        s = raw.decode("latin-1")
        lines = s.split("\r\n")
        check(b"\n" not in raw.replace(b"\r\n", b""), "CR/LF line ends only")
        check(all(ord(c) < 256 for c in s) and "\xaf" in s and "\xae" in s and "\x82" in s,
              "the Atari character set (\u00bb \u00ab \u00e9)")
        check('@database "Test - user guide"' in lines and '@subject "Programs"' in lines and
              "@$VER: TEST.HYP 1.2" in lines and '@help "Keyboard"' in lines,
              "the header from the guide and the defaults")
        nodes = re.findall(r'@node "([^"]+)" "([^"]+)"', s)
        check(nodes == [("Main", "Test 1.2"), ("Keyboard", "Test: Keyboard"),
                        ("Settings and files", "Test: Settings and files")], "a page a section: %s" % nodes)
        check(s.count("@node") == s.count("@endnode"), "every page ended")
        check('@{"Keyboard" LINK "Keyboard"}' in s and '@{"the settings" LINK "Settings and files"}' in s,
              "links between sections")
        check("example.com" not in s and "a site" in s, "a link out of the guide is its text")
        check("window.png" not in s and "The window" not in s, "pictures left out")
        check("An @@ stays one" in s, "@ written as @@")
        check("@{B}Bold@{b}" in s and "@{B}A heading inside@{b}" in s, "bold and ### headings")
        check("Shift-Left" in s and "Up and Down" in s, "arrows and shift in words")
        check("a|b" in s, "a | inside code stays in its cell")

        def vis(l):
            return len(re.sub(r"@\{[^}]*\}", "", l).replace("@@", "@"))
        body = [l for l in lines if not l.startswith("@") or l.startswith("@{")]
        check(all(vis(l) <= 75 for l in body), "every line within 75 columns (the widest %d)" %
              max(vis(l) for l in body))
        check(any(l.startswith("  ") and "hangs" in l for l in lines), "a list item hangs under itself")
        col = len("C:\\GUIDES\\TEST.HYP        ")
        check(any(l.startswith(" " * col) and l[col] != " " for l in lines),
              "a long listing line goes on under its description")
        run(guide, ver, out)
        check(open(out, "rb").read() == raw, "the same guide gives the same file")

        out2 = os.path.join(d, "OTHER.STG")
        run(guide, ver, out2, "--app", "Other", "--database", "Other - the guide", "--subject", "Sound",
            "--help-page", "")
        s2 = open(out2, encoding="latin-1").read()
        check('@database "Other - the guide"' in s2 and '@subject "Sound"' in s2 and "@help" not in s2 and
              "@$VER: OTHER.HYP 1.2" in s2 and '"Other: Keyboard"' in s2, "the options")

        # the family's front page over it, as the apps' Makefiles do
        front = os.path.join(d, "front.json")
        with open(front, "w") as f:
            f.write('{"name": "Test", "banner": "EPLAY", "address": "github.com/whomper/Test", '
                    '"welcome": ["Hello. See [[Keyboard]]."], '
                    '"contents": [["Start", ["Keyboard", "Settings and files"]]]}')
        try:
            subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "helpfront.py"), "page", front, out, out],
                           check=True, capture_output=True)
            check(True, "helpfront.py page takes it")
        except subprocess.CalledProcessError as e:
            check(False, "helpfront.py page: %s" % e.stderr.decode(errors="replace").strip())
        hcp = os.environ.get("HCP") or shutil.which("hcp")
        if hcp:
            r = subprocess.run([hcp, "-o", os.path.join(d, "TEST.HYP"), out], capture_output=True, cwd=d)
            check(r.returncode == 0 and os.path.exists(os.path.join(d, "TEST.HYP")),
                  "hcp compiles it" + ("" if r.returncode == 0 else ": " + r.stderr.decode(errors="replace")))
        else:
            print("NOTE  no hcp: not compiled")
    finally:
        shutil.rmtree(d)
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
