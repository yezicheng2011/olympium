#!/bin/bash
# Boots the install ISO and records the review window, the launched apps,
# and Launchpad. qemu-system-x86_64 on this host. Not a physical PC.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64-install.iso}
OUT=${2:-/opt/cursor/artifacts/with-install/wine}
STAGE=${3:-/var/tmp/dev-live/iso-amd64-install}
MON=/tmp/qemu-wine.sock
LOG=$OUT/wine-serial.log
mkdir -p "$OUT"
# Without /dev/kvm QEMU emulates every instruction. The boot still works, and
# everything in it takes many times longer, so the waits here and the waits
# inside the image (olympium.waitscale) are stretched by the same factor.
# OLYMPIUM_BOOT_FLAGS adds kernel flags: olympium.winefresh=1 also times a
# prefix Wine has to build, olympium.importmac=1 installs the Mac samples.
# The emulated CPU is QEMU's fullest one with SMAP taken out. QEMU 8.2 reads
# the stack as the kernel would when a program runs IRETQ in user mode, which
# Wine does on every return from a system call it handles. With SMAP on, the
# guest kernel takes that for its own fault and kills the Wine process
# ("supervisor read access in user mode"). That is the emulator, not Wine and
# not this image: a real CPU and KVM do not do it.
KVM=(-cpu max,smap=off)
SCALE=${OLYMPIUM_WAITSCALE:-4}
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
	SCALE=${OLYMPIUM_WAITSCALE:-1}
fi
FLAGS=${OLYMPIUM_BOOT_FLAGS:-}
rm -f "$MON" "$LOG"
qemu-system-x86_64 -m "${OLYMPIUM_VM_MB:-3072}" -smp "${OLYMPIUM_VM_CPUS:-2}" "${KVM[@]}" \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.showwine=1 olympium.importwine=1 olympium.waitscale=$SCALE $FLAGS" \
	-cdrom "$ISO" -vga std -display none \
	-serial file:"$LOG" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet -device usb-kbd -no-reboot &
pid=$!

dump() {
	local ppm=$1
	python3 - "$MON" "$ppm" <<'PY'
import socket, sys, time
mon, dest = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
s.settimeout(20)
time.sleep(0.2)
try:
    s.recv(4096)
except socket.timeout:
    pass
s.sendall(("screendump %s\n" % dest).encode())
time.sleep(1.2)
s.close()
PY
}

png_from() {
	local ppm=$1 png=$2
	python3 - "$ppm" "$png" <<'PY'
import struct, sys, zlib
ppm, png = sys.argv[1], sys.argv[2]
data = open(ppm, "rb").read()
if not data.startswith(b"P6"):
    raise SystemExit("not a P6 ppm")
header, raw = data.split(b"\n", 3)[0], None
# Skip comments and read width, height, maxval.
parts = []
i = 0
lines = data.split(b"\n")
tokens = []
for line in lines:
    if line.startswith(b"#") or line == b"P6":
        continue
    tokens.extend(line.split())
    if len(tokens) >= 3:
        break
w, h, maxv = int(tokens[0]), int(tokens[1]), int(tokens[2])
marker = b"\n%d %d\n%d\n" % (w, h, maxv)
# Find the pixel start after the maxval line.
idx = data.find(b"\n" + str(maxv).encode() + b"\n")
raw = data[idx + len(str(maxv)) + 2:]
if len(raw) < w * h * 3:
    raise SystemExit("short ppm %d" % len(raw))
raw = raw[:w * h * 3]

def chunk(tag, payload):
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
rows = b"".join(b"\x00" + raw[y * w * 3:(y + 1) * w * 3] for y in range(h))
out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b"")
open(png, "wb").write(out)
print("wrote", png, w, h)
PY
	rm -f "$ppm"
}

wait_log() {
	local pat=$1 tries=$(($2 * SCALE))
	for _ in $(seq 1 "$tries"); do
		if grep -a -E -q "$pat" "$LOG" 2>/dev/null; then
			return 0
		fi
		if ! kill -0 "$pid" 2>/dev/null; then
			return 1
		fi
		sleep 2
	done
	return 1
}

if wait_log 'window mapped: Install' 50; then
	sleep 2
	dump "$OUT/wine-review.ppm" || true
	png_from "$OUT/wine-review.ppm" "$OUT/wine-review.png" || true
fi
if wait_log 'window mapped: Desk Win' 80; then
	sleep 3
	dump "$OUT/wine-window.ppm" || true
	png_from "$OUT/wine-window.ppm" "$OUT/wine-window.png" || true
fi
if wait_log 'wine_cold_ms' 30; then
	sleep 2
	dump "$OUT/wine-marker.ppm" || true
	png_from "$OUT/wine-marker.ppm" "$OUT/wine-marker.png" || true
fi
case " $FLAGS " in
*" olympium.winefresh=1 "*)
	wait_log 'wine_fresh_request_to_window_ms' 120 || true
	;;
esac
case " $FLAGS " in
*" olympium.importmac=1 "*)
	if wait_log 'mac_program_said|mac refusal window mapped|mac runtime none' 90; then
		sleep 3
		dump "$OUT/mac.ppm" || true
		png_from "$OUT/mac.ppm" "$OUT/mac.png" || true
	fi
	;;
esac
if wait_log 'launchpad entry:' 30; then
	sleep 2
	dump "$OUT/wine-launchpad.ppm" || true
	png_from "$OUT/wine-launchpad.ppm" "$OUT/wine-launchpad.png" || true
fi
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo '--- marks ---'
if [ ! -e /dev/kvm ]; then
	echo "no /dev/kvm: this boot was emulated, and its times are not a real machine's"
fi
grep -a -E 'xwayland on|clipboard helper|wine install|wine import|wine_cold_ms|wine[a-z_]*_request_to_window_ms|wine[a-z_]*_prefix|wine[a-z_]*_server_ms|prewarm|wine_exchange|rpm_|mac |mac_|launchpad entry|dock pin|window mapped|compositor on|install-sample done' "$LOG" \
	| sed 's/^\[ *[0-9.]*\] //' | awk '!seen[$0]++' | tail -120
echo SHOT_INSTALL_DONE
ls -l "$OUT"/*.png 2>/dev/null || true
