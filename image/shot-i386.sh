#!/bin/bash
# Boots the i386 ISO in qemu-system-i386 -M pc and stores two VGA pictures.
# The machine is an emulated 32-bit PC. It is not a physical computer.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-i386.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-i386}
MON=/tmp/qemu-i386.sock
LOG=$OUT/olympium-i386-serial.log
DISK=/var/tmp/olympium-i386-disk.img
mkdir -p "$OUT"
rm -f "$DISK"
truncate -s 4G "$DISK"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi
rm -f "$MON" "$LOG"
# virtio-vga: Debian 6.1 bochs-drm does not advertise PRIME import, and
# wlroots 0.17 refuses a DRM device without it. virtio-gpu does.
echo "qemu-system-i386 -M pc -vga virtio. Kernel from the i386 image. Not a physical PC." | tee "$OUT/olympium-i386-machine.txt"
qemu-system-i386 -M pc -m 2048 -smp 2 "${KVM[@]}" \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.open64=1 olympium.opentest=1 olympium.formatdisk=1" \
	-cdrom "$ISO" \
	-drive file="$DISK",if=virtio,format=raw \
	-vga virtio -display none \
	-serial file:"$LOG" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet -no-reboot &
pid=$!

shot() {
	local dest=$1
	python3 - "$MON" "$dest" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(20)
time.sleep(0.3)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(1.5)
s.close()
PY
}

png_from() {
	local ppm=$1 png=$2
	python3 - "$ppm" "$png" <<'PY'
import sys
ppm, png = sys.argv[1], sys.argv[2]
try:
    from PIL import Image
    Image.open(ppm).save(png)
except Exception:
    import subprocess
    subprocess.check_call(["convert", ppm, png])
PY
}

desk=0
for i in $(seq 1 90); do
	if grep -a -q 'compositor on' "$LOG" 2>/dev/null; then
		desk=1
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		break
	fi
	sleep 2
done
if [ "$desk" = 1 ]; then
	sleep 6
	shot "$OUT/olympium-i386-01-desktop.ppm" || true
	png_from "$OUT/olympium-i386-01-desktop.ppm" "$OUT/olympium-i386-01-desktop.png" || true
fi
done64=0
for i in $(seq 1 300); do
	if grep -a -q 'olympium-64demo: done' "$LOG" 2>/dev/null; then
		done64=1
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		break
	fi
	sleep 2
done
if [ "$done64" = 1 ]; then
	sleep 2
	shot "$OUT/olympium-i386-02-64bit.ppm" || true
	png_from "$OUT/olympium-i386-02-64bit.ppm" "$OUT/olympium-i386-02-64bit.png" || true
fi
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "desktop=$desk demo=$done64"
grep -a -E 'olympium-run64|OLYMPIUM64_|64demo|session start|window mapped' "$LOG" | tail -80
echo I386-SHOT-DONE
