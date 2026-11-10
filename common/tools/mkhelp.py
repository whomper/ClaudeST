#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The family's help generator (guidelines 4.3): an app's docs/GUIDE.md
into an ST-Guide hypertext source, NAME.STG. From EPlay's (EPDF's,
EContact's, EMail's, EHypView's before it): one generator for every app.

    mkhelp.py docs/GUIDE.md VERSION build/NAME.STG [--app EPlay]
              [--database "EPlay - music on the Falcon"] [--subject Sound]
              [--help-page Keyboard]

--app is the name in the page titles ("EPlay: Settings"; by default the
guide's first heading's first word), --database the database's title (by
default the guide's first heading), --subject the category ST-Guide's
catalogue sorts it under (Programs by default), --help-page the page the Help
key opens in the viewer (Keyboard by default; none with ""). NAME.HYP is
OUT's name.

Each "## " section becomes a page, the text before the first one the
main page with the contents. ST-Guide shows text as it is written, so
tables are laid out in columns here and every line is kept within the
page width (a long line in a listing goes on under its description);
bold becomes @{B}, links between sections become ST-Guide links, and
pictures are left out. The text is written in the Atari character set,
and the same guide always gives the same file (no date in it). Compile
with HCP (the hypview project): hcp -o binary/NAME.HYP build/NAME.STG;
helpfront.py page then puts the family's front page in as Main.
"""
import argparse
import os
import re
import sys

WIDTH = 75

# what the guide uses beyond ASCII, in the Atari character set
ATARI = {"»": "\xaf", "«": "\xae", "×": "x", "—": "-", "–": "-",
         "─": "-", "‘": "'", "’": "'", "“": '"', "”": '"', "≡": "\xf0",
         "·": "\xfa", "é": "\x82", "è": "\x8a", "ü": "\x81", "ö": "\x94", "ä": "\x84"}


def atari(s):
    return "".join(ATARI.get(c, c if ord(c) < 128 else "?") for c in s)


def slug(title):
    """GitHub's anchor for a heading: lower case, spaces to dashes"""
    return re.sub(r"[^a-z0-9 -]", "", title.lower()).replace(" ", "-")


def plain(s):
    """the visible text of a line of markdown, without its markup"""
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    return s.replace("**", "").replace("`", "")


def markup(s, anchors):
    """markdown inline markup to ST-Guide's; links to sections become links"""
    def link(m):
        text, target = m.group(1), m.group(2)
        if target.startswith("#") and target[1:] in anchors:
            return '@{"%s" LINK "%s"}' % (text, anchors[target[1:]])
        return text
    s = s.replace("@", "@@")
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"@{B}\1@{b}", s)
    return s.replace("`", "")


def vislen(w):
    """the width of a word on the page: no @{...} commands, @@ as one"""
    return len(re.sub(r"@\{[^}]*\}", "", w).replace("@@", "@"))


def wrap(text, width):
    """words into lines of at most width characters; an ST-Guide command
    with spaces in it (a link) stays one word"""
    text = re.sub(r"@\{[^}]*\}", lambda m: m.group(0).replace(" ", "\0"), text)
    out, line, n = [], "", 0
    for w in text.split():
        lw = vislen(w.replace("\0", " "))
        if line and n + 1 + lw > width:
            out.append(line)
            line, n = w, lw
        else:
            line, n = (w, lw) if not line else (line + " " + w, n + 1 + lw)
    if line or not out:
        out.append(line)
    return [l.replace("\0", " ") for l in out]


def table(rows):
    """a markdown table in columns, the widest column wrapped to fit"""
    def split(r):
        r = re.sub(r"`[^`]*`", lambda m: m.group(0).replace("|", "\0"), r.strip().strip("|"))
        r = r.replace("\\|", "\0")          # an escaped one, as GitHub writes it
        return [plain(c.strip().replace("\0", "|")).replace("@", "@@") for c in r.split("|")]
    cells = [split(r) for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r":?-+:?", c) for c in r)]
    n = max(len(r) for r in cells)
    cells = [r + [""] * (n - len(r)) for r in cells]
    widths = [max(len(r[i]) for r in cells) for i in range(n)]
    while sum(widths) + 2 * (n - 1) > WIDTH:
        widest = widths.index(max(widths))
        widths[widest] -= 1
    out = []
    for k, r in enumerate(cells):
        parts = [wrap(c, widths[i]) for i, c in enumerate(r)]
        for j in range(max(len(p) for p in parts)):
            line = "  ".join((p[j] if j < len(p) else "").ljust(widths[i]) for i, p in enumerate(parts))
            out.append(("@{B}%s@{b}" % line.rstrip()) if k == 0 else line.rstrip())
        if k == 0:
            out.append("  ".join("-" * w for w in widths))
    return out


def code_line(s):
    """a line of a listing as it is, or, when it's too long, its
    description (after the last gap of two spaces) wrapped under itself"""
    if len(s) <= WIDTH:
        return [s.replace("@", "@@")]
    m = None
    for m in re.finditer(r"  +(?=\S)", s):
        pass
    col = m.end() if m else 0
    if not m or col > WIDTH - 20:
        return [s[k:k + WIDTH].replace("@", "@@") for k in range(0, len(s), WIDTH)]
    lines = wrap(s[col:].replace("@", "@@"), WIDTH - col)
    return [s[:col].replace("@", "@@") + lines[0]] + [" " * col + l for l in lines[1:]]


def page(lines, anchors):
    """the body of one page: paragraphs and list items flowed to the page
    width, code as it is, tables in columns"""
    out, i = [], 0
    while i < len(lines):
        s = lines[i]
        if s.startswith("```"):
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                out += code_line(lines[i])
                i += 1
            i += 1
        elif s.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            out += table(rows)
        elif s.startswith("### "):
            out.append("@{B}%s@{b}" % s[4:].strip())
            i += 1
        elif not s.strip():
            out.append("")
            i += 1
        elif re.fullmatch(r"(\s*!\[[^\]]*\]\([^)]*\))+\s*", s):
            i += 1		# a picture: not in the help
            if i < len(lines) and not lines[i].strip() and out and not out[-1].strip():
                i += 1
        else:
            # a paragraph, or a list item with its indented continuation
            ind = len(s) - len(s.lstrip())
            item = s.lstrip().startswith("- ")
            text = [s.strip()]
            i += 1
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("```", "|", "#")):
                nxt = lines[i]
                nind = len(nxt) - len(nxt.lstrip())
                if nxt.lstrip().startswith("- ") or (item and nind <= ind):
                    break
                text.append(nxt.strip())
                i += 1
            hang = ind + (2 if item else 0)
            for k, l in enumerate(wrap(markup(" ".join(text), anchors), WIDTH - hang)):
                out.append(" " * (ind if k == 0 else hang) + l)
    while out and not out[-1].strip():
        out.pop()
    while out and not out[0].strip():
        out.pop(0)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="docs/GUIDE.md to an ST-Guide source")
    ap.add_argument("guide")
    ap.add_argument("version", help="the VERSION file")
    ap.add_argument("out", help="NAME.STG")
    ap.add_argument("--app")
    ap.add_argument("--database")
    ap.add_argument("--subject", default="Programs")
    ap.add_argument("--help-page", default="Keyboard")
    a = ap.parse_args(argv)
    src, dst = a.guide, a.out
    version = open(a.version).read().strip()
    name = os.path.splitext(os.path.basename(dst))[0].upper()
    # words for what the Atari's font has no letter for, before laying out
    text = open(src, encoding="utf-8").read().replace("↑", "Up").replace("↓", "Down")
    text = text.replace("⇧-", "Shift-").replace("⇧", "Shift+").split("\n")
    title = text[0].lstrip("# ").strip()
    app = a.app or (title.split() or [name.capitalize()])[0]
    sections, cur = [], ("Main", [])
    for s in text[1:]:
        if s.startswith("## "):
            sections.append(cur)
            cur = (s[3:].strip(), [])
        else:
            cur[1].append(s)
    sections.append(cur)
    anchors = {slug(t): t for t, _ in sections[1:]}
    out = ['@database "%s"' % (a.database or title),
           '@subject "%s"' % a.subject,
           '@author "AERA | Erez Yaary"',
           '@$VER: %s.HYP %s' % (name, version),
           "@width %d" % WIDTH,
           "@inputenc atarist",
           "@charset atarist",
           "@autorefoff"]
    if a.help_page:
        out.append('@help "%s"' % a.help_page)
    out += ['@options "+z"',		# and NAME.REF, for ST-Guide's index
            "",
            '@node "Main" "%s %s"' % (app, version),
            "@{U}%s@{u}" % title, ""]
    out += page(sections[0][1], anchors)
    out += ["", "@{B}Contents@{b}", ""]
    out += ['  @{"%s" LINK "%s"}' % (t, t) for t, _ in sections[1:]]
    out += ["@endnode", ""]
    for t, body in sections[1:]:
        out += ['@node "%s" "%s: %s"' % (t, app, t), "@{U}%s@{u}" % t, ""]
        out += page(body, anchors)
        out += ["", '@{"Contents" LINK "Main"}', "@endnode", ""]
    with open(dst, "w", encoding="latin-1", newline="\r\n") as f:
        f.write(atari("\n".join(out)) + "\n")


if __name__ == "__main__":
    main()
