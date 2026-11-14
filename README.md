# Development Desktop

Temporary name. The title is `Development Desktop` in `branding/branding.conf` so it can be replaced when a product name is chosen. The repository name is not the product name.

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

x86_64 ISO `build/dev-live-amd64.iso`, 1.2 GB, volume id `DEV_LIVE`, hybrid BIOS and UEFI (GRUB El Torito plus an EFI image).

```
6dcec4aaff42c096beb23e89f916b5c6e88fb9ff27f6c01b621a6ef370cc74e3  dev-live-amd64.iso
```

QEMU `pc`, KVM, standard VGA, 1280×800. The serial console reached `devdesk login`. The VGA frame showed the panel (frosted over the indigo wallpaper), the flat indigo field in the middle, and a dock whose pixels vary with the color window behind it. A headless pixman run of the same compositor wrote `desktop-headless.ppm` before the ISO existed; that frame has the same structure.

aarch64 ISO `build/dev-live-arm64.iso`, 1.3 GB, same volume id. It contains `/efi/boot/bootaa64.efi`. The desktop binaries inside it are aarch64 ELF, compiled in an arm64 chroot.

```
40b936424be926851bf86a14c90354515bd07e397c770bc04499e5a919188bbf  dev-live-arm64.iso
```

QEMU `virt`, TCG, AAVMF (`UEFI firmware (version 2024.02-2ubuntu0.10 ...)`), GRUB 2.12. The serial console reached `Development Desktop (temporary name)` and `devdesk login:`. That is this image's userspace, not a stock cloud image. It is not a framebuffer capture: virtio-gpu on that machine has no VGA screendump, and the kernel console name there is not `ttyS0`, so the compositor's kernel log line was not in the serial file. It is not evidence for a Raspberry Pi, a generic ARM server, or Apple Silicon.

## Licenses

First-party source is Apache-2.0 (`LICENSE`). Ubuntu packages and the libraries they contain keep their own licenses (`THIRD_PARTY_NOTICES.md`). The image is built from Ubuntu packages and is not Ubuntu. Ubuntu is a trademark of Canonical.
