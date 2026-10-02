"""Text helpers for the Atari side: the ST character set and a light
Markdown-to-protocol formatter that works on streamed deltas."""
import re
import unicodedata

# Atari ST character set, 0x80-0xFF
_ST_HIGH = (
    "ÇüéâäàåçêëèïîìÄÅ"
    "ÉæÆôöòûùÿÖÜ¢£¥ßƒ"
    "áíóúñÑªº¿⌐¬½¼¡«»"
    "ãõØøœŒÀÃÕ¨´†¶©®™"
    "ĳĲאבגדהוזחטיכלמנ"
    "סעפצקרשתןךםףץ§∧∞"
    "αβΓπΣσµτΦΘΩδ∮ϕ∈∩"
    "≡±≥≤⌠⌡÷≈°∙·√ⁿ²³¯"
)
_TO_ST = {ch: 0x80 + i for i, ch in enumerate(_ST_HIGH)}
_FROM_ST = {0x80 + i: ch for i, ch in enumerate(_ST_HIGH)}

# common characters the ST font lacks
_SUBST = {
    "‘": "'", "’": "'", "‚": "'", "“": '"', "”": '"',
    "„": '"', "–": "-", "—": "--", "―": "--", "…": "...",
    "•": "∙", "●": "∙", "◦": "∙", "‣": "∙",
    " ": " ", " ": " ", "​": "", "‍": "", "️": "",
    "→": "->", "←": "<-", "⇒": "=>", "≤": "≤",
    "✓": "v", "✔": "v", "✗": "x", "✘": "x", "×": "x",
    "™": "™", "€": "EUR", "µ": "µ", "−": "-",
    "─": "-", "│": "|", "┌": "+", "┐": "+", "└": "+",
    "┘": "+", "├": "+", "┤": "+", "┬": "+", "┴": "+",
    "┼": "+", "═": "=", "·": "·",
}


def to_atari(text: str) -> bytes:
    out = bytearray()
    for ch in text:
        ch = _SUBST.get(ch, ch)
        for c in ch:
            o = ord(c)
            if o == 9:
                out += b"    "
            elif 32 <= o < 127:
                out.append(o)
            elif o < 32 or o == 127:
                continue
            elif c in _TO_ST:
                out.append(_TO_ST[c])
            else:
                base = unicodedata.normalize("NFKD", c)
                base = "".join(b for b in base if not unicodedata.combining(b))
                if base and all(32 <= ord(b) < 127 for b in base):
                    out += base.encode("ascii")
                elif unicodedata.category(c).startswith("S"):
                    continue  # emoji and pictographs: drop
                else:
                    out.append(ord("?"))
    return bytes(out)


def from_atari(data: bytes) -> str:
    return "".join(_FROM_ST.get(b, chr(b)) if b >= 0x80 else chr(b) for b in data)


_LINK = re.compile(r"\[([^\]]+)\]\((https?://[^)\s]+)\)")
_BOLD = re.compile(r"\*\*|__")
_ITALIC = re.compile(r"(?<![\w*])\*(?=\S)([^*\n]+?)(?<=\S)\*(?![\w*])")
_HEADING = re.compile(r"^(#{1,6})\s+(.*)$")
_BULLET = re.compile(r"^(\s*)[-*+]\s+(.*)$")
_FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
_HR = re.compile(r"^\s*([-*_])(\s*\1){2,}\s*$")


def _inline(s: str) -> str:
    s = _LINK.sub(r"\1 <\2>", s)
    s = _BOLD.sub("", s)
    s = _ITALIC.sub(r"\1", s)
    return s


class Formatter:
    """Turns (possibly streamed) Markdown into protocol ops:
    ("P", text) append text, ("B",) line break, ("H", text) heading line."""

    def __init__(self):
        self.pending = ""
        self.at_line_start = True
        self.in_code = False

    def _line(self, line: str):
        """A complete line (without newline)."""
        m = _FENCE.match(line)
        if m:
            self.in_code = not self.in_code
            if self.in_code:
                return [("P", "[" + (m.group(1) or "code") + "]"), ("B",)]
            return [("P", "[end]"), ("B",)]
        if self.in_code:
            return [("P", "  " + line), ("B",)]
        m = _HEADING.match(line)
        if m:
            return [("H", _inline(m.group(2)))]
        if _HR.match(line):
            return [("P", "-" * 20), ("B",)]
        m = _BULLET.match(line)
        if m:
            return [("P", m.group(1) + "- " + _inline(m.group(2))), ("B",)]
        return [("P", _inline(line)), ("B",)]

    def feed(self, text: str):
        ops = []
        self.pending += text
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            if self.at_line_start:
                ops += self._line(line)
            else:
                ops += [("P", _inline(line)), ("B",)]
            self.at_line_start = True
        # stream the unfinished line once we know it isn't special markup
        p = self.pending
        if p and not self.in_code:
            if self.at_line_start:
                m = re.match(r"^(\s*)[-*+]\s+(?=\S)", p)
                if m and not _HR.match(p):
                    ops.append(("P", m.group(1) + "- "))
                    p = self.pending = p[m.end():]
                    self.at_line_start = False
                else:
                    s = p.lstrip()
                    if not s or s[0] in "#-*+`_>" or len(p) < 4 and s[0].isdigit():
                        return ops
            cut = len(p)
            while cut > 0 and p[cut - 1] in "*_`[":
                cut -= 1
            i = p.rfind("[", 0, cut)
            if i >= 0 and cut - i < 300 and not re.match(r"\[[^\]]*\]\([^)]*\)", p[i:cut]):
                cut = i  # possibly a link: wait for the rest of it
            if cut > 0:
                ops.append(("P", _inline(p[:cut])))
                self.pending = p[cut:]
                self.at_line_start = False
        return ops

    def finish(self):
        ops = self.feed("\n") if self.pending else []
        self.in_code = False
        return ops


def format_text(text: str):
    f = Formatter()
    return f.feed(text) + f.finish()
