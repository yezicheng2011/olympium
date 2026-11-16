#!/bin/bash
# Refresh an existing rootfs with dev-install and the Desk Note package,
# then pack a new ISO. Does not remove older artifact trees.
set -euo pipefail
ARCH=${1:?amd64|arm64|i386}
REPO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=/var/tmp/dev-live/rootfs-$ARCH
WORK=/var/tmp/dev-live/iso-$ARCH-install
OUT=$REPO/build/dev-live-$ARCH-install.iso
if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
if [ ! -d "$ROOT/usr/bin" ]; then
	echo "missing $ROOT" >&2
	exit 1
fi

mount_chroot() {
	mkdir -p "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" "$ROOT/dev/pts"
	mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
	mountpoint -q "$ROOT/sys" || mount -t sysfs sysfs "$ROOT/sys"
	mountpoint -q "$ROOT/dev" || mount --bind /dev "$ROOT/dev"
	mountpoint -q "$ROOT/dev/pts" || mount --bind /dev/pts "$ROOT/dev/pts"
}
umount_chroot() {
	umount "$ROOT/dev/pts" 2>/dev/null || true
	umount "$ROOT/dev" 2>/dev/null || true
	umount "$ROOT/proc" 2>/dev/null || true
	umount "$ROOT/sys" 2>/dev/null || true
}

install -d "$ROOT/usr/bin" "$ROOT/usr/libexec/dev-desktop" "$ROOT/usr/share/olympium/samples"
install -m 755 "$REPO/image/run-session" "$REPO/image/startup.sh" \
	"$REPO/image/install-sample.sh" "$REPO/image/olympium-mac-run" \
	"$ROOT/usr/libexec/dev-desktop/"
# dev-install unpacks Mac disk images and packages with 7-Zip and cpio, and
# an .rpm with rpm and rpm2cpio. A root made before those were needed gets
# them added. Nothing else is downloaded.
if { [ ! -x "$ROOT/usr/bin/7z" ] && [ ! -x "$ROOT/usr/bin/7zz" ]; } || [ ! -x "$ROOT/usr/bin/rpm2cpio" ]; then
	mount_chroot
	chroot "$ROOT" apt-get update
	DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
		7zip cpio librsvg2-common rpm rpm2cpio
	umount_chroot
fi
# Mac samples for the boot test: a disk image and an installer package that
# desktop/mac-fixtures.py writes byte by byte. Nothing in them is Apple's.
macfx=$(mktemp -d)
python3 "$REPO/desktop/mac-fixtures.py" "$macfx" >/dev/null
install -m 644 "$macfx/DeskMac.dmg" "$macfx/DeskMac.pkg" "$ROOT/usr/share/olympium/samples/"
# With a staged Darling, one more: a disk image around a real Mach-O program,
# Darling's own build of echo, so the boot test can start a Mac program.
DARLING_STAGE=${DARLING_OUT:-/var/tmp/darling-out}
rm -f "$ROOT/usr/share/olympium/samples/DeskEcho.dmg"
if [ "${OLYMPIUM_DARLING:-0}" = 1 ] && [ "$ARCH" = amd64 ] \
		&& [ -f "$DARLING_STAGE/usr/local/libexec/darling/bin/echo" ]; then
	python3 "$REPO/desktop/mac-fixtures.py" --wrap \
		"$DARLING_STAGE/usr/local/libexec/darling/bin/echo" "Desk Echo" "$macfx/DeskEcho.dmg"
	install -m 644 "$macfx/DeskEcho.dmg" "$ROOT/usr/share/olympium/samples/"
fi
rm -rf "$macfx"
install -m 644 "$REPO/image/hello.note" "$ROOT/usr/share/olympium/samples/hello.note"
install -m 644 "$REPO/image/hello.note" "$ROOT/usr/share/olympium/samples/hello.dnote"
rm -rf "$ROOT/tmp/desktop"
mkdir -p "$ROOT/tmp/desktop"
cp -a "$REPO/desktop/." "$ROOT/tmp/desktop/"

if [ "$ARCH" = amd64 ]; then
	make -C "$REPO/desktop" clip-helper dev-compositor dev-files dev-settings dev-swatch dev-compat dev-install olympium-run olympium-testapp
	install -m 755 "$REPO/desktop/dev-compositor" "$REPO/desktop/dev-files" \
		"$REPO/desktop/dev-settings" "$REPO/desktop/dev-swatch" \
		"$REPO/desktop/dev-compat" "$REPO/desktop/dev-install" "$ROOT/usr/bin/"
	if [ -x "$REPO/desktop/dev-clip-helper" ]; then
		install -m 755 "$REPO/desktop/dev-clip-helper" "$ROOT/usr/libexec/dev-desktop/"
	fi
	install -m 755 "$REPO/desktop/olympium-run" "$REPO/desktop/olympium-testapp" "$ROOT/usr/bin/"
	install -m 755 "$REPO/desktop/olympium-run" "$ROOT/usr/libexec/dev-desktop/olympium-run"
	sh "$REPO/image/build-sample-deb.sh" "$ROOT" "$REPO/desktop"
	if [ -x /usr/bin/x86_64-w64-mingw32-gcc ]; then
		x86_64-w64-mingw32-gcc -mwindows -O2 -o "$ROOT/usr/share/olympium/samples/desk-win.exe" \
			"$REPO/desktop/desk-win.c"
	fi
	if [ -x /usr/bin/i686-w64-mingw32-gcc ]; then
		i686-w64-mingw32-gcc -mwindows -O2 -o "$ROOT/usr/share/olympium/samples/desk-win32.exe" \
			"$REPO/desktop/desk-win.c"
	fi
	if [ -x /usr/bin/rpmbuild ]; then
		top=$(mktemp -d)
		mkdir -p "$top/SOURCES" "$top/SPECS" "$top/BUILD" "$top/RPMS" "$top/BUILDROOT"
		cp "$REPO/image/desk-rpm" "$top/SOURCES/desk-rpm"
		cp "$REPO/image/desk-rpm.spec" "$top/SPECS/desk-rpm.spec"
		rpmbuild -bb --define "_topdir $top" "$top/SPECS/desk-rpm.spec" >/tmp/rpmbuild-image.log
		install -m 644 "$top"/RPMS/noarch/desk-rpm-1.0-1.noarch.rpm \
			"$ROOT/usr/share/olympium/samples/desk-rpm.rpm"
		rm -rf "$top"
	fi
else
	mount_chroot
	chroot "$ROOT" make -C /tmp/desktop clean clip-helper dev-compositor dev-files dev-settings dev-swatch dev-compat dev-install olympium-run olympium-testapp
	install -m 755 "$ROOT/tmp/desktop/dev-compositor" "$ROOT/tmp/desktop/dev-files" \
		"$ROOT/tmp/desktop/dev-settings" "$ROOT/tmp/desktop/dev-swatch" \
		"$ROOT/tmp/desktop/dev-compat" "$ROOT/tmp/desktop/dev-install" "$ROOT/usr/bin/"
	if [ -x "$ROOT/tmp/desktop/dev-clip-helper" ]; then
		install -m 755 "$ROOT/tmp/desktop/dev-clip-helper" "$ROOT/usr/libexec/dev-desktop/"
	fi
	install -m 755 "$ROOT/tmp/desktop/olympium-run" "$ROOT/tmp/desktop/olympium-testapp" "$ROOT/usr/bin/"
	install -m 755 "$ROOT/tmp/desktop/olympium-run" "$ROOT/usr/libexec/dev-desktop/olympium-run"
	cp "$REPO/image/build-sample-deb.sh" "$ROOT/tmp/build-sample-deb.sh"
	chroot "$ROOT" sh /tmp/build-sample-deb.sh / /tmp/desktop
	umount_chroot
fi

file "$ROOT/usr/bin/dev-install"
file "$ROOT/usr/share/olympium/samples/desk-note.deb"
dpkg-deb -c "$ROOT/usr/share/olympium/samples/desk-note.deb" | head

rm -rf "$WORK"
mkdir -p "$WORK/boot/grub" "$WORK/live"
if [ "$ARCH" = i386 ]; then
	kimg=$(echo "$ROOT"/boot/vmlinuz-*-686-pae)
	initrd=$(echo "$ROOT"/boot/initrd.img-*-686-pae)
else
	kimg=$(echo "$ROOT"/boot/vmlinuz-*)
	initrd=$(echo "$ROOT"/boot/initrd.img-*)
fi
cp -L "$kimg" "$WORK/boot/vmlinuz"
cp -L "$initrd" "$WORK/boot/initrd.img"
if [ -f "$REPO/desktop/splash.png" ]; then
	cp "$REPO/desktop/splash.png" "$WORK/boot/grub/splash.png"
fi
# OLYMPIUM_WINE=1 installs the distribution's Wine for this architecture.
# Without it the root is used as it is.
if [ "${OLYMPIUM_WINE:-0}" = 1 ]; then
	mount_chroot
	bash "$REPO/image/install-wine.sh" "$ROOT" "$ARCH"
	umount_chroot
fi
# OLYMPIUM_DARLING=1 copies in a Mac runtime staged by image/build-darling.sh.
if [ "${OLYMPIUM_DARLING:-0}" = 1 ] && [ "$ARCH" = amd64 ]; then
	mount_chroot
	bash "$REPO/image/install-darling.sh" "$ROOT"
	umount_chroot
fi
# A prefix built now is one the first Windows program does not wait for.
# amd64 and i386 roots run natively on an x86_64 builder. An arm64 root
# would run wineboot under qemu-user, which was not tried.
if [ "$ARCH" != arm64 ] && [ -x "$ROOT/usr/bin/wineboot" ]; then
	mount_chroot
	bash "$REPO/image/wine-prefix.sh" "$ROOT"
	umount_chroot
fi
for mnt in proc sys dev/pts dev run; do
	if mountpoint -q "$ROOT/$mnt"; then
		umount -l "$ROOT/$mnt" || true
	fi
done
mkdir -p "$ROOT/proc" "$ROOT/sys" "$ROOT/dev" "$ROOT/run" "$ROOT/tmp"
mksquashfs "$ROOT" "$WORK/live/filesystem.squashfs" -comp gzip -b 1M -noappend \
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
mkdir -p "$REPO/build"
if [ "$ARCH" = amd64 ]; then
	grub-mkrescue -o "$OUT" "$WORK" -- -volid OLYMPIUM
else
	mount_chroot
	rm -rf "$ROOT/tmp/iso" "$ROOT/tmp/dev-live-install.iso"
	mkdir -p "$ROOT/tmp/iso"
	cp -a "$WORK/." "$ROOT/tmp/iso/"
	chroot "$ROOT" grub-mkrescue -o /tmp/dev-live-install.iso /tmp/iso -- -volid OLYMPIUM
	cp "$ROOT/tmp/dev-live-install.iso" "$OUT"
	rm -rf "$ROOT/tmp/iso" "$ROOT/tmp/dev-live-install.iso"
	umount_chroot
fi
sha256sum "$OUT" | tee "$OUT.sha256"
ls -lh "$OUT"
echo "REPACK_OK $ARCH"
