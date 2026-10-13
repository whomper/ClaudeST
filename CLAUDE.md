# Notes for working on ClaudeST

- **Every new build gets a new version.** Whenever `st/CLAUDEST.PRG` is
  rebuilt with a change, raise `VERSION` in `st/claudest.c` (1.17 -> 1.18),
  even for a fix or a rebuild that is later reverted: a version number is
  never reused for a different program. Run `tools/check_version.sh`
  before committing a build.
- Commit and push to `main`, and send the user the new `st/CLAUDEST.PRG`
  when the Atari side changes; say whether the Pi needs
  `cd ~/atari_claude/pi && git pull && sudo ./install.sh`.
- Run the bridge tests (`cd bridge && python3 -m unittest test_bridge`)
  and build (`make -C st`) before pushing.
