<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright (C) 2026 AERA | Erez Yaary -->
# ClaudeST: the UI standards' backlog

Erez answered the family's 54 UI decisions on 9 Nov 2026 (AERA-Atari a261597, `standards/ui`:
DECISIONS-2026-11-09.md, MENUS.md, CONTROLS.md, WINDOWS.md, DEVIATIONS.md). These are ClaudeST's items,
from the hub's list of 9 Nov.

Erez lifted maintenance mode for this work (10 Nov 2026, through the hub) until it is released; the
hub says when ClaudeST returns to it. Every item below is done on the 2.5 branch; each commit names its
rows.

Sizes: S small, H half a day, M more.

| # | Item | Rows | Size | State |
|---|---|---|---|---|
| 1 | File lists `Quit` alone, not `Close window ^U` as well; ^U still quits | D3, MD7 | S | done on main (rides the bridge-move release) |
| 2 | Clipboard notices end with a full stop: `Copied to the clipboard.` / `Couldn't write the clipboard.` | D50, W17 | S | done on main (rides the bridge-move release) |
| 3 | Clr/Home in the text dialog goes to the start of the field (Esc empties); dialogs centred, not at a third of the height | C14 | S | done on main (rides the bridge-move release) |
| 4 | Pop-up padding dropped once `popup()` adds its margin; the model menu over its chip | D15, MD18, D27 | S | done for 2.5 |
| 5 | A black title band in dialogs in place of the heading inside; in a MiNT or MagiC window the window's name is the title | D22, C4 | H | done for 2.5 |
| 6 | Dialog buttons act on the release over them, with a pressed look | C7 | H | done for 2.5 |
| 7 | INF `FONT=id,pt` becomes `FONT=name,points`, the old key read once. `SIDEBAR=`, `HEBFONT=` stay (not decided) | D43, D44, W18 | H | done for 2.5 |
| 8 | ⇧^S hides the chat list: Chat ▸ Hide chat list (Erez, 10 Nov 2026) | D45, W20 | M | done for 2.5 |
| 9 | Keyboard reach in Settings (Tab stops, arrows in the radio group, Space, the ring); the window's arrows go to the pane under the pointer | D25, C6, D37 | M | done for 2.5 |
| 10 | Build with `-DDLG_CLICK` | C16 | S | done for 2.5 |

Also for 2.5: the family's selection colour (D42, theme.c); `key_shift` and `key_state_init`; Options
as Settings, Account, Font with no line (D6, D13: Erez, 10 Nov 2026); tips after 700 ms and BubbleGEM
first (D18; ClaudeST's own save-under box is the written exception). `Search chats ^F` stays (D4, the
hub).

**Nothing to do:** D46 (written from ClaudeST's list keys), D52 (F-keys as views), D51 ("Show" is
right), the out-of-range alert.

## Fault found 9 Nov 2026 (not a UI item): gone with the serial cable

**MiNT with memory protection, over the serial cable** (the hub, 9 Nov 2026; by reading). ClaudeST
pointed the system's serial IOREC at its own buffer, also under MiNT, which memory protection would
stop. Moot from 2.3: Erez removed the serial cable (9 Nov 2026), and with it the IOREC code; ClaudeST
reaches the Gateway through STinG only.
