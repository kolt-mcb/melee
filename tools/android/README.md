# Melee PC port on Android

Status: builds, links, packages; renders on an OpenGL ES context and runs as a
PIE binary — both verified on Linux against the golden suite
(`MELEE_GLES=1`, `PC_PIE=1`). Not yet run on a device. See
`../../port-android.md` for the plan and the per-phase results.

## Build

```
ANDROID_NDK=~/Android/Sdk/ndk/<ver> tools/android/build_apk.sh        # debug
ANDROID_NDK=~/Android/Sdk/ndk/<ver> tools/android/build_apk.sh release
```

One-time prerequisites (all outside the tree, see `build_apk.sh` header):

- `tools/android/deps/` (or `$ANDROID_DEPS`): `SDL2-2.30.x/` + `sdl2-build/`
  and `libjpeg-turbo-3.x/` + `jpeg-build/`, both built for arm64-v8a with the
  NDK's CMake toolchain (`-DANDROID_PLATFORM=android-31`, Release).
- `tools/android/app/local.properties` with `sdk.dir=...`; JDK 17; the
  Gradle wrapper fetches AGP 8.1.1.

Output: `tools/android/app/app/build/outputs/apk/debug/app-debug.apk`
(arm64-v8a only, minSdk 31, GLES 3.2 required, landscape).

## Run on a device

The app reads the extracted disc from its external files directory, which
needs no storage permission:

```
adb install -r tools/android/app/app/build/outputs/apk/debug/app-debug.apk
adb shell mkdir -p /sdcard/Android/data/com.melee.pcport/files
adb push orig/GALE01 /sdcard/Android/data/com.melee.pcport/files/GALE01   # ~1 GB
adb shell am start -n com.melee.pcport/org.libsdl.app.SDLActivity
adb logcat -s melee SDL SDL/APP AndroidRuntime libc DEBUG
```

`boot.dol` must be in `GALE01/sys/` — the 1-P difficulty tables, the SIS font
atlas and several data tables come from it.

### Knobs

Android apps have no environment, so every `MELEE_*` variable the desktop
port honours is read from `files/melee.env` at startup (`KEY=VALUE` per
line, `#` comments; a real environment variable, if any, wins):

```
adb shell 'cat > /sdcard/Android/data/com.melee.pcport/files/melee.env' <<EOF
MELEE_BOOT_MODE=14
MELEE_BOOT_MATCH=13,12,9,0,0
MELEE_FPS=1
EOF
```

Everything the port would print to a terminal — `PORT_LOG_*`, `OSReport`,
raw `fprintf(stderr)` — is forwarded line by line to logcat under the tag
`melee` (`src/port/pc_android.c`). A native crash prints the port's own
`[CRASH]` block there before Android's tombstone.

### Symbolising

`build/android/arm64-v8a/libmain.so` is the unstripped library; the APK
carries a stripped copy. For a `[CRASH]` pc or a tombstone frame:

```
$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-addr2line \
    -f -e build/android/arm64-v8a/libmain.so 0x<offset>
```

## What to expect first time

Untested territory, in the order it is likely to bite: the GLES shader
compilers on Adreno/Mali (the shaders pass Mesa's ES 3.2 front end — a
stricter or laxer driver may differ); the 1 GB `MAP_NORESERVE` low-memory
reservation (`[MEM] Low-memory pool reserved at ...` must appear in logcat
with an address below 4 GB); `highp` precision limits; frame pacing against
the display's refresh. Input is SDL GameController — a Bluetooth pad works,
touch does not yet.
