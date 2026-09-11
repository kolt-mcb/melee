# Melee PC port on Android

Status: builds, links, packages; renders on an OpenGL ES context and runs as a
PIE binary — both verified on Linux against the golden suite
(`MELEE_GLES=1`, `PC_PIE=1`), and verified on a Pixel 9 at 60 fps. See
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
adb shell am start -n com.melee.pcport/org.libsdl.app.MeleeActivity
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
failed`), or `vf_open: open failed` (asset permissions, above). See "Controllers" below for input;
there is still no touch overlay, so a phone with no pad attached cannot
play.

## Controllers

Pair the pad in Android's Bluetooth settings first (an Xbox Wireless
Controller: hold the pair button until the Xbox button flashes fast, then
pick it in *Settings → Connected devices → Pair new device*). No permission
and no in-app pairing UI is involved — a paired gamepad reaches the app as
an ordinary `InputDevice`, which SDL's Java shell reads and presents as an
`SDL_GameController`.

The pad bridge maps a controller SDL knows the layout of onto the GameCube
pad (`poll_controller` in `src/pc_stub/undef_stubs.c`). On an Xbox pad:

| Xbox | GameCube | |
|---|---|---|
| A / B / X / Y | A / B / X / Y | X and Y are both jump |
| Left stick | control stick | clamped to the console's radius 80 |
| Right stick | C-stick | smashes, taunt |
| RB | Z | grab |
| LB | L (digital) | full shield |
| LT / RT | L / R analog | light shield; a full press is the click |
| D-pad | D-pad | |
| Menu (☰) | Start | |
| View (⧉) | *nothing* | trapped, see below |

Two Android-only settings make this work, set natively in `window.c` and
again as `SDL_ENV.*` meta-data in the manifest (either wins over the other
harmlessly; a value in `melee.env` beats both):

- `SDL_ACCELEROMETER_AS_JOYSTICK=0` — SDL otherwise registers the phone's
  accelerometer as joystick device 0. The bridge assigns controller ports by
  device index, so tilt would drive player 1 and the pad player 2.
- `SDL_ANDROID_TRAP_BACK_BUTTON=1` — the View button and the system back
  gesture otherwise finish the activity, i.e. quit mid-match.

A Bluetooth pad that sleeps or wanders out of range is closed and reopened
on the same port (`pc_pad_open`); before, the port stayed silently dead
until the game was restarted. Ports are filled in SDL device order, so with
two pads the one that connects first is player 1.

For a pad SDL has no mapping for, `[PAD] port N: joystick <name> guid <guid>
(no button map; raw numbering)` appears in logcat. Add a matching
[gamecontrollerdb.txt](https://github.com/mdqinc/SDL_GameControllerDB) line
next to the assets:

```
adb push gamecontrollerdb.txt /sdcard/Android/data/com.melee.pcport/files/
```

or put a single `SDL_GAMECONTROLLERCONFIG=<line>` in `melee.env`. A mapped
pad logs its mapping string instead, which is the thing to read when a
button lands in the wrong place.

A USB keyboard also works, with the desktop bindings.

## Fullscreen

The game runs edge to edge: the activity theme is
`Theme.NoTitleBar.Fullscreen` with `windowLayoutInDisplayCutoutMode`
`shortEdges`, and `window.c` asks for `SDL_WINDOW_FULLSCREEN_DESKTOP` on
Android, which is what drives `SDLActivity.setWindowStyle(true)` —
`IMMERSIVE_STICKY`, both system bars hidden, and re-hidden if a swipe
brings them back. The theme alone would not do it: SDLActivity calls
`setWindowStyle(false)` when it starts.

The cutout mode is the half SDL cannot do. Left at the default, a landscape
activity is letterboxed clear of the camera hole, costing a black band down
one short edge even in immersive mode. The game's own 4:3 image is centred
inside whatever surface results (`pc_fb_rect_to_window`), so nothing it
draws ends up under the camera.

The activity is `MeleeActivity`, an `SDLActivity` subclass, for the other
half: on Android 16 SDL's legacy `setSystemUiVisibility` hides the navigation
bar but no longer the status bar, which returns as a transient bar and stays
drawn over the game (`dumpsys window` shows
`mShowingTransientInsetsTypes=statusBars` with nothing having touched the
screen). `WindowInsetsController.hide(systemBars())` does hide it, and is
re-applied on every focus gain because the bars come back with the activity.
Launch it as `com.melee.pcport/org.libsdl.app.MeleeActivity`.
