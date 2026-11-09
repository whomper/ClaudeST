<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright (C) 2026 AERA | Erez Yaary -->
# ClaudeST: the UI standards' backlog

Erez answered the family's 54 UI decisions on 9 Nov 2026 (AERA-Atari a261597, `standards/ui`:
DECISIONS-2026-11-09.md, MENUS.md, CONTROLS.md, WINDOWS.md, DEVIATIONS.md). These are ClaudeST's items,
from the hub's list of 9 Nov.

**Nothing here starts on its own.** ClaudeST is in maintenance mode (CLAUDE.md). An item goes into a
release ClaudeST makes anyway (a fault fix, or one Erez asked for) without growing its risk: the S items
freely, H and M only if that release is otherwise untouched. Every GEM release still passes the speed
rig. When an item is done, send the hub one line with the commit (it ticks the section-8 table), and
mark it done here.

Sizes: S small, H half a day, M more.

| # | Item | Rows | Size | State |
|---|---|---|---|---|
| 1 | File lists `Quit` alone, not `Close window ^U` as well; ^U still quits | D3, MD7 | S | done on main (rides the bridge-move release) |
| 2 | Clipboard notices end with a full stop: `Copied to the clipboard.` / `Couldn't write the clipboard.` | D50, W17 | S | done on main (rides the bridge-move release) |
| 3 | Clr/Home in the text dialog goes to the start of the field (Esc empties); dialogs centred, not at a third of the height | C14 | S | done on main (rides the bridge-move release) |
| 4 | Pop-up padding dropped once `popup()` adds its margin | D15, MD18 | S | waits for the hub (common/) |
| 5 | A black title band in dialogs in place of the heading inside; in a MiNT or MagiC window the window's name is the title | D22, C4 | ? | open |
| 6 | Dialog buttons and choices act on the release over them, with a pressed look | C7 | H | open |
| 7 | INF `FONT=id,pt` becomes `FONT=name,points`, the old key read once. `SIDEBAR=`, `HEBFONT=` and a saved `WINDOW=` are not decided: leave them | D43, D44, W18 | H | open |
| 8 | ⇧^S hides the chat list | D45, W20 | new work | where its menu item goes is open: ask the hub first |
| 9 | Keyboard reach in dialogs (Settings has only Tab/Up/Down, no keys for the choices; the Keys box closes on any key); the window's arrows go to the pane under the pointer | D25, C6, D37 | M | waits for the hub (common/) |
| 10 | Build with `-DDLG_CLICK` | C16 | S | the hub's call |

**Still open, don't start:** `Search chats ^F` (D4: its place and words); the Options order (D6, D13).

**Nothing to do:** D46 (written from ClaudeST's list keys), D52 (F-keys as views), D51 ("Show" is
right), the out-of-range alert.
