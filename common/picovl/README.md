# PIC.OVL

The Falcon's DSP and a 256-colour screen for pictures, in a file of its own
(`src/picovl.h`): 68030 code, loaded by a program on a 68020 or later
(`atari/pic_ovl.c`) into its own memory, as TLS.OVL is. An ST never loads it;
a program without it does the same work its own way, as it did before.

`make` builds `build/pic.ovl`; `make check` that it is `pic.ovl` byte for byte;
`make release` after raising `VERSION`. `tests/pic`'s `make testdsp` runs
`test_pic_ovldsp`: PIC.OVL's dither entries against the 68030's dither, the
same pens to the bit, at 4, 16 and 256 colours.

| Entry | What | Who may use it |
|---|---|---|
| `prepare`, `dither`, `dither_state` | a picture's pens on the DSP (`dsp/dither.a56`), checked once against the 68030's | any program with PIC.OVL that dithers with `pic.c` |
| `near_load`, `near_save` | `pic.c`'s table of nearest pens (32 KB) kept in a file between starts, for its palette | any program dithering in 16 or 256 colours (EContact: `cache\near.dat`) |
| `dither_cells` | small picture cells shown at another size, dithered on the DSP, with a mask of one span a row | a small picture shown smaller or larger than kept (EContact's round contact photos) |
| `chunky8` | an 8-bit chunky picture to an 8-plane screen in two copies (a mask, the picture) | a small picture in 256 colours, transparent where it isn't (EContact's photos) |

The program lends it its memory, clock, log, files and two VDI calls
(`PIC_HOST`); both tables only ever grow at the end (`PICOVL_MAJOR` otherwise).
