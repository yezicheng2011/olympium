#!/bin/bash
# Boots the amd64 ISO and opens the native 32-bit sample plus the lawful test app.
# qemu-system-x86_64. Not a statement about any other machine.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-amd64}
MON=/tmp/qemu-hello32.sock
NAME=olympium-x86_64-13-hello32-testapp
LOG=$OUT/${NAME}-serial.log
PPM=$OUT/${NAME}.ppm
mkdir -p "$OUT"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi
rm -f "$MON" "$LOG" "$PPM"
qemu-system-x86_64 -m 3072 -smp 2 "${KVM[@]}" \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.open32=1 olympium.opentest=1" \
	-cdrom "$ISO" -vga std -display none \
	-netdev user,id=n0 -device e1000,netdev=n0 \
	-serial file:"$LOG" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet -no-reboot &
pid=$!
seen=0
for i in $(seq 1 90); do
	if grep -a -E -q 'window mapped:.*(32-bit Hello|Lawful test app|Test App)' "$LOG" 2>/dev/null; then
		seen=1
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		break
	fi
	sleep 2
done
if [ "$seen" = 1 ]; then
	sleep 8
	python3 - "$MON" "$PPM" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(15)
time.sleep(0.2)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(1.5)
s.close()
PY
fi
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if [ ! -s "$PPM" ]; then
	echo "HELLO32-SHOT: no picture" >&2
	grep -a -E 'window mapped|olympium-run|compositor on' "$LOG" | tail -30 >&2 || true
	exit 1
fi
python3 - "$PPM" "$OUT/${NAME}.png" <<'PY'
import sys
ppm, png = sys.argv[1], sys.argv[2]
try:
    from PIL import Image
    Image.open(ppm).save(png)
except Exception:
    import subprocess
    subprocess.check_call(["convert", ppm, png])
PY
echo "shot $OUT/${NAME}.png"
grep -a -E 'window mapped|olympium-run|hello32|compositor on' "$LOG" | tail -40
echo HELLO32-SHOT-DONE
