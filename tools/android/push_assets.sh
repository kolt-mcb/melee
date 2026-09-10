#!/bin/bash
# Push the extracted disc into the app's external files directory.
#
#   tools/android/push_assets.sh [orig/GALE01] [--all]
#
# Skips MvOmake15.mth (466 MB, the gallery movie) unless --all. The app must
# be installed; it is launched once first so Android creates its files/
# directory with the app as owner. Everything pushed from adb is owned by
# `shell`, and a directory adb creates is drwxrws--- -- the app cannot even
# traverse it (vf_open: open failed on every file, black screen). The
# chmod at the end is what makes the tree readable by the app.
set -euo pipefail
SRC=${1:-orig/GALE01}
ALL=${2:-}
PKG=com.melee.pcport
DST=/sdcard/Android/data/$PKG/files/GALE01
[ -d "$SRC" ] || { echo "no such directory: $SRC"; exit 1; }
adb shell am start -n $PKG/org.libsdl.app.MeleeActivity > /dev/null
sleep 3
adb shell am force-stop $PKG
adb shell mkdir -p $DST
for e in "$SRC"/*; do
    n=$(basename "$e")
    if [ "$n" = MvOmake15.mth ] && [ "$ALL" != --all ]; then continue; fi
    adb push "$e" "$DST/" > /dev/null || echo "push failed: $n"
done
adb shell chmod -R a+rwX $DST
adb shell du -sh $DST
echo "pushed $(adb shell ls $DST | wc -l) entries (source has $(ls "$SRC" | wc -l))"
