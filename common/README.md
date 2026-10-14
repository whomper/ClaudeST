# AERA common code

This is the code the AERA Atari programs share. Each app carries a copy in its own `common/` folder, so
it still clones and builds on its own. The copy is pulled with `git subtree` from this repo's `common`
branch, which holds just this folder and is kept up to date by a workflow.

**Change shared code here, never in an app's `common/`.** Then, in each app:

    tools/sync-common.sh        # git subtree pull --prefix=common <AERA-Atari> common --squash

## Who uses it

| App | Since | Check when it moved |
|---|---|---|
| EFTP | 14 Oct 2026 | programs byte-identical before/after; unit, FTP/FTPS and gateway tests |
| EMail | 14 Oct 2026 | programs byte-identical; unit and Dovecot tests (plain, Falcon TLS, stunnel gateway) |
| ECal | 14 Oct 2026 | programs byte-identical; unit, gateway and Radicale tests (same results as before) |
| EContact | 14 Oct 2026 | programs byte-identical; unit, low-memory and Radicale tests |

ClaudeST (`st/`) and EHypView carry older copies of some of these files; they follow once layer 2
is in, since their copies differ more.

## What's here (layer 1)

These files were the same code in ECal, EContact, EMail and EFTP when they moved here (October 2026).

| Folder | Files | What they are |
|---|---|---|
| `atari/` | `link.ld`, `nf.S`, `sting.S`, `dsprsa.c/.h` | linker script; NatFeats; STinG's calling glue; RSA on the Falcon's DSP |
| `atari/include/` | `ctype.h` `limits.h` `stdio.h` `string.h` `strings.h` | the parts of the freestanding C library headers every app shares |
| `src/` | `bidi.c/.h`, `ctime.c/.h`, `futil.c`, `secret.c/.h` | Hebrew right-to-left layout; dates and times; safe file writes; password encryption |
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
| `src/tz.c/.h` | ECal | a small cache of the last zones looked up |
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

## Coming next

| Area | Files | Plan |
|---|---|---|
| GEM building blocks | `popup.c`, `about.c`, `font.c`, alert, help launcher, menu builder, crash handler | the guidelines' reference copies |
