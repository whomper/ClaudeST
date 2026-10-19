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

One change in the library's own files: `src/jmorecfg.h` defines
`DCT_FLOAT_SUPPORTED` only without `AERA_NO_FLOAT`, so the floating-point
IDCT (and libgcc's software floating point, which an Atari program can't
link) is left out. Keep it if the library is ever updated.

Build flags: `-DPIC_LIBJPEG -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT
-Icommon/third_party/libjpeg-turbo/src -Icommon/third_party/libjpeg-turbo/config`.

Measured (68030 instructions under QEMU, a 480x320 photo; `tests/pic`):
TJpgDec 26 M at full size and 26 M shown at 192x128; libjpeg-turbo 19 M and
8.5 M (its IDCT unpacks at n/8 of the size); a 1589x1043 photo shown at
596x391: TJpgDec 297 M, libjpeg-turbo 61 M.
