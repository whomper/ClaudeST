# Notes for working on ClaudeST

- **Releases carry the program.** `binary/` and `dist/` are build output,
  never committed. `VERSION` in `st/claudest.c` holds the last release
  (a build shows `x.y-dev`; `RELEASE=1` shows `x.y`). A release:
  `tools/bump.py` (1.34 -> 1.35), commit, push, then run the Release
  workflow on main with the notes (or push a tag `vX.Y` made with
  `git tag -a`); `.github/workflows/release.yml` tags vX.Y, tests, builds
  `CLAUDEST.PRG` and the help and publishes them, with `st/CLAUDEST.INF`
  and the icons (`icons/`), as a GitHub Release. A version number is
  never reused: `tools/check_version.sh` fails when vX.Y is out already.
- Commit and push to `main`; when the Atari side changes, release it and
  give the user the release's link; say whether the Pi needs
  `cd ~/atari_claude/pi && git pull && sudo ./install.sh`.
- Run `make test` (the bridge's tests and the layout's) and build (`make`)
  before pushing.
- `common/` is the AERA programs' shared code (start-up, C library, TOS,
  AES/VDI, STinG, right-to-left layout), a git subtree of
  github.com/whomper/AERA-Atari. Change it there, never here, then
  `tools/sync-common.sh`; `make -C st test` runs the layout tests.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright
  lines (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help
  file's `@author`, the docs, and as the author of every commit:
  `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer (AERA-Atari, `standards/GITHUB-IDENTITY.md`).
