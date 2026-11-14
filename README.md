# Olympium

The live session and ISO volume are named Olympium. The menu-bar mark is an original geometric dragon, a placeholder until official art is supplied. It is not an Apple logo.

This is a native Linux live system with an original Wayland desktop. It is not a web page, not Electron, and not a copy of macOS. The panel, dock, and launcher are drawn by a small compositor in `desktop/compositor.c`. Windows are normal Wayland clients.

## What the desktop does

- A top panel and a floating dock sample the framebuffer and blur it on every frame. That is live blur of whatever is behind them, including a moving color window. It is not a pre-blurred wallpaper.
- If the renderer cannot read pixels back, the shell switches to an opaque high-contrast drawing and says so. That fallback does not claim a blur.
- Files, Settings, a terminal (`foot`), and a launcher opened from the panel or the Super key.
- Alt-drag moves a window.
- The dock entry Olympium runs `olympium-linux` when that binary is installed. Otherwise it opens an explanation of the pinned revision. See `docs/olympium-integration.md`.

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

`install-to-disk /dev/sdX` from a terminal on the live system copies the running tree onto a disk after the device name is typed a second time.

## macOS and iOS applications

Instruction translation and an application ABI are different problems. A translator that rewrites x86_64 and aarch64 instructions does not load Mach-O, dyld, AppKit, or UIKit. `docs/isa-and-abi.md` and `docs/compatibility-matrix.md` are the current matrix.

`dev-compat probe FILE` identifies a Mach-O or fat header and stops. It does not execute it. A Linux program built here is not evidence of an original macOS or iOS GUI application.

Olympium at `25a592cfd65a4f07449ca03e919bfa97626bb164` refuses macOS and iOS guests on Linux. This repository does not change that project. No Apple system image, Google Play image, or Windows image is bundled. See `docs/guest-images.md`.

## What this environment actually booted

x86_64 ISO `build/dev-live-amd64.iso`, 1.2 GB, volume id `OLYMPIUM`, hybrid BIOS and UEFI (GRUB El Torito plus an EFI image).

```
c0aef0ca0ce2f0c4cfe3f6ca5ee2bd8b29d879a1ae9461a6f8e045f1ff155f74  dev-live-amd64.iso
```

QEMU `pc`, KVM, standard VGA, 1280×800. The same boot attached an e1000 NIC, a virtio-net NIC, and an NVMe disk. The kernel log shows `e1000 0000:00:03.0`, `virtio_net virtio0`, and `nvme nvme0`. The VGA frames show the menu bar, an open File menu, Launchpad, and live blur (`blur=live` in Settings). Drivers and the optional NVIDIA/Broadcom installer are described in `docs/drivers.md`. `driver-install --detect` lists PCI devices and does not download anything.

aarch64 ISO `build/dev-live-arm64.iso`, 1.3 GB, same volume id. It contains `/efi/boot/bootaa64.efi`. The desktop binaries inside it are aarch64 ELF, compiled in an arm64 chroot, including the same purchase reminder.

```
4ac50d5c524fbf5662395856e06f9ddbe6ca3cd849ac4b14062b3d775d404304  dev-live-arm64.iso
```

An earlier QEMU `virt` + AAVMF serial boot reached a login prompt on a previous arm64 image. That boot is not a framebuffer capture: virtio-gpu on that machine has no VGA screendump. It is not evidence for a Raspberry Pi, a generic ARM server, or Apple Silicon. This newer image was checked by reading the squashfs: the reminder text, `olympium-activate`, iris, radeonsi, Nouveau, amdgpu, radeon, and xe are present. The arm64 kernel does not include `i915.ko`.

## Purchase reminder

For the first 5 days after the first boot, Olympium does not ask for a license. After that, one reminder says the system is commercial and asks you to purchase one. It has a close control. Closing it leaves the desktop, menus, Launchpad, blur, drivers, and Olympium fully usable. The same reminder can appear again every 5 days until `olympium-activate LICENSE-KEY` records a purchase, which only hides the reminder.

A personal computer, a school, a business, an intranet, a cloud or CI image, and a factory or bulk install all follow that same rule. There is no second dialog, no watermark, no feature lock, and no shorter timer for those installs. The Olympium name and dragon mark stay on the boot splash and menu bar either way. That is the product name, not a lock.

## Licenses

First-party source is under daniel.ye Source License, Version 1.0. The full text is [LICENSE](LICENSE). Ubuntu packages and the libraries they contain keep their own licenses (`THIRD_PARTY_NOTICES.md`). The image is built from Ubuntu packages and is not Ubuntu. Ubuntu is a trademark of Canonical.
