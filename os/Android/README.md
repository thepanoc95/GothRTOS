# GothRTOS on Android

This directory contains the Android launcher, the native `gothdroid` runtime
and `libgothgfx`, the Lua C module used by `SystemUI.lua`.

GothRTOS does not replace Android's Linux kernel. The launcher installs a small
userland under `/data/local/tmp/gothrtos`, stops the Android framework through
root, and starts a native process that hosts the **real GothRTOS kernel**:
`rtos/kernel.c` initializes the static task table, IPC endpoints and syscalls,
`SystemUI.lua` runs as a GothRTOS task with the `goth` userland API from
`os/init/gothlib.c`, and `goth_um_run()` drives the cooperative scheduler.
Framebuffer and evdev access remain userspace policy in `libgothgfx`, exactly
as on the other ports. When GothRTOS exits, the supervisor restarts Android.

Android's bionic libc has no `makecontext`/`swapcontext`, so `gothdroid` uses a
platform expert, `pexpert/android/pe_android.c`, that implements the same
`um_internal.h` contract with one pthread per GothRTOS task and a scheduler
token. The host build uses the regular `pexpert/um/pe_linux.c` expert (and can
force the pthread one with `-DGOTHDROID_PLATFORM=android` for testing).

## Layout

| Path | Purpose |
| --- | --- |
| `gothdroid/` | Gradle/Compose launcher (`com.thepanoc95.gothrtos.gothdroid`) |
| `AndroidIO.cpp` | Native `gothdroid` executable; hosts the kernel and runs `SystemUI.lua` as a task |
| `gothgfx/gothgfx.c` | `libgothgfx`: framebuffer + evdev + drawing + Lua bindings |
| `gothgfx/gothgfx_font.h` | Embedded public-domain 8x13 console font |
| `SystemUI/SystemUI.lua` | Touch UI drawn on the framebuffer |
| `SurfaceLinger.lua` | Stop-file/stop-request helper |
| `devices/harpia.lua` | Example device profile (Moto G4 Play) |
| `gothdroid-run.sh` | Root supervisor: stop Android, run UI, start Android |
| `build-native.sh` | Builds `gothdroid` and `libgothgfx.so` with the NDK |
| `CMakeLists.txt` | Host/NDK build for the native pieces |
| `linker.ld` | Optional freestanding linker script (not used by the APK) |
| `../pexpert/android/pe_android.c` | pthread-based GothRTOS platform expert for Android |

## How starting works

The app never executes `/system/sbin/stop` in its own process. Doing so would
kill zygote and the app itself before the UI could start. Instead,
`GothRuntime.startRuntime()` asks root to run the detached supervisor:

```sh
su -c "sh /data/local/tmp/gothrtos/gothdroid-run.sh /data/local/tmp/gothrtos"
```

The supervisor:

1. Detaches with `setsid` (or `nohup`) so Android's `stop` cannot kill it.
2. Runs `/system/sbin/stop` (falling back to `/system/bin/stop`) and waits for
   zygote to stop.
3. Starts `gothdroid`, which initializes the GothRTOS kernel, runs
   `SystemUI/SystemUI.lua` as a kernel task with the `goth` userland API, opens
   `/dev/graphics/fb0` and auto-detects touch devices in `/dev/input`.
4. Monitors the UI process and the stop file.
5. Runs `/system/sbin/start` (falling back to `/system/bin/start`) from an
   `EXIT` trap, so Android comes back after a normal exit, a crash, a signal
   or a stop request.

Logs are written to:

```
/data/local/tmp/gothrtos/gothdroid.log
/data/local/tmp/gothrtos/supervisor.log
```

The supervisor removes itself and the stop file when it exits. Only one
supervisor can run at a time (`.supervisor.lock` plus `supervisor.pid`).

## Build the native runtime

Install an Android NDK and run:

```sh
os/Android/build-native.sh      # or: make android-native
```

By default it builds four ABIs (`arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`)
and stages:

```
os/Android/prebuilt/<abi>/gothdroid
os/Android/prebuilt/<abi>/libgothgfx.so
```

This has been verified with NDK r27c (`27.2.12479018`) and CMake 4.x. The
module keeps `lua_*` symbols undefined, as a loadable Lua module should; the
NDK is told to allow that via `ANDROID_ALLOW_UNDEFINED_SYMBOLS` in
`CMakeLists.txt`, so no `-Wl,--no-undefined` failure.

Use `GOTHDROID_ABIS="arm64-v8a"` to build only one ABI. `ANDROID_NDK_HOME` can
point at a specific NDK; otherwise the script looks under `$ANDROID_HOME/ndk`.

`prebuilt/` and the intermediate `build/` tree are ignored by git; they are
produced locally and then packaged by Gradle.

The Gradle build stages those files into APK assets automatically.

## Build and install the app

```sh
cd os/Android/gothdroid
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The APK contains:

```
assets/gothrtos/SystemUI/SystemUI.lua
assets/gothrtos/SurfaceLinger.lua
assets/gothrtos/devices/harpia.lua
assets/gothrtos/gothdroid-run.sh
assets/gothrtos/bin/<abi>/gothdroid
```

`GothRuntime.installRuntime()` extracts those assets into the app's private
storage, pushes them to `/data/local/tmp/gothrtos` as root, selects the device
ABI, and makes the runtime executable. `isRuntimeInstalled()` only reports
success when the `gothdroid` binary is present.

## Using the app

1. Grant root when the app asks for it.
2. Tap **Install runtime**.
3. Tap **Start GothRTOS**. Android's framework stops and the framebuffer UI
   appears. The launcher app is normally killed by `stop`.
4. In GothRTOS:
   - tap a tile to open **Console**, **Diagnostics**, **Rotate** or
     **Reboot Android**;
   - **Back** returns to the launcher, or exits GothRTOS from the launcher;
   - **Volume Up** rotates, **Volume Down** opens diagnostics.
5. **Reboot Android** writes the stop file; the supervisor runs
   `/system/sbin/start` and Android returns.

The app also has **Stop GothRTOS** for the case where it is still alive while
the UI is running (for example on a desktop/framebuffer test image).

## libgothgfx Lua API

```lua
local gfx = require("gothgfx")

local screen = assert(gfx.open{
    fb = "/dev/graphics/fb0",   -- default
    rotate = 0,                 -- 0, 90, 180, 270
    input = nil,                -- nil = auto-detect /dev/input/event*
    -- input = "/dev/input/event3",
    -- input = { "/dev/input/event3", "/dev/input/event4" },
    -- input = false,           -- no touch input
    -- x_min = 0, x_max = 720, y_min = 0, y_max = 1280,  -- override evdev range
    -- memory = true, width = 720, height = 1280, bpp = 32,
})

screen:clear(0x101018)
screen:fill(40, 40, 200, 80, 0x2b6cb0)
screen:rect(40, 40, 200, 80, 0x7c5cff, 2)
screen:line(0, 0, 100, 100, 0xffffff)
screen:text(56, 64, "GothRTOS", 0xffffff, 2)
screen:present()

for _, event in ipairs(screen:pollEvents(16)) do
    -- event.type   "touch" | "key"
    -- event.action "down" | "up" | "move" | "cancel"
    -- event.id     touch slot
    -- event.x/y    logical (rotated) coordinates
    -- event.raw_x/raw_y  evdev coordinates
    -- event.code   Linux key code for key events
end

screen:close()
```

Module helpers: `gothgfx.create(width, height, bpp)` (memory surface),
`scanInputs()`, `now()`, `sleep(ms)`, `keyName(code)`, `KEY_BACK`, `KEY_HOME`,
`KEY_MENU`, `KEY_ENTER`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_POWER`,
`fontWidth`, `fontHeight`.

Display methods: `info`, `size`, `clear`, `pixel`, `getPixel`, `fill`
(alias `fillRect`), `rect`, `line`, `text`, `measure`, `present` (alias
`flush`), `pollEvents`, `waitEvent`, `poll`, `inputs`, `setRotation`, `close`.

Colours are `0xRRGGBB` or `0xAARRGGBB`. They are packed into the framebuffer's
kernel bitfields (RGB565, RGBX/BGRA, grayscale, etc.). Rotation applies to
both drawing and touch coordinates.

`gothgfx.open{ memory = true, width = W, height = H, bpp = B }` creates an
in-memory surface, which is how the host tests run without a framebuffer.

## Device profiles

`SystemUI.lua` loads `devices/<name>.lua` for the device named by `--device`
(the supervisor passes `ro.product.device`). There is no implicit fallback: an
unknown device uses the framebuffer's own geometry and evdev auto-detection.
A profile can override the framebuffer, rotation, theme and touch settings; see
`devices/harpia.lua`. `touch.x_min`/`x_max`/`y_min`/`y_max` override the range
that evdev reports, which helps on devices with incomplete input metadata.

## Host tests

`make test </dev/null` now includes:

- `tests/test_gothgfx.lua` — drawing, colour packing, rotation, clipping,
  text metrics and input API using memory surfaces;
- `tests/test_systemui.lua` — headless SystemUI smoke test (layout, touch
  navigation, rotation, diagnostics/about screens, stop request).

Build just those with `make build/test_gothgfx` and run:

```sh
./build/test_gothgfx tests/test_gothgfx.lua
./build/test_gothgfx tests/test_systemui.lua
```

The same `CMakeLists.txt` also builds a desktop `gothdroid`, which is the
closest thing to the on-device runtime that can be exercised without a phone:

```sh
cmake -S os/Android -B os/Android/build/host -DCMAKE_BUILD_TYPE=Release
cmake --build os/Android/build/host --target gothdroid gothgfx --parallel

GOTHDROID_MEMORY=1 ./os/Android/build/host/gothdroid \
    --root os/Android \
    --script os/Android/SystemUI/SystemUI.lua \
    --fb /dev/null --device harpia \
    --log /tmp/gothdroid.log --stop-file /tmp/gothrtos-stop
```

With `GOTHDROID_MEMORY=1` the UI draws to an in-memory surface, so this works
on machines with no framebuffer. Write `/tmp/gothrtos-stop` to shut it down.
The log shows the kernel coming up (`platform: ...`, `kernel ready, UI task N`)
and `SystemUI.lua` yielding to the scheduler via `goth.yield()`.

To exercise the Android pthread platform expert without a device:

```sh
cmake -S os/Android -B os/Android/build/host-pthread \
      -DCMAKE_BUILD_TYPE=Release -DGOTHDROID_PLATFORM=android
cmake --build os/Android/build/host-pthread --target gothdroid --parallel
```

That binary runs the same kernel and scheduler code as the device build, using
`pexpert/android/pe_android.c` instead of the host's ucontext expert.

## Caveats

- Root is mandatory. The launcher uses `su -c`; if the device's `su` only
  understands `su 0 sh -c`, `GothRuntime` falls back automatically.
- SELinux policies may still block a root shell from opening `fb0` or writing
  `/data/local/tmp`. If `gothdroid.log` reports `permission denied`, adjust the
  policy (or temporarily use a permissive build) for the `su` domain.
- Some devices and newer Android releases do not expose a classic fbdev
  framebuffer at `/dev/graphics/fb0`. `libgothgfx` intentionally uses the
  classic `FBIOGET_*`/`mmap` path requested here; a DRM-backed device needs a
  separate platform port.
- `/system/sbin/stop` and `/system/sbin/start` are preferred, with
  `/system/bin/...` as fallback. If neither exists the supervisor refuses to
  stop Android and exits with a log message.
- If the UI starts (`SystemUI entering main loop`) but the screen stays black,
  first check the panel with the built-in framebuffer probe:

  ```sh
  su
  stop
  # Wait until init has really stopped both services; while SurfaceFlinger is
  # still alive it keeps flipping its own buffers over anything we draw.
  until [ "$(getprop init.svc.surfaceflinger)" != running ] &&
        [ "$(getprop init.svc.zygote)" != running ]; do sleep 0.5; done
  /data/local/tmp/gothrtos/gothdroid --fb-probe
  cat /data/local/tmp/gothrtos/gothdroid.log
  ```

  It prints every `/sys/class/graphics/fb*` device, the geometry that
  `FBIOGET_VSCREENINFO` reports, paints colour bars for five seconds and then
  exits. `start` (or a reboot) brings Android back.
  `libgothgfx` unblanks the panel (`FBIOBLANK` plus
  `/sys/class/graphics/fbN/blank`) and pans the scanout back to its own buffer
  on every `present()`, because after `stop` some msm framebuffer drivers keep
  scanning out of SurfaceFlinger's last buffer.
- The native code is not built by Gradle. Run `build-native.sh` before
  packaging if you want the APK to contain a runnable `gothdroid`.
