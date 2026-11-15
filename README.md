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

That aarch64 command is emulated when the host is x86_64. It does not show that the image boots a Raspberry Pi or Apple Silicon. Details are in `docs/firmware.md`. There is no i686 or armhf image; see `docs/32-bit.md`.

`install-to-disk /dev/sdX` from a terminal on the live system erases that disk, copies the running system onto it, and installs the boot loader. It asks you to type the device name before it changes anything.

## macOS and iOS applications

Instruction translation and an application ABI are different problems. A translator that rewrites x86_64 and aarch64 instructions does not load Mach-O, dyld, AppKit, or UIKit. `docs/isa-and-abi.md` and `docs/compatibility-matrix.md` are the current matrix.

`dev-compat probe FILE` identifies a Mach-O or fat header and stops. It does not execute it. A Linux program built here is not evidence of an original macOS or iOS GUI application.

The Olympium runtime at `517aaaa5409f227de42444cf16612d546c29251b` keeps macOS and iOS software in its library and does not start it. This repository does not change that project. No Apple system image, Google Play image, or Windows image is bundled. See `docs/guest-images.md`.

## What this environment actually booted

Both images below were built from the same tree. Each carries `/usr/bin/olympium-linux` from the separate Olympium repository at `517aaaa5409f227de42444cf16612d546c29251b`, the Ubuntu font family as the interface face, and Mozilla Firefox 160.0 from `packages.mozilla.org` (not Ubuntu's snap transitional package). `install-to-disk` copies that whole root onto a disk, so an installed system gets the same browser and Settings program. The disposable-disk boot test was run on the previous x86_64 image (`25ee807c92c2e7bf7b96fbd5309d0858e996b81202824fee1d1db8d4c51e0f20`) and was not repeated for the checksums below.

x86_64 ISO `build/dev-live-amd64.iso`, 1,459,703,808 bytes, volume id `OLYMPIUM`, hybrid BIOS and UEFI (GRUB El Torito plus an EFI image).

```
a97dabb9745fbab571615229c8390dbbc21c0de0a401c7dcb88bacab20bfc2b0  dev-live-amd64.iso
```

QEMU `pc`, KVM, standard VGA, 1280×800, one e1000 NIC. A boot with `olympium.opensettings=1` opened Settings: search field, icon sidebar, grouped rows, the Liquid Glass switch on, and the system font named Ubuntu. A second boot recorded `window mapped: Mozilla Firefox (app firefox)` and showed Firefox's own window in front of that Settings pane. The first Firefox window is Firefox's welcome screen. Earlier pictures of the desktop, menus, Files, Launchpad, the reminder, and the Olympium runtime are from the previous image `25ee807c…`. An earlier boot of a previous image attached e1000, virtio-net, and NVMe devices and the kernel bound all three; see `docs/drivers.md`.

aarch64 ISO `build/dev-live-arm64.iso`, 1,523,171,328 bytes, same volume id. It contains `/efi/boot/bootaa64.efi`. The desktop binaries inside it are aarch64 ELF, compiled in an arm64 chroot. `/usr/lib/firefox/firefox` in this image is an aarch64 ELF. Inside the build chroot it printed `Mozilla Firefox 160.0`. A full arm64 desktop boot that draws the Firefox window was not run; instruction emulation of that browser is slow, and the live window was checked on x86_64.

```
8fc90725754dcadfab92c3d849f8774f7a0e86730e12ead4092600097ba3028e  dev-live-arm64.iso
```

QEMU `virt`, AAVMF, a `virtio-gpu-pci` display, software emulation on an x86_64 host. The existing arm64 Files picture is from an earlier image (`8600cb65c4c04caa15ed69d39743d9a955e34106d06758466c036cd2f5975027`), which did not carry the runtime or Firefox. That boot is not evidence for a Raspberry Pi, a generic ARM server, or Apple Silicon. The arm64 kernel does not include `i915.ko`.

On the build machine the Firefox content sandbox cannot create a user namespace (`clone` returns `EPERM`). The live image sets `kernel.apparmor_restrict_unprivileged_userns` to 0 so the sandbox can. If a boot still blocks it, `firefox-launch` starts Firefox again with the content sandbox off and writes that fact to the session log.

## Purchase reminder

For the first 5 days after the first boot, Olympium does not ask for a license. After that, one reminder says the system is commercial and asks you to purchase one. It has a close control. Closing it leaves the desktop, menus, Launchpad, blur, drivers, and Olympium fully usable. The same reminder can appear again every 5 days until `olympium-activate LICENSE-KEY` records a purchase, which only hides the reminder.

A personal computer, a school, a business, an intranet, a cloud or CI image, and a factory or bulk install all follow that same rule. There is no second dialog, no watermark, no feature lock, and no shorter timer for those installs. The Olympium name and dragon mark stay on the boot splash and menu bar either way. That is the product name, not a lock.

## Licenses

First-party source is under daniel.ye Source License, Version 1.0. The full text is [LICENSE](LICENSE). Ubuntu packages and the libraries they contain keep their own licenses (`THIRD_PARTY_NOTICES.md`). The image is built from Ubuntu packages and is not Ubuntu. Ubuntu is a trademark of Canonical.
