# AERA common code

This is the code the AERA Atari programs share. Each app carries a copy in its own `common/` folder, so
it still clones and builds on its own. The copy is pulled with `git subtree` from this repo's `common`
branch, which holds just this folder and is kept up to date by a workflow.

**Change shared code here, never in an app's `common/`.** Then, in each app:

    tools/sync-common.sh        # git subtree pull --prefix=common <AERA-Atari> common --squash

git subtree makes its squash commit with `git commit-tree`, which doesn't sign it, so GitHub shows it
as Unverified. `tools/sync-common.sh` (and `tools/sync-gateway.sh`) end with
`sh common/tools/sign-subtree.sh BEFORE`, which makes that commit again, signed, with the merge on top
(the template's `tools/sync-common.sh` has it).

## Who uses it

| App | Since | Check when it moved |
|---|---|---|
| EFTP | 14 Oct 2026 | programs byte-identical before/after; unit, FTP/FTPS and gateway tests |
| EMail | 14 Oct 2026 | programs byte-identical; unit and Dovecot tests (plain, Falcon TLS, stunnel gateway) |
| ECal | 14 Oct 2026 | programs byte-identical; unit, gateway and Radicale tests (same results as before) |
| EContact | 14 Oct 2026 | programs byte-identical; unit, low-memory and Radicale tests |
| ClaudeST | 14 Oct 2026 | layout and bridge tests; demo recording (Falcon, serial) and STinG through FAKESTNG (ST): every screenshot the same as the old build's |
| EHypView | 14 Oct 2026 | unit, ASan, big-endian (QEMU) and golden tests; Atari report equal to the host's; GUI test (45 checks) |

## What's here (layer 1)

These files were the same code in ECal, EContact, EMail and EFTP when they moved here (October 2026).

| Folder | Files | What they are |
|---|---|---|
| `atari/` | `link.ld`, `nf.S`, `sting.S`, `dsprsa.c/.h` | linker script; NatFeats; STinG's calling glue; RSA on the Falcon's DSP |
| `atari/include/` | `ctype.h` `limits.h` `stdio.h` `string.h` `strings.h` | the parts of the freestanding C library headers every app shares |
| `src/` | `bidi.c/.h`, `ctime.c/.h`, `futil.c`, `secret.c/.h` | Hebrew right-to-left layout; dates and times; safe file writes (`pf_save` writes `NAME.IN$` and renames it over `NAME.INF`; `tests/futil`); password encryption |
| `ui/` | `dsp.c` | checks the DSP and turns Falcon mode on |
| `dsp/` | `rsa.a56`, `rsa_prog.h`, `mkh.py` | the DSP program (source, and as a C array) |
| `tools/` | `elf2tos.py`, `mkroots.c` | ELF to TOS program header; root certificates to `ROOTS.DAT` |
| `third_party/bearssl/` | BearSSL 0.6 | TLS for Falcon mode (MIT licence) |
| `certs/` | `CACERT.PEM` | Mozilla's root certificates (MPL 2.0) |

### What the app provides
The shared files include headers that each app keeps itself, because their contents differ per app.
These are found through the app's `-I` paths:

- `plat.h`: the platform layer (`pf_*`). Used by `futil.c`, `secret.c` and `ui/dsp.c`.
- `util.h`, `conn.h`, `tls.h`: used by `ui/dsp.c` and `secret.c`.
- `ui.h`: the app's GEM layer, used by `ui/dsp.c`.
- the rest of the C library: `stdlib.h` and `tos.h` in `atari/include/`, plus `libc.c`.

### Build flags
Add the shared folders after the app's own, so an app header always wins:

```make
CFLAGS += -Iatari/include -Icommon/atari/include -Isrc -Icommon/src -Iatari -Icommon/atari -Iui
BSSL   := common/third_party/bearssl
LDFLAGS += -Wl,-T,common/atari/link.ld
```

Then add pattern rules for `common/src/%.c`, `common/atari/%.c`, `common/ui/%.c` and the two `.S`
files. `binary/CACERT.PEM` is copied from `common/certs/`. EFTP's Makefile is the reference.

## Layer 2a: merged copies (14 Oct 2026)

These had drifted between the apps; each is now the most complete copy, with every app's additions:

| Files | Taken from | What the others gain |
|---|---|---|
| `atari/sting.c/.h`, `atari/net_tos.c` | EFTP | `net_close_wait()` (close once everything sent has gone out); **and a random DNS query ID** (was the fixed "TR", which let anyone on the network answer for the name server) |
| `atari/tos.c` | ECal | `boot_drive()`; 32-bit multiply and divide with `mulu.w`/`divu.w` instead of bit loops (checked against the 68020's own instructions on 2 million cases under QEMU) |
| `atari/include/tos.h` | EFTP + ECal | `Dfree`, `Fattrib`, `Fdatime`, `Drvmap`, `boot_drive` |
| `atari/include/stdlib.h` | ECal | `heap_add()` (the accessories' fixed heap) |
| `src/util.c/.h` | ECal | `sb_room()` |
| `src/tz.c/.h` | ECal | a small cache of the last zones looked up; `tz_offset()` keeps the last four zone-years with their two changes (ECal 2.6's `tzm_offset`), so a series of events no longer works out summer time from the rules each time; `tz_offset_rules()` is the slow way, for tests |
| `src/charset.c/.h`, `charset_tab.h` | EMail | ECal and EContact lose the Troll-proxy guesswork EMail dropped (it could take real text for already-converted Atari text) |

## Layer 2b: TLS and connections (14 Oct 2026)

| Files | Taken from | What the others gain |
|---|---|---|
| `src/tls.c/.h` | EFTP (itself ECal + EMail) | ECal's TLS session resumption for EMail; EMail's ChaCha20-first handshake (twice as fast as AES-GCM on a 68030) for ECal and EContact |
| `src/tls_none.c` | EFTP | the ST editions' stand-in; names the Falcon edition from `TLS_FULL_EDITION` |
| `src/conn.c/.h` | EFTP + EMail | one API: `conn_open(name, host, port, bufsize, sec, err, errlen)` with `bufsize` 0 for `conn_bufsize` (16 KB; EMail lowers it on a small Atari); `SEC_STARTTLS`; EFTP's Stop (`conn_stop`), raw reads (`conn_read`: 0 = closed, -1 = timeout/stopped), `conn_pending`, `conn_close_flush`; ECal's handshake timing; a block-buffered log flushed at each milestone |

An app provides `net_close_wait()` in its `plat.h` (declared; the code is in `atari/net_tos.c`).

## Layer 2c: runtime (14 Oct 2026)

| Files | Taken from | What the others gain |
|---|---|---|
| `atari/crt0.S` | ECal | starts as a desk accessory or a program (`_is_acc`); stack size from `STACK_SIZE` (default 48 KB) |
| `atari/libc.c` | EContact + EMail | the accessory's fixed heap (`heap_add`); a heap that doubles (16 MB in six GEMDOS blocks); EMail's ST/TT RAM counts (`heap_st`, `heap_tt`); a long-word `memset`; heap sort instead of shell sort (no quadratic case) |
| `atari/gem.c/.h` | ECal + EContact + EFTP | `appl_getinfo`, iconify messages, `vsl_type`; the clipboard (`scrp_read/write`); EFTP's VDI attribute cache; the AES event type is `AEVENT` |

## ClaudeST and EHypView (14 Oct 2026)

What they brought in:
- **EHypView:** a corrected `atari/nf.S`. NatFeats read their argument at 4(sp), so the earlier copy's
  lookup always failed and `nf_debug()` never printed under Hatari. Also `nf_find()`, and the VDI
  calls `v_bar`, `v_rbox`, `v_rfbox`, `vsl_ends`, `vr_trnfm`, `vst_unload_fonts`, and the names
  for the horizontal slider (`HSLIDE`, `LFARROW`, `RTARROW`, `WF_HSLIDE`, `WF_HSLSIZE`), the
  arrow actions (`WA_*`), the writing modes (`MD_*`) and the fill styles (`FIS_*`) in `gem.h`.
- **ClaudeST:** the serial port calls (`Rsconf`, `Bconmap`, `Bcostat`, `DEV_AUX`, the `BAUD_*`
  codes) in `tos.h`.
- `appl_write()` now takes a `const` message.

In EFTP, EMail, ECal and EContact only `gem.o` and `nf.o` changed: every other object file builds the
same as before.

## Layer 3: GEM building blocks (October 2026)

| File | From | What it does |
|---|---|---|
| `atari/crash.c/.S/.h` | EMail | the crash catcher: a crash in the program (2 to 8 bombs) writes `CRASH.LOG` and quits tidily; TOS's record of a crash in the system is added at the next start. `crash_install(workdir, "ECal", version)`, in a program only (a desk accessory never installs it, and takes no memory for it). `crash_report = conn_log_append` adds the report to the protocol log too. Test builds (`-DCRASH_TEST`) crash on purpose with F1 to F4 (`crash_test()` in the app's key handler). `tests/crash/run.py` checks every kind of crash in Hatari on a 68000 and a 68030; `tests/crash/app_crash.py` checks an app's own handling (its alert, a tidy exit, "crashed last time" after a reset); `crash_log()` is where CRASH.LOG is |
| `atari/crash_none.c` | | the same calls, doing nothing, for an edition that is only ever a desk accessory (ECal's and EContact's ST editions) |
| `tools/crashaddr.py` | EMail | turns the offsets in a `CRASH.LOG` into function names, from the ELF file of the same build |
| `ui/help.c/.h` | ECal + EContact | the Help key: `help_show(dir, "ECAL.HYP", page)` sends `VA_START "C:\ECAL\ECAL.HYP page"` (or `"*:\ECAL.HYP page"`) to the viewer that runs, EHypView first, then ST-Guide and HypView; 0 when none runs. The string is in memory the viewer may read (`av_shared()`); `help_init()` takes it at start-up. `tests/help/app_help.py` checks an app's Help key in Hatari with a stand-in viewer. `help_key(dir, hyp, page, "Keys")` is the Help key: the viewer, else an alert saying to install EHypView or ST-Guide, with a button for what the app shows itself (guidelines 4.3) |
| `ui/av.c/.h` | EHypView + EContact | the AV protocol (guidelines 4.1): `av_init("ECONTACT\|ECONTST")` tells the desktop's AV server (`AVSERVER`, else Thing, jinnee or Gemini) that we take VA_START, under the name the AES knows us by; `av_message()` takes a VA_START, keeps its own copy and answers AV_STARTED; `av_arg()` takes the next (quoted) file name off it; `av_exit()` says AV_EXIT when a program quits; `av_send(name, text)` hands another program a string (EContact's `mailto:` to EMail), in memory it may read (`av_shared()`: Mxalloc 0x2B under MiNT and MagiC, static on plain TOS). `av_window(h, 1)` tells the desktop an accessory opened a window (AV_ACCWINDOPEN), so files dropped on it come as VA_DRAGACCWIND. `av_command()` is the command line a program was started with (the desktop's way to open a file with it), as a VA_START string. `tests/av` checks it on the build machine, and `app_av.py` checks an app in Hatari with a stand-in AV server, `THING.ACC`, which also drops files (`dropfiles`) and text (`drop`, AP_DRAGDROP under MiNT), puts text on the clipboard (`clip`) and writes down what is there (`clipshow`) |
| `ui/alert.c/.h` | ECal + EContact | `alert(def, "[3][Can't reach %s.|...][ OK ]", host)`: form_alert's string with printf formatting, words wrapped into lines of 30 characters, at most 5 lines, brackets in the text made parentheses, an empty line kept (EmuTOS reads `||` as `|`), Hebrew put into display order; `alert_hebrew` lets an app map the letters for its font. Since 17 Oct 2026 the box is our own, built as EmuTOS's `fm_alert` builds it (the icon, our own 32 by 32 drawings of `!`, `?` and stop; the lines; up to 3 buttons, the default one marked) in the system font's cells, and run by `ui/dialog.c` with the screen under it saved and put back: Return gives the default button, **Esc and Undo the last one** (guidelines 3.5, 6.2), on every AES. What it can't show that way (4 buttons, one over 20 characters, a screen too small) goes to form_alert. It returns 1 to 3 as before; an app links `dialog.o` too. `tests/alert` checks the text and the tree's layout in five screen modes on the build machine. `screen_check(app, width)`: on a screen narrower than 640 pixels an alert says so (guidelines 5.2) |
| `ui/dialog.c/.h` | | the dialog loop (guidelines 3.5): `dlg_do(tree, start, cancel)` is form_do written out with `evnt_multi`, `form_keybd`, `form_button` and `objc_edit` (Return, Tab and the arrows, clicks, the double-click bit as form_do), where **Esc and Undo end it at once with `cancel`**, the text field as it was (form_do hands Esc to the field, which clears it); `cancel` -1 for a dialog with no button that does nothing (EFTP's Skip\|Resume\|Replace during a transfer): Esc then gives -1. `dlg_modal(tree, start, cancel)` does the rest of the modal dance (`wind_update`, `form_center`, `form_dial`, drawing, the exit object deselected) and returns the object; `dlg_modal_save` puts the screen back itself, as form_alert does. (Not `dlg_run`: ECal's and EContact's FIELD engine has one.) An app moves over by `form_do(tree, 0)` becoming `dlg_do(tree, 0, CANCEL)`, or its whole dialog function `dlg_modal(tree, 0, CANCEL)`. `tests/dialog`: `make test` against a stand-in AES that clears a field on Esc as TOS's does; `make prg` and `run.py etos512us.img` run DLGT.PRG in Hatari with EmuTOS in ST high, medium, low and the Falcon's VGA mode, and `--xaaes` under FreeMiNT and XaAES (`../xaaes`): alerts answered with Return, Esc, Undo and a click, a dialog's field typed into, then Esc, Undo, Return and a click. Built with `-DDLG_TRACE`, `dlg_modal` says where each exit button is on the emulator's console, for a rig to click |
| `ui/mbar.c/.h` | EHypView | the menu bar, built at run time from the app's table of titles and items (`{ "Open...", "^O", C_OPEN }`): no resource file, the system font's size, shortcuts right-aligned, the Desk menu's separator and accessory slots added. `mbar_object()` and `mbar_command()` map between commands and objects. `tests/menu` walks the tree as the AES does, with AddressSanitizer |
| `ui/gemui.h` | | what `popup.c`, `aboutbox.c` and `iconify.c` take from the app's own drawing code (its `ui.h`): the screen's sizes, `fill`, `frame`, `hline`, `line_col`, `text_at` with `TX_BOLD`, `TX_INVERSE`, `TX_LTR`, `TX_DIM`, `clip_on`, `clip_off`, `font_sys_push/pop`, `st_ram`; the compiler checks the app keeps to it |
| `ui/popup.c/.h` | ECal + EContact | right-click menus: at the mouse, in the system font, click or press-drag-release, arrows, Return and Esc; `-` separators, `~` disabled, the check mark; `popup_area` for the app's repaint; `popup_alt()` opens above the row it is for when there is no room below |
| `ui/aboutbox.c/.h` | ECal | the About box: `about_show(&ABOUT)` with the name, version, tagline, up to three lines of the app's own and its icon; in the small font, the licence line always and up to three of the app's (the trademarks it names); `about_ram()` for "TT RAM"; `about_icon16()` for the colour icon. In the same frame, `text_box(title, text)` shows plain text, and `crash_offer(app, where)`, at start-up after a crash TOS recorded, says so and offers to show the report from CRASH.LOG (guidelines 6.4) |
| `ui/clip.c/.h`, `ui/clipedit.c` | EContact | the GEM clipboard (guidelines 4.4): `clip_put(text, n)` writes SCRAP.TXT in the AES's clipboard folder (made when it isn't there, else `\CLIPBRD` on the boot drive or the drive the program runs from), the other `SCRAP.*` gone, lines ending CR LF, and tells the other programs (SC_CHANGED) where the AES broadcasts; `clip_get(buf, size)` reads it, lines ending `'\n'`; `clip_line()` keeps the first line for a field of one; `clip_key(key)` tells ^C ^X ^V; `clip_edit(tree, obj, &idx, key)` does them on a dialog's edit field through `objc_edit`, so the field's length and template hold. `tests/dd` checks it on the build machine |
| `ui/dd.c/.h` | | things dropped on a window (guidelines 4.2): `dd_message(msg, DD_WANT_FILES \| DD_WANT_TEXT, &d)` takes AP_DRAGDROP (MiNT, MagiC: the pipe `U:\PIPE\DRAGDROP.xx`, as cflib's `dd_open`), offering ARGS and `.TXT`, refusing what isn't wanted (DD_EXT, DD_NAK) or is too long (DD_LEN), and VA_DRAGACCWIND from the desktop; files come quoted as VA_START's, text with `'\n'` line ends. `tests/dd` checks the protocol on the build machine with a stand-in pipe |
| `ui/iconify.c/.h` | ECal | iconified windows (guidelines 5.1): `icon_kind(kind)` adds the iconifier where the AES can iconify; `icon_redraw(h, area, icon, icon16, "3 new")` draws the window as the app's icon, in colour with 16 colours or more, with a short status under it when there is room, through the rectangle list over the work area as it is now. `tests/av/app_av.py` with `icon NAME` and `uniconify NAME` in THING.ACC's script sends an app WM_ICONIFY and WM_UNICONIFY under EmuTOS (which can't shrink the window, so the icon is drawn in the whole of it) |
| `ui/twin.c/.h` | | NAME.PRG and NAME.ACC on one machine (guidelines 2.3), one in use at a time: `twin_handover(av_command())` at the PRG's start hands the command line to the ACC (VA_START, then AV_STARTED) where the AES lists its programs (`appl_search`: MagiC, XaAES, N.AES) and the PRG quits; `twin_prg()` tells the ACC its PRG runs (under single TOS the program in front, asked with TWIN_ASK, which `av_message()` answers), and `twin_pass(s)` hands it the ACC's AC_OPEN or VA_START. `tests/twin` checks it on the build machine with a stand-in AES |
| `tests/xaaes/` | | FreeMiNT 1.17.0 with XaAES in Hatari: an AES that iconifies, multitasks and does drag and drop, where EmuTOS's can't. `make base` fetches FreeMiNT from the Sparemint archive (checked against its SHA-256; it isn't kept here) and makes the C: drive, an IDE disk image (`fatimg.py`), since FreeMiNT can't read Hatari's emulated drive. `run.py WORK EMUTOS.IMG APP.PRG --steps "..."` starts an app there and drives it: clicks, drags, keys, screenshots, and waits for what it says through NatFeats. `app_icon.py` checks an app's iconify with XaAES's own gadgets (guidelines 5.1); `--later S` starts the app S seconds after XaAES (`LATER.PRG`), once the accessories are in, for the PRG/ACC hand-over; `make check TOS=...` checks the rig itself |

## Layer 4: network security (October 2026, the guidelines' section 7)

| File | What it does |
|---|---|
| `atari/dns.h` | the answer in a nameserver's reply, for `net_tos.c`'s DNS client (MiNTnet): every count and length checked against what came, positions in `long`s (a reply claiming 32767 questions used to wrap a `short` and read before the buffer). `tests/dns` checks it and fuzzes it on the build machine. `net_tos.c` also refuses a host name too long for its query, and waits for a STinG connection a second at a time, so Esc can stop it |
| `ui/busy.c/.h` | the busy bee, and Esc or Undo to stop the network meanwhile (guidelines 7.1): `busy_bee(on)` for the app's `busy()`, `pf_idle = busy_idle` (while the bee shows it reads the keys: Esc and Undo set `conn_stop`, any other key is kept), and in the main loop `conn_stop = 0` and `busy_key(&ev)` before `evnt_multi_`, so a key typed while busy comes through afterwards. `conn.c` then opens nothing more and says "stopped" (logged as `!! stopped`) |
| `src/secret.c/.h` | `secret_encode()` returns 0 when it has to write a password as it is (no `NAME.KEY` to be had, too long), and calls `secret_warn(keyfile)` the first time in a run: the app's alert (guidelines 7.2). `tests/secret` checks it |
| `src/util.c`, `src/tz.c` | an `SBUF` grows to 1 GB at most, and refuses sizes that would overflow; a time zone offset's digits stop at four (both found by fuzzing) |
| `tests/fuzz/fuzz_net.c/.h` | the network for an app's fuzz targets: `fuzz_net_set(data, n)`, and the program's own protocol code hears the input, in pieces of 1 to 97 bytes. `FUZZ_NET_NEXT` splits the input into one part per connection, in the order they are opened (FTP's commands and data); without it, every connection carries on where the last one stopped |
| `tests/fuzz/replay.c` | runs a fuzz target on files, without libFuzzer: an app's `make test` runs its seeds and `tests/fuzz/found` (what once broke a parser) under AddressSanitizer and UBSan with the build machine's compiler, 10 s at most each |

Each networked app has `make fuzz` (clang with libFuzzer, `FUZZTIME` seconds per target) and
`tests/fuzz/` with its targets, `mkseeds.py`, `seeds/` and `found/`:

| App | Targets | What fuzzing found |
|---|---|---|
| ECal | `dav`, `ics`, `rrule`, `xml`, `inflate` | a repeated `BYMONTHDAY`/`BYYEARDAY`/`BYWEEKNO`/`BYSETPOS` writing past its array; overflows in `BYDAY` ordinals, durations and alarm triggers; XML with no text |
| EContact | `dav`, `vcard`, `jpeg`, `xml`, `inflate` | a JPEG with a second SOF leaking; a cut-short scan read for ever; a DQT read past its segment; TJpgDec's IDCT overflowing (now built with `-fwrapv`) |
| EMail | `imap`, `pop3`, `smtp`, `mime` | an IMAP literal of minus five bytes (memmove of -1) or of gigabytes; a POP3 `STAT` count sizing three arrays past 32 bits; EHLO extensions one byte past their buffer |
| EFTP | `ftp`, `list` | nothing; a multi-line reply is now kept to 8 KB |

## The heap (16 Oct 2026)

`atari/libc.c`'s `malloc`, `free`, `calloc` and `realloc` no longer walk a list of every free block
(K&R's, 14% of ECal's calendar reading in its profile). Blocks carry their size at both ends, so a
freed block joins its free neighbours at once; free blocks are in lists by size (one per size up to
508 bytes, then four per power of two), and the block that new ones are cut from is kept out of the
lists. An allocated block costs 4 bytes instead of 8. `heap_add`, the GEMDOS block sizes (512 KB, then
doubling, at most 40 blocks), `heap_st` and `heap_tt` are as they were; `realloc` now grows a block in
place when the one after it is free, and `calloc` refuses a size that overflows.

| In Hatari (EmuTOS) | K&R | now |
|---|---|---|
| 3000 calendar events' blocks, 8 MHz ST | 56.8 s | 11.2 s |
| the same, 16 MHz Falcon | 8.5 s | 4.6 s |
| 33000 `malloc`/`free` pairs, ST / Falcon | 4.9 s / 1.3 s | 4.2 s / 1.4 s |
| the same mixed calls on a 600 KB fixed heap (an accessory's), until one fails | 7000-11000 | 43000-850000 |

`tests/heap`: `make test` runs the heap on the build machine with ASan and UBSan, and as 68000 code
under QEMU (four-byte sizes and pointers): an accessory's heap filled and emptied, the GEMDOS growth,
little memory, TOS 1.x, the 40-block limit, and millions of mixed calls with every block's bytes and
the heap's structure checked. `make prg` and `run.py etos512us.img` run `HEAPT.PRG` in Hatari on a
68000 ST (where an odd address bombs) and a 68030 Falcon.

## The clock (16 Oct 2026)

`tos_ms()` in `atari/tos.c` is milliseconds by TOS's 200 Hz counter (`_hz_200`), right even when
something has set MFP timer C faster and still calls TOS's handler each time. On Erez's Falcon the
counter runs at 312 Hz, so EFTP showed a 16-second transfer as 25 and its speeds 1.56 times too low.
At its first call `tos_ms()` measures the counter against timer C's own count-down, which runs off the
MFP's crystal however the timer is set, for 6 of the counter's steps (30 ms when all is well, busy, once per run); more
than 3% off, it scales its times. `tos_clock()` is `""` on a true clock, else the line for an app's log
("the 200 Hz clock runs at 312.5 Hz: times corrected"). The code is EFTP's (whomper/EFTP a1e3b44).
An app's `pf_ms()` becomes `return tos_ms();`.

`Supexec` now goes through `tos_supexec()`, a function GCC can't see into. Called straight through
`trap14_wl()`, GCC took a static that the supervisor routine wrote as unchanged when nothing else took
its address (the trap's "memory" clobber doesn't cover it): `nf_debug()` dropped its first line in
every app, and `tos_ms()` dropped its correction.

`tests/clock`: `make prg` and `run.py etos512us.img` run `CLOCKT.PRG` in Hatari on an ST and a Falcon,
once with timer C at 312 Hz (a file `C:\FASTCLK`) and once at 200 Hz, and time the 8 seconds it counts
by `tos_ms()` with the build machine's clock: 8.04 and 8.00 s on the ST, 8.00 and 7.94 s on the Falcon.

## Background work (16 Oct 2026)

`ui/bgwork.c` lets an accessory, or a program, do background work without making the machine feel
slow (guideline 2.2, "A quiet start"). Work waits for its start (`START=` in the INF: `now`,
`opened`, or seconds after boot; the family's default is 60 s), runs in slices of 40 ms (80 ms on a
68000) with an event call between them, rests for 2 s after the mouse moves or a key or button is
pressed or when the program in front keeps the machine, and stops while the app's program edition
runs. It allocates nothing, so an accessory may use it at any time. Built with `-DAES_PROFILE`,
`atari/gem.c` reports every stretch between event calls over 50 ms to the emulator's console
(`AESGAP <ms> ms <tag>`).

`tests/bgwork`: `make test` checks the rules on the build machine (four planted bugs, all caught);
`make prg` and `run.py etos512us.img` run `BGT.PRG` in Hatari: 300 ms of work in one go shows as
300 ms, and 3 s of work through bgwork never held the machine longer than 85 ms on an ST (80 ms
slices) and 40 ms on a Falcon (40 ms slices).

## People from EContact (17 Oct 2026)

EMail and ECal show people by name and let the user pick them in EContact by asking EContact, with
the family's one message besides the AV protocol's, PEOPLE (0x4E43, guideline 4.1). CONTACTS.TXT
and EContact's integration patches go; nothing reads another app's files.
- `ui/av.c`: `av_people(id, text)` sends a PEOPLE string (laid out as VA_START, answered with
  AV_STARTED, one at a time, lost after 3 s); `av_message()` hands PEOPLE strings over as
  `AV_PEOPLE`; `av_name()` is the name the AES knows us by.
- `ui/people_ask.c`: the asker's side (`people_name(addr)` gives the name at once, or 0 while it is
  being asked; `people_pick()`; `people_message()`; `people_poll()` each turn; a callback when names
  come or change) and EContact's (`people_question()`, `people_next()`, `people_answer()`, which cuts
  answers into whole commands of at most 1 KB). Memory is taken once, in `people_init()`.

`tests/people`: `make test`, the asker against a pretend EContact and EContact's side (seven planted
bugs, all caught); `tests/av` checks PEOPLE in and out.

`tests/tz`: `make test`, 83 million offsets from the kept zone-years against the rules: every built-in
zone around each change from 1990 to 2040, random moments, a zone whose rules change at the same
address, and one that changes on new year's day (four planted bugs, all caught).

## Coming next

| Area | Files | Plan |
|---|---|---|
| GEM building blocks | `font.c` (the font selector), tooltips, the toolbar | the guidelines' reference copies |
