# The build story of Claude ST

A claude.ai client for the Atari ST and Falcon, written in one week by
talking to Claude in Claude Code on claude.ai. Every line of code came
from conversation: Erez described what he wanted, tried each build on a
real Falcon, and reported back with photos of the screen.

![Claude ST on an Atari](docs/st-high-mono.png)

## The first prompt

> Let's build a Claude.ai Atari ST/Falcon application. It should allow me
> native access to my Claude.ai account. I should see on the left the
> previous chats plus the usual options like New, Projects, Artifacts etc
> and to the right side the large display of active chat.

That was 2 October 2026. About half an hour later the first version ran
in an emulator with sample chats. That same evening Erez had it running
on his Falcon against his real account: "I gave it a try and it works,
quite well dare I say."

## The numbers

- **7 days**, from the first prompt to the latest release
- **About 80 prompts** from Erez, and **53 commits**
- **About 5,000 lines of C** for the Atari and **4,000 lines of Python**
  for the bridge, plus **82 automated tests**
- **CLAUDE.PRG is 46 KB**, smaller than one photo of the screen
- Runs on an **8 MHz 68000** from 1985, with 1 MB of RAM

## How it works

```
Atari Falcon (CLAUDE.PRG)  -- WiFi -->  Raspberry Pi (bridge)  -- HTTPS -->  claude.ai
```

An 8 MHz 68000 can't do modern encryption, so a small Python program,
the bridge, runs on a Raspberry Pi. Erez already had a Pi doing the same
job for his Atari email and web access. The Atari does all the drawing,
word wrapping and right-to-left text itself. The bridge does the
encryption and turns claude.ai's large replies into short text lines.

His second request set the tone: "I do not want any wire, only wireless
connection." So the Falcon talks to the Pi over WiFi through STinG, the
Atari's classic network stack, and reconnects by itself when the WiFi
drops.

## Milestones

**Day 1 (2 October): from nothing to a working app**
- The GEM application: menu bar, sidebar, chat pane, reply line, and
  replies streaming in as Claude writes them
- The Raspberry Pi bridge, installed as a service with one command
- Wireless: STinG networking instead of a serial cable
- The code moved to its own GitHub repository,
  [whomper/atari_claude](https://github.com/whomper/atari_claude)
- Right-click menus like the website's (Pin, Rename, Move to project,
  Archive, Delete) and a sidebar you can drag wider
- **Hebrew, right to left**, on a computer from 1985
- A Hebrew keyboard mode (F10) for typing Hebrew on the Falcon
- A one-command way to try it all in the Hatari emulator

**Day 2: polish**
- Proper cursor editing in the reply line
- A modern About box with a new icon, in black and white and in 16
  colours, plus desktop icon files
- Artifacts: listed, opened and saved to the Atari's disk
- Every click acts on mouse-down, like the real website

**Day 3: matching claude.ai**
- Choose the **model and effort** from the Atari; each chat keeps its own
- An **Account page**: your plan, usage and when it resets
- Tooltips for text that doesn't fit
- **Claude Code sessions** in the sidebar: read them and reply from the
  Atari
- A short README with commands you can paste straight into a terminal,
  and a full user guide

**Days 4 to 6: features and a demo video**
- New project, straight from the Move to project menu
- A **demo video**, recorded automatically in the emulator: a Falcon in
  16 colours boots to the desktop, the Claude icon is double-clicked, and
  a question is typed and answered
  ([docs/demo](docs/demo))
- **Date headings** in the chat list (Today, Yesterday, Older), as soon
  as Erez noticed claude.ai had added them to its website

**Days 6 to 7: keeping up with claude.ai**
- Chats that claude.ai moved to its new chat system work again
- Faster Artifacts, newest first, with date headings

## Interesting aspects

- **An AI building a client for itself.** Claude wrote an app that lets
  a 1980s computer talk to Claude, working inside Claude Code on
  claude.ai. It's a claude.ai client built by talking to claude.ai.
- **Real hardware in the loop.** Claude never touched the Falcon. Erez
  copied each build over, tried it, and sent a phone photo of the
  screen. Claude read the photos, found the problems and sent a fix,
  usually the same hour.
- **Hebrew on an Atari.** Each paragraph takes its direction from its
  first letter. English words and numbers inside Hebrew keep their order.
  Vowel points and invisible direction marks are dropped, because the
  Atari font doesn't have them.
- **Following a moving target.** claude.ai has no public interface for
  personal accounts, so the bridge uses the same private interface as the
  website. During the week claude.ai changed twice under the app: date
  headings appeared, and some chats moved to a new chat system. The app
  kept up both times.
- **Tested without the hardware.** Claude ran the app in the Hatari
  emulator, typed into it with simulated keystrokes and read the
  screenshots to check its own work. A small fake network stack let it
  test the WiFi code over an emulated serial port.
- **A "who is Iguana necktie?" moment.** The usage page suddenly showed a
  limit called "Iguana necktie", reset in 31 days. It turned out to be
  claude.ai's internal code name for a promotional credit for cloud
  sessions. Erez spotted the matching "Cloud session credits" on the
  website, and the label was fixed.
- **A demo video with no camera.** The reel was recorded entirely in the
  emulator by a script: boot, double-click, chat, type, answer.

## The main bugs and how we fixed them

**Artifacts crashed after adding Pin.** Pinning added a third value to
each chat in the list, but the code that scans chats for artifacts still
expected two. The emulator's demo data didn't have pins, so the tests
missed it. Fixed, and a test now covers it.

**Only the top two artifacts would open.** The Atari stores each list
item's ID in 39 characters. Artifacts that Claude creates as files use
their full file path as their ID, which is longer, so the Atari cut it
short and the bridge didn't recognise it. Now any long ID gets a short
stand-in that the bridge translates back.

**Missing artifacts, such as two cover letters.** claude.ai stores
artifacts in three different ways over its history: an artifact tool,
tags in older chats, and files Claude creates with scripts. The bridge
only knew one. It now finds all three.

**The wrong effort level.** A chat set to High showed as Medium.
claude.ai keeps the chat's effort somewhere else than the bridge was
looking. Found by comparing a chat's stored settings, field by field,
with what the website shows.

**"HTTP 409", then "HTTP 429".** Some old chats suddenly refused new
messages. claude.ai's error text was hidden because the reply was a
stream, so the bridge first had to read the stream to see the reason:
"This chat is available in the new Claude experience". claude.ai had
moved those chats to its new chat system. Retrying made it worse (HTTP
429, too many requests).
- **The detective work:** Erez opened Safari's network inspector and
  shared what the website loads for a moved chat. That revealed each
  moved chat's link to a session in claude.ai's newer system, the same
  kind of session the bridge already used for Claude Code. Moved chats
  now continue there.
- **A second twist:** the bridge kept asking claude.ai every 2 seconds
  for the end of each reply, for 15 minutes. Moved chats never send the
  "finished" signal it was waiting for, and that many requests got the
  account rate-limited. It now stops 8 seconds after the reply goes
  quiet.

**Two bombs on the Falcon.** A one-line change made the Falcon crash
before its window even opened: two bombs, the Atari's sign of a bus
error. The same build ran fine in the emulator, on an emulated Falcon
and ST. The change was rolled back the same afternoon. The feature (start
with no chat selected) was then rebuilt a different way, inside the
Atari program itself, and that version works.

**"Why can't you tell a fresh start from a reconnect?"** After a
restart, the list highlighted the last chat while the chat pane was
empty. Claude's first fix forgot the chat on every reconnect too. Erez
pushed back, rightly: the Atari knows when it has just started. Now the
Atari ignores the highlight until you open something, and sends its
first message to a new chat.

## What Claude ST does now

- **Your real claude.ai account** on an Atari ST, STE, Mega, TT or
  Falcon, over WiFi or a serial cable
- **Chats** with date headings, pinned chats first, and search
- **Projects:** open them, start chats in them, create new ones
- **Claude Code sessions:** read them and reply
- **Artifacts:** view them and save them to disk, Word documents included
- **Model and effort** per chat, from a small chip in the title bar
- **Account page:** plan, usage and reset times
- **Right-click menus:** Pin, Rename, Move to project, Archive, Delete
- **Hebrew:** right-to-left display and a Hebrew keyboard
- **Monochrome or colour,** any screen from 640×200
- **Replies stream in** as Claude writes them

The code, setup guide and demo videos are on GitHub:
[github.com/whomper/atari_claude](https://github.com/whomper/atari_claude).

## Pictures and clips for posts

| File | Shows |
|---|---|
| [docs/demo/claude-st-reel.mp4](docs/demo/claude-st-reel.mp4) | Vertical reel: Falcon desktop, launch, a question answered |
| [docs/demo/claude-st-falcon.mp4](docs/demo/claude-st-falcon.mp4) | The same demo in 4:3 |
| [docs/st-high-mono.png](docs/st-high-mono.png) | The main window in classic black and white |
| [docs/ste-medium-colour.png](docs/ste-medium-colour.png) | The same on a colour STE |
| [docs/hebrew.png](docs/hebrew.png) | Hebrew, right to left |
| [docs/code.png](docs/code.png) | A Claude Code session on the Atari |
| [docs/account.png](docs/account.png) | Plan and usage |
| [docs/model-menu.png](docs/model-menu.png) | Picking the model and effort |
| [docs/about-colour.png](docs/about-colour.png) | The About box with the 16-colour icon |

Claude ST is an unofficial client, not affiliated with Anthropic or
Atari.
