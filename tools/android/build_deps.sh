#!/bin/bash
# Builds the two native prerequisites build_apk.sh expects in tools/android/deps
# (or $ANDROID_DEPS): SDL2 and libjpeg-turbo, cross-compiled for the device.
#
# This existed only as a scratch script for a long time, and tools/android/deps
# was a symlink into a session temp directory -- so a routine clean-up of that
# directory took the whole Android build out, with the failure showing up as
# "'SDL.h' file not found" from the glshim header rather than as a missing
# dependency. Keep the deps somewhere durable and this script next to the build
# that needs it.
set -euo pipefail

D=${ANDROID_DEPS:-$HOME/android-deps}
NDK=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/27.0.12077973}
TC=$NDK/build/cmake/android.toolchain.cmake
SDL=SDL2-2.30.11
JPEG=libjpeg-turbo-3.0.4

[ -f "$TC" ] || { echo "no NDK toolchain at $TC (set ANDROID_NDK)" >&2; exit 1; }
mkdir -p "$D"
cd "$D"

# The tarballs are not vendored; fetch them if they are not already here.
[ -f $SDL.tar.gz ] || curl -fsSL -o $SDL.tar.gz \
  "https://github.com/libsdl-org/SDL/releases/download/release-${SDL#SDL2-}/$SDL.tar.gz"
[ -f $JPEG.tar.gz ] || curl -fsSL -o $JPEG.tar.gz \
  "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${JPEG#libjpeg-turbo-}/$JPEG.tar.gz"
[ -d $SDL ] || tar xf $SDL.tar.gz
[ -d $JPEG ] || tar xf $JPEG.tar.gz

# arm64-v8a is the device; pass x86_64 to also build for the SDK emulator,
# which is what build_apk.sh keys its second ABI off.
for ABI in "${@:-arm64-v8a}"; do
    SUF=""; [ "$ABI" != arm64-v8a ] && SUF="-$ABI"
    cmake -S $SDL -B "sdl2-build$SUF" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-31 -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "sdl2-build$SUF" -j"$(nproc)" >/dev/null
    cmake -S $JPEG -B "jpeg-build$SUF" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-31 -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON >/dev/null
    cmake --build "jpeg-build$SUF" -j"$(nproc)" >/dev/null
    echo "built $ABI: $D/sdl2-build$SUF/libSDL2.so $D/jpeg-build$SUF/libturbojpeg.a"
done

# build_apk.sh looks through tools/android/deps; point it at what we just built.
LINK=$(cd "$(dirname "$0")" && pwd)/deps
[ -e "$LINK" ] || ln -s "$D" "$LINK"
echo "deps ready: $LINK -> $(readlink -f "$LINK")"
