# 32-bit and 64-bit programs

Olympium ships three live images when the i386 build is included: x86_64, aarch64, and i386. There is no armhf image. iOS is not part of this system.

## Booting on 32-bit x86

The i386 image is Debian 12 (bookworm) userspace plus Debian's `linux-image-686-pae` kernel. That kernel is built with `CONFIG_X86_32=y` and `CONFIG_64BIT` unset. A 64-bit kernel is not the boot kernel. `image/build-iso-i386.sh` builds the ISO. The boot test uses `qemu-system-i386 -M pc`, which is an emulated 32-bit PC, not a physical machine.

Bookworm's wlroots is too old for this desktop, so the image builds wlroots 0.17 from upstream source. The interface font is the Ubuntu font package (an architecture-independent package) with Noto and DejaVu behind it. The browser is Debian's `firefox-esr` package, a Debian rebuild. It is not Mozilla's `packages.mozilla.org` build, and that archive has no i386 Firefox. The dock starts it through the same launcher the 64-bit images use.

## 32-bit programs on a 64-bit system

This is the fast path. `olympium-run` reads the ELF header and `exec`s the program.

- On x86_64 the kernel's IA-32 emulation runs an i386 ELF. The amd64 image installs `libc6:i386`.
- On aarch64 the kernel's 32-bit compatibility mode runs an armhf ELF. The arm64 image installs `libc6:armhf`.

Swap and the 64-bit guest are not used for this direction. User-mode QEMU is only a fallback if the kernel refuses the ELF (`ENOEXEC`). Launchpad has "32-bit Hello". Files starts an executable the same way.

## 64-bit programs on the 32-bit system

A 32-bit process cannot map a 64-bit address space, so this direction boots a small 64-bit guest. The translator on the i386 image is Debian's `qemu-system-x86_64`, which is a 32-bit program. The guest kernel is the Ubuntu amd64 kernel already built for the x86_64 image, used only inside that guest. It is not the kernel that boots the 32-bit machine. Its copyright file is copied with it.

The guest runs a static hello, a program that maps a page at 4 GiB, a dynamically linked Ubuntu bash, and any other 64-bit ELF copied in for that boot. Guest RAM is a file on an extra disk when `olympium.formatdisk=1` is set or a spare disk is already formatted; the 32-bit kernel can page the translator to a swap file on that disk. Without an extra disk the guest still runs, and its RAM is ordinary anonymous memory. The guest asks for 512 MiB by default. A program that needs more RAM than the translator process can hold, roughly 2 GiB, is still a gap.

On the build machine, `qemu-system-x86_64` (the host's 64-bit build of QEMU, TCG) ran that guest four times. Each exited 0 in about 3 seconds: `hello64 bits=64`, a page at `0x100000000`, `GNU bash, version 5.2.21` reporting `x86_64`, and a payload ELF that was not stored in the base initrd. The i386 image's own 32-bit QEMU is a separate boot.

## What was booted

The i386 ISO is `bef87685bdebc51f24a8b1117a652a01aea65189a50bb3cc0e43dcfbac16d7a2`, 724,064,256 bytes. `qemu-system-i386 -M pc -vga virtio` with KVM booted `6.1.0-54-686-pae`. The session line is `arch i686`. The Lawful test app window opened. Four 64-bit programs exited 0 under the image's 32-bit QEMU, in 7, 6, 5, and 5 seconds: hello, a page at `0x100000000`, GNU bash 5.2.21, and a second ELF. Guest RAM was a 512 MiB file on an extra disk. A 2 GiB swap file was active; SwapFree fell by 264 KiB. `-vga std` does not reach the desktop, because that kernel's bochs driver has no PRIME import and wlroots 0.17 rejects the device.

The x86_64 ISO is `6258fda904f660ce2011f9e7f4e27e99548e99fa1e0d2818f64b0388dfece955`. Its desktop ran `hello32` by native IA-32 and opened the Lawful test app.

The arm64 ISO is `2ce330f0d42a6aff714dbc7d80490c13ec2e02e93a004c764c4f69592f9272df`. Its kernel (`CONFIG_COMPAT=y`) ran the shipped armhf `hello32` under QEMU `virt` / `cortex-a57` and printed `bits=32 machine=arm`. A full arm64 desktop was not drawn for this image.

## What is not claimed

The i386 image does not boot a 64-bit kernel. The 64-bit images do not use the swap guest to run 32-bit programs. No armhf ISO is produced. No iOS support is included. These boots are QEMU, not a physical 32-bit PC, a Raspberry Pi, or Apple Silicon. `install-to-disk` was not run on these three images. A 64-bit program that needs more than about 2 GiB of guest RAM still does not fit in the 32-bit translator.
