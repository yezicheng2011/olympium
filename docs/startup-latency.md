# Time to the first Windows window

The boot of `9c1b41e4…` mapped `Desk Win` at 43.3 s of compositor time. This page splits that number, says what was changed, and says what was measured afterwards and where.

## Desktop startup changes

The desktop service no longer requests the global `systemd-udev-settle` barrier. Graphics module probes remain synchronous, and wlroots 0.17's DRM discovery waits for GPU devices when needed. Network/storage helper probes and the driver inventory run in the background. The inventory indexes the module tree once, preserving compressed and built-in driver reporting.

`run-session` uses `dev-compositor -n -s ...`: a Wayland session starts as soon as the Wayland socket and backend are ready, without waiting for Xwayland. The first-run settings window no longer has an extra three-second sleep. The default `-s` behavior still waits for Xwayland for scripts that need `DISPLAY`; Windows launches from the native session resolve the asynchronously published `olympium-xdisplay` note. Screenshot/test-only delays remain separate from normal startup.

`make -C desktop test-startup` checks launch ordering with a deliberately delayed Xwayland and confirms that the session is started exactly once. One headless/pixman run mapped the GTK test window at 1.982 seconds in the X-compatible path and 0.243 seconds in the native path while Xwayland was deliberately delayed by 1.5 seconds. This is a controlled dependency test, not a complete cold-boot benchmark. `TEST_STARTUP_WINE=1 make -C desktop test-startup` additionally checks a real Wine launch from a native session without an inherited `DISPLAY` (requires Wine, mingw and `dev-install`).

The ISO has not been rebuilt or booted for these startup changes. Firmware, initramfs, physical GPU discovery, and real cold-boot time remain unmeasured.

## Where the 43.3 s went

The marks are from that boot's session log. The two waits are `sleep` lines in the boot-test scripts. Nothing in that boot timed the phases directly, so the split of the middle rows is read from the scripts, not measured.

| Compositor time | What was happening | Whose time |
| --- | --- | --- |
| 0 to 1.1 s | The compositor started and Xwayland became ready. | Desktop |
| 1.1 to 9.6 s | `startup.sh` slept 8 s before opening Install, then Install mapped. | Test script |
| 9.6 to about 23.6 s | `install-sample.sh` slept 14 s so the review could be photographed. | Test script |
| about 23.6 to 24.6 s | Import (a checksum and a library row), then `wine` started and its setup window mapped. | Desktop and Wine |
| 24.6 to 43.3 s | Wine built its prefix: about 690 MB of DLL copies read out of a gzip squashfs and written to the tmpfs overlay. Then it started the program. | Wine |
| 26 ms | The program, from process start to `WM_CREATE`. | Application |

So about 22 s was the test waiting on purpose, about 19 s was Wine building a prefix for the first time, about 1 s was the desktop, and the program itself was 26 ms. A person who clicks Launch does not sit through the 22 s. They did sit through the 19 s.

## What changed

- `image/wine-prefix.sh` builds the prefix while the image is built. It installs one copy at `/usr/lib/olympium/wine-prefix` and one at `/var/lib/olympium/desk/wine`, which is the live session's prefix. The files are identical to the DLLs Wine already ships, and squashfs stores identical files once. On the build machine a squashfs of Wine's libraries grew from 203,456,512 to 203,681,792 bytes with one prefix added, 225,280 bytes.
- `dev-install launch` uses the prefix when it exists. When it does not, and the prebuilt copy is installed, it copies that into a sibling directory and renames it into place. An interrupted copy leaves a `wine.seed-*` directory and no half prefix. With neither, Wine builds the prefix as before.
- The copies are real files. They are not links into `/usr/lib`. A Windows installer that overwrites a system DLL changes the prefix and not the Wine package.
- Before it starts Wine, launch asks `wineserver` to stay 60 s after the last Windows program exits (`OLYMPIUM_WINE_IDLE`, in seconds, 0 for Wine's own 3 s). A second launch inside that time reuses it. After it, `wineserver` exits by itself. Nothing runs before the first launch.
- Prewarm is off. `dev-install prewarm on` turns it on, `off` turns it off and stops the server, `status` prints which. When on, the session start prepares the prefix, starts `wineserver` with a 600 s idle limit (`OLYMPIUM_WINE_PREWARM_IDLE`), and runs `wine cmd /c exit` once so Wine's services are up. That took 0.76 s on the build machine. Under Xvfb there, a launch right after it mapped its window in 0.16 s and a launch 30 s later in 0.43 s, against 0.67 s with nothing running. It costs a resident `wineserver` for up to ten minutes, which is why it is off. `olympium.prewarm=1` on the kernel command line turns it on for a boot test.
- Each launch writes `launch_start_ms`, `wine_prefix` (`existing`, `seeded`, or `fresh`), `wine_prefix_ms`, and `wine_server_ms` to `$OLYMPIUM_DESK_HOME/wine-launch-marks`. The boot test now logs `wine_request_to_window_ms`, the time from the launch request to the mapped window, so the next boot reports the launch separately from the test's own waits.
- The 14 s dwell is still in `install-sample.sh`. It is there for the picture. It is no longer inside the number that is reported.

## What was measured after the change

`make -C desktop test-wine` on the build machine: Ubuntu 24.04, Wine `9.0~repack-4build3`, four CPUs, the compositor with the headless backend and Xwayland, `desk-win.exe` built with mingw. Times are from the launch request to `window mapped: Desk Win`, in 10 ms steps. Four runs.

| Launch | Request to window |
| --- | --- |
| Wine builds the prefix (the old first launch) | 4.8 s |
| Prefix copied from the prebuilt one | 1.0 to 1.7 s, of which 0.4 to 1.0 s is the copy |
| Prefix exists, Wine's services not running | 0.67 to 0.69 s |
| Prefix exists, right after prewarm | 0.13 s |
| Prefix exists, `wineserver` kept from the last launch | 0.16 to 0.19 s |

The build machine has a page cache and a fast disk, so its 4.8 s is the same work that took about 19 s on the booted ISO.

## What was not measured

An amd64 image was then built from a fresh clone and booted; see "On a booted image" below.

`install-to-disk` does not copy `/var/lib/olympium`, so an installed system starts without a prefix and seeds one on the first launch. That costs about 690 MB on the disk on top of the 690 MB prebuilt copy. `install-to-disk` was not changed and not run.

The arm64 and i386 images have no Wine yet. `repack-install.sh` builds the prefix for amd64 and i386 roots. With `wine32` installed next to `wine64` the prefix is 1.3 GB before deduplication, not 690 MB; see `docs/wine-architectures.md`.

## On a booted image

`image/build-iso.sh amd64`, then `OLYMPIUM_WINE=1 OLYMPIUM_DARLING=1 image/repack-install.sh amd64`, then `image/shot-wine.sh` with `olympium.winefresh=1 olympium.importmac=1`. The build machine has no KVM, so QEMU 8.2 emulated the CPU (three cores, 6 GB). These times are an emulated machine's and are only comparable with each other.

| Launch, same boot | Request to window |
| --- | --- |
| First launch, prefix carried in the image | 22.9 s |
| Second launch, Wine still up | 3.7 s |
| A prefix Wine has to build (what every first launch used to cost) | 126.5 s |

The same boot unpacked the sample `.rpm` under Landlock and ran it, and installed and ran the Mac samples (`docs/mac-apps.md`).

What the boot found and what was changed:

- `build-iso.sh` wrote into `build/` before creating it, so a build from a fresh clone failed after the packages were installed.
- An image without the separate Olympium runtime could not recognize an `.exe` or an `.rpm` at all. `dev-install` now reads both itself. `rpm` and `rpm2cpio` were not in the image and are now.
- The session can start before Xwayland is ready (it waits four seconds). What the session started then had no `DISPLAY`, and a Windows program was refused. The compositor now writes the display's name to `$XDG_RUNTIME_DIR/olympium-xdisplay`, and `dev-install` waits for it (20 s, `OLYMPIUM_XWAIT`).
- The compositor did not handle Xwayland's `dissociate` event. Its listeners stayed on a freed surface, and a window that was hidden and shown again never came back. Fixed, with `test-xremap` in `make test-wine`. One test run under load aborted with glibc's "corrupted unsorted chunks" after a Wine window closed. That abort was not reproduced again, before or after the fix, so it is not proven to be this bug.
- Under emulation only: QEMU 8.2 with SMAP makes the guest kernel kill Wine. `shot-wine.sh` turns SMAP off when there is no KVM.
- The earlier idle redraw issue is fixed in the current working tree; see `five-platform-integration.md` for the headless measurement and its limits.
