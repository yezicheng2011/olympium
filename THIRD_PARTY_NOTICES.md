# Third-party notices

First-party files in this repository are under daniel.ye Source License, Version 1.0. See `LICENSE` and `NOTICE`.

The live ISO also contains unmodified Ubuntu 24.04 packages. Those packages are not relicensed. Their copyright files are installed under `/usr/share/doc` in the image, and the installed versions are listed in `sbom.tsv` produced by `image/build-iso.sh`. Corresponding source packages are available from Ubuntu's archives.

`image/fetch-compat-sources.py` can additionally obtain unmodified Waydroid (GPL-3.0), Darling (GPL-3.0 with separately licensed submodules), and the OpenHarmony source manifest (Apache-2.0). These optional source checkouts retain their own license files. Fetching them does not link them into the desktop or include their runtime/system images in the ISO; see `docs/five-platform-integration.md`.

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
| DejaVu fonts | Fallback glyphs for scripts the Ubuntu fonts do not cover | Bitstream Vera license |
| Ubuntu fonts (`fonts-ubuntu`) | Default interface and monospace face | Ubuntu Font License 1.0 |
| Noto fonts (`fonts-noto-core`, `fonts-noto-mono`, `fonts-noto-cjk`) | Fallback for scripts outside the Ubuntu fonts | SIL Open Font License 1.1 |
| Firefox | Preinstalled browser on the x86_64 and aarch64 images, from Mozilla's package archive, not Ubuntu's snap transitional package | Mozilla Public License 2.0 |
| Ubuntu base packages | libc, systemd, and the rest of the userspace on those two images | Each package's own copyright file |
| Debian 12 (bookworm) i386 | Userspace of the i386 image, including `linux-image-686-pae`, `firefox-esr`, and `qemu-system-x86` | Each package's own copyright file, from Debian |
| Firefox ESR | The i386 browser. Debian's rebuild, not Mozilla's packages.mozilla.org build | Mozilla Public License 2.0 |
| QEMU | The i386 image's `qemu-system-x86_64`, a 32-bit program, boots the 64-bit guest | GPL-2.0 |

Olympium source is not copied into this tree. When the image carries the Olympium runtime programs, they are built from commit `517aaaa5409f227de42444cf16612d546c29251b` of that project, and that project's own `LICENSE` and `NOTICE` files are installed unchanged at `/usr/share/doc/olympium-runtime/`. The runtime links the system copies of GTK 4 and libadwaita (LGPL-2.1-or-later), which stay under their own license.

The i386 image also carries a small 64-bit guest used only to run 64-bit programs. That guest's kernel is the Ubuntu amd64 `vmlinuz` already built for the x86_64 image, plus Ubuntu's bash, libc, and libtinfo. Their copyright files are copied to `/usr/share/doc/olympium-amd64-guest/`. That kernel does not boot the 32-bit machine.

When the image is built with `OLYMPIUM_DARLING=1` it carries Darling, built from commit `3fcd2e1aa85079789d19d82415695bdecc512241` by `image/build-darling.sh`. The build applies the libdispatch patch in `image/darling-patches/`. GUI builds also apply the Cocotron patches. Darling itself is GPL-3.0. Its submodules keep their own licenses, and the parts Apple published as open source are under the Apple Public Source License. The image places Darling's `LICENSE` and a `SOURCE` file with the commit, patch checksums, and installed program checksums at `/usr/share/doc/darling/`.

The image also uses Ubuntu's `7zip` and `cpio` packages to unpack Mac disk images and packages. They are Ubuntu packages like the rest.

No Apple artwork, font, or closed framework is included. iOS is not part of this system.

The experimental limited microG framework source in `validation/android/microg-framework/` adapts LineageOS MicrogUtils policy and public certificate constants at revision `48b4df3c577586ef05a3cd7205fa1c0e71504cbd`. Those files retain Apache-2.0 notices and their LICENSE. The generated framework is not committed or staged in the ISO; the immutable source URL and source digest are in profile.json.
