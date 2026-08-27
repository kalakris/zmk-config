#!/usr/bin/env bash
# Refresh the vendored copy of the raw touch module from its own repo.
#
# The module lives at https://github.com/kalakris/zmk-raw-touch, which is
# private while its name and the protocol-v3 question are open. `west update`
# in CI authenticates anonymously and so cannot clone it, hence this vendored
# copy plus -DZMK_EXTRA_MODULES in build.yaml. Delete all of this once the
# repo is public and the west.yml entry is uncommented.
set -euo pipefail

SRC="${1:-$HOME/src/zmk-raw-touch}"
DEST="$(cd "$(dirname "$0")/.." && pwd)/vendor/zmk-raw-touch"

[ -d "$SRC/.git" ] || { echo "no git repo at $SRC" >&2; exit 1; }

SHA="$(git -C "$SRC" rev-parse HEAD)"
DIRTY="$(git -C "$SRC" status --porcelain)"
[ -n "$DIRTY" ] && echo "WARNING: $SRC has uncommitted changes; vendoring the working tree" >&2

rm -rf "$DEST"
mkdir -p "$DEST"
git -C "$SRC" ls-files -z | while IFS= read -r -d '' f; do
    mkdir -p "$DEST/$(dirname "$f")"
    cp "$SRC/$f" "$DEST/$f"
done

printf '%s\n' "$SHA" > "$DEST/.vendored-from-sha"
echo "vendored $SRC @ ${SHA:0:8} -> $DEST"
