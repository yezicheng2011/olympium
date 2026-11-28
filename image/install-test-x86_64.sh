#!/bin/bash
# Installs the x86_64 ISO onto a throwaway disk image in QEMU, then starts a
# second machine from that disk alone and saves a picture of its screen.
# The first machine has the ISO and the empty disk. The second has only the disk.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-amd64}
DISK=${DISK:-/var/tmp/olympium-install-test.img}
MON=/tmp/qemu-install.sock
LOG=$OUT/olympium-x86_64-install-serial.log
SHOT=$OUT/olympium-x86_64-installed-disk-boot.ppm
mkdir -p "$OUT"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi

rm -f "$DISK" "$LOG" "$SHOT" "$MON"
truncate -s 12G "$DISK"

echo "phase 1: live ISO installs onto the empty disk"
qemu-system-x86_64 -m 3072 -smp 2 "${KVM[@]}" \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.install=/dev/sda" \
	-drive file="$DISK",format=raw,if=ide,index=0,media=disk \
	-cdrom "$ISO" -vga std -display none \
	-serial file:"$LOG" -no-reboot &
pid=$!
result=""
for i in $(seq 1 240); do
	if grep -a -q 'install-to-disk result:' "$LOG" 2>/dev/null; then
		result=$(grep -a 'install-to-disk result:' "$LOG" | tail -1)
		break
	fi
	if ! kill -0 "$pid" 2>/dev/null; then
		break
	fi
	sleep 5
done
sleep 3
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "serial said: ${result:-nothing about the install}"
grep -a 'install-to-disk' "$LOG" | tail -8
case "$result" in
	*"result: ok"*) ;;
	*) echo "INSTALL-TEST: the install did not report success" >&2; exit 1 ;;
esac

echo "phase 2: a machine with only that disk"
qemu-system-x86_64 -m 2048 -smp 2 "${KVM[@]}" \
	-drive file="$DISK",format=raw,if=ide,index=0,media=disk -boot c \
	-vga std -display none \
	-monitor unix:"$MON",server,nowait -no-reboot &
pid=$!
sleep "${SETTLE:-80}"
python3 - "$MON" "$SHOT" <<'PY'
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
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
chmod 666 "$SHOT" "$LOG" 2>/dev/null || true
if [ ! -s "$SHOT" ]; then
	echo "INSTALL-TEST: no picture from the disk boot" >&2
	exit 1
fi
# The partition table and file system the installer left on the disk.
sfdisk -l "$DISK" 2>/dev/null | tail -4 || true
echo INSTALL-TEST-DONE
