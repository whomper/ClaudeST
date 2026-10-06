#!/usr/bin/env python3
"""The bridge with a scripted "showcase" account, for recording demo videos.

It has a realistic set of chats and projects, and answers the question
typed in the video with a pre-written reply, streamed at a natural pace.
Nothing here talks to claude.ai: it is for screen recordings only.

    reel_bridge.py --pipe FROM_ST TO_ST
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bridge"))
import backends  # noqa: E402
import claude_bridge  # noqa: E402

CLEAR_SCREEN = (
    "Use `movem.l`: one instruction stores 13 registers, 52 bytes. Fill "
    "them with zeros and write backwards from the end of the screen:\n\n"
    "```asm\n"
    "    movem.l zeros(pc),d1-d7/a1-a6\n"
    "    movea.l screen,a0\n"
    "    lea     32000(a0),a0   ; screen end\n"
    "    move.w  #614,d0        ; 615 x 52\n"
    ".loop:\n"
    "    movem.l d1-d7/a1-a6,-(a0)\n"
    "    dbra    d0,.loop\n"
    "    movem.l d1-d5,-(a0)    ; last 20\n"
    "zeros:  dcb.l   13,0\n"
    "```\n\n"
    "Each loop clears 52 bytes, so it runs just 615 times.\n")

ANSWER = (
    "Plenty of reasons, but three stand out:\n\n"
    "1. **MIDI built in.** Studios ran on it: Cubase started on the ST, and "
    "some musicians still use one today.\n"
    "2. **GEM.** A clean desktop and a crisp 640x400 monochrome screen made it "
    "a serious work machine for its price.\n"
    "3. **The scene.** Demos, games and coders who never stopped making new "
    "things for it.\n\n"
    "And now it can talk to me, too.")


class ShowcaseBackend(backends.DemoBackend):
    name = "claude.ai"

    def __init__(self):
        super().__init__()
        self.projects = {"p1": {"name": "Falcon restoration"},
                         "p2": {"name": "Demoscene intro"},
                         "p3": {"name": "Music studio"}}
        chats = [
            ("r1", "68000: fastest way to clear the screen", "p2",
             "What's the fastest way to clear the screen on a 68000?", CLEAR_SCREEN),
            ("r2", "Recapping a Falcon 030 motherboard", "p1",
             "Which capacitors should I replace first on a Falcon 030?",
             "Start with the electrolytics near the power input and the audio "
             "section: they age the most. Note each value and voltage before "
             "you desolder, and check the polarity markings twice."),
            ("r3", "Cubase on the Atari ST: getting started", "p3",
             "How do I set up Cubase on my ST?",
             "Connect MIDI OUT to your synth's MIDI IN, load Cubase, and pick "
             "the ST as the sync source. Then record a track in the Arrange "
             "window."),
            ("r4", "Plan a retro computing meetup", None,
             "Help me plan a small retro computing meetup.",
             "Pick a venue with plenty of power sockets, ask everyone to bring "
             "one machine, and plan one short talk per hour."),
            ("r5", "מתכון לחלה לשבת", None,
             "מתכון פשוט לחלה?",
             "קמח, שמרים, ביצה, סוכר, שמן ומים. לשים, להתפיח שעה, לקלוע ולאפות "
             "ב-180 מעלות כחצי שעה."),
            ("r6", "Explain GEM to a ten-year-old", None,
             "Explain GEM to a ten-year-old.",
             "GEM is the part of the computer that draws windows, icons and "
             "menus, so you can point and click instead of typing commands."),
        ]
        self.chats = {cid: {"title": title, "project": proj,
                            "messages": [("U", q), ("A", a)]}
                      for cid, title, proj, q, a in chats}
        self.chats["r1"]["model"] = ("claude-opus-5-5", "high")
        self.code = {
            "session_r1": {"title": "Add a Code view to Claude ST", "model": "claude-opus-5-5",
                           "messages": [("U", "Add a Code entry after Chats"),
                                        ("A", "[Read: st/claude.c]\n\nDone: Code lists your "
                                              "sessions and opens them like chats.")]}}

    def whoami(self):
        return "claude.ai"

    def send(self, chat_id, project_id, text, on_delta):
        if not chat_id:
            chat_id = "r%d" % (len(self.chats) + 1)
            self.chats = {chat_id: {"title": "Why the Atari ST is still loved",
                                    "project": project_id, "messages": []}, **self.chats}
        time.sleep(1.6)                       # Claude thinking
        reply = ANSWER if "loved" in text.lower() else (
            "That's a great question for an Atari. Here's a short answer: it "
            "depends on your machine, but start simple and build from there.")
        words = reply.split(" ")
        for i, w in enumerate(words):
            on_delta(w + (" " if i < len(words) - 1 else ""))
            time.sleep(0.07)
        c = self.chats[chat_id]
        c["messages"] += [("U", text), ("A", reply)]
        return chat_id, c["title"]


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] != "--pipe":
        sys.exit(__doc__)
    import logging
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
    link = claude_bridge.PipeLink(sys.argv[2], sys.argv[3])
    try:
        claude_bridge.Session(link, ShowcaseBackend()).run()
    except (KeyboardInterrupt, EOFError):
        pass
