# Runtime validation experiments

The production desktop changes and existing Linux/64-bit Wine checks are described in `../docs/runtime-validation-handoff.md`.

`runtime-artifacts.json` records the URLs and checksums of artifacts obtained during the unfinished Android, OpenHarmony and macOS GUI experiments. It does not download or execute them, and does not certify application compatibility. Check each publisher's metadata before intentionally changing a version; preserve checksum, TLS and package-signature verification.

`macos/hello-gui.c` is an independent Cocoa/Objective-C runtime smoke program written without Apple SDK headers or assets. It was compiled to x86_64 Mach-O using Darling's Darwin linker and runtime libraries. Its window and button callback have **not** been verified. It is not included in the ISO or enabled as an application.

Large images, downloaded binaries, temporary SSH keys, proxy settings, build outputs and raw experiment logs remain in ignored `build/runtime-validation/`. They are not part of this PR.
