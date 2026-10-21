# libjpeg-turbo in the AERA family

The JPEG decoder EWeb uses (`src/pic.c` built with `-DPIC_LIBJPEG`): the
library NetSurf and the other browsers take, from
https://github.com/libjpeg-turbo/libjpeg-turbo at release 3.1.4
(`e352b02f794f701407b39af08576035ba3360d60`). Its licences (the IJG's and the
modified BSD licence) are in `LICENSE.md` and `README.ijg`; both allow it in
GPL programs.

Only what decoding 8-bit JPEGs needs was taken (`src/`): no encoder, no SIMD,
no 12-bit, 16-bit or lossless decoders (`src/pic_lj8.c` stands in for their
entry points), no `jerror.c` (it prints to stderr and calls `exit()`: pic.c
has its own error handler, which jumps back), no `jdatasrc.c` (pic.c reads
from memory itself) and no `jidctflt.c`. `config/` holds the headers CMake
makes, with the size of `size_t` the compiler's and no thread-local storage.

Two changes in the library's own files; keep them if it is ever updated:

- `src/jmorecfg.h` defines `DCT_FLOAT_SUPPORTED` only without
  `AERA_NO_FLOAT`, so the floating-point IDCT (and libgcc's software
  floating point, which an Atari program can't link) is left out.
- `src/jdcolor.c`: `JCS_RGB565` on a big-endian machine gives the
  machine's own 16-bit values (upstream swaps them to little-endian order
  everywhere, Android's), so pic.c can have libjpeg write a picture shown
  at its size straight into its pixels.

Build flags: `-DPIC_LIBJPEG -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT
-Icommon/third_party/libjpeg-turbo/src -Icommon/third_party/libjpeg-turbo/config`.

Measured (68030 instructions under QEMU, a 480x320 photo; `tests/pic`):
TJpgDec 26 M at full size and 26 M shown at 192x128; libjpeg-turbo 19 M and
8.5 M (its IDCT unpacks at n/8 of the size); a 1589x1043 photo shown at
596x391: TJpgDec 297 M, libjpeg-turbo 61 M.

Since 20 Oct 2026 pic.c asks for the faster integer IDCT (`JDCT_IFAST`)
and, for a picture shown at the size it unpacks to, 5-6-5 pixels written
straight into the picture; otherwise the RGB rows go to the scaler as they
are (before, each pixel was copied to RGBA, then packed with its alpha
checked). The decoder's own functions on the 480x320 photo (`tools/prof68k.py`
in EWeb): 22.6 M instructions at its size before, 16.7 M after (IDCT 8.0 M
to 5.0 M; colour, packing and copies 9.1 M to 6.2 M); shown at 192x128,
10.2 M to 8.9 M. Against a decode in floating point (Pillow), the photo
was 39.2 dB with the exact IDCT and is 38.2 dB with the fast one, both
with the 5-6-5 pixels' own loss: no one sees it. libjpeg's merged
upsampling would save 40% in all, but makes colour fringes on sharp
coloured edges (31.7 dB), so it isn't used.

Since 20 Oct 2026 pic.c can hand a baseline JPEG to the Falcon's DSP
(common/README.md, JPEGs on the DSP): libjpeg reads the headers and
starts the decompression; pic.c builds with `JPEG_INTERNALS` for the
natural order, and sends the scan from where libjpeg's source stopped
(`next_input_byte`) to the DSP, with the Huffman and quantisation tables
libjpeg read, in place of `jpeg_read_scanlines`. Nothing in libjpeg
changed for it.

Since 21 Oct 2026 `src/jdtrans.c` is taken too (`jpeg_read_coefficients`,
unchanged): a progressive or arithmetic-coded JPEG's scans are read into
coefficients by libjpeg on the 68030, and the DSP does the transform,
colours and shrinking from them (common/README.md); on the 68030 alone
the picture goes through libjpeg's own output as before.
