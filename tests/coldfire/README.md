<!-- SPDX-License-Identifier: GPL-2.0-or-later
     Copyright (C) 2026 AERA | Erez Yaary -->
# common's ColdFire code under QEMU

`common/`'s assembly built for the ColdFire (`-mcpu=5475`, the FireBee's MCF5474) and run under
`qemu-m68k -cpu cfv4e` as Linux programs with no C library: the m68k one on the build machine is
68020 code, and so is its libgcc. `make test` runs every case; `make same` checks that each `.S` in
`atari/` and `tlsovl/`, built for the 68000 and the 68030, is still byte for byte what `origin/main`
builds (the ColdFire's code is its own `#ifdef __mcoldfire__` path).

| Test | What it shows |
| --- | --- |
| `test_crt0` | `crt0.S` started as TOS starts an application (Mshrink of the basepage, text, data and BSS; `main` on its own stack; Pterm with `main`'s answer) and as an accessory |
| `test_sting` | `sting_call` passes Pure C's packed words and keeps every register C keeps |
| `test_nf` | `nf.S` finds no NatFeats and touches no vector (on a V4e NatFeats' opcodes are `mvs.b`, not illegal) |
| `test_inflate` | `inflate_fast.S`'s ColdFire loop, and the C, give the host's C every byte and answer of 980 streams |
| `test_mont` | `mont030` and `dbl030`, and `rsa030.c`'s RSA on them, against the host's 64-bit C (one sum of every word) |
| `test_tls` | `tests/tls030`'s checks with BearSSL, `mont31_030.S` and `ll64.c` all ColdFire code, no libgcc |
| `test_tlsfb` | the shipped `tlsovl/tlsfb.ovl` loaded and relocated by `src/ovl.c` (ColdFire code), its `TLS_OVL` checked (magic, major, version, every entry in its text), `init` given a `TLS_HOST` of Linux calls; `CACERT.PEM` and two test authorities decoded, then read again from the `ROOTS.DAT` it made; TLS 1.2 to `tlsfb_run.py`'s servers on 127.0.0.1 (openssl's certificates): RSA and ECDSA (P-256, P-384) keys, X25519, P-256 and P-384, ChaCha20, AES-GCM, AES-CBC and the RSA key exchange, a session resumed and forgotten, 40,000 bytes each checked; refused: a wrong name, an authority not loaded, ECDSA to an RSA-only hello |
| `test_crash` | `crash.S` entered through a ColdFire frame: a fault not ours goes on untouched; one taken reaches `rte` with a ColdFire frame into `crash_landing` |
| `test_cpucheck` | `cpucheck_cf.S`: the same bytes for the 68000 and the ColdFire; `app_main` with `_CF_` in the cookie jar, the alert without |
| `test_cpucheck_c` | `atari/cpucheck.c` as 68000 code in a 68030 harness: `cpu_alert` below `cpu_need`; `app_main` with no `_CF_`; with `_CF_` (whatever its value) `cpu_cf_alert` where the program defines it, else `app_main` |

`gemdos.c` stands in for GEMDOS, Supexec and the AES: Linux gives `trap #1`, `#2` and `#14` as SIGILL,
and the handler (in and out through `rt.S`'s ColdFire code, as QEMU's own signal return has a `not.b`)
does what TOS would.

`test_tlsfb` needs `python3` and `openssl`. What qemu-user can't do is the test's own: `ovl_flush` isn't
called (`cpushl` is supervisor-only; QEMU keeps its translations right by itself), Supexec calls its
function in user mode, and the program is linked with `-N` so that the heap the overlay is loaded into
may run code, as all of TOS's memory may.

## What only a real FireBee can prove

QEMU runs the ColdFire's instructions in user mode; it has no FireBee and no TOS. These need the
tester's machine, under FireTOS, FireTOS with FreeMiNT, and EmuTOS for the ColdFire:

- **TOS itself:** GEMDOS, the BIOS, the XBIOS and the AES as FireTOS and EmuTOS really answer (here
  they are stand-ins), the basepage they build, and that `_CF_` is in their cookie jars.
- **Exceptions in supervisor mode:** the crash catcher's frames as the CPU really pushes them (formats 5
  to 7 when the stack is out of line), `move usp` and `rte` for real, an access error's address in the
  MMU, and FireTOS's own handlers chained to.
- **STinG:** FireTOS's STinG-compatible stack called through `sting_call`, and a TLS handshake over it.
- **Speed:** QEMU counts no cycles; the V4e's caches, its 266 MHz and the four `mulu.w` a 64-bit product
  takes are measured only there.
- **The screen:** the FireBee's Videl modes (256, 65k and 16M colours) and its VDI drawing.
- **cf68klib:** that a 68000 or 68030 build started under FireTOS still runs as it does today, and that
  the ColdFire build started on an ST, a TT or a Falcon refuses with its alert (here: under QEMU's
  68030).
