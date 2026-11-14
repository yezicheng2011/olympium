#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Builds a live ISO for amd64 or arm64 from Ubuntu 24.04 packages plus the
# first-party desktop. The image is not Ubuntu. No guest operating-system
# images are embedded.
set -euo pipefail

ARCH=${1:-amd64}
case "$ARCH" in
	amd64|arm64) ;;
	*) echo "usage: $0 amd64|arm64" >&2; exit 1 ;;
esac

REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=/var/tmp/dev-live/rootfs-$ARCH
WORK=/var/tmp/dev-live/iso-$ARCH
MIRROR_AMD64=http://archive.ubuntu.com/ubuntu
MIRROR_ARM64=http://ports.ubuntu.com/ubuntu-ports
SUITE=noble

if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi

write_sources() {
	local mirror=$1
	cat > "$ROOT/etc/apt/sources.list" <<EOF
deb $mirror $SUITE main universe
deb $mirror ${SUITE}-updates main universe
deb $mirror ${SUITE}-security main universe
EOF
}

bootstrap() {
	if [ -x "$ROOT/bin/sh" ] && [ -d "$ROOT/usr" ]; then
		echo "reusing $ROOT"
		return
	fi
	mkdir -p "$ROOT"
	if [ "$ARCH" = amd64 ]; then
		debootstrap --variant=minbase "$SUITE" "$ROOT" "$MIRROR_AMD64"
		write_sources "$MIRROR_AMD64"
	else
		if [ ! -e /proc/sys/fs/binfmt_misc/register ]; then
			mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc
		fi
		if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
			echo ':qemu-aarch64:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-aarch64-static:F' \
				> /proc/sys/fs/binfmt_misc/register
		fi
		debootstrap --arch=arm64 --foreign --variant=minbase "$SUITE" "$ROOT" "$MIRROR_ARM64"
		cp /usr/bin/qemu-aarch64-static "$ROOT/usr/bin/qemu-aarch64-static"
		chroot "$ROOT" /debootstrap/debootstrap --second-stage
		write_sources "$MIRROR_ARM64"
	fi
}

mount_chroot() {
	mkdir -p "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" "$ROOT/dev/pts"
	mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
	mountpoint -q "$ROOT/sys" || mount -t sysfs sysfs "$ROOT/sys"
	mountpoint -q "$ROOT/dev" || mount --bind /dev "$ROOT/dev"
	mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
	cp /etc/resolv.conf "$ROOT/etc/resolv.conf"
}

umount_chroot() {
	umount "$ROOT/dev/pts" 2>/dev/null || true
	umount "$ROOT/dev" 2>/dev/null || true
	umount "$ROOT/proc" 2>/dev/null || true
	umount "$ROOT/sys" 2>/dev/null || true
}

install_drivers() {
	mount_chroot
	chroot "$ROOT" apt-get update
	# Match the kernel already installed in this root, not the host kernel.
	local kver
	kver=$(cd "$ROOT/lib/modules" && echo *)
	# Redistributable modules, Mesa, and linux-firmware.
	# NVIDIA proprietary and Broadcom STA are not installed; see driver-install.
	local pkgs="linux-modules-${kver} linux-modules-extra-${kver} linux-firmware mesa-vulkan-drivers mesa-va-drivers mesa-vdpau-drivers wireless-regdb pciutils usbutils alsa-ucm-conf firmware-sof-signed iproute2"
	# Open Intel VAAPI userspace. The non-free sibling stays out (multiverse).
	local cand
	cand=$(chroot "$ROOT" apt-cache policy intel-media-va-driver 2>/dev/null | awk '/Candidate:/{print $2}')
	if [ -n "$cand" ] && [ "$cand" != "(none)" ]; then
		pkgs="$pkgs intel-media-va-driver"
	fi
	if [ "$ARCH" = amd64 ]; then
		pkgs="$pkgs intel-microcode amd64-microcode"
		if chroot "$ROOT" apt-cache show "linux-modules-iwlwifi-${kver}" >/dev/null 2>&1; then
			pkgs="$pkgs linux-modules-iwlwifi-${kver}"
		fi
		if chroot "$ROOT" apt-cache show "linux-modules-ipu6-${kver}" >/dev/null 2>&1; then
			pkgs="$pkgs linux-modules-ipu6-${kver}"
		fi
	fi
	DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends $pkgs
	if [ ! -e "$ROOT/usr/share/doc/linux-firmware/copyright" ] && [ ! -e "$ROOT/usr/share/doc/linux-firmware-misc/copyright" ]; then
		echo "linux-firmware copyright file missing" >&2
		exit 1
	fi
}

verify_drivers() {
	local kver miss=0
	kver=$(cd "$ROOT/lib/modules" && echo *)
	local builtin="$ROOT/lib/modules/$kver/modules.builtin"
	have_mod() {
		local name=$1 alt=${1//_/-}
		local hit
		hit=$(find "$ROOT/lib/modules/$kver" \( -name "${name}.ko*" -o -name "${alt}.ko*" \) -print -quit)
		if [ -n "$hit" ]; then
			return 0
		fi
		if [ -f "$builtin" ] && grep -E -q "/(${name}|${alt})\\.ko$" "$builtin"; then
			return 0
		fi
		return 1
	}
	local required="nvme snd xhci-hcd hid-multitouch btusb amdgpu radeon nouveau xe e1000 bochs virtio_net virtio_blk virtio-gpu qxl"
	if [ "$ARCH" = amd64 ]; then
		required="$required i915 iwlwifi"
	fi
	for name in $required; do
		if have_mod "$name"; then
			echo "driver-ok module $name"
		else
			echo "driver-missing module $name" >&2
			miss=1
		fi
	done
	local fwroot="$ROOT/lib/firmware"
	if [ -d "$ROOT/usr/lib/firmware" ]; then
		fwroot="$ROOT/usr/lib/firmware"
	fi
	for dir in amdgpu radeon xe i915 iwlwifi ath10k brcm rtl_nic nvidia; do
		local fw
		fw=$(find "$fwroot" -maxdepth 2 -name "${dir}*" -print -quit)
		if [ -d "$fwroot/$dir" ] || [ -n "$fw" ]; then
			echo "driver-ok firmware $dir"
		else
			echo "driver-missing firmware $dir" >&2
			miss=1
		fi
	done
	local dri="$ROOT/usr/lib/x86_64-linux-gnu/dri"
	if [ ! -d "$dri" ]; then
		dri="$ROOT/usr/lib/aarch64-linux-gnu/dri"
	fi
	if [ ! -d "$dri" ]; then
		echo "driver-missing mesa dri" >&2
		miss=1
	else
		for so in iris_dri.so radeonsi_dri.so r600_dri.so nouveau_dri.so; do
			if [ -e "$dri/$so" ]; then
				echo "driver-ok mesa $so"
			else
				echo "driver-missing mesa $so" >&2
				miss=1
			fi
		done
	fi
	for icd in intel_icd.json radeon_icd.json nouveau_icd.json; do
		if [ -e "$ROOT/usr/share/vulkan/icd.d/$icd" ]; then
			echo "driver-ok vulkan $icd"
		else
			echo "driver-missing vulkan $icd" >&2
			miss=1
		fi
	done
	install -d "$ROOT/usr/share/olympium"
	{
		echo "arch ${ARCH}"
		echo "kernel ${kver}"
		for pat in 'linux-image-*' 'linux-modules-*' 'linux-firmware*' 'mesa-*' 'libgl1-mesa-*' \
			intel-microcode amd64-microcode wireless-regdb firmware-sof-signed \
			alsa-ucm-conf pciutils usbutils iproute2 intel-media-va-driver; do
			chroot "$ROOT" dpkg-query -W -f '${db:Status-abbrev} ${Package}\t${Version}\n' "$pat" 2>/dev/null \
				| awk '$1 == "ii" { $1=""; sub(/^ /,""); print }' || true
		done
		echo "not-bundled nvidia-driver broadcom-sta-dkms"
	} | tee "$ROOT/usr/share/olympium/driver-manifest.txt" "$REPO/build/driver-manifest-${ARCH}.txt"
	if [ "$miss" -ne 0 ]; then
		echo "driver verification failed" >&2
		exit 1
	fi
}

install_packages() {
	mount_chroot
	chroot "$ROOT" apt-get update
	DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
		linux-image-generic systemd systemd-sysv udev dbus kmod live-boot initramfs-tools \
		libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libinput10 libxkbcommon0 \
		libdrm2 libpixman-1-0 libwayland-server0 libegl1 libgles2 libgl1-mesa-dri \
		libgtk-3-0 foot fonts-dejavu-core adwaita-icon-theme ca-certificates \
		e2fsprogs dosfstools parted grub-common util-linux bash coreutils \
		libwlroots12t64 libseat1
	if [ "$ARCH" = amd64 ]; then
		DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
			grub-efi-amd64-bin grub-pc-bin
	else
		DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
			grub-efi-arm64-bin xorriso mtools build-essential pkg-config libwlroots-dev libwayland-dev \
			libcairo2-dev libpango1.0-dev libinput-dev libxkbcommon-dev libdrm-dev \
			libpixman-1-dev libgtk-3-dev wayland-protocols
	fi
}

stage_desktop() {
	mount_chroot
	DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends parted dosfstools kmod || true
	make -C "$REPO/desktop" all
	install -d "$ROOT/usr/bin" "$ROOT/usr/libexec/dev-desktop" \
		"$ROOT/etc/dev-desktop" "$ROOT/etc/systemd/system/multi-user.target.wants" \
		"$ROOT/usr/share/doc/dev-desktop"
	install -m 755 "$REPO/desktop/dev-compositor" "$REPO/desktop/dev-files" \
		"$REPO/desktop/dev-settings" "$REPO/desktop/dev-swatch" \
		"$REPO/desktop/dev-compat" "$ROOT/usr/bin/"
	install -m 755 "$REPO/image/run-session" "$REPO/image/startup.sh" \
		"$REPO/image/olympium-launch" "$REPO/image/install-to-disk" \
		"$REPO/image/guest-fetch.sh" \
		"$REPO/image/driver-report" "$REPO/image/driver-install" \
		"$ROOT/usr/libexec/dev-desktop/"
	install -m 755 "$REPO/image/driver-install" "$ROOT/usr/bin/driver-install"
	install -m 755 "$REPO/image/driver-report" "$ROOT/usr/bin/driver-report"
	install -m 755 "$REPO/image/olympium-activate" "$ROOT/usr/bin/olympium-activate"
	install -d "$ROOT/usr/share/olympium"
	install -m 644 "$REPO/image/guest-pins.txt" "$ROOT/usr/share/olympium/guest-pins.txt"
	install -m 644 "$REPO/branding/branding.conf" "$ROOT/etc/dev-desktop/branding.conf"
	install -m 644 "$REPO/image/dev-desktop.service" "$ROOT/etc/systemd/system/dev-desktop.service"
	ln -sfn /etc/systemd/system/dev-desktop.service \
		"$ROOT/etc/systemd/system/multi-user.target.wants/dev-desktop.service"
	ln -sfn /dev/null "$ROOT/etc/systemd/system/getty@tty1.service"
	printf 'olympium\n' > "$ROOT/etc/hostname"
	cat > "$ROOT/etc/hosts" <<'EOF'
127.0.0.1 localhost olympium
::1 localhost ip6-localhost ip6-loopback
EOF
	printf 'Olympium\n' > "$ROOT/etc/issue"
	: > "$ROOT/etc/machine-id"
	cp "$REPO/docs/isa-and-abi.md" "$REPO/docs/compatibility-matrix.md" \
		"$REPO/docs/firmware.md" "$REPO/docs/guest-images.md" \
		"$REPO/docs/32-bit.md" "$REPO/docs/olympium-integration.md" \
		"$REPO/docs/drivers.md" \
		"$ROOT/usr/share/doc/dev-desktop/"
	cat > "$ROOT/usr/share/doc/dev-desktop/SOURCE" <<'EOF'
First-party desktop source is under daniel.ye Source License, Version 1.0.
The full text is the LICENSE file in the git tree that built this image.

Third-party binary packages keep their own licenses. Copyright files are under
/usr/share/doc. Corresponding Ubuntu source packages for the versions in
sbom.tsv are published by Ubuntu at http://archive.ubuntu.com/ubuntu (amd64)
and http://ports.ubuntu.com/ubuntu-ports (arm64). This image does not relicense
those packages.

No macOS, iOS, Windows, or Google Play image is included. Olympium source is
not vendored. The integration pin is commit
25a592cfd65a4f07449ca03e919bfa97626bb164 of the separate Olympium repository.
EOF
	chroot "$ROOT" dpkg-query -W -f '${Package}\t${Version}\t${Architecture}\n' \
		> "$ROOT/usr/share/doc/dev-desktop/sbom.tsv"
	cp "$ROOT/usr/share/doc/dev-desktop/sbom.tsv" "$WORK-sbom.tsv"
	rm -f "$ROOT/var/cache/apt/archives/"*.deb || true
	rm -rf "$ROOT/var/lib/apt/lists/"* || true
}

pack_iso() {
	# A previous arm64 grub-mkrescue leaves the ISO and a squashfs copy here.
	# ISO9660 cannot hold a file over 4 GiB, so those copies must not be packed.
	rm -rf "$ROOT/tmp/iso" "$ROOT/tmp/desktop" "$ROOT/tmp/dev-live-arm64.iso"
	umount_chroot
	rm -rf "$WORK"
	mkdir -p "$WORK/boot/grub" "$WORK/live"
	local kimg initrd
	kimg=$(echo "$ROOT"/boot/vmlinuz-*)
	initrd=$(echo "$ROOT"/boot/initrd.img-*)
	cp -L "$kimg" "$WORK/boot/vmlinuz"
	cp -L "$initrd" "$WORK/boot/initrd.img"
	if [ -f "$REPO/desktop/splash.png" ]; then
		cp "$REPO/desktop/splash.png" "$WORK/boot/grub/splash.png"
	fi
	mksquashfs "$ROOT" "$WORK/live/filesystem.squashfs" -comp xz -b 1M -noappend \
		-e var/cache/apt/archives var/lib/apt/lists
	cat > "$WORK/boot/grub/grub.cfg" <<'EOF'
set timeout=4
set default=0
if loadfont unicode ; then
	set gfxmode=1280x800,auto
	terminal_output gfxterm
fi
insmod png
background_image /boot/grub/splash.png
set menu_color_normal=white/black
set menu_color_highlight=black/light-gray
menuentry "Olympium" {
	linux /boot/vmlinuz boot=live quiet console=ttyS0,115200 console=tty0
	initrd /boot/initrd.img
}
menuentry "Olympium, contrast" {
	linux /boot/vmlinuz boot=live quiet console=ttyS0,115200 console=tty0 olympium.contrast=1
	initrd /boot/initrd.img
}
EOF
	local out="$REPO/build/dev-live-${ARCH}.iso"
	mkdir -p "$REPO/build"
	if [ "$ARCH" = amd64 ]; then
		grub-mkrescue -o "$out" "$WORK" -- -volid OLYMPIUM
	else
		# grub-mkrescue inside the arm64 chroot emits an arm64 EFI image.
		mount_chroot
		rm -rf "$ROOT/tmp/iso"
		mkdir -p "$ROOT/tmp/iso"
		cp -a "$WORK/." "$ROOT/tmp/iso/"
		chroot "$ROOT" grub-mkrescue -o /tmp/dev-live-arm64.iso /tmp/iso -- -volid OLYMPIUM
		cp "$ROOT/tmp/dev-live-arm64.iso" "$out"
		umount_chroot
	fi
	sha256sum "$out" | tee "$REPO/build/dev-live-${ARCH}.iso.sha256"
	ls -lh "$out" "$WORK/live/filesystem.squashfs"
}

bootstrap
if [ ! -e "$ROOT/usr/bin/foot" ]; then
	install_packages
fi
install_drivers
verify_drivers
mkdir -p "$WORK"
stage_desktop
if [ "$ARCH" = arm64 ]; then
	# Rebuild the desktop against the arm64 sysroot. The host binaries are amd64.
	mount_chroot
	cp -a "$REPO/desktop" "$ROOT/tmp/desktop"
	chroot "$ROOT" make -C /tmp/desktop clean all
	install -m 755 "$ROOT/tmp/desktop/dev-compositor" "$ROOT/tmp/desktop/dev-files" \
		"$ROOT/tmp/desktop/dev-settings" "$ROOT/tmp/desktop/dev-swatch" \
		"$ROOT/tmp/desktop/dev-compat" "$ROOT/usr/bin/"
	file "$ROOT/usr/bin/dev-compositor"
fi
pack_iso
echo "built $REPO/build/dev-live-${ARCH}.iso"
