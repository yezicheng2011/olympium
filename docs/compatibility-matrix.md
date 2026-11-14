# Compatibility matrix

Levels mean what this image and the pinned Olympium revision actually do. They are not a promise that every application in a family runs.

Pinned Olympium revision: `25a592cfd65a4f07449ca03e919bfa97626bb164` on `cursor/guest-runtime-site-9aa9`. This tree does not modify that repository. That revision's own policy refuses macOS and iOS guests on Linux, and it refuses IPA files even on a Mac.

| Target | This live image | Olympium at the pin |
| --- | --- | --- |
| Native Linux program for the same ISA | Runs. Files, Settings, the terminal, and the shell are Linux programs. | Runs Linux programs on Linux. |
| Linux program for the other ISA | Not bundled. A future `OLYMPIUM_TRANSLATOR` can be placed on `PATH`. QEMU in the build machine is not that translator and is not installed as a guest engine. | Pure software translation is in progress in that repository. It is not claimed here. |
| macOS GUI application | Header probe only. No AppKit, no dyld, no execution. | Refused on Linux by that revision. |
| iOS GUI application or IPA | Refused. No UIKit and no decryption. | Refused. |
| OpenHarmony HAP | Not bundled. The dock launches Olympium when that binary has been installed separately. | Linux launch/stop of a downloaded image is work in progress there, with host input. The image is not in this ISO. |
| Android APK via Cuttlefish | Not bundled. No Google Play. | A Linux Cuttlefish path exists there and is still being integrated. It is not a result of this ISO. |
| Windows executable | Not bundled. Wine is not installed. No Windows license is implied. | Separate from this desktop. |

Darling is an open-source reimplementation that can cover some macOS ABI surface. It is not built into this image. Integrating it would still not be proof that arbitrary AppKit applications run, and it does not implement UIKit.

`dev-compat matrix` prints the same distinction on the running system.
