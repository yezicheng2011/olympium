#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
# Boots the x86_64 ISO in QEMU and stores two VGA screenshots plus the serial log.
set -euo pipefail
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-amd64}
mkdir -p "$OUT"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi

shot() {
	local mon=$1 dest=$2
	python3 - "$mon" "$dest" <<'PY'
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
try:
    s.recv(4096)
except socket.timeout:
    pass
s.close()
PY
}

wait_desktop() {
	local log=$1 qemu=$2
	for i in $(seq 1 60); do
		if grep -a -E 'compositor on WAYLAND_DISPLAY|olympium login:' "$log" >/dev/null 2>&1; then
			return 0
		fi
		if ! kill -0 "$qemu" 2>/dev/null; then
			echo "qemu exited early" >&2
			return 1
		fi
		sleep 2
	done
	return 0
}

boot_one() {
	local name=$1
	local extra=$2
	shift 2
	local mon=/tmp/qemu-monitor.sock
	local log=$OUT/${name}-serial.log
	rm -f "$mon" "$log" "$OUT/${name}.ppm"
	qemu-system-x86_64 -m 2048 -smp 2 "${KVM[@]}" "$@" \
		-vga std -display none \
		-serial file:"$log" \
		-monitor unix:"$mon",server,nowait \
		-usb -device usb-tablet -no-reboot &
	local qemu=$!
	wait_desktop "$log" "$qemu" || true
	sleep "$extra"
	shot "$mon" "$OUT/${name}.ppm" || true
	kill "$qemu" 2>/dev/null || true
	wait "$qemu" 2>/dev/null || true
	ls -lh "$OUT/${name}.ppm" || echo "missing $name screenshot"
}

if [ -f "$STAGE/boot/vmlinuz" ] && [ -f "$STAGE/boot/initrd.img" ]; then
	# One boot. The compositor opens the File menu after a few seconds.
	# Files and the Olympium launcher follow on the same command line.
	mon=/tmp/qemu-monitor.sock
	log=$OUT/olympium-boot-serial.log
	rm -f "$mon" "$log"
	qemu-system-x86_64 -m 2048 -smp 2 "${KVM[@]}" \
		-kernel "$STAGE/boot/vmlinuz" \
		-initrd "$STAGE/boot/initrd.img" \
		-append "boot=live console=ttyS0,115200 console=tty0 olympium.openmenu=1 olympium.openfiles=1 olympium.openruntime=1" \
		-cdrom "$ISO" \
		-vga std -display none \
		-serial file:"$log" \
		-monitor unix:"$mon",server,nowait \
		-usb -device usb-tablet -no-reboot &
	qemu=$!
	wait_desktop "$log" "$qemu" || true
	sleep 3
	shot "$mon" "$OUT/olympium-desktop-clean.ppm" || true
	sleep 6
	shot "$mon" "$OUT/olympium-desktop-menu.ppm" || true
	sleep 8
	shot "$mon" "$OUT/olympium-desktop-files.ppm" || true
	sleep 10
	shot "$mon" "$OUT/olympium-desktop-olympium.ppm" || true
	kill "$qemu" 2>/dev/null || true
	wait "$qemu" 2>/dev/null || true
else
	echo "no staged kernel" >&2
	exit 1
fi
sha256sum "$ISO" | tee "$OUT/dev-live-amd64.iso.sha256"
file "$OUT"/olympium-desktop-*.ppm || true
