# ClaudeST for the Atari

Everything that goes on the Atari, ready to use:

| File | What it is |
|------|-----------|
| `CLAUDEST.PRG` | the program |
| `CLAUDEST.HYP` | the help, for ST-Guide: keep it next to `CLAUDEST.PRG`, and press Help in ClaudeST |
| `CLAUDEST.INF` | an example settings file: put your Pi's address on its `tcp` line, and keep it next to `CLAUDEST.PRG` |
| `CLAUDEST.RSC` | the desktop icon, black-and-white and 16-colour |
| `CLAUDEST.ICN`, `CLAUDSMK.ICN` | the black-and-white icon and its mask, for icon editors |

The [README](../README.md) has the setup, and the [user guide](../docs/GUIDE.md)
everything else. The program is built from `st/` with `make -C st`.
