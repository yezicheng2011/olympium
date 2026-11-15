#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Live ISO for a 32-bit x86 machine. The kernel is Debian's 686-pae kernel.
# Userspace is Debian bookworm. wlroots 0.17 is built from source because
# bookworm's wlroots is too old for this desktop. Firefox is Debian's ESR
# rebuild, not a package from packages.mozilla.org.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=/var/tmp/dev-live/rootfs-i386
WORK=/var/tmp/dev-live/iso-i386
SRC=/var/tmp/dev-live/src
ARCH=i386

if [ ! -x "$ROOT/bin/sh" ]; then
	echo "missing $ROOT; debootstrap bookworm i386 first" >&2
	exit 1
fi

cat > "$ROOT/etc/apt/sources.list" <<'EOF'
deb http://deb.debian.org/debian bookworm main contrib non-free-firmware
deb http://deb.debian.org/debian bookworm-updates main contrib non-free-firmware
deb http://security.debian.org/debian-security bookworm-security main contrib non-free-firmware
EOF

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

ver_lt() {
	[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ] && [ "$1" != "$2" ]
}

pkg_ver() {
	chroot "$ROOT" pkg-config --modversion "$1" 2>/dev/null || echo 0
}

mount_chroot
if [ ! -x "$ROOT/usr/bin/firefox-esr" ] || [ ! -d "$ROOT/lib/modules" ]; then
	chroot "$ROOT" apt-get update
	DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
		linux-image-686-pae live-boot live-boot-initramfs-tools initramfs-tools \
		systemd systemd-sysv udev dbus kmod \
		foot firefox-esr fonts-dejavu-core fonts-noto-core fonts-noto-mono fontconfig \
		grub-pc-bin grub-efi-ia32-bin grub2-common grub-common xorriso mtools squashfs-tools \
		parted dosfstools e2fsprogs fdisk \
		libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libgtk-3-0 \
		libcairo2-dev libpango1.0-dev libgtk-3-dev \
		libwayland-dev wayland-protocols \
		libdrm-dev libinput-dev libxkbcommon-dev libpixman-1-dev \
		libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libgl1-mesa-dri \
		libseat-dev libudev-dev \
		libxcb1-dev libx11-xcb-dev libxcb-dri3-dev libxcb-present-dev \
		libxcb-render0-dev libxcb-render-util0-dev libxcb-shm0-dev \
		libxcb-xfixes0-dev libxcb-xinput-dev libxcb-composite0-dev \
		libxcb-ewmh-dev libxcb-icccm4-dev \
		meson ninja-build pkg-config build-essential hwdata \
		qemu-system-x86 ca-certificates curl python3 \
		bash coreutils util-linux pciutils usbutils \
		mesa-vulkan-drivers wireless-regdb adwaita-icon-theme file cpio sudo
fi

install -d "$ROOT/usr/share/olympium"
{
	echo "arch i386"
	echo "base debian-bookworm"
	kver=$(cd "$ROOT/lib/modules" && echo *-686-pae)
	echo "kernel $kver"
} > "$ROOT/usr/share/olympium/driver-manifest.txt"
for pkg in firmware-linux-free firmware-misc-nonfree firmware-amd-graphics \
	firmware-intel-graphics firmware-iwlwifi firmware-realtek firmware-sof-signed \
	intel-microcode amd64-microcode mesa-va-drivers; do
	if DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends "$pkg"; then
		echo "driver-ok package $pkg" | tee -a "$ROOT/usr/share/olympium/driver-manifest.txt"
	else
		echo "driver-missing package $pkg" | tee -a "$ROOT/usr/share/olympium/driver-manifest.txt"
	fi
done
cfg=$(echo "$ROOT"/boot/config-*-686-pae)
if ! grep -q '^CONFIG_X86_32=y' "$cfg" || grep -q '^CONFIG_64BIT=y' "$cfg"; then
	echo "the installed kernel is not a 32-bit x86 kernel" >&2
	exit 1
fi
echo "kernel-config $cfg CONFIG_X86_32=y" | tee -a "$ROOT/usr/share/olympium/driver-manifest.txt"

if ! chroot "$ROOT" sh -c 'ls /usr/share/fonts/truetype/ubuntu/Ubuntu*.ttf >/dev/null 2>&1'; then
	cp "$SRC/fonts-ubuntu.deb" "$ROOT/tmp/fonts-ubuntu.deb"
	chroot "$ROOT" dpkg -i /tmp/fonts-ubuntu.deb
	rm -f "$ROOT/tmp/fonts-ubuntu.deb"
fi

DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
	libffi-dev libxml2-dev libexpat1-dev meson ninja-build pkg-config build-essential hwdata

build_tree() {
	local archive=$1 dir=$2
	shift 2
	rm -rf "$ROOT/tmp/depbuild"
	mkdir -p "$ROOT/tmp/depbuild"
	tar -xzf "$archive" -C "$ROOT/tmp/depbuild"
	chroot "$ROOT" meson setup "/tmp/depbuild/$dir/build" "/tmp/depbuild/$dir" --prefix=/usr "$@"
	chroot "$ROOT" ninja -C "/tmp/depbuild/$dir/build"
	chroot "$ROOT" ninja -C "/tmp/depbuild/$dir/build" install
	rm -rf "$ROOT/tmp/depbuild"
}

if ver_lt "$(pkg_ver wayland-server)" 1.22; then
	build_tree "$SRC/wayland-1.22.0.tar.gz" wayland-1.22.0 -Ddocumentation=false -Dtests=false -Ddtd_validation=false
fi
if ver_lt "$(pkg_ver wayland-protocols)" 1.32; then
	build_tree "$SRC/wayland-protocols-1.33.tar.gz" wayland-protocols-1.33 -Dtests=false
fi
if ver_lt "$(pkg_ver pixman-1)" 0.42.0; then
	if [ ! -f "$SRC/pixman-0.43.4.tar.gz" ]; then
		curl -fsSL -o "$SRC/pixman-0.43.4.tar.gz" \
			https://www.cairographics.org/releases/pixman-0.43.4.tar.gz
	fi
	build_tree "$SRC/pixman-0.43.4.tar.gz" pixman-0.43.4 -Dtests=disabled
fi
if ! chroot "$ROOT" pkg-config --exists libdisplay-info; then
	build_tree "$SRC/libdisplay-info-0.1.1.tar.gz" libdisplay-info-0.1.1
fi
if ver_lt "$(pkg_ver wlroots)" 0.17.0; then
	build_tree "$SRC/wlroots-0.17.1.tar.gz" wlroots-0.17.1 \
		-Dexamples=false -Dxwayland=disabled -Drenderers=gles2 -Dbackends=drm,libinput
fi
echo "wlroots $(pkg_ver wlroots)"

rm -rf "$ROOT/tmp/desktop"
cp -a "$REPO/desktop" "$ROOT/tmp/desktop"
chroot "$ROOT" make -C /tmp/desktop clean all
file "$ROOT/tmp/desktop/dev-compositor" | grep -q '80386'
install -d "$ROOT/usr/bin" "$ROOT/usr/libexec/dev-desktop" \
	"$ROOT/etc/dev-desktop" "$ROOT/etc/systemd/system/multi-user.target.wants" \
	"$ROOT/usr/share/doc/dev-desktop" "$ROOT/etc/xdg/foot"
install -m 755 "$ROOT/tmp/desktop/dev-compositor" "$ROOT/tmp/desktop/dev-files" \
	"$ROOT/tmp/desktop/dev-settings" "$ROOT/tmp/desktop/dev-swatch" \
	"$ROOT/tmp/desktop/dev-compat" "$ROOT/usr/bin/"
install -m 755 "$REPO/image/run-session" "$REPO/image/startup.sh" \
	"$REPO/image/olympium-launch" "$REPO/image/install-to-disk" \
	"$REPO/image/guest-fetch.sh" "$REPO/image/driver-report" "$REPO/image/driver-install" \
	"$REPO/image/firefox-launch" "$ROOT/usr/libexec/dev-desktop/"
install -m 755 "$REPO/image/driver-install" "$ROOT/usr/bin/driver-install"
install -m 755 "$REPO/image/driver-report" "$ROOT/usr/bin/driver-report"
install -m 755 "$REPO/image/olympium-activate" "$ROOT/usr/bin/olympium-activate"
install -m 755 "$REPO/image/install-to-disk" "$ROOT/usr/bin/install-to-disk"
install -m 644 "$REPO/image/foot.ini" "$ROOT/etc/xdg/foot/foot.ini"
install -m 644 "$REPO/image/browser-home.html" "$ROOT/usr/share/olympium/browser-home.html"
install -m 644 "$REPO/image/guest-pins.txt" "$ROOT/usr/share/olympium/guest-pins.txt"
install -D -m 644 "$REPO/image/99-olympium-ui.conf" "$ROOT/etc/fonts/conf.d/99-olympium-ui.conf"
install -D -m 644 "$REPO/image/gtk-3.0-settings.ini" "$ROOT/etc/gtk-3.0/settings.ini"
install -D -m 644 "$REPO/image/gtk-4.0-settings.ini" "$ROOT/etc/gtk-4.0/settings.ini"
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
printf 'kernel.unprivileged_userns_clone = 1\n' > "$ROOT/etc/sysctl.d/60-olympium-userns.conf"

if ! chroot "$ROOT" id -u olympium >/dev/null 2>&1; then
	chroot "$ROOT" useradd -m -s /bin/bash -u 1000 olympium
fi
uid=$(chroot "$ROOT" id -u olympium)
gid=$(chroot "$ROOT" id -g olympium)
install -d "$ROOT/home/olympium/.mozilla/firefox/olympium.default" \
	"$ROOT/home/olympium/.config/gtk-3.0"
cat > "$ROOT/home/olympium/.mozilla/firefox/profiles.ini" <<'EOF'
[Profile0]
Name=olympium
IsRelative=1
Path=olympium.default
Default=1

[General]
StartWithLastProfile=1
Version=2
EOF
install -m 644 "$REPO/image/firefox-user.js" \
	"$ROOT/home/olympium/.mozilla/firefox/olympium.default/user.js"
install -d "$ROOT/etc/firefox-esr/policies" "$ROOT/usr/lib/firefox-esr/distribution" \
	"$ROOT/etc/firefox/policies"
install -m 644 "$REPO/image/firefox-policies.json" "$ROOT/etc/firefox-esr/policies/policies.json"
install -m 644 "$REPO/image/firefox-policies.json" "$ROOT/etc/firefox/policies/policies.json"
if [ -d "$ROOT/usr/lib/firefox-esr/distribution" ]; then
	install -m 644 "$REPO/image/firefox-policies.json" \
		"$ROOT/usr/lib/firefox-esr/distribution/policies.json"
fi
install -m 644 "$ROOT/etc/gtk-3.0/settings.ini" "$ROOT/home/olympium/.config/gtk-3.0/settings.ini"
chown -R "$uid:$gid" "$ROOT/home/olympium"
ffver=$(chroot "$ROOT" dpkg-query -W -f '${Version}' firefox-esr)
printf 'Firefox ESR %s (Debian i386 rebuild)\n' "$ffver" > "$ROOT/usr/share/olympium/firefox-version.txt"
cat > "$ROOT/etc/xdg/mimeapps.list" <<'EOF'
[Default Applications]
text/html=firefox-esr.desktop
application/xhtml+xml=firefox-esr.desktop
x-scheme-handler/http=firefox-esr.desktop
x-scheme-handler/https=firefox-esr.desktop
EOF
if [ -x "$ROOT/usr/bin/firefox-esr" ]; then
	chroot "$ROOT" update-alternatives --install /usr/bin/x-www-browser x-www-browser /usr/bin/firefox-esr 200 || true
fi
shown=$(chroot "$ROOT" runuser -u olympium -- env MOZ_DISABLE_CONTENT_SANDBOX=1 \
	firefox-esr --version 2>&1 || true)
echo "firefox-esr: $ffver"
printf '%s\n' "$shown" | head -5
printf '%s\n' "$shown" | grep -q 'Mozilla Firefox'
if ! file "$ROOT/usr/lib/firefox-esr/firefox-esr" | grep -q '80386'; then
	echo "firefox-esr is not a 32-bit binary" >&2
	exit 1
fi

# shellcheck source=image/stage-bitness.sh
. "$REPO/image/stage-bitness.sh"
DESKTOP_BINS=$ROOT/tmp/desktop
stage_bitness

# Package installs were allowed to skip initramfs regeneration. Build it once.
DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends zstd || true
if [ -x "$ROOT/usr/sbin/update-initramfs.real" ]; then
	rm -f "$ROOT/usr/sbin/update-initramfs"
	chroot "$ROOT" dpkg-divert --local --rename --remove /usr/sbin/update-initramfs
fi
chroot "$ROOT" update-initramfs -u
chroot "$ROOT" dpkg-query -W -f '${Package}\t${Version}\t${Architecture}\n' \
	> "$ROOT/usr/share/doc/dev-desktop/sbom.tsv"
rm -rf "$ROOT/tmp/iso" "$ROOT/tmp/desktop" "$ROOT/tmp/depbuild" "$ROOT/var/cache/apt/archives/"*.deb
rm -rf "$ROOT/var/lib/apt/lists/"*
umount_chroot
rm -rf "$WORK"
mkdir -p "$WORK/boot/grub" "$WORK/live"
kimg=$(echo "$ROOT"/boot/vmlinuz-*-686-pae)
initrd=$(echo "$ROOT"/boot/initrd.img-*-686-pae)
cp -L "$kimg" "$WORK/boot/vmlinuz"
cp -L "$initrd" "$WORK/boot/initrd.img"
if [ -f "$REPO/desktop/splash.png" ]; then
	cp "$REPO/desktop/splash.png" "$WORK/boot/grub/splash.png"
fi
mksquashfs "$ROOT" "$WORK/live/filesystem.squashfs" -comp xz -b 1M -noappend \
	-e var/cache/apt/archives var/lib/apt/lists tmp
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
out="$REPO/build/dev-live-i386.iso"
mkdir -p "$REPO/build"
mount_chroot
rm -rf "$ROOT/tmp/iso"
mkdir -p "$ROOT/tmp/iso"
cp -a "$WORK/." "$ROOT/tmp/iso/"
chroot "$ROOT" grub-mkrescue -o /tmp/dev-live-i386.iso /tmp/iso -- -volid OLYMPIUM
cp "$ROOT/tmp/dev-live-i386.iso" "$out"
rm -rf "$ROOT/tmp/iso" "$ROOT/tmp/dev-live-i386.iso"
umount_chroot
sha256sum "$out" | tee "$REPO/build/dev-live-i386.iso.sha256"
ls -lh "$out"
echo "built $out"
