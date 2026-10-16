#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Signs the commits the last `git subtree pull --squash` made. git subtree
# makes its squash commit with commit-tree, which signs only when told to
# (-S; commit.gpgsign doesn't reach it, git 2.43), so GitHub shows that
# commit as Unverified. This makes the squash commit again, signed (the
# same tree, parent, author and message), and the merge on top of it.
#
#   sh common/tools/sign-subtree.sh BEFORE
#
# BEFORE is the commit HEAD was at before the pull (tools/sync-common.sh
# and tools/sync-gateway.sh pass it). Nothing happens when the pull made no
# commits, when this machine doesn't sign commits, or when both are signed.
set -e
before=$(git rev-parse "$1")
[ "$(git config --bool commit.gpgsign)" = true ] || exit 0
merge=$(git rev-parse HEAD)
[ "$merge" != "$before" ] || exit 0
squash=$(git rev-parse -q --verify HEAD^2) || exit 0
[ "$(git rev-parse HEAD^1)" = "$before" ] || exit 0
git cat-file commit "$squash" | grep -q '^git-subtree-dir:' || exit 0

signed() {
	git cat-file commit "$1" | sed '/^$/q' | grep -q '^gpgsig'
}
signed "$squash" && signed "$merge" && exit 0

# resign COMMIT [PARENT...]: COMMIT made again and signed, with its own
# parents or those given
resign() {
	c=$1
	shift
	[ $# -gt 0 ] || set -- $(git rev-list --parents -n 1 "$c" | cut -s -d' ' -f2-)
	ps=
	for p in "$@"; do
		ps="$ps -p $p"
	done
	git cat-file commit "$c" | sed '1,/^$/d' |
		GIT_AUTHOR_NAME=$(git log -1 --format=%an "$c") \
		GIT_AUTHOR_EMAIL=$(git log -1 --format=%ae "$c") \
		GIT_AUTHOR_DATE=$(git log -1 --format=%ad --date=raw "$c") \
		git commit-tree -S "$(git rev-parse "$c^{tree}")" $ps
}

new_squash=$(resign "$squash")
new_merge=$(resign "$merge" "$before" "$new_squash")
git update-ref -m "sign-subtree: the subtree pull's commits, signed" HEAD "$new_merge" "$merge"
echo "signed: $(git log -1 --format='%h %s' "$new_squash")"
echo "        $(git log -1 --format='%h %s' "$new_merge")"
