# Compatibility matrix

Levels mean what this image and the pinned Olympium revision actually do. They are not a promise that every application in a family runs.

Olympium revision in the image: `517aaaa5409f227de42444cf16612d546c29251b` on `cursor/guest-runtime-site-9aa9`. This tree does not modify that repository. That revision does not start macOS software on Linux. iOS is not part of this system.

| Target | This live image | Olympium at the pin |
| --- | --- | --- |
| Native Linux program for the same ISA | Runs. Files, Settings, the terminal, and the shell are Linux programs. | Runs Linux programs on Linux. |
| 32-bit ELF on the matching 64-bit CPU | Runs natively. x86_64 uses IA-32 and `libc6:i386`. aarch64 uses 32-bit ARM compatibility and `libc6:armhf`. | Not that runtime's job. |
| 64-bit x86 ELF on the i386 image | Runs in a 64-bit guest. The translator is a 32-bit `qemu-system-x86_64`. Swap can back it. It is slow, and guest RAM stays within what that 32-bit process can hold. | Not that runtime's job. |
| Lawful test app | A Linux window titled "Lawful test app". It is not a Mac application. The dock, Launchpad, and Files open it. | The runtime's library lists it as a Linux program and can start it that way. |
| macOS GUI application | A Mach-O header is identified and not executed. No AppKit, dyld, or Apple SDK. | Does not start macOS software on Linux. |
| OpenHarmony HAP | Not bundled. The dock launches Olympium when that binary has been installed separately. | Linux launch/stop of a downloaded image is work in progress there, with host input. The image is not in this ISO. |
| Android APK via Cuttlefish | Not bundled. No Google Play. | A Linux Cuttlefish path exists there and is still being integrated. It is not a result of this ISO. |
| Windows executable | Not bundled. Wine is not installed. No Windows license is implied. | Separate from this desktop. |
| Install window (`dev-install`) | Not in the checksummed ISOs. On the build machine, a native Linux program can be imported after consent and launched; other formats are identified and not installed. | The desktop calls `classify`, `plan`, and, after consent, `add`. It does not call `run`, `fetch`, or `guest`. |

Darling is not built into this image. Integrating it would still not be proof that arbitrary AppKit applications run. iOS is not part of this system. The install matrix, including what was actually executed, is `docs/desktop-apps.md`. The fields this desktop reads from the pinned command are `docs/runtime-desktop.md`.

`dev-compat matrix` prints the same distinction on the running system.
