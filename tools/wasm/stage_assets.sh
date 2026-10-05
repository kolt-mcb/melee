#!/usr/bin/env bash
# Stage your own extracted game files for the WebAssembly page.
#
#   tools/wasm/stage_assets.sh [extracted-dir] [stage-dir]
#
# Defaults: orig/GALE01 -> build/wasm/stage. The stage is packaged into the
# page with WASM_ASSETS=<stage-dir> (see configure_pc.py), so it stays under
# build/, which is gitignored -- never commit it, it is the game's data.
#
# Hard links where possible, so it usually costs no space. Left out: the .mth
# movies (687 MB) and the .hps music streams. Kept: the .ssm sound-effect banks under audio/ --
# the game loads them at boot (lbAudioAx_80028690 -> HSD_SynthSFXLoad) and
# faults when main.ssm is missing.
#
# This packaging is a development convenience; port-wasm.md Phase 4 replaces
# it with the page reading the player's own folder.
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC=${1:-orig/GALE01}
DST=${2:-build/wasm/stage}
[ -f "$SRC/PlCo.dat" ] || { echo "no game data at $SRC (run tools/pc/extract_game.py)" >&2; exit 1; }
rm -rf "$DST"
mkdir -p "$DST/orig/GALE01"
DST_ABS=$(cd "$DST/orig/GALE01" && pwd)
# Hard links where the stage is on the same filesystem, a copy where not.
LINK=-l
ln "$SRC/PlCo.dat" "$DST_ABS/.probe" 2>/dev/null && rm -f "$DST_ABS/.probe" || LINK=
(cd "$SRC" && find . -type f ! -name '*.mth' ! -name '*.hps' -print0 |
    xargs -0 cp $LINK --parents -t "$DST_ABS/")
du -sh --apparent-size "$DST"
echo "now: WASM_ASSETS=$(cd "$DST" && pwd) WASM_LINK=1 EMSDK=<emsdk> python3 configure_pc.py && ninja -f build.ninja.wasm"
