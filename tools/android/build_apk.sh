#!/bin/bash
# Build the Android APK of the PC port.
#
#   ANDROID_NDK=~/Android/Sdk/ndk/<ver> tools/android/build_apk.sh [debug|release]
#
# Steps (see port-android.md):
#   1. configure_pc.py ANDROID_NDK flavour -> build/android/arm64-v8a/libmain.so
#   2. strip it and stage it with libSDL2.so into app/jniLibs/arm64-v8a
#   3. Gradle assembles the APK around SDL's Java shell
#
# Prerequisites, one time: tools/android/deps (or $ANDROID_DEPS) holding
# SDL2-2.30.x/ + sdl2-build/ and libjpeg-turbo-3.x/ + jpeg-build/, both built
# for arm64-v8a with the NDK's CMake toolchain (android-31, Release);
# tools/android/app/local.properties with sdk.dir.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
: "${ANDROID_NDK:?set ANDROID_NDK to the NDK root}"
DEPS=${ANDROID_DEPS:-$ROOT/tools/android/deps}
VARIANT=${1:-debug}
STRIP="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

# arm64-v8a always; x86_64 (the SDK emulator) when its deps were built.
ABIS="arm64-v8a"
[ -f "$DEPS/sdl2-build-x86_64/libSDL2.so" ] && ABIS="$ABIS x86_64"
for ABI in $ABIS; do
    SUFFIX=""; [ "$ABI" != arm64-v8a ] && SUFFIX="-$ABI"
    ANDROID_ABI=$ABI python3 configure_pc.py
    ninja -f "build.ninja.android$SUFFIX"
    JNI="$ROOT/tools/android/app/app/jniLibs/$ABI"
    mkdir -p "$JNI"
    "$STRIP" -o "$JNI/libmain.so" "$ROOT/build/android$SUFFIX/$ABI/libmain.so"
    cp "$DEPS/sdl2-build$SUFFIX/libSDL2.so" "$JNI/"
done

cd "$ROOT/tools/android/app"
if [ "$VARIANT" = release ]; then
    ./gradlew assembleRelease --no-daemon
    ls -la app/build/outputs/apk/release/
else
    ./gradlew assembleDebug --no-daemon
    ls -la app/build/outputs/apk/debug/
fi
