#!/bin/bash
# Boots the x86_64 ISO once per view on QEMU pc + KVM and saves a VGA screenshot of each.
# Output: style-<view>.ppm and style-<view>-serial.log in the output directory.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-amd64}
MON=/tmp/qemu-style.sock
mkdir -p "$OUT"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi

shot() {
	python3 - "$MON" "$1" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(15)
time.sleep(0.15)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(1.1)
s.close()
PY
	chmod 666 "$1" 2>/dev/null || true
}

# boot NAME "EXTRA KERNEL FLAGS" "SERIAL LINE TO WAIT FOR" SETTLE_SECONDS
boot() {
	local name=$1 extra=$2 pattern=$3 settle=$4
	local log=$OUT/style-${name}-serial.log
	rm -f "$MON" "$log" "$OUT/style-${name}.ppm"
	qemu-system-x86_64 -m 2048 -smp 2 "${KVM[@]}" \
		-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
		-append "boot=live console=ttyS0,115200 console=tty0 $extra" \
		-cdrom "$ISO" -vga std -display none \
		-netdev user,id=n0 -device e1000,netdev=n0 \
		-serial file:"$log" \
		-monitor unix:"$MON",server,nowait \
		-usb -device usb-tablet -no-reboot &
	local pid=$!
	local seen=0
	for i in $(seq 1 90); do
		if grep -a -q "$pattern" "$log" 2>/dev/null; then
			seen=1
			break
		fi
		if ! kill -0 "$pid" 2>/dev/null; then
			break
		fi
		sleep 2
	done
	if [ "$seen" -eq 1 ]; then
		sleep "$settle"
		shot "$OUT/style-${name}.ppm"
	else
		echo "never saw '${pattern}' for ${name}" >&2
	fi
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
	if [ -s "$OUT/style-${name}.ppm" ]; then
		echo "shot ${name}"
	else
		echo "missing ${name}" >&2
	fi
}

boot clean "" "compositor on WAYLAND_DISPLAY" 6
boot menu "olympium.openmenu=1" "demo: menu open" 3
boot files "olympium.openfiles=1" "window mapped: Files" 4
boot settings "olympium.opensettings=1" "window mapped: Settings" 4
boot terminal "olympium.openterm=1" "window mapped" 4
boot olympium "olympium.openruntime=1" "window mapped" 4
boot launchpad "olympium.openlaunch=1" "demo: launchpad open" 3
boot nag "olympium.nag=1" "compositor on WAYLAND_DISPLAY" 6
boot after-nag "olympium.nag=1 olympium.nagdismiss=1 olympium.openfiles=1" "reminder closed" 8
sha256sum "$ISO"
echo SHOTS-DONE
