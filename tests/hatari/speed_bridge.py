#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The demo bridge for speed.py: a long conversation at the top of
Recents ("Long conversation", 40 turns, about 40 KB of paragraphs, lists
and code), and a long reply to anything sent, streamed in small pieces
as claude.ai does. The arguments are claude_bridge.py's (the rig gives
--tcp: ClaudeST reaches it over STinG, through common's FAKESTNG.PRG).
FAKESTNG's tunnel paces what goes to the Atari, as a network would."""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "gateway", "claude-st", "bridge"))
import backends  # noqa: E402
import claude_bridge  # noqa: E402

PARA = ("The 68000 reads its instructions a word at a time, so a loop that "
        "fits the prefetch runs faster than one that doesn't, and a "
        "**movem.l** moves thirteen registers in one instruction. On the ST "
        "the screen shares the bus with the processor, and every access to "
        "it waits for the shifter's slot.")
CODE = ("```asm\n"
        "    movea.l screen,a0\n"
        "    move.w  #7999,d0\n"
        ".loop:\n"
        "    clr.l   (a0)+\n"
        "    dbra    d0,.loop\n"
        "```")
LIST = ("- keep the loop in the prefetch\n"
        "- use the quick forms (`moveq`, `addq`)\n"
        "- draw only what changed")


def answer(n):
    return "\n\n".join(["%d. %s" % (n, PARA), PARA, LIST, CODE, PARA])


REPLY = "\n\n".join([PARA, LIST, CODE, PARA, PARA, LIST, PARA, CODE, PARA])


class SpeedBackend(backends.DemoBackend):
    def __init__(self):
        super().__init__()
        msgs = []
        for n in range(1, 41):
            msgs += [("U", "Question %d: how do I make this loop faster?" % n), ("A", answer(n))]
        self.chats = {"long": {"title": "Long conversation", "project": None, "messages": msgs,
                               "updated": time.time()}, **self.chats}
        for n in range(1, 31):      # Recents longer than it shows: it scrolls
            self.chats["more%d" % n] = {"title": "Older chat %d" % n, "project": None,
                                        "messages": [("U", "Question %d" % n), ("A", PARA)],
                                        "updated": time.time() - n * 3600}

    def send(self, chat_id, project_id, text, on_delta, files=None):
        for i in range(0, len(REPLY), 12):
            on_delta(REPLY[i:i + 12])
            time.sleep(0.04)
        c = self.chats[chat_id or "long"]
        c["messages"] += [("U", text), ("A", REPLY)]
        return chat_id or "long", c["title"]


backends.DemoBackend = SpeedBackend
if __name__ == "__main__":
    claude_bridge.main()
