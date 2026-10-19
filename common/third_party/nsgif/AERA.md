# libnsgif in the AERA family

NetSurf's GIF decoder (MIT licence, `COPYING`), from
https://github.com/netsurf-browser/libnsgif at commit
`22e99eb6818b1284d0f3ff1b7f46159e87221220` (6 Aug 2026). Only `include/`,
`src/` (without its Makefile), `COPYING` and `README.md` were taken; nothing
in them is changed. `src/pic.c` decodes a GIF's first frame with it
(R8G8B8A8, its own bitmaps), for EWeb's pages since 19 Oct 2026.

An app that builds it needs `<assert.h>` and `<inttypes.h>` among its
freestanding headers (EWeb's `atari/include/` has them), and, for a 68000,
the family's `-fno-store-merging -fdisable-tree-bswap` (the LZW reader
takes its codes a byte at a time).
