#!/bin/sh
# Every CLAUDEST.PRG that is committed must carry a new version number.
# Run before committing a new build: fails if st/CLAUDEST.PRG differs from
# the last committed one but VERSION in st/claudest.c is the same.
cd "$(dirname "$0")/.." || exit 1
now=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' st/claudest.c)
was=$(git show HEAD:st/claudest.c 2>/dev/null | sed -n 's/^#define VERSION "\(.*\)"/\1/p')
if ! git diff --quiet HEAD -- st/CLAUDEST.PRG && [ "$now" = "$was" ]; then
  echo "CLAUDEST.PRG changed but VERSION is still $now: raise it in st/claudest.c" >&2
  exit 1
fi
echo "version $now"
