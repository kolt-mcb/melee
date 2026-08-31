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
(arm64-v8a + x86_64, minSdk 31, GLES 3.0+ (3.2 preferred), landscape).

## Run on a device

```
adb install -r tools/android/app/app/build/outputs/apk/debug/app-debug.apk
tools/android/push_assets.sh orig/GALE01          # ~1 GB; see the script header
adb shell am start -n com.melee.pcport/org.libsdl.app.SDLActivity
adb logcat -s melee SDL SDL/APP AndroidRuntime libc DEBUG
```

The app reads the extracted disc from its external files directory, which
needs no storage permission. Do not `adb shell mkdir` that directory by
hand: adb creates it `drwxrws---` owned by `shell` and the app cannot
traverse it (every `vf_open` fails, black screen). `push_assets.sh`
launches the app once so Android creates `files/` with the right owner,
pushes, then `chmod -R a+rwX`.

`boot.dol` must be in `GALE01/sys/` — the 1-P difficulty tables, the SIS font
atlas and several data tables come from it.

## Run in the SDK emulator

The APK also carries x86_64 (`build_apk.sh` builds it when
`tools/android/deps/sdl2-build-x86_64` exists — same CMake recipe with
`-DANDROID_ABI=x86_64`). An API-35 x86_64 AVD with KVM runs the game;
headless works:

```
emulator -avd <name> -no-window -gpu swiftshader_indirect -no-snapshot -no-audio -memory 3072
```

Expect: no ES 3.1/3.2 config (`fell back to ES 3.0 D24 S8`, shaders as
`#version 300 es`), the low pool at `0x20000000` (512 MB: Dalvik owns
`0x14000000-0x20000000` and `0x40000000+` there), and ~17 fps — SwiftShader
is a software rasteriser. `adb exec-out screencap -p > shot.png` for frames.

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

## Status on hardware

Verified on a Pixel 9 (Mali-G715, Android 16): boots to a VS match at a
steady 60 fps, ~290 MB resident. Sound and speed are correct (the game is
a fixed-step 60 Hz simulation; if it ever runs fast, the pacer has been
defeated — check the `[PACE]` line in logcat).

## What to expect first time

Verified in the emulator (2026-08-31): boots, plays the opening movie,
runs the title. On a real device the unknowns are the Adreno/Mali shader
compilers (the shaders pass Mesa's ES 3.2 and SwiftShader's ES 3.0 front
ends), `highp` precision limits, and pacing against the display refresh.
If the screen stays black, `adb logcat -s melee` has the answer: the
context attempts (`SDL GL context creation failed (...)`), the pool
(`[MEM] Low-memory pool reserved at`), shader errors (`Shader compile
failed`), or `vf_open: open failed` (asset permissions, above). Input: a USB keyboard is
confirmed working on device (same bindings as the desktop); a Bluetooth
controller should work through SDL GameController but is untested, and
there is no touch overlay yet — so a phone on its own cannot play.
