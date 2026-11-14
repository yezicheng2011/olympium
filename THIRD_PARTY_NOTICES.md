# Third-party notices

First-party files in this repository are under daniel.ye 源码许可证（第 1.0 版）. See `LICENSE` and `NOTICE`.

The live ISO also contains unmodified Ubuntu 24.04 packages. Those packages are not relicensed. Their copyright files are installed under `/usr/share/doc` in the image, and the installed versions are listed in `sbom.tsv` produced by `image/build-iso.sh`. Corresponding source packages are available from Ubuntu's archives.

Components the desktop links directly:

| Component | Role | License |
| --- | --- | --- |
| wlroots 0.17 | Wayland compositor library | MIT |
| Wayland | Display protocol | MIT |
| wayland-protocols `xdg-shell.xml` | Generated header `desktop/protocol/xdg-shell-protocol.h` | MIT |
| cairo, pixman | Drawing and blur buffers | LGPL-2.1 or MPL-1.1 |
| Pango, GLib, GTK 3 | Text and the Files and Settings windows | LGPL-2.1 |
| libxkbcommon | Keyboard mapping | MIT |
| libdrm | Buffer formats | MIT |
| Mesa llvmpipe | Software OpenGL when the firmware has no GPU | MIT |
| foot | Terminal launched from the dock | MIT |
| Linux kernel | Boots the machine | GPL-2.0 |
| GRUB | ISO bootloader | GPL-3.0 |
| live-boot | Mounts the squashfs and the writable overlay | GPL-3.0 |
| DejaVu fonts | UI text | Bitstream Vera license |
| Ubuntu base packages | libc, systemd, and the rest of the userspace | Each package's own copyright file |

Olympium is not copied into this tree. Its license at the pinned commit is Apache-2.0 and stays with that project.

No Apple artwork, font, or framework is included.
