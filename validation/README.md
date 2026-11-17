# Runtime validation experiments

The production desktop changes and existing Linux/64-bit Wine checks are described in `../docs/runtime-validation-handoff.md`.

`runtime-artifacts.json` records the URLs and checksums of artifacts obtained during the unfinished Android, OpenHarmony and macOS GUI experiments. It does not download or execute them, and does not certify application compatibility. Check each publisher's metadata before intentionally changing a version; preserve checksum, TLS and package-signature verification.

`macos/hello-gui.c` is an independent Cocoa/Objective-C runtime smoke program written without Apple SDK headers or assets. It was compiled to x86_64 Mach-O using Darling's Darwin linker and runtime libraries. Its window and button callback were verified through desktop search, the installed launcher, Xwayland and a normal compositor pointer event; see `macos/evidence/`. This covers one independent probe. It is not included in the ISO or enabled as an application.

`android/` contains an actual Fossify Calculator execution record, the subsequent API 28 cold-start failure, and API 29 independent Wayland window/pointer evidence. The shared window backend is now used by `image/olympium-android` and the desktop APK installer. Android 17 end-to-end validation remains blocked by system-service failures under TCG.

`macos/build-smoke.sh` builds the `.app` against Darling and explicitly supplies SDK metadata. The full Ubuntu VM rendered the app and delivered its button callback with Mesa softpipe and MIT-SHM disabled in Xwayland. The callback changed the window title; text-field repaint and third-party GUI compatibility remain unverified.

Large images, downloaded binaries, temporary SSH keys, proxy settings, build outputs and raw experiment logs remain in ignored `build/runtime-validation/`. They are not part of this PR.
