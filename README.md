# Olympium

The live session and ISO volume are named Olympium. The menu-bar mark is an original abstract Eastern dragon. It is not an Apple logo, and no Apple artwork, wallpaper, font, or system asset is in this tree.

This is a native Linux live system with its own Wayland desktop. It is not a web page and not Electron. The layout follows the macOS desktop closely: a thin menu bar, left window controls, a floating dock, and a full-screen launcher. Every pixel of it is drawn here, by a small compositor in `desktop/compositor.c`. Windows are normal Wayland clients.

## What the desktop does

- Glass. The menu bar, menus, dock, window title bars, dialogs, and Launchpad read the framebuffer back on every frame, bend it toward the edges, blur it, and add a bright rim and a sheen. It is the live picture behind each surface, not a pre-blurred wallpaper. Glass is the default.
- If the renderer cannot read pixels back, the shell switches to an opaque drawing and says so in Settings. That fallback does not claim glass.
- Menu bar: the dragon mark, the front application's name, File, Edit, View, Go, Window, and Help on the left. A network glyph, search and quick-settings glyphs, and the clock on the right. The network glyph is a fan wedge that fills with the live wireless signal, shows three dots under a full wedge for a wired link, and is struck through with no link.
- Windows: rounded corners, a soft shadow, a centered title, and red, yellow, and green controls on the left. That includes Settings, Files, the terminal, and the purchase reminder.
- Files: a sidebar of favorites and locations, back and forward, icon and list views, search, and an item count.
- Dock: a centered glass capsule of unlabeled icons. An icon grows under the pointer, shows its name above the dock, and has a dot while that application has a window open.
- Launchpad: a full-screen blurred backdrop, a search field at the top center, a five-by-three grid of large icons, folders, and page dots. It lists programs that are really on the image. Arrow keys, Return, Page Up and Page Down, typing to search, and Escape all work.
- Settings: a search field, an icon sidebar, and grouped rows, in the arrangement of a system-settings window. The panes are appearance, drivers, compatibility, Olympium, license, and about. The sidebar glyphs are original.
- Alt-drag moves a window.
- The dock entry Olympium starts the Olympium runtime, a separate program that the image build copies in when a prebuilt copy is on the build machine. Without it the entry opens an explanation. See `docs/olympium-integration.md`.
- The interface font is the Ubuntu family. Ubuntu Mono is the monospace face. Glyphs those faces do not include come from Noto Sans, then Noto Sans CJK, then DejaVu. Settings → Appearance names that chain.
- Firefox is installed from Mozilla's package archive and is the default handler for http and https. The dock entry starts it. Ubuntu's transitional snap package is not used.
- `install-to-disk /dev/sdX` copies the running system onto a whole disk and makes it bootable, for BIOS or UEFI, whichever this boot used.

What still differs from macOS at a glance: the system font is Ubuntu, not San Francisco. There is one title bar above each application's own toolbar instead of a single merged one. Application content is drawn by GTK, so buttons, text fields, and scroll bars inside a window are GTK's. There is no window-open or minimize animation, no Mission Control, no Spotlight panel behind the search glyph, and no quick-settings panel behind its glyph yet.

## Build

The build needs root, `debootstrap`, `mksquashfs`, `grub-mkrescue`, and a network path to the Ubuntu 24.04 archive. On x86_64:

```sh
./image/build-iso.sh amd64
```

The ISO is written to `build/dev-live-amd64.iso` with a sibling `.sha256` file. The script records every installed package in the image at `/usr/share/doc/dev-desktop/sbom.tsv`.

For aarch64, on an x86_64 machine with `qemu-user-static`:

```sh
./image/build-iso.sh arm64
```

That path cross-builds the desktop inside an arm64 chroot and asks GRUB there for an arm64 EFI image. It is slower because every package script runs under qemu-user.

Host checks that do not need an ISO:

```sh
make -C desktop test
```

`desktop/protocol/xdg-shell-protocol.h` is generated from wayland-protocols (MIT). Regenerate it with `wayland-scanner server-header` if you need to.

## Boot

x86_64 hybrid ISO, BIOS or UEFI, tested in QEMU:

```sh
qemu-system-x86_64 -m 2048 -smp 2 -cdrom build/dev-live-amd64.iso -boot d \
  -vga std -serial stdio
```

Add `-enable-kvm` when the host is x86_64 and KVM is available. The live root is a squashfs. Writes go to a tmpfs overlay and disappear on power-off.

aarch64, only as a QEMU `virt` guest with AAVMF:

```sh
qemu-system-aarch64 -machine virt -cpu max -m 2048 \
  -bios /usr/share/AAVMF/AAVMF_CODE.fd \
  -cdrom build/dev-live-arm64.iso -boot d -serial stdio \
  -device virtio-gpu-pci -device usb-kbd -device usb-tablet
```

That aarch64 command is emulated when the host is x86_64. It does not show that the image boots a Raspberry Pi or Apple Silicon. Details are in `docs/firmware.md`.

i386, a 32-bit kernel, tested in QEMU as an emulated PC:

```sh
qemu-system-i386 -M pc -m 2048 -cdrom build/dev-live-i386.iso -boot d \
  -vga virtio -serial stdio
```

Use `-vga virtio`. Debian 6.1's bochs display does not advertise PRIME import, and this compositor will not open that device. There is no armhf image. See `docs/32-bit.md`.

`install-to-disk /dev/sdX` from a terminal on the live system erases that disk, copies the running system onto it, and installs the boot loader. It asks you to type the device name before it changes anything.

## macOS applications

The dock, Launchpad, and Files open the Lawful test app. It is a Linux window. It is not a Mac application. This image does not include Apple trademarks, fonts, artwork, an Apple SDK, or a macOS system image, and it does not bypass code signing or DRM.

The Olympium runtime at `517aaaa5409f227de42444cf16612d546c29251b` lists that program as a Linux executable. It also lists a Mach-O header fixture and does not start it. This repository does not change that runtime. iOS is not part of this system.

`dev-compat probe FILE` identifies a Mach-O or fat header and stops. `docs/isa-and-abi.md` and `docs/compatibility-matrix.md` are the current matrix. No Apple, Google Play, or Windows system image is bundled. See `docs/guest-images.md`.

## Installing a program

Double-clicking a file in Files opens Install. The file is not run. A Linux program whose plan is a native process can be imported after a checkbox, then launched. Launch uses `olympium-run` when that helper is installed, so a 32-bit program on a 64-bit CPU still takes the native path and a 64-bit program on the i386 image still takes the swap-backed guest.

A Mach-O file can be kept and is not started. An iOS package is refused. A `.deb`, `.rpm`, `.apk`, `.hap`, or `.exe` is identified and not installed. Nothing is downloaded. The checksummed ISOs in the next section do not contain `dev-install` yet; the next image build installs it. `desktop/test-install.sh` is the host test. The matrix is `docs/desktop-apps.md`. The contract for the runtime is `docs/runtime-desktop.md`.

## What this environment actually booted

Three images, volume id `OLYMPIUM`. These checksums predate `dev-install` and do not contain it. The x86_64 and aarch64 images carry `/usr/bin/olympium-linux` from commit `517aaaa5409f227de42444cf16612d546c29251b`. The i386 image has no runtime binary for that architecture, so its Olympium dock entry opens Settings. The interface font on all three is Ubuntu. `install-to-disk` was verified on an earlier x86_64 image (`25ee807c92c2e7bf7b96fbd5309d0858e996b81202824fee1d1db8d4c51e0f20`) and was not run again for the checksums below. Older part trees, including `a97dabb9…` and `8fc90725…`, were left in place.

x86_64 ISO `build/dev-live-amd64.iso`, 1,465,339,904 bytes, hybrid BIOS and UEFI.

```
6258fda904f660ce2011f9e7f4e27e99548e99fa1e0d2818f64b0388dfece955  dev-live-amd64.iso
```

QEMU `pc`, KVM, on an x86_64 host. The desktop ran a real i386 ELF by the kernel's IA-32 path. The log says `olympium-run: native IA-32` and the program printed `hello32 bits=32 machine=i386`. The same boot mapped the Lawful test app window. Mozilla Firefox in this image is `160.0~build1` from `packages.mozilla.org`. The picture is `olympium-x86_64-13-hello32-testapp.png`. Settings, the Ubuntu font row, and the Firefox window were drawn on the previous image `a97dabb9…`. That Firefox window is Firefox's welcome screen.

aarch64 ISO `build/dev-live-arm64.iso`, 1,575,903,232 bytes. It contains `/efi/boot/bootaa64.efi`. Desktop binaries are aarch64. `/usr/lib/firefox/firefox` is aarch64 and printed `Mozilla Firefox 160.0` in the build chroot.

```
2ce330f0d42a6aff714dbc7d80490c13ec2e02e93a004c764c4f69592f9272df  dev-live-arm64.iso
```

The kernel has `CONFIG_ARM64=y` and `CONFIG_COMPAT=y`. QEMU `virt`, `-cpu cortex-a57`, software emulation on an x86_64 host, booted that kernel and executed the image's armhf `hello32`. It printed `hello32 bits=32 machine=arm` and exited 0. That is not a Raspberry Pi, a generic ARM board, or Apple Silicon, and it is not a full desktop draw of this image. The earlier arm64 Files picture is from `8600cb65…`. The arm64 kernel does not include `i915.ko`.

i386 ISO `build/dev-live-i386.iso`, 724,064,256 bytes. The boot kernel is Debian `6.1.0-54-686-pae` (`CONFIG_X86_32=y`, `CONFIG_64BIT` unset). Userspace is Debian 12. The browser is Debian `firefox-esr` `153.7.0esr-1~deb12u1`, labeled as a Debian rebuild. It is not Mozilla's packages.mozilla.org build, and its window was not drawn in this boot.

```
bef87685bdebc51f24a8b1117a652a01aea65189a50bb3cc0e43dcfbac16d7a2  dev-live-i386.iso
```

`qemu-system-i386 -M pc -vga virtio`, KVM, 2 GiB, on an x86_64 host. The session log says `arch i686 6.1.0-54-686-pae`. The compositor started. The Lawful test app opened (the log says `window mapped: Olympium (app olympium-testapp)`; the window's own subtitle is "Lawful test app"). A terminal then ran four 64-bit programs with the image's 32-bit `qemu-system-x86_64`, TCG, 512 MiB of guest RAM in a file on an extra virtio disk, and a 2 GiB swap file on that disk. All four exited 0: `hello64 bits=64 machine=x86_64` in 7 seconds, a mapped page at `0x100000000` in 6 seconds, GNU bash 5.2.21 reporting `x86_64` in 5 seconds, and `payload64 bits=64` in 5 seconds. SwapTotal was 2,097,148 kB and SwapFree fell to 2,096,884 kB, so 264 KiB was in swap. This is an emulated PC, not a physical 32-bit computer. The pictures are `olympium-i386-01-desktop.png` and `olympium-i386-02-64bit.png` (2:02 AM ET, Sunday, November 15, 2026). A boot with `-vga std` reached the same kernel and then the compositor exited, because bochs-drm does not advertise PRIME import. There is no armhf ISO. A 64-bit program that needs more than about 2 GiB of guest RAM still does not run: the translator is a 32-bit process.

On the build machine the Firefox content sandbox cannot create a user namespace (`clone` returns `EPERM`). The live image sets `kernel.apparmor_restrict_unprivileged_userns` to 0 so the sandbox can. If a boot still blocks it, `firefox-launch` starts Firefox again with the content sandbox off and writes that fact to the session log.

## Purchase reminder

For the first 5 days after the first boot, Olympium does not ask for a license. After that, one reminder says the system is commercial and asks you to purchase one. It has a close control. Closing it leaves the desktop, menus, Launchpad, blur, drivers, and Olympium fully usable. The same reminder can appear again every 5 days until `olympium-activate LICENSE-KEY` records a purchase, which only hides the reminder.

A personal computer, a school, a business, an intranet, a cloud or CI image, and a factory or bulk install all follow that same rule. There is no second dialog, no watermark, no feature lock, and no shorter timer for those installs. The Olympium name and dragon mark stay on the boot splash and menu bar either way. That is the product name, not a lock.

## Licenses

First-party source is under daniel.ye Source License, Version 1.0. The full text is [LICENSE](LICENSE). Ubuntu packages and the libraries they contain keep their own licenses (`THIRD_PARTY_NOTICES.md`). The image is built from Ubuntu packages and is not Ubuntu. Ubuntu is a trademark of Canonical.
