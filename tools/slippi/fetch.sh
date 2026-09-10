#!/bin/sh
# Fetch the Slippi upstream sources listed in tools/slippi/manifest.json into
# extern/slippi/. Nothing fetched here is compiled into melee-pc -- see the
# manifest's own comment for what each repository is for and why the licences
# do not mix. The tree is gitignored; re-running is cheap and idempotent.
#
#   tools/slippi/fetch.sh            fetch everything in the manifest
#   tools/slippi/fetch.sh <name>...  fetch only the named repositories
#
# A repository with a "commit" is checked out at exactly that commit, so the
# reference material cannot move under a build. One left empty tracks the
# default branch; fetch.sh prints what it landed on so it can be pinned.
set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST=$ROOT/extern/slippi
MANIFEST=$ROOT/tools/slippi/manifest.json

[ -f "$MANIFEST" ] || { echo "fetch.sh: no $MANIFEST" >&2; exit 1; }
mkdir -p "$DEST"

# The manifest is JSON so it can carry comments and stay readable; python is
# already a build dependency (configure_pc.py), so parse it there and emit one
# tab-separated line per repository.
python3 - "$MANIFEST" "$@" <<'PY' | while IFS="" read -r name url commit sparse why
import json, sys
m = json.load(open(sys.argv[1]))
only = set(sys.argv[2:])
for r in m["repos"]:
    if only and r["name"] not in only:
        continue
    print("\x1f".join([r["name"], r["url"], r.get("commit", ""),
                     ",".join(r.get("sparse", [])), r.get("why", "")]))
PY
do
    # The fields are separated by US (0x1f), not by whitespace: a repository
    # with no pinned commit emits an empty field, and whitespace separators
    # collapse runs, which would silently shift every field after it. Restore
    # a normal IFS here so the comma-separated sparse list still splits.
    IFS=" 	
"
    dir=$DEST/$name
    echo "=== $name  ($why)"
    if [ ! -d "$dir/.git" ]; then
        if [ -n "$sparse" ]; then
            # A sparse clone: Ishiiruka is a full Dolphin tree and only its
            # Slippi subsystem is of any interest here. --no-cone because the
            # list names individual files as well as directories.
            git clone --filter=blob:none --no-checkout -q "$url" "$dir"
            git -C "$dir" sparse-checkout set --no-cone $(echo "$sparse" | tr ',' ' ')
            git -C "$dir" checkout -q
        elif [ -n "$commit" ]; then
            git clone -q "$url" "$dir"
        else
            git clone --depth 1 -q "$url" "$dir"
        fi
    fi
    if [ -n "$commit" ]; then
        git -C "$dir" fetch -q origin "$commit" 2>/dev/null || git -C "$dir" fetch -q origin
        git -C "$dir" checkout -q "$commit"
    fi
    echo "    $(git -C "$dir" rev-parse HEAD)  $(du -sh "$dir" | cut -f1)"
done

echo
echo "Fetched into $DEST. Build with Slippi support:"
echo "    PC_SLIPPI=1 python3 configure_pc.py && ninja -f build.ninja.pc"
