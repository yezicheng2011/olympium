#!/bin/bash
# Boots the arm64 ISO on QEMU virt + AAVMF with a virtio-gpu display and saves
# one screenshot of the desktop. On an x86_64 host this is full software
# emulation (TCG), so the boot takes several minutes.
# This is a QEMU virt machine. It is not a Raspberry Pi, an SBSA server, or Apple Silicon.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-arm64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-arm64}
EXTRA=${4:-olympium.openfiles=1}
NAME=${5:-arm64-files}
PATTERN=${6:-window mapped: Files}
MON=/tmp/qemu-arm64.sock
LOG=$OUT/style-${NAME}-serial.log
mkdir -p "$OUT"
rm -f "$MON" "$LOG" "$OUT/style-${NAME}.ppm"

qemu-system-aarch64 -machine virt -cpu max -accel tcg,thread=multi -m 3072 -smp 4 \
	-bios /usr/share/AAVMF/AAVMF_CODE.fd \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyAMA0,115200 console=tty0 $EXTRA" \
	-cdrom "$ISO" \
	-device virtio-gpu-pci,xres=1280,yres=800 \
	-device qemu-xhci -device usb-kbd -device usb-tablet \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
	-serial file:"$LOG" -monitor unix:"$MON",server,nowait \
	-display none -no-reboot &
qemu=$!

up=0
for i in $(seq 1 360); do
	if grep -a -q "$PATTERN" "$LOG" 2>/dev/null; then
		up=1
		break
	fi
	if ! kill -0 "$qemu" 2>/dev/null; then
		echo "qemu exited before the desktop started" >&2
		break
	fi
	sleep 5
done
if [ "$up" -eq 1 ]; then
	# The emulated machine needs time to start the session programs and draw.
	sleep "${SETTLE:-45}"
	python3 - "$MON" "$OUT/style-${NAME}.ppm" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(30)
time.sleep(0.3)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(4)
s.close()
PY
else
	echo "never saw '${PATTERN}' in the serial log" >&2
fi
kill "$qemu" 2>/dev/null || true
wait "$qemu" 2>/dev/null || true
if [ -s "$OUT/style-${NAME}.ppm" ]; then
	echo "shot ${NAME}"
else
	echo "missing ${NAME}" >&2
fi
grep -a -c 'virtio_gpu\|virtio-gpu' "$LOG" || true
sha256sum "$ISO"
echo ARM-SHOT-DONE
