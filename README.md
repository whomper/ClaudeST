# Claude ST

<img src="docs/icon.png" width="96" align="right" alt="Claude ST icon: an Atari SM124-style monitor showing a spark and a prompt">

A native GEM client for [claude.ai](https://claude.ai) on the Atari ST,
STE, Mega ST/STE, TT and Falcon.

![Claude ST in ST high resolution](docs/st-high-mono.png)

It works like the claude.ai website: chats, Search, Code, Projects and
Artifacts on the left, the open conversation on the right, and replies
streaming in as Claude writes them.

- Your real claude.ai account: recent and pinned chats, projects, search
- Claude Code sessions: read them and reply
- Artifacts: view them and save them to disk, Word documents included
- Model and effort for each chat, from a chip in the title bar
- Right-click menus: Pin, Rename, Move to project, Archive, Delete
- An Account page with your plan and usage
- Hebrew: right-to-left text and a Hebrew keyboard layout
- Monochrome and colour, any resolution from 640×200

The [user guide](docs/GUIDE.md) describes every feature.

## How it works

```
Atari (CLAUDE.PRG)  <-- STinG or serial -->  Raspberry Pi (bridge)  <-- HTTPS -->  claude.ai
```

An 8 MHz 68000 can't do modern HTTPS, so a small Python program, the
bridge, runs on a Raspberry Pi or any Linux computer on your network.
The Atari reaches it through STinG, over Ethernet or WiFi, or through a
serial cable. The Atari does all the drawing and word wrapping itself.

## What you need

- An ST, STE, Mega ST/STE, TT or Falcon with TOS 1.0 to 4.x, EmuTOS or
  MiNT, a 640×200 or larger screen, and about 250 KB of free RAM
- STinG with a working network, or a null-modem serial cable
- A Raspberry Pi, or any Linux computer, on the same network
- A claude.ai account, or an Anthropic API key

## Setup

Give the Atari and the Pi fixed IP addresses, for example with a DHCP
reservation in your router. The examples below use **192.168.1.10** for
the Pi and **192.168.1.20** for the Atari. Replace them with your own.

### 1. Get your claude.ai session key

1. Log in to claude.ai in a desktop browser.
2. Open the developer tools (`F12`). Go to **Application** (Chrome) or
   **Storage** (Firefox), then **Cookies**, then `https://claude.ai`.
3. Copy the value of the `sessionKey` cookie. It starts with `sk-ant-sid`.

The key gives full access to your account. It's stored only on the Pi,
readable only by root and the bridge.

### 2. Install the bridge on the Pi

```sh
git clone https://github.com/whomper/atari_claude.git
cd atari_claude/pi
sudo ./install.sh --network --atari 192.168.1.20
```

The installer asks for the session key. It then runs the bridge as a
service called `claude-st`, which starts at every boot and accepts
connections only from the Atari's address. Use `--atari IP1,IP2` for
several Ataris, or `--atari any` to accept any address on your network.
If the `ufw` firewall is on, the installer opens port 2323 for them.

### 3. Set up the Atari

1. Load **STinG** and check that the network works, for example by
   pinging the Pi.
2. Copy `st/CLAUDE.PRG` and `st/CLAUDE.INF` into one folder.
3. Put the Pi's address on the `tcp` line of `CLAUDE.INF`, with any text
   editor:
   ```
   tcp 192.168.1.10 2323
   ```
4. Run `CLAUDE.PRG`. The status line at the bottom left shows
   *Connecting…*, then *Online: claude.ai*.

Instead of step 3 you can type `/connect 192.168.1.10` in Claude ST's
reply line; Claude ST saves the address to `CLAUDE.INF`. If the Pi
restarts or the network drops, Claude ST reconnects by itself.

### Using a serial cable instead

Connect the Atari's Modem port to a USB-to-RS-232 adapter on the Pi with
a null-modem cable. Don't wire the Atari straight to the Pi's GPIO pins:
the Atari's ±12 V signals damage the Pi. Then, on the Pi:

```sh
sudo ./install.sh --port auto
```

On the Atari, choose **Options ▸ Serial port**, or leave out
`CLAUDE.INF`. Both sides use 19200 baud by default.

### Using the Anthropic API instead of claude.ai

```sh
sudo ./install.sh --backend api
```

The installer asks for an API key. Chats are then stored on the Pi
instead of in your claude.ai account. `--backend demo` gives sample chats
without any account, to test the setup.

## Looking after the Pi

Run these in the `atari_claude/pi` folder:

| Task | Command |
|------|---------|
| Update to the latest version | `git pull && sudo ./install.sh` |
| Paste a new session key, when claude.ai logs you out | `sudo ./install.sh --set-key` |
| Change the Atari's address | `sudo ./install.sh --atari 192.168.1.21` |
| See whether the bridge is running | `systemctl status claude-st` |
| Watch its log | `journalctl -u claude-st -f` |
| Remove it | `sudo ./install.sh --uninstall` |

The settings are in `/etc/claude-st/claude-st.env`. If claude.ai can't be
reached or the session key has expired, the error appears on the Atari.

## Keys

| Key | Does |
|-----|------|
| `F1` / `Ctrl+N` | New chat |
| `F2`, `F6`, `F3`, `F4` | Chats, Code, Projects, Artifacts |
| `F5` / `Ctrl+F` | Search chat titles |
| `Tab`, then `Return` | Pick and open a sidebar item |
| `Insert`, or right-click | The item's menu |
| `F8`, or click the status line | Account: plan, usage and details |
| `F9`, or click the model chip | Model and effort |
| `F10` | Hebrew keyboard on or off |
| `Help` | About |
| `Ctrl+Q` | Quit |

## Good to know

Claude ST is an unofficial client, not affiliated with Anthropic or
Atari. claude.ai has no public API for personal accounts, so the bridge
uses the same private interface as the claude.ai website. If claude.ai
changes it, parts can stop working until the bridge is updated. The
[troubleshooting section](docs/GUIDE.md#troubleshooting) has commands
that check each part from the Pi.

Claude ST was written with Claude, in Claude Code on claude.ai: a
claude.ai client for the Atari, built by talking to claude.ai.

## For developers

- [docs/GUIDE.md](docs/GUIDE.md#building): building, running in Hatari,
  tests and the source layout
- [PROTOCOL.md](PROTOCOL.md): the line protocol between the Atari and the
  bridge

© 2026 Whomper
