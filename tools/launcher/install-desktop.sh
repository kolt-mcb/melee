#!/bin/sh
# Generate a desktop entry pointing at this clone. The .desktop format has no
# relative-path support, so the absolute path is filled in at install time
# rather than committed.
set -eu
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST="${1:-$HOME/.local/share/applications}"
mkdir -p "$DEST"
sed "s|@REPO@|$REPO|g" "$REPO/tools/launcher/melee-pc.desktop.in" \
    > "$DEST/melee-pc.desktop"
echo "Installed $DEST/melee-pc.desktop -> $REPO"
