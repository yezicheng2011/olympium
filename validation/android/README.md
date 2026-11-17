# Real Android application verification

`verify-app.py` uses an actual selected Android device/emulator and ADB. It requires Python 3 and Pillow on the machine running the script. `--container` runs only the ADB client in Docker; screenshots and reports are saved on the invoking machine.

Verified sample: Fossify Calculator 1.4.0 (`org.fossify.math`), official APK identity in `../runtime-artifacts.json`, on the official AOSP API 28 x86 emulator image. See [recorded result](evidence/api28-calculator-result.json), [UI hierarchy](evidence/api28-calculator-ui.xml), and [rendered screenshot](evidence/api28-calculator.png).

From the repository root with the prepared test container/emulator running:

```sh
python3 validation/android/verify-app.py \
  --container bbb-dev \
  --adb /workspace/bbb/build/runtime-validation/android-sdk/platform-tools/adb \
  --serial emulator-5556 \
  --package org.fossify.math \
  --apk-sha256 7c5443c22d6d62c67dfe80df33ea9161ff0fa440f92590bcb45d7008b57613ed \
  --fossify-calculation \
  --output build/runtime-validation/android-evidence
```

The script resolves the launch activity rather than guessing it, checks the installed base APK against the expected digest when supplied, verifies the foreground window and app-owned UI hierarchy, and captures a nonblank rendered screenshot. The Fossify check clicks controls identified in the real UI hierarchy and requires both `formula=2+3` and `result=5`; a keypad containing a digit 5 is insufficient.

To install the already-downloaded APK before verification, add `--apk` with a path visible at the same location inside the container. An expected SHA256 is mandatory for installation. APK installation success alone never sets a successful app-window result. `--unlock` can wake/dismiss an unprotected test image's keyguard; it does not handle PINs or authentication.

The initial emulator session showed a System UI ANR, acknowledged with Wait before successful runs. Later strict cold-start checks produced a black app area and no fresh accessibility root; see [failure record](evidence/api28-cold-start-failure.json). The successful API 28 evidence predates the current helper's fresh-clear/cold-start checks. It is historical execution evidence, not a passing cold-start result for the latest helper. API 28's `desktop_window_verified` remains false: scrcpy requires Android 10 for a separate virtual display.

On API 29, the same installed APK rendered in the desktop's native Wayland window. Pointer clicks through the compositor and scrcpy produced **2+3=5**, checked against fresh Android UI fields: [desktop screenshot](evidence/api29-desktop-calculator.png), [UI hierarchy](evidence/api29-desktop-calculator-ui.xml), [input/result record](evidence/api29-desktop-calculator-result.json). The initial launch returned to display 0; moving its app-only stack to scrcpy's actual display ID restored the app window. Clipboard support was unavailable in this experiment.

`open-window.py` delegates to the shared `image/android_window.py` backend. The API 29 result above is historical; Android 17 (API 37) is the current target, with separate root-task parsing and end-to-end verification still pending. It runs in the same environment as ADB and scrcpy, with a valid Wayland session; install and verify the APK separately first:

```sh
SDL_VIDEODRIVER=wayland python3 validation/android/open-window.py \
  --adb /path/to/platform-tools/adb \
  --scrcpy /path/to/scrcpy \
  --serial emulator-5558 --package org.fossify.math \
  --title Calculator --output build/runtime-validation/android-window
```

The helper owns the scrcpy child until its window closes. It resolves the launcher activity, waits for a real virtual display ID, and only relocates a unique standard stack containing one task for the requested package. `task-on-app-display` records placement readiness; it deliberately does not certify pixels or input. This experiment does not automatically start/download an Android runtime, install desktop icons, or implement the production APK flow.

For headless host-input verification, explicitly build `make -C desktop dev-compositor-input-test` and run that binary with `DEV_DESK_HEADLESS=1`. Only this test binary exposes the standard wlr virtual-pointer protocol. The normal compositor never exposes it. Compile the test client with:

```sh
mkdir -p build/runtime-validation
wayland-scanner client-header validation/android/virtual-pointer.xml build/runtime-validation/virtual-pointer-client.h
wayland-scanner private-code validation/android/virtual-pointer.xml build/runtime-validation/virtual-pointer-protocol.c
cc -Wall -Wextra -O2 -Ibuild/runtime-validation \
  validation/android/pointer-click.c build/runtime-validation/virtual-pointer-protocol.c \
  -o build/runtime-validation/pointer-click $(pkg-config --cflags --libs wayland-client)
```

`pointer-click WIDTH HEIGHT X Y [X Y ...]` sends host pointer events through the test compositor's normal cursor and seat handlers. Use the actual output dimensions and inspect the app's current position before choosing coordinates. The recorded run clicked clear, 2, +, 3 and =; no ADB tap generated that recorded calculation. The XML comes from [swaywm/wlr-protocols at b010a03](https://github.com/swaywm/wlr-protocols/blob/b010a03648b88d143236de193bddbfea0c08bc84/unstable/wlr-virtual-pointer-unstable-v1.xml) and retains its MIT license notice; the vendored bytes were compared with that commit.

These results verify one x86 application/input case; they do not establish stability, arbitrary APK or ARM compatibility, clipboard/files, repeated cold-start reliability, or near-native performance. Stop other test compositors before running desktop regression suites: they share `/run/dev-desktop/status` and would contaminate idle frame measurements.

Managed Android 17 startup checks access to `/dev/kvm` before downloading runtime images. Hardware acceleration is required for the normal desktop path. `OLYMPIUM_ANDROID_ALLOW_TCG=1` explicitly enables software emulation for development only; the recorded TCG session did not establish a stable Android 17 boot. Explicitly selected existing test devices remain available through `OLYMPIUM_ANDROID_SERIAL`.
