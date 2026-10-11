#!/bin/sh
# Every ECLAUDE.PRG that is committed must carry a new version number.
# Run before committing a new build: fails if st/ECLAUDE.PRG differs from
# the last committed one but VERSION in st/eclaude.c is the same.
cd "$(dirname "$0")/.." || exit 1
now=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' st/eclaude.c)
was=$(git show HEAD:st/eclaude.c 2>/dev/null | sed -n 's/^#define VERSION "\(.*\)"/\1/p')
if ! git diff --quiet HEAD -- st/ECLAUDE.PRG && [ "$now" = "$was" ]; then
  echo "ECLAUDE.PRG changed but VERSION is still $now: raise it in st/eclaude.c" >&2
  exit 1
fi
echo "version $now"
