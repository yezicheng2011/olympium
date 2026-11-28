#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Builds ONE single universal multi-architecture hybrid bootable ISO
# (build/olympium-universal.iso) supporting:
#   - x86_64 (amd64)  BIOS + UEFI (bootx64.efi)
#   - x86 32-bit (i386) BIOS + IA32 UEFI (bootia32.efi)
#   - ARM 64-bit (arm64) UEFI (bootaa64.efi)
#   - ARM 32-bit (armhf) UEFI (bootarm.efi, when included in OLYMPIUM_UNIVERSAL_ARCHES)
#
# Every architecture rootfs inside the universal ISO includes cross-architecture
# execution (qemu-user-static + foreign libc + binfmt_misc) so x86/x86_64/arm32/arm64
# software runs seamlessly on ANY host CPU, plus auto-Wine, Darling (Intel & M-series
# Mac support), slim Android (scrcpy), auto-configured OpenHarmony SDK, and the
# shared VB.NET (.NET 8) Olympium desktop suite.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCHES=${OLYMPIUM_UNIVERSAL_ARCHES:-"amd64 i386 arm64"}
WORK=/var/tmp/dev-live/iso-universal
OUT="$REPO/build/olympium-universal.iso"

mkdir -p "$REPO/build"
rm -rf "$WORK"
mkdir -p "$WORK/boot/grub" "$WORK/EFI/BOOT"

# 1. Ensure each architecture rootfs is built and packed as its own compact
#    xz+BCJ squashfs under $WORK/live/<arch>/.
stage_arch_payload() {
	local arch=$1
	local root="/var/tmp/dev-live/rootfs-$arch"
	local live_dir="$WORK/live/$arch"
	mkdir -p "$live_dir"

	if [ ! -d "$root/usr/bin" ] || [ "${OLYMPIUM_REBUILD_ROOTFS:-0}" = 1 ]; then
		case "$arch" in
		amd64|arm64)
			OLYMPIUM_ROOTFS_ONLY=1 bash "$REPO/image/build-iso.sh" "$arch"
			;;
		i386)
			if [ -x "$root/bin/sh" ]; then
				OLYMPIUM_ROOTFS_ONLY=1 bash "$REPO/image/build-iso-i386.sh" || true
			else
				echo "universal-iso: bootstrapping i386 Debian bookworm rootfs..."
				debootstrap --arch=i386 --variant=minbase bookworm "$root" http://deb.debian.org/debian
				OLYMPIUM_ROOTFS_ONLY=1 bash "$REPO/image/build-iso-i386.sh"
			fi
			;;
		armhf)
			echo "universal-iso: armhf rootfs must be pre-staged at $root" >&2
			return 1
			;;
		esac
	fi

	# Ensure universal cross-arch, VB.NET, unified dragon icon, and slimming are applied.
	bash "$REPO/image/slim-rootfs.sh" "$root"

	local kimg initrd kver bcj
	if [ "$arch" = i386 ]; then
		kimg=$(ls -1 "$root"/boot/vmlinuz-*-686-pae 2>/dev/null | sort -V | tail -n 1)
		[ -n "$kimg" ] || kimg=$(ls -1 "$root"/boot/vmlinuz-* | sort -V | tail -n 1)
	else
		kimg=$(ls -1 "$root"/boot/vmlinuz-* | sort -V | tail -n 1)
	fi
	kver=${kimg##*/vmlinuz-}
	initrd="$root/boot/initrd.img-$kver"
	cp -L "$kimg" "$live_dir/vmlinuz"
	cp -L "$initrd" "$live_dir/initrd.img"

	case "$arch" in
	amd64|i386) bcj=x86 ;;
	arm64|armhf) bcj=arm ;;
	*) bcj=x86 ;;
	esac

	echo "universal-iso: packing $arch squashfs (xz + BCJ $bcj)..."
	mksquashfs "$root" "$live_dir/filesystem.squashfs" -comp xz -b 1M -Xbcj "$bcj" -noappend \
		-e var/cache/apt/archives var/lib/apt/lists tmp
	ls -lh "$live_dir/filesystem.squashfs"
}

for a in $ARCHES; do
	stage_arch_payload "$a"
done

# 2. Build shared GRUB splash and unified auto-detecting grub.cfg.
if [ -f "$REPO/desktop/splash.png" ]; then
	cp "$REPO/desktop/splash.png" "$WORK/boot/grub/splash.png"
fi

cat > "$WORK/boot/grub/grub.cfg" <<'EOF'
set timeout=5
if loadfont unicode ; then
	set gfxmode=1280x800,auto
	terminal_output gfxterm
fi
insmod png
insmod all_video
background_image /boot/grub/splash.png
set menu_color_normal=white/black
set menu_color_highlight=black/light-gray

# Automatically pick the right OS kernel for this computer's CPU architecture:
# ARM64 -> arm64, ARM32 -> armhf, 64-bit x86 -> amd64, 32-bit x86 -> i386.
if [ "$grub_cpu" = "arm64" ]; then
	set default="Olympium (ARM64)"
elif [ "$grub_cpu" = "arm" ]; then
	set default="Olympium (ARM32)"
elif cpuid -l; then
	set default="Olympium (x86_64)"
else
	set default="Olympium (x86 32-bit)"
fi

if [ -f /live/amd64/vmlinuz ]; then
	menuentry "Olympium (x86_64)" {
		linux /live/amd64/vmlinuz boot=live live-media-path=/live/amd64 quiet console=ttyS0,115200 console=tty0
		initrd /live/amd64/initrd.img
	}
	menuentry "Olympium (x86_64, high contrast)" {
		linux /live/amd64/vmlinuz boot=live live-media-path=/live/amd64 quiet console=ttyS0,115200 console=tty0 olympium.contrast=1
		initrd /live/amd64/initrd.img
	}
fi

if [ -f /live/i386/vmlinuz ]; then
	menuentry "Olympium (x86 32-bit)" {
		linux /live/i386/vmlinuz boot=live live-media-path=/live/i386 quiet console=ttyS0,115200 console=tty0
		initrd /live/i386/initrd.img
	}
fi

if [ -f /live/arm64/vmlinuz ]; then
	menuentry "Olympium (ARM64)" {
		linux /live/arm64/vmlinuz boot=live live-media-path=/live/arm64 quiet console=ttyAMA0,115200 console=tty0
		initrd /live/arm64/initrd.img
	}
fi

if [ -f /live/armhf/vmlinuz ]; then
	menuentry "Olympium (ARM32)" {
		linux /live/armhf/vmlinuz boot=live live-media-path=/live/armhf quiet console=ttyAMA0,115200 console=tty0
		initrd /live/armhf/initrd.img
	}
fi
EOF

# 3. Generate multi-architecture EFI bootloaders (x86_64 + i386 + arm64 + optional armhf)
#    plus BIOS El Torito bootloader into a single unified ISO image.
EFI_MODS="part_gpt part_msdos fat iso9660 normal boot linux search search_fs_uuid search_label configfile all_video gfxterm font png"

build_efi() {
	local fmt=$1 out_efi=$2 mod_dir=$3
	if [ -d "$mod_dir" ]; then
		grub-mkstandalone -O "$fmt" -d "$mod_dir" -o "$out_efi" \
			"boot/grub/grub.cfg=$WORK/boot/grub/grub.cfg"
		echo "universal-iso: built EFI image $out_efi ($fmt)"
	else
		echo "universal-iso: note: GRUB modules for $fmt not found at $mod_dir (skipping $out_efi)"
	fi
}

build_efi x86_64-efi "$WORK/EFI/BOOT/bootx64.efi" "/usr/lib/grub/x86_64-efi"
build_efi i386-efi   "$WORK/EFI/BOOT/bootia32.efi" "/usr/lib/grub/i386-efi"
build_efi arm64-efi  "$WORK/EFI/BOOT/bootaa64.efi" "/usr/lib/grub/arm64-efi"
build_efi arm-efi    "$WORK/EFI/BOOT/bootarm.efi"  "/usr/lib/grub/arm-efi"

# Also check inside chroot roots if host didn't have cross-arch grub-efi-*-bin installed.
for pair in "arm64:arm64-efi:bootaa64.efi" "i386:i386-efi:bootia32.efi"; do
	IFS=: read -r rarch fmt fname <<< "$pair"
	if [ ! -f "$WORK/EFI/BOOT/$fname" ] && [ -d "/var/tmp/dev-live/rootfs-$rarch/usr/lib/grub/$fmt" ]; then
		build_efi "$fmt" "$WORK/EFI/BOOT/$fname" "/var/tmp/dev-live/rootfs-$rarch/usr/lib/grub/$fmt"
	fi
done

# Create unified FAT EFI System Partition image containing ALL architectures' EFI loaders.
EFI_IMG="$WORK/boot/grub/efiboot.img"
dd if=/dev/zero of="$EFI_IMG" bs=1M count=24 status=none
mkfs.vfat -F 16 -n OLYMPIUMEFI "$EFI_IMG" >/dev/null
mmd -i "$EFI_IMG" ::/EFI ::/EFI/BOOT
for efi in "$WORK/EFI/BOOT/"*.efi; do
	[ -f "$efi" ] && mcopy -i "$EFI_IMG" "$efi" ::/EFI/BOOT/
done

# Build BIOS El Torito core image when grub-pc-bin is present.
XORRISO_BIOS=()
if [ -d /usr/lib/grub/i386-pc ]; then
	grub-mkimage -O i386-pc -p /boot/grub -o "$WORK/boot/grub/core.img" \
		biosdisk iso9660 part_msdos part_gpt normal boot linux search configfile cpuid all_video gfxterm font png
	cat /usr/lib/grub/i386-pc/cdboot.img "$WORK/boot/grub/core.img" > "$WORK/boot/grub/bios.img"
	XORRISO_BIOS=(
		-b boot/grub/bios.img
		-no-emul-boot
		-boot-load-size 4
		-boot-info-table
		--grub2-boot-info
		--grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img
	)
fi

# 4. Pack ONE single hybrid multi-arch ISO (ISO9660 level 3 supports >4 GiB if needed).
xorriso -as mkisofs \
	-iso-level 3 \
	-full-iso9660-filenames \
	-volid OLYMPIUM \
	-joliet -joliet-long -rock \
	"${XORRISO_BIOS[@]}" \
	-eltorito-alt-boot \
	-e boot/grub/efiboot.img \
	-no-emul-boot \
	-isohybrid-gpt-basdat \
	-output "$OUT" \
	"$WORK"

sha256sum "$OUT" | tee "$OUT.sha256"
ls -lh "$OUT"
echo "universal-iso: built single universal multi-arch ISO at $OUT (arches: $ARCHES)"
