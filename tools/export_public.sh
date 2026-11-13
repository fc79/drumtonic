#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Put this repository's committed tree into the public one as one new commit (and a tag), without the development
# history: tools/export_public.sh PUBLIC_DIR VERSION      e.g. tools/export_public.sh ../drumtonic-public 0.1
#
# PUBLIC_DIR is a separate clone of the public repository (made here with `git init` the first time). Only what
# `git archive HEAD` holds goes in: committed files, nothing from build/ or uncommitted work. Author, committer and
# tagger are fc79 with GitHub's noreply address. Nothing is pushed: check, then `git -C PUBLIC_DIR push origin main
# vVERSION` (the tag starts the release workflow: packages, release, the installer page).
set -eu
DEST=${1:?public repository folder}
V=${2:?version, e.g. 0.1}
NAME=fc79
MAIL=322146628+fc79@users.noreply.github.com
SRC=$(cd "$(dirname "$0")/.." && pwd)

[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ] || { echo "export: commit first ($SRC has changes)"; exit 2; }
mkdir -p "$DEST"
[ -d "$DEST/.git" ] || git -C "$DEST" init -q -b main
git -C "$DEST" rev-parse -q --verify "refs/tags/v$V" >/dev/null && { echo "export: v$V exists in $DEST"; exit 2; }

git -C "$DEST" rm -rq --ignore-unmatch . >/dev/null
git -C "$SRC" archive HEAD | tar -x -C "$DEST"
git -C "$DEST" add -A
export GIT_AUTHOR_NAME=$NAME GIT_AUTHOR_EMAIL=$MAIL GIT_COMMITTER_NAME=$NAME GIT_COMMITTER_EMAIL=$MAIL
git -C "$DEST" -c user.name=$NAME -c user.email=$MAIL commit -q -m "Drumtonic $V"
git -C "$DEST" -c user.name=$NAME -c user.email=$MAIL tag -a "v$V" -m "Drumtonic $V"

echo "$DEST: Drumtonic $V, $(git -C "$DEST" ls-files | wc -l | tr -d ' ') files"
echo "emails in it (only $MAIL expected):"
{ git -C "$DEST" log --all --format='%ae%n%ce'; git -C "$DEST" for-each-ref --format='%(taggeremail)' refs/tags; } | tr -d '<>' | sort -u
