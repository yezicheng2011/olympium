# Compatibility matrix

Levels mean what this image and the pinned Olympium revision actually do. They are not a promise that every application in a family runs.

Olympium revision in the image: `517aaaa5409f227de42444cf16612d546c29251b` on `cursor/guest-runtime-site-9aa9`. This tree does not modify that repository. That revision does not start macOS software on Linux. iOS is not part of this system.

| Target | This live image | Olympium at the pin |
| --- | --- | --- |
| Native Linux program for the same ISA | Runs. Files, Settings, the terminal, and the shell are Linux programs. | Runs Linux programs on Linux. |
| 32-bit ELF on the matching 64-bit CPU | Runs natively. x86_64 uses IA-32 and `libc6:i386`. aarch64 uses 32-bit ARM compatibility and `libc6:armhf`. | Not that runtime's job. |
| 64-bit x86 ELF on the i386 image | Runs in a 64-bit guest. The translator is a 32-bit `qemu-system-x86_64`. Swap can back it. It is slow, and guest RAM stays within what that 32-bit process can hold. | Not that runtime's job. |
| Lawful test app | A Linux window titled "Lawful test app". It is not a Mac application. The dock, Launchpad, and Files open it. | The runtime's library lists it as a Linux program and can start it that way. |
| macOS GUI application | A `.dmg`, `.pkg`, or `.app` opens in Install: the application is copied into Applications and listed in Launchpad with its icon. It is started only through Darling, which is in the image only when it is built with `OLYMPIUM_DARLING=1`; without it a window says why nothing started. On the build machine a command-line Mach-O program went the whole way and ran. No graphical Mac application was run. A Mach-O file on its own is identified and not executed. No AppKit, dyld, or Apple SDK is in this tree. | Does not start macOS software on Linux. Mac files are not sent to it. |
| OpenHarmony HAP | Not bundled. The dock launches Olympium when that binary has been installed separately. | Linux launch/stop of a downloaded image is work in progress there, with host input. The image is not in this ISO. |
| Android APK via Cuttlefish | Not bundled. No Google Play. | A Linux Cuttlefish path exists there and is still being integrated. It is not a result of this ISO. |
| Windows executable | The three checksummed images have no Wine. The install image `9c1b41e4…` has Ubuntu's 64-bit Wine and ran a 64-bit `.exe`. `image/install-wine.sh` adds the 32-bit half; which CPU each image can run is in `docs/wine-architectures.md`. No Windows license is implied. | Its plan names the distribution's Wine; the desktop launches that. |
| Install window (`dev-install`) | Not in the checksummed ISOs. On the build machine, a native Linux program can be imported after consent and launched; other formats are identified and not installed. | The desktop calls `classify`, `plan`, and, after consent, `add`. It does not call `run`, `fetch`, or `guest`. |

Darling is built into the image only on request. The desktop hands an installed Mac application to it when it is present, and `docs/mac-apps.md` says what was run with it: a command-line program, on the build machine. Having it is not proof that arbitrary AppKit applications run: the build that was run has no AppKit, and Darling describes its own support for graphical applications as in development. iOS is not part of this system. The install matrix, including what was actually executed, is `docs/desktop-apps.md`. The fields this desktop reads from the pinned command are `docs/runtime-desktop.md`.

`dev-compat matrix` prints the same distinction on the running system.
