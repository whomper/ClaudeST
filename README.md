# ClaudeST

<img src="docs/icon.png" width="96" align="right" alt="ClaudeST icon: a terminal window with Claude's orange spark and a green prompt">

A native GEM client for [claude.ai](https://claude.ai) on the Atari ST, STE, Mega ST/STE, TT and
Falcon, through the AERA Gateway (a Raspberry Pi or any Linux computer on your network).

**Highlights**

- **Like the website:** your claude.ai chats, projects and search, replies streaming in.
- **Claude Code sessions, artifacts, attachments:** read and reply, save to disk, attach any file.
- **Model, effort and usage** at a glance in the window.
- **Modern but Atari:** right-click menus, drag and drop, iconify, help in EHypView or ST-Guide.
- **Hebrew:** right-to-left text and a Hebrew keyboard (F10).

![ClaudeST on a Falcon in 256 colours](docs/falcon-colour.png)

## Getting started

Download the latest `claudest-x.y.zip` from **[Releases](../../releases/latest)** and unpack it:

| File | What it is |
|---|---|
| `CLAUDEST.PRG` | the program, for every ST, STE, TT and Falcon |
| `CLAUDEST.HYP`, `CLAUDEST.REF` | the help, for EHypView or ST-Guide |
| `CLAUDEST.RSC`, `CLSTNV.RSC` | the desktop icon (`CLSTNV.RSC` for NVDI's own palette) |

1. Set up the Gateway with [AERA-Gateway](https://github.com/whomper/AERA-Gateway) and your claude.ai
   session key: `sudo gateway/aera-gateway install claude --network --atari ATARI_IP`.
2. Copy `CLAUDEST.PRG`, `CLAUDEST.HYP` and `CLAUDEST.REF` into one folder on the Atari and run it.
3. Choose **Options ▸ Settings…** (`Ctrl+,`), pick **Network** and type the Gateway's address (port
   2323). The status line shows *Online* with a green dot.

[Installing](docs/GUIDE.md#installing) has every step: the session key, a serial cable, the API, updating.

**Needs:** TOS 1.04 or newer, EmuTOS or MiNT; 640×200 or larger; about 370 KB free RAM; STinG or a
null-modem cable; a claude.ai account or an Anthropic API key.

Under TOS, put `FOLDR100.PRG` first in `AUTO`; with STinG, set `STING.CPX`'s Delay to 10 on a Falcon
or TT, 25 to 50 on an ST ([why](docs/GUIDE.md#3-set-up-the-atari)).

## How it reaches the servers

An 8 MHz 68000 can't do modern HTTPS, so a small Python program, the bridge, does it for the Atari:

```
STinG:   Atari (CLAUDEST.PRG) --plain TCP, home LAN, port 2323--> Gateway (bridge) --HTTPS--> claude.ai
Serial:  Atari (CLAUDEST.PRG) --null-modem cable, 19200 baud----> Gateway (bridge) --HTTPS--> claude.ai
```

AERA-Gateway releases the bridge; ClaudeST defines its [protocol](PROTOCOL.md) and owns its tests. The
bridge uses claude.ai's private web interface ([good to know](docs/GUIDE.md#good-to-know)).

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 zip   # Debian/Ubuntu; no MiNTLib needed
make                         # binary/CLAUDEST.PRG
make test                    # the bridge's tests and the right-to-left layout's
make help HCP=/path/to/hcp   # binary/CLAUDEST.HYP and .REF (common/tools/build-hcp.sh DIR builds hcp)
make dist                    # dist/claudest-x.y.zip and each file, as on the Releases page
```

A release: `tools/bump.py`, push, then **Actions > Release > Run workflow**. [More](docs/GUIDE.md#building).

## Source

```
st/        the Atari program: claudest.c (UI and protocol), bidi_test.c
help/      CLAUDEST.STG, the help (make help)
gateway/   a copy of AERA-Gateway's (tools/sync-gateway.sh), with claude-st/, ClaudeST's bridge
common/    the AERA programs' shared code, a copy of AERA-Atari's (tools/sync-common.sh)
tools/     hatari-test.sh, bump.py, docshots.py, the icon and the demo video
tests/     hatari/: the speed, disk, font and long-name rigs
docs/      the user guide (GUIDE.md) and its pictures
```

## Tests

- `make test`: the bridge's tests (`gateway/claude-st/bridge/test_bridge.py`) and the right-to-left layout's.
- `tools/hatari-test.sh`: ClaudeST in [Hatari](https://hatari.tuxfamily.org/) with the bridge's demo chats.
- `tests/hatari/`: speed, disk access, GDOS fonts, long names ([what each checks](docs/GUIDE.md#the-hatari-rigs)).

## Licence

ClaudeST is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 2 of the License, or (at
your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
[COPYING](COPYING).

Copyright (C) 2026 AERA | Erez Yaary.

Not affiliated with Anthropic or Atari; Claude is a trademark of Anthropic PBC, Atari of Atari
Interactive. `common/` also carries [BearSSL](common/third_party/bearssl) (MIT) for the other apps.

---

Part of **AERA** for Atari: [ECal](https://github.com/whomper/ECal) · [EContact](https://github.com/whomper/EContact) · [EMail](https://github.com/whomper/EMail) · [EFTP](https://github.com/whomper/EFTP) · [EHypView](https://github.com/whomper/EHypView) · [ClaudeST](https://github.com/whomper/ClaudeST) · [EWeb](https://github.com/whomper/EWeb) · [EUpdate](https://github.com/whomper/EUpdate) · [EPDF](https://github.com/whomper/EPDF) · 8-bit: [EDOS](https://github.com/whomper/EDOS) · [EFTP_XL](https://github.com/whomper/EFTP_XL) · Gateway: [AERA-Gateway](https://github.com/whomper/AERA-Gateway)
