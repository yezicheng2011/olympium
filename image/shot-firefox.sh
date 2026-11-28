#!/bin/bash
# One live boot of the x86_64 ISO that opens Settings and Firefox.
# The picture is a new file. Earlier screenshots are left where they are.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-amd64}
MON=/tmp/qemu-firefox.sock
NAME=olympium-x86_64-11-firefox-ubuntu-font
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
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.openbrowser=1" \
	-cdrom "$ISO" -vga std -display none \
	-netdev user,id=n0 -device e1000,netdev=n0 \
	-serial file:"$LOG" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet -no-reboot &
pid=$!
seen=0
for i in $(seq 1 90); do
	if grep -a -E -q 'window mapped:.*(Firefox|firefox)|app firefox' "$LOG" 2>/dev/null; then
		seen=1
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		break
	fi
	sleep 2
done
if [ "$seen" -eq 1 ]; then
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
	chmod 666 "$PPM" "$LOG" 2>/dev/null || true
fi
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if [ ! -s "$PPM" ]; then
	echo "FIREFOX-SHOT: no picture" >&2
	grep -a -E 'firefox|window mapped|compositor on' "$LOG" | tail -30 >&2 || true
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
grep -a -E 'firefox|window mapped|System font|compositor on' "$LOG" | tail -40
echo FIREFOX-SHOT-DONE
