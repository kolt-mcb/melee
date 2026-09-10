#!/bin/sh
# Fetch Project Slippi's own Dolphin builds into extern/slippi/dolphin/, so the
# port can be run beside the emulator everyone else uses -- and so a replay this
# port wrote can be handed to the program that is the reference reader for the
# format.
#
# Two builds, because they are not interchangeable:
#
#   netplay   Slippi_Online-x86_64.AppImage. The one people play on. It has the
#             Slippi Recording gecko codes enabled, so it writes .slp files of
#             its own; it has no -i flag and cannot open one.
#   playback  Slippi_Playback-x86_64.AppImage. Takes -i <comm.json> naming a
#             replay and plays it back through the real game.
#
# Both are AppImages and are extracted rather than run through FUSE, which is
# not available everywhere. extern/slippi/ is gitignored; nothing here is built
# or linked into melee-pc.
set -e

NETPLAY_VER=3.6.4
PLAYBACK_VER=3.5.2

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST=$ROOT/extern/slippi/dolphin
mkdir -p "$DEST"
cd "$DEST"

if [ ! -x squashfs-root/usr/bin/dolphin-emu ]; then
    echo "=== Slippi netplay Dolphin $NETPLAY_VER"
    [ -f Slippi_Online-x86_64.AppImage ] || curl -fsSL -o Slippi_Online-x86_64.AppImage \
        "https://github.com/project-slippi/Ishiiruka/releases/download/v$NETPLAY_VER/Slippi_Online-x86_64.AppImage"
    chmod +x Slippi_Online-x86_64.AppImage
    ./Slippi_Online-x86_64.AppImage --appimage-extract > /dev/null
fi
echo "netplay   $DEST/squashfs-root/usr/bin/dolphin-emu  ($(./squashfs-root/usr/bin/dolphin-emu --version))"

if [ ! -x playback/squashfs-root/usr/bin/dolphin-emu ]; then
    echo "=== Slippi playback Dolphin $PLAYBACK_VER"
    [ -f playback-Linux.zip ] || curl -fsSL -o playback-Linux.zip \
        "https://github.com/project-slippi/Ishiiruka-Playback/releases/download/v$PLAYBACK_VER/playback-$PLAYBACK_VER-Linux.zip"
    mkdir -p playback
    unzip -q -o playback-Linux.zip -d playback
    chmod +x playback/Slippi_Playback-x86_64.AppImage
    (cd playback && ./Slippi_Playback-x86_64.AppImage --appimage-extract > /dev/null)
fi
echo "playback  $DEST/playback/squashfs-root/usr/bin/dolphin-emu  ($(./playback/squashfs-root/usr/bin/dolphin-emu --version))"

echo
echo "Side by side with the port:"
echo "    tools/slippi/side_by_side.sh"
