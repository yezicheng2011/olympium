# Five-platform application integration

The intended product is a Linux system with a Mac-inspired desktop and applications from Linux, Windows, Android, OpenHarmony and macOS presented through the same desktop. Downloading source is a prerequisite, not an application-compatibility result. No current combination of these upstream projects establishes support for every application or native performance for every workload.

## Reproducible upstream source acquisition

From the repository root:

```sh
python3 image/fetch-compat-sources.py
```

This fetches the exact commits in `image/compat-sources.tsv` into the ignored `build/compat-sources` directory. Git object connectivity and the checked-out commit are verified. Repeating it keeps existing checkouts and refuses local changes or a different revision. Git/TLS verification remains enabled. Source licenses remain in each upstream tree.

- [Waydroid](https://github.com/waydroid/waydroid) 1.6.3: the Android container manager. Its Android system and vendor images are separate inputs; they have not been downloaded or initialized by this command.
- [Darling](https://github.com/darlinghq/darling): the same revision already used by `image/build-darling.sh`. The fetcher gets its top-level source. The existing build script obtains its submodules. A CLI build does not include the GUI frameworks.
- [OpenHarmony](https://github.com/openharmony/manifest) 6.1 LTS: the upstream multi-repository manifest. It is not the complete OS source tree, SDK, device image or a commercial HarmonyOS runtime. Synchronizing and building its projects remains separate work.

## Actual validation in this change

| Platform | Verified | Still unverified |
| --- | --- | --- |
| Linux | Desktop builds; native GTK Install/search/Launchpad interactions pass | New ISO boot and GPU performance |
| Windows | Wine 9.0 64-bit application startup, clipboard in both directions, document/shared-folder access, 26 X window unmap/remap/destroy cycles, and a compositor build without Xwayland | 32-bit Wine; arbitrary Windows applications; the previously reported non-reproducible heap-corruption incident |
| macOS | Real Cocoa Mach-O compiled; official Darling GUI installed in full Ubuntu VM; local `.app` import and launcher generation under Landlock; program enters main and creates NSApplication | Persistent Cocoa window and button callback: program exits during window initialization; third-party GUI applications |
| Android | APK bytes verified; Fossify Calculator 1.4.0 on API 28; API 29 x86 independent Wayland window and host-pointer-driven 2+3=5, repeated after scripted relaunch, with screenshot/UI evidence | Production APK install/launcher integration; reliable cold boot (API 28 repeat failed); clipboard/shared files; ARM/arbitrary APKs and native performance |
| OpenHarmony | Official Eclipse Oniro 6.1 image booted; HDC connected; OS version and bundle list read | Calculator foreground/UI/input: launch was refused while screen locked; HAP installation and desktop integration |

The Mac fixture suite still fails its two world-writable-directory confinement assertions on this cloud runtime's `nobody` fallback. Those assertions are not disabled. Its separate fallback checks and the functional installation/window checks pass. Those Linux windows are not Mac application execution.

The rendering regression is `make -C desktop test-render`. It checks that an idle headless output stops redrawing, chrome changes wake it, and X windows survive repeated hide/show/destroy cycles. The default run uses 12 cycles. A separate AddressSanitizer/UBSan run completed 40 cycles with no diagnostics. This does not prove the historical Wine heap-corruption report has been reproduced or fully fixed.

On the same cloud machine, a five-second no-client 1280x800 headless/pixman sample measured 25.59% of one CPU core before the rendering change and 0.80% after it. Real display, GPU and application workloads need separate measurements.

## Remaining implementation

Waydroid needs a running Android image, Linux Binder support and its container/session services. The present cloud machine exposes neither Binder devices nor binderfs. Source downloads cannot add that host-kernel capability. An image with a suitable kernel, or a full-system VM, is needed for execution tests. KVM is not exposed here; software VM tests would not establish near-native performance.

Darling GUI needs a successful Cocoa application execution test. Official GUI packages were acquired, and a source build was paused after partial compilation once those packages were available. Neither route has produced a verified Mac GUI window here. The current CLI path and an executable named `darling` are insufficient evidence. Its namespace/container setup must also work on the target Linux installation. See the [upstream build instructions](https://docs.darlinghq.org/build-instructions.html).

The selected OpenHarmony validation target is the official Eclipse Oniro 6.1 x86_64 QEMU image, with SDK/HDC 6.1.0.31. It still needs an unlocked, stable GUI and a successful HAP installation/ability execution test. OpenHarmony results must not be represented as compatibility with every commercial HarmonyOS application or service.

See [runtime-validation-handoff.md](runtime-validation-handoff.md) for the exact checkpoint, experiment artifacts and remaining acceptance tests.

After each engine runs, it still needs installation records, launcher entries, independent windows, input, clipboard, shared files, lifecycle/error handling and CPU-architecture checks integrated into this desktop. None of these engines is enabled merely by fetching its source, and the ISO build has not been changed to silently include unvalidated runtimes.
