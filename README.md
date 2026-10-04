# Claude ST

<img src="docs/icon.png" width="96" align="right" alt="Claude ST icon: an Atari SM124-style monitor showing a spark and a prompt">

A Claude.ai client for the Atari ST, STE, Mega ST/STE, TT and Falcon,
running as a native GEM application.

![Claude ST in ST high resolution](docs/st-high-mono.png)

The window works like claude.ai. The sidebar on the left has **New chat**,
**Search**, **Chats**, **Projects** and **Artifacts**, followed by your
recent conversations. The open conversation fills the right side, with
the reply line at the bottom. Replies stream in as Claude writes them.

![Claude ST in STE medium resolution, inside a project](docs/ste-medium-colour.png)

## How it works

```
 Atari ST / Falcon                         Raspberry Pi (or any computer)
┌──────────────────┐  WiFi/LAN via STinG  ┌────────────────────┐   HTTPS   ┌───────────┐
│ CLAUDE.PRG (GEM) │ ◄──────────────────► │  claude_bridge.py  │ ◄───────► │ claude.ai │
└──────────────────┘  or a serial cable   └────────────────────┘           └───────────┘
```

An 8 MHz 68000 can't do modern TLS, so a small Python **bridge** on a
modern computer handles HTTPS. The Atari reaches the bridge over the
network through **STinG** (no cable at all) or over the serial port.
They exchange simple text lines (see [PROTOCOL.md](PROTOCOL.md)). The
bridge converts between Unicode and the Atari character set and turns
Markdown into something a monochrome 80-column screen can show. The
Atari does all the drawing and word wrapping itself.

The Atari program is about 21 KB. It's written in freestanding C with
its own small AES/VDI/STinG bindings, so it doesn't need MiNTLib or a
resource file. It runs on plain TOS 1.0 through 4.x, EmuTOS and MiNT, in
any resolution of at least 640×200: ST high, ST medium, TT and Falcon
modes.

## Installing

The setup for your machines (Atari at 192.168.1.20, Pi at
192.168.1.10) is fully wireless. Give both a fixed address (a DHCP
reservation in your router) so they keep these IPs.

### 1. The Pi: install the gateway

The Pi runs the bridge permanently, so you never start anything by hand.
It can share a Pi you already use for other jobs: the installer adds
only its own user, `/opt/claude-st`, `/etc/claude-st` and one service
called `claude-st`.

```sh
ssh pi@192.168.1.10
git clone https://github.com/whomper/Atari_claude.git   # private repo: sign in with a GitHub token
cd Atari_claude/pi
sudo ./install.sh --network --atari 192.168.1.20   # asks for your claude.ai sessionKey (hidden)
```

The Pi now listens on TCP port 2323 and accepts connections **only from
192.168.1.20**. If `ufw` is enabled, the installer opens the port for
that address. The service starts at every boot. If claude.ai is
unreachable or the session key has expired, the error appears in the
Claude ST window, so you can see what's wrong from the Atari.

How to get the session key is described under
[the claude.ai backend](#3b-or-on-any-computer-the-bridge-by-hand) below.

| Task | Command on the Pi |
|------|-------------------|
| See whether it's running | `systemctl status claude-st` |
| Watch the log | `journalctl -u claude-st -f` |
| New session key (when claude.ai logs you out) | `sudo ./install.sh --set-key` |
| Use the Anthropic API instead | `sudo ./install.sh --backend api` |
| Try the demo backend | `sudo ./install.sh --backend demo` |
| Change the Atari's address | `sudo ./install.sh --atari 192.168.1.21` |
| Switch to a serial cable | `sudo ./install.sh --port auto` |
| Update after a `git pull` | `sudo ./install.sh` |
| Remove | `sudo ./install.sh --uninstall` |

Settings are stored in `/etc/claude-st/claude-st.env`, readable only by
root and the service. The session key lives there, not in the
repository.

### 2. The Atari

1. Make sure **STinG** is loaded and the network works, for example by
   pinging the Pi from STinG's tools.
2. Copy `st/CLAUDE.PRG` **and** `st/CLAUDE.INF` into the same folder.
   `CLAUDE.INF` holds the gateway address:
   ```
   tcp 192.168.1.10 2323
   ```
3. Double-click `CLAUDE.PRG`. The status line at the bottom left shows
   *Connecting to 192.168.1.10*, then *Online*.

To point Claude ST at a different gateway, type
`/connect 192.168.1.10` (or `/connect host:port`) in the reply line.
It's saved to `CLAUDE.INF`. **Options ▸ Network (STinG)** and **Options ▸
Serial port** switch between the two links. If the Pi restarts or the
WiFi drops, Claude ST reconnects by itself.

Claude ST needs about 250 KB of free RAM, and works with less by keeping
a shorter scrollback.

### Alternative: a serial cable

If you don't have STinG, use the serial link instead. Delete
`CLAUDE.INF` (or choose **Options ▸ Serial port**) and install the Pi
with `sudo ./install.sh --port auto`.

| Machine | Port |
|---------|------|
| ST, STE, Mega ST/STE | the 25-pin **Modem** (RS-232) port |
| TT | **Modem 1** |
| Falcon 030 | the 9-pin **Modem** port |

Use a null-modem cable and a **USB-to-RS-232 adapter** on the Pi. Don't
wire the Atari straight to the Pi's GPIO pins: the Atari's port uses
±12 V RS-232 signals, and they will damage the Pi. If you want to use
the GPIO UART instead, put a MAX3232 level-shifter board in between,
turn off the serial console in `raspi-config`, and use
`--port /dev/serial0`. The default speed is 19200 baud, 8N1. You can
change it under **Options**; give the Pi the same `--baud`.

A **WiFi modem** on the serial port also works, if it runs in
transparent TCP mode. Point it at `pi-address:2323`, keep Claude ST on
the serial link, and install the Pi with
`--network --atari <the modem's IP>`.

### 3b. Or on any computer: the bridge by hand

```sh
cd bridge
pip install -r requirements.txt
```

There are three backends.

**Your claude.ai account** (the default). You get your real chats,
projects and artifacts, and new chats appear on claude.ai too.

1. Log in to claude.ai in a desktop browser.
2. Open the developer tools: *Application* (Chrome) or *Storage*
   (Firefox) → *Cookies* → `https://claude.ai`.
3. Copy the value of the `sessionKey` cookie. It starts with
   `sk-ant-sid`.

```sh
export CLAUDE_SESSION_KEY='sk-ant-sid01-...'
python3 claude_bridge.py --serial /dev/ttyUSB0          # Linux
python3 claude_bridge.py --serial /dev/tty.usbserial-X  # macOS
python3 claude_bridge.py --serial COM3                  # Windows
```

> **Heads-up.** claude.ai has no public API for personal accounts. This
> backend uses the same private endpoints as the claude.ai website, so it
> can stop working whenever claude.ai changes. The session key gives
> full access to your account, so keep it private and never commit it.
> If requests are blocked (HTTP 403), install `curl_cffi`, which is in
> `requirements.txt`. The bridge uses it automatically.

**The Anthropic API.** This backend is official and stable, but chats
are stored on your computer, not in your claude.ai account. It uses
`claude-opus-5-5` by default (change it with `--model`), with
server-side refusal fallbacks turned on.

```sh
export ANTHROPIC_API_KEY=sk-ant-api...
python3 claude_bridge.py --backend api --serial /dev/ttyUSB0
```

History is stored in `~/.claude-st/chats/`. To add projects, create
`~/.claude-st/projects.json`:

```json
[{"id": "retro", "name": "Retro coding", "instructions": "Answer as a 68000 expert."}]
```

**Demo.** Uses built-in sample chats and canned replies, with no
network. Use it to check the cable and the Atari side:

```sh
python3 claude_bridge.py --backend demo --serial /dev/ttyUSB0
```

## Using Claude ST

| Action | Mouse | Keyboard |
|--------|-------|----------|
| New chat | **+ New chat** | `F1` / `Ctrl+N` |
| Recent chats | **Chats** | `F2` |
| Claude Code sessions | **Code** | `F6` |
| Projects | **Projects** | `F3` |
| Artifacts | **Artifacts** | `F4` |
| Search chat titles | **Search** | `F5` / `Ctrl+F` |
| Open a sidebar item | click it | `Tab` / `Shift+Tab` to pick, then `Return` |
| Item menu: Open, Pin, Rename, Move to project, Archive, Delete | **right-click** the chat or project | `Tab` to pick it, then `Insert`; arrows + `Return` in the menu, `Esc` closes |
| Resize the sidebar | drag the divider line (the pointer turns into a hand) | — |
| Page through the sidebar list | the ↑ ↓ arrows next to the list title | — |
| Send a message | — | type, then `Return` |
| Your plan, usage and account | click the status line at the bottom left, or Options ▸ Account | `F8` |
| Choose the Claude model and its effort | click the model chip at the right of the chat's title bar | `F9` |
| Scroll the conversation | scroll bar, or click the upper/lower half | `↑` `↓`, `Shift+↑/↓` by page, `Clr/Home` top, `Shift+Clr/Home` bottom |
| Type in Hebrew (on/off) | Options ▸ Hebrew keys | `F10` |
| Edit the reply line or a dialog field | — | `←` `→` move, `Shift+←/→` start/end, `Ctrl+←/→` by word, `Backspace`/`Delete` |
| Clear the input | — | `Esc` / `Undo` |
| Reconnect to the bridge | File ▸ Reconnect | `Ctrl+R` |
| Set the gateway address | Options ▸ Network (STinG) | type `/connect 192.168.1.10` |
| Use the serial cable instead | Options ▸ Serial port | type `/serial` |
| About | Desk ▸ About | `Help` |
| Quit | close box / File ▸ Quit | `Ctrl+Q` |

### Code (Claude Code sessions)

**Code**, below Chats (or `F6`), lists your Claude Code sessions from
claude.ai/code, newest first.

- **Open a session** to read it: your messages and Claude's replies,
  with each tool call as a one-line note such as `[Bash: make -C st]`.
  Tool output is left out.
- **Reply** by typing below. The message goes to that session, and
  Claude Code's replies appear as it works. If it is still working after
  15 minutes, open the session again later to see the rest.
- **Right-click** a session for **Open**, **Rename…** and **Archive**.
- The model chip shows the session's own model. Choosing another one
  asks claude.ai to switch that session's model; it doesn't change the
  model for your new chats.

![A Claude Code session](docs/code.png)

claude.ai/code keeps sessions in its own sessions store, separate from
chats. The bridge uses the same unofficial interface the web page does,
so it can stop working if claude.ai changes it. If **Code** shows an
error, run this on the Pi and send the output. It shows only session
titles and counts:

```sh
sudo -u claude-st bash -c 'set -a; . /etc/claude-st/claude-st.env; /opt/claude-st/venv/bin/python /opt/claude-st/bridge/claude_bridge.py --probe-code'
```

### Tooltips, status line and notices

- **Tooltips:** rest the mouse for a moment on a chat, project or
  artifact whose name is cut off in the sidebar, on a cut-off list
  heading, or on the chat's title when it doesn't fit. A tooltip shows
  the whole text and goes away when the mouse moves.

  ![A tooltip](docs/tooltip.png)

- **Switching lists:** clicking **Chats**, **Projects** or **Artifacts**
  empties the list at once. It shows *Loading...*, or the bridge's
  progress, until the new list arrives.
- **Status line:** the line at the bottom of the sidebar shows only the
  connection: *Connecting…*, *Online: claude.ai*, *Offline: …*.
- **Notices:** passing messages, such as *Loading chat...* or a finished
  *Renamed*, appear in grey at the right of the title bar for a few
  seconds.

### Account, plan and usage

Click the status line at the bottom of the sidebar, choose **Options ▸
Account…**, or press `F8`. The conversation pane then shows:

- **Plan:** Free, Pro, Max (5x or 20x usage), Team or Enterprise.
- **Usage:** each of your limits as a meter, for example the current
  5-hour session and this week's usage, and any included cloud session
  credit, with how long until it resets (or expires)
  and the day and time it resets, in the Pi's time zone.
- **Account:** your name, email, organization, and since when you've had
  the account.

![The Account page (demo data)](docs/account.png)

This comes from the same private claude.ai endpoints as the rest of the
claude.ai backend (`/api/account` and the organization's usage), so it
can stop working if claude.ai changes them. With the API backend, the
page shows the tokens used since the gateway started; your bill is in
the Claude Console.

### Model and effort

The right end of the chat's title bar shows the model and effort of the
open chat in small type, for example **Opus 5.5 · Med**. Click it (or press `F9`)
for a menu: the models first, then the effort levels the chosen model
supports (**Low**, **Medium**, **High**, **Extra high**, **Max**). The
current ones are ticked.

![The model menu](docs/model-menu.png)

**Each chat keeps its own model**, as on claude.ai:

- Opening a chat switches the chip to the model that chat uses, even an
  older one that is no longer offered (such as Sonnet 4; it is added to
  the menu while you need it). Older models have no effort setting.
- Changing the model while a chat is open changes it for that chat, and
  it also becomes the model for your next new chats.
- A new chat, and an old chat with no model recorded, use the model
  you chose last. That choice is saved in `CLAUDE.INF` and applied again
  whenever Claude ST connects to the gateway.

The models offered are Opus 5.5, Fable 5.1, Sonnet 5.5, Haiku 4.5,
Opus 5 and Opus 4.8. Haiku 4.5 has no effort setting, so its effort
items disappear. Effort controls how much Claude thinks before it
answers: higher is slower and more thorough. Each model's default is
**Medium** for Opus 5.5 and **High** for the others.

- **API backend:** the model and effort are sent with every request and
  stored with each chat.
- **claude.ai backend:** a chat's model is read from claude.ai when you
  open it. New chats start on **Default model**, your account's own
  choice, until you pick one. Picking a model makes the bridge ask
  claude.ai for it, if your plan includes that model. claude.ai has no
  documented effort setting; the bridge asks for it anyway, and if
  claude.ai refuses, it sends the message without it and says so at the
  top of the reply.

Right-click a chat for **Open, Pin/Unpin, Rename…, Move to project…,
Delete…**, or a project for **Open, Pin/Unpin, Rename…, Archive,
Delete…**. The item the menu applies to is highlighted before the menu opens, and
stays highlighted while the menu, a confirmation or a rename is in
progress. The open chat is outlined meanwhile, so the two can't be
confused. The menu opens just below the item, or above it near the
bottom of the list. Pinned items move to the top of the list and show a
small diamond. Rename opens a small dialog with the current name in a text field:
edit it anywhere (the arrow keys move the cursor) and press `Return` or
click **Rename**, or press `Esc`/`Undo` or
click **Cancel**. `Clr/Home` clears the field and `F10` switches it to
Hebrew typing. Delete always asks first. The
sidebar width you drag to is remembered in `CLAUDE.INF`.

On claude.ai, these actions use the same private endpoints as the
website, like the rest of the claude.ai backend. If one stops working,
the Atari shows claude.ai's error in the chat pane. With `--backend api`
they act on the chats and projects stored on the Pi.

**Hebrew** and other right-to-left text is laid out by Claude ST itself,
because TOS has no bidirectional text support. A paragraph whose first
letter is Hebrew is right-aligned and reads right to left. English words
and numbers inside it keep their left-to-right order, and Hebrew phrases
inside English text are reversed in place. Brackets are mirrored. The
same applies to chat titles in the sidebar and title bar, and to the
reply line when you type Hebrew. The bridge sends the Atari's Hebrew
letters, drops vowel points (niqqud) and invisible direction marks, and
turns maqaf, geresh and gershayim into `-`, `'` and `"`. The Atari font
only has the plain letters.

**Typing Hebrew.** TOS has no Hebrew keyboard layout, so Claude ST has
its own: press `F10` (or choose **Options ▸ Hebrew keys**) and the
letter keys type Hebrew in the Israeli SI-1452 layout. `T` gives א, `A`
gives ש, `,` gives ת, `.` gives ץ, `/` gives a full stop, and `Q`/`W`
give `/` and `'`. An **HE** badge shows in the reply box while it's on.
Shift still types English capitals, and keys are mapped by position, so
any national Atari keyboard works. The setting is saved in `CLAUDE.INF`.

Switching area (**Search**, **Chats**, **Projects**, **Artifacts**, or
opening a project) clears the conversation pane and shows a hint for that
area, so nothing on screen belongs to the previous chat. What you type
next starts a new chat. After you open a project, the new chat is
created inside it.

Projects open as a list of their chats. A new chat started while a
project is open is created in that project. On claude.ai, artifacts are
stored inside conversations, so **Artifacts** lists the ones in your 100
most recent chats, newest first (while it looks, the list is empty and
counts the chats, "Scanning chat 12/100"; the bridge remembers chats it
has already scanned, so opening the list again is quick). It finds every form claude.ai has used:
artifact tool calls, `<antArtifact>` tags in older chats, files Claude
created, and files Claude made with a script, such as a `.docx` cover
letter. Opening one shows its source; a Word document shows its text.
Right-click it (or `Insert`) and choose **Save to disk…** to save it as a
file on the Atari. The GEM file selector suggests an 8.3 name such as
`SNAKE_GA.PY` or `COVER_LE.DOC`. Text is saved in the Atari character set
with CR/LF line ends; Word, PDF and other binary files are saved
unchanged. To look further back, add `--artifact-scan 300` to the
bridge's options.

If an artifact is missing, run this on the Pi with a word from the
chat's title. It shows where that chat is in your history and which
tools and files it uses, without any of its content:

```sh
sudo -u claude-st bash -c 'set -a; . /etc/claude-st/claude-st.env; /opt/claude-st/venv/bin/python /opt/claude-st/bridge/claude_bridge.py --probe cover'
```

## About box and icon

**Desk ▸ About Claude ST…** (or the `Help` key) shows the About box:

![About box](docs/about.png)

On a screen with 16 colours or more (Falcon and TT colour modes), the
About box shows a colour version of the icon: a putty-beige case like a
real Atari monitor, a dark CRT screen, the spark in a warm terracotta and
the prompt in green phosphor. Elsewhere it shows the black-and-white one.

![About box in colour, on a TT](docs/about-colour.png)

The icon is 32×32 one-bit pixel art: the silhouette of the ST's SM124
monochrome monitor, with a simple eight-ray spark and a GEM-style
prompt on its dark screen. It's original artwork, not either company's
logo. `tools/icon/make_icon.py` draws both versions and writes `st/icon.h`
and `st/icon16.h` (built into the program), `docs/icon.png` and
`docs/icon16.png`.

### Desktop icon files

The `icons/` folder has the icon in the formats Atari icon and resource
editors use, to give `CLAUDE.PRG` its own icon on the desktop:

| File | What it is |
|------|-----------|
| `CLAUDE.RSC` | GEM resource with both icons, each with a mask and the label "Claude ST": a black-and-white `G_ICON` and a 16-colour `G_CICON` |
| `CLAUDE.ICN` | the black-and-white image, ICN format |
| `CLAUDEMK.ICN` | its mask, ICN format |

Open `CLAUDE.RSC` in a resource editor (Interface, ORCS, RSM...) and copy
the icon into the desktop's icon file: `DESKICON.RSC` for black-and-white
icons, or `DESKCICN.RSC` for colour icons on TOS 4 (the Falcon). Then
install it for `CLAUDE.PRG` from the desktop. The colour icon uses the
standard 16 system colours, since desktop icons can't bring their own
palette. Where the screen has fewer colours, the AES shows the
black-and-white image instead.

`tools/icon/make_rsc.py` writes these files. `tools/icon/RSCTEST.PRG`
(`make` in that folder) loads `CLAUDE.RSC` with the AES and draws it, as
a check.

## Settings (CLAUDE.INF)

Claude ST saves its settings to `CLAUDE.INF`, next to `CLAUDE.PRG`, as
soon as you change them. You can also edit the file in any text editor:

| Line | Set by |
|------|--------|
| `tcp 192.168.1.10 2323` or `serial` | `/connect`, `/serial`, Options ▸ Network / Serial port |
| `baud 19200` (or 9600, 4800) | Options ▸ baud rate |
| `sidebar 240` | dragging the divider (width in pixels) |
| `keyboard hebrew` | F10 / Options ▸ Hebrew keys |
| `model claude-sonnet-5-5 max` | the model chip / F9 (model id, then effort) |

The window's size and position, and the chat that was open, aren't
saved: Claude ST always opens full-screen with a new chat.

## Building

You need any m68k GCC. A stock Debian/Ubuntu cross compiler works:

```sh
sudo apt install gcc-m68k-linux-gnu
cd st && make          # -> CLAUDE.PRG
```

The program is freestanding, with its own startup code, traps and libgcc
helpers. `tools/elf2tos.py` turns the ELF into a relocatable TOS program.
An `m68k-atari-mint` toolchain also works: `make CROSS=m68k-atari-mint-`.

## Trying it in Hatari

To run Claude ST in the [Hatari](https://hatari.tuxfamily.org/) emulator on
your own Mac or Linux computer:

```sh
git clone https://github.com/whomper/Atari_claude.git && cd Atari_claude
tools/hatari-test.sh                 # demo chats, no account needed
```

The script copies `CLAUDE.PRG` to an emulated hard disk and downloads
EmuTOS (a free TOS) the first time. It starts the bridge, then boots
Hatari straight into Claude ST, with the emulated serial port wired to
the bridge through two named pipes. To use your real claude.ai account:

```sh
pip install -r bridge/requirements.txt
export CLAUDE_SESSION_KEY='sk-ant-sid01-...'
tools/hatari-test.sh claudeai
```

`MACHINE=ste tools/hatari-test.sh` runs an STE in colour, and
`TOS=/path/to/tos.img` uses your own TOS image. Quitting Hatari stops the
bridge too. The script uses the serial link because Hatari can't emulate
a network card. It also works only on an ST, STE or TT: Hatari doesn't
connect the Falcon's serial port to the pipes.

## Testing without hardware

The bridge has offline tests:

```sh
cd bridge && python3 -m unittest -v
```

The screenshots above come from [Hatari](https://hatari.tuxfamily.org/)
running EmuTOS, with the emulated serial port connected to the bridge
through FIFOs:

```sh
python3 bridge/claude_bridge.py --backend demo --pipe st_out st_in &
hatari --machine st --mono --harddrive st/ --auto 'C:\CLAUDE.PRG' \
       --rs232-out st_out --rs232-in st_in
```

Hatari only connects the ST's MFP serial port this way, so use an ST,
STE or TT machine type. On a real Falcon, the Modem port works normally.

Hatari has no network card, so the STinG code path is tested with
`tools/fakesting/FAKESTNG.PRG`. It's a stand-in for STinG: put it in the
emulated drive's `AUTO` folder, and it installs a `STiK` cookie and a
TCP/IP table whose one "connection" is tunnelled over the emulated serial
port. Claude ST then runs exactly as it would on STinG, reading
`CLAUDE.INF`, opening TCP to the gateway, and sending and receiving
through the STinG API. The bridge log shows
`DBG OPEN 192.168.1.10 2323`. For a serial-only test, leave
`CLAUDE.INF` out of the emulated drive. The fake driver is for testing
only; never install it on a real Atari.

## Files

```
Atari_claude/
├── st/                 the Atari program (C, GEM)
│   ├── claude.c        UI, word wrap, protocol
│   ├── gem.c / gem.h   minimal AES + VDI bindings
│   ├── sting.c / .S    STinG TCP client (Pure C calling convention shim)
│   ├── bidi.c          right-to-left (Hebrew) layout; `make test` runs bidi_test.c
│   ├── icon.h          the app icon (generated by tools/icon/make_icon.py)
│   ├── CLAUDE.INF      gateway address (tcp 192.168.1.10 2323)
│   ├── tos.c / tos.h   GEMDOS/BIOS/XBIOS traps, mini libc, 68000 libgcc helpers
│   ├── crt0.S          TOS startup
│   ├── link.ld         flat text/data/bss layout
│   └── CLAUDE.PRG      prebuilt binary
├── bridge/
│   ├── claude_bridge.py  serial / TCP / FIFO links + protocol
│   ├── backends.py       claude.ai, Anthropic API and demo backends
│   ├── atari_text.py     Atari charset + streaming Markdown formatter
│   └── test_bridge.py
├── pi/
│   ├── install.sh      Raspberry Pi gateway installer
│   └── claude-st.service
├── tools/
│   ├── hatari-test.sh  run Claude ST in Hatari with a local bridge
│   ├── elf2tos.py      ELF → TOS .PRG converter
│   └── fakesting/      STinG stand-in for testing in an emulator
└── PROTOCOL.md
```
