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

`open-window.py` delegates to the shared `image/android_window.py` backend. The API 29 result above is historical; Android 17 (API 37) is the current target; its actual root-task parsing and formal desktop evidence are recorded below. It runs in the same environment as ADB and scrcpy, with a valid Wayland session; install and verify the APK separately first:

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

`pointer-click WIDTH HEIGHT X Y [X Y ...]` sends host pointer events through the test compositor's normal cursor and seat handlers. Use the actual output dimensions and inspect the app's current position before choosing coordinates.

`pointer-click --drag WIDTH HEIGHT X1 Y1 X2 Y2 [...]` holds the left button from the first coordinate to the last. The recorded run clicked clear, 2, +, 3 and =; no ADB tap generated that recorded calculation. The XML comes from [swaywm/wlr-protocols at b010a03](https://github.com/swaywm/wlr-protocols/blob/b010a03648b88d143236de193bddbfea0c08bc84/unstable/wlr-virtual-pointer-unstable-v1.xml) and retains its MIT license notice; the vendored bytes were compared with that commit.

These results verify one x86 application/input case; they do not establish stability, arbitrary APK or ARM compatibility, clipboard/files, repeated cold-start reliability, or near-native performance. Stop other test compositors before running desktop regression suites: they share `/run/dev-desktop/status` and would contaminate idle frame measurements.

Managed Android 17 startup checks access to `/dev/kvm` before downloading runtime images. Hardware acceleration is required for the normal desktop path. `OLYMPIUM_ANDROID_ALLOW_TCG=1` explicitly enables software emulation for development only; the passing diagnostic run also required explicit `OLYMPIUM_ANDROID_TCG_ICOUNT=1` (virtual instruction clock). Normal-clock KVM managed provisioning and two-boot guest UI/input acceptance now pass; see the hardware report below. Readiness/tool deadlines can be raised explicitly with `OLYMPIUM_ANDROID_READY_TIMEOUT` (60–3600 seconds) and `OLYMPIUM_ANDROID_TOOL_TIMEOUT` (15–600 seconds). Explicitly selected existing test devices remain available through `OLYMPIUM_ANDROID_SERIAL`.

Android 17 (API 37.0 revision 6, x86_64) now has actual formal-installer, desktop-search, original adaptive-icon/Launchpad, independent-window, pointer **2+3=5**, close and icon-relaunch evidence in `evidence/api37-*`. The publisher image was unmodified, but the TCG run required diagnostic `-icount shift=0,align=off,sleep=off`; it does not establish normal-clock/KVM cold-boot reliability or near-native performance. The window now fits above the Dock, and SVG decoding is an explicit ISO dependency.

The independent `gms-probe/` app verified a real microG Common service handshake on Android 17. Legacy/modern certificate compatibility failed; `evidence/api37-microg-probe.json` preserves that failure. Package installation and a successful RPC do not certify all GMS APIs.

The limited microG framework prototype now passes the same client certificate and Common RPC checks; original-signer updates pass and wrong-signer updates are rejected without changing the installed APK. See `microg-framework/README.md` and `evidence/api37-microg-compatible-probe.json`. Full GMS compatibility remains false.

Normal-clock KVM managed provisioning now passes two separate emulator boots: pinned APK installation, ordinary-UID microG signature/Common RPC checks, actual Calculator input and automatic framework reapplication after shutdown. See `evidence/api37-kvm-managed-roundtrip.json` and [CI run 44208210089](https://github.com/yezicheng2011/bbb/actions/runs/44208210089). This hardware run verifies guest app UI/input; the separate desktop-window evidence remains the recorded diagnostic TCG run. Full GMS APIs, ARM-only APKs and hardware desktop rendering remain unverified.

The [ADB recovery regression run](https://github.com/yezicheng2011/bbb/actions/runs/44289494539) repeated both boots with the unprivileged ADB readiness check and bounded read-only retries enabled. Managed installation took 160.791 and 76.705 seconds on the two boots. The same workflow also confirmed signed APK upgrade, rejection of a different signer, and rollback of the APK and its private data.
