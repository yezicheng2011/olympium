#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0
# Boots the x86_64 ISO in QEMU and stores a VGA screenshot plus the serial log.
set -euo pipefail
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
mkdir -p "$OUT"
MON=/tmp/qemu-monitor.sock
rm -f "$MON" "$OUT/qemu-serial.log" "$OUT/qemu-desktop.ppm"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi
qemu-system-x86_64 \
	-m 2048 -smp 2 "${KVM[@]}" \
	-cdrom "$ISO" -boot d \
	-vga std -display none \
	-serial file:"$OUT/qemu-serial.log" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet \
	-no-reboot &
QEMU=$!
cleanup() { kill "$QEMU" 2>/dev/null || true; wait "$QEMU" 2>/dev/null || true; }
trap cleanup EXIT
for i in $(seq 1 90); do
	if grep -q 'compositor on WAYLAND_DISPLAY' "$OUT/qemu-serial.log" 2>/dev/null; then
		sleep 8
		break
	fi
	if ! kill -0 "$QEMU" 2>/dev/null; then
		echo "qemu exited early" >&2
		exit 1
	fi
	sleep 2
done
python3 - "$MON" "$OUT/qemu-desktop.ppm" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(30)
time.sleep(0.2)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(2)
try:
    print(s.recv(4096).decode(errors="replace"))
except socket.timeout:
    pass
s.close()
PY
ls -lh "$OUT/qemu-desktop.ppm" "$ISO"
sha256sum "$ISO" | tee "$OUT/dev-live-amd64.iso.sha256"
tail -30 "$OUT/qemu-serial.log"
