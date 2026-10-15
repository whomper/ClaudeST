# Notes for working on ClaudeST

ClaudeST is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds with `RELEASE=1`, tags vX.Y and publishes each file and the zip as a
  GitHub Release. A normal build must leave every tracked file unchanged (it shows `x.y-dev`). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release
  workflow from `main`, nothing else: no test builds, no `x.y-dev` builds, no CI artifacts, no builds
  from a branch. Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't
  work, roll back with a release: revert on `main`, release the next number, and say in the bad
  release's notes that it is withdrawn and which one to use.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## ClaudeST

- **The version** is `#define VERSION` in `st/claudest.c`, not a `VERSION` file: `tools/bump.py`
  raises it (1.34 -> 1.35), and `tools/check_version.sh` fails when vX.Y is out already (the
  release workflow runs it). A release carries `CLAUDEST.PRG`, `CLAUDEST.HYP` and `.REF`, the
  example `st/CLAUDEST.INF` and the icon (`icons/`).
- `bridge/` is ClaudeST's; `gateway/` is a read-only copy of
  [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s, whose
  `aera-gateway install claude` sets the bridge up (`pi/install.sh` hands
  on to it). Change the installer there, with that repository's owner
  session, and bring it here with `tools/sync-gateway.sh`. ClaudeST says
  what the `claude` recipe needs (packages, settings, the unit, port 2323).
- Commit and push to `main`; when the Atari side changes, release it and
  give the user the release's link; say whether the Pi needs
  `cd ~/atari_claude/pi && git pull && sudo ./install.sh`.
- Run `make test` (the bridge's tests and the layout's) and build (`make`)
  before pushing.
