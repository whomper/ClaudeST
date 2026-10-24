# libwebp in the AERA family

Google's WebP library, its decoder only (BSD 3-clause licence, `COPYING`,
with Google's patent grant, `PATENTS`), from
https://github.com/webmproject/libwebp at its release v1.6.0 (commit
`4fa21912338357f89e4fd51cf2368325b59e9bd9`). Only the decoder's sources
were taken: `src/dec/` (without `idec_dec.c`), the plain C ones of
`src/dsp/` and `src/utils/` that it needs (no SIMD, no encoder), the
headers they include, `COPYING`, `PATENTS`, `AUTHORS` and `README.md`;
nothing in them is changed. Two files are AERA's own:

- `src/webp/config.h`, the build's settings (upstream's build makes it):
  plain C, no threads, `WEBP_REDUCE_SIZE` and `WEBP_REDUCE_CSP`;
- `aera/random_utils.c`, built in place of `src/utils/random_utils.c`:
  the same, with `VP8InitRandom`'s dithering amplitude worked out from
  the float's bits in integers (the same value for every float, checked
  for over a million). libgcc's soft-float routines for the 68030 reach
  their state through a GOT, which a TOS program can't have, and
  `EWEB.PRG` has no FPU instruction; it is the decoder's only float
  arithmetic (pic.c never asks for dithering).

`src/pic.c` decodes WebP with it when built with `-DPIC_LIBWEBP` (EWeb's
`EWEB.PRG`, since 24 Oct 2026); the other programs' builds don't change.
A build compiles the `.c` files listed in `tests/pic/Makefile`'s `WP_C`,
and `aera/random_utils.c`, with `-DHAVE_CONFIG_H` and this folder on the
include path. A lossy picture comes to pic.c's scaler a band of rows at a
time (libwebp's `VP8Io` and its `put`), so it is never held whole; a
lossless one is (libwebp's own decoder keeps it, 4 bytes a pixel, and
pic.c takes it from there). An animation isn't taken (PIC_UNSUPPORTED).
