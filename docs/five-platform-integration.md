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
| Linux | Native desktop installer, search and Launchpad. Latest headless sample rendered zero frames in four idle seconds. | Shipping ISO boot and hardware GPU measurements |
| Windows | Wine 9.0 x86_64 startup, bidirectional text clipboard, document/shared folder access, and 20 real titlebar closes with normal process exits | 32-bit Wine in this environment, the original heap-corruption incident, and a broader application set |
| macOS | Source-built Darling ran the publisher's Intel PikoPixel through drawing, native file save/reopen, and close confirmation. Hex Fiend read and edited bytes in its window. A separate Cocoa fixture exchanged plain text with GTK in both directions. | Hex Fiend disk save, image and file clipboard, hardware GPU performance |
| Android | API 37 Fossify Calculator install, icon, independent window and input. Separate KVM runs verify two boots and microG signature queries and Common broker RPC. | ARM-only execution, account/push/maps and broader Play services compatibility |
| OpenHarmony | SDK 6.1 signed HAP installation and application input in KVM. Calculator also passed host-window input and relaunch. Configured-SDK HAP import, icon and launcher are implemented. | KVM acceptance of the new desktop installer route, runtime provisioning, arbitrary HAP windows and commercial HarmonyOS |

The Mac fixture suite still fails its two world-writable-directory confinement assertions on this cloud runtime's `nobody` fallback. Those assertions are not disabled. Its separate fallback checks and the functional installation/window checks pass. Those Linux windows are not Mac application execution.

The rendering regression is `make -C desktop test-render`. It checks that an idle headless output stops redrawing, chrome changes wake it, and X windows survive repeated hide/show/destroy cycles. The default run uses 12 cycles. A separate AddressSanitizer/UBSan run completed 40 cycles with no diagnostics. This does not prove the historical Wine heap-corruption report has been reproduced or fully fixed.

On the same cloud machine, a five-second no-client 1280x800 headless/pixman sample measured 25.59% of one CPU core before the rendering change and 0.80% after it. Real display, GPU and application workloads need separate measurements.

## Remaining implementation

The managed Android image is x86_64. The installer now reports the native
ABIs required by an APK when the selected Android device cannot run them.
ARM-only acceptance needs a guest reporting the matching ABI and a real
native app run. The microG profile still needs real app coverage beyond its
tested Common broker.

The OpenHarmony desktop runtime uses an already running owned SDK guest.
Its desktop bridge forwards primary clicks and displays the app's region.
The new installation path needs its own hardware acceptance run. SDK
provisioning, keyboard and drag support, and commercial app compatibility
remain separate implementation targets.

The release still needs a single ISO boot, a multi-platform app matrix and
measured startup, input latency and GPU behavior on the target hardware.
See [runtime-validation-handoff.md](runtime-validation-handoff.md) for the
exact validation checkpoint and evidence.
