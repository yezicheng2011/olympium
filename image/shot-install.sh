#!/bin/bash
# Boots the install ISO and records the review window, the launched apps,
# and Launchpad. qemu-system-x86_64 on this host. Not a physical PC.
set -u
if [ -e /dev/kvm ] && [ ! -w /dev/kvm ] && [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-amd64-install.iso}
OUT=${2:-/opt/cursor/artifacts/with-install}
STAGE=${3:-/var/tmp/dev-live/iso-amd64-install}
MON=/tmp/qemu-install.sock
LOG=$OUT/install-serial.log
mkdir -p "$OUT"
KVM=()
if [ -e /dev/kvm ]; then
	KVM=(-enable-kvm -cpu host)
fi
rm -f "$MON" "$LOG"
qemu-system-x86_64 -m 3072 -smp 2 "${KVM[@]}" \
	-kernel "$STAGE/boot/vmlinuz" -initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyS0,115200 console=tty0 olympium.showinstall=1 olympium.importsample=1" \
	-cdrom "$ISO" -vga std -display none \
	-serial file:"$LOG" \
	-monitor unix:"$MON",server,nowait \
	-usb -device usb-tablet -no-reboot &
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
	local pat=$1 tries=$2
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
	dump "$OUT/install-review.ppm" || true
	png_from "$OUT/install-review.ppm" "$OUT/install-review.png" || true
fi
if wait_log 'window mapped: Desk Note' 50; then
	sleep 3
	dump "$OUT/install-windows.ppm" || true
	png_from "$OUT/install-windows.ppm" "$OUT/install-windows.png" || true
fi
if wait_log 'paste clipboard:' 30; then
	sleep 2
	dump "$OUT/install-paste.ppm" || true
	png_from "$OUT/install-paste.ppm" "$OUT/install-paste.png" || true
fi
if wait_log 'launchpad entry: Desk Note' 30; then
	sleep 2
	dump "$OUT/install-launchpad.ppm" || true
	png_from "$OUT/install-launchpad.ppm" "$OUT/install-launchpad.png" || true
fi
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo '--- marks ---'
grep -a -E 'install window|import |cold_app_ms|warm_app_ms|runtime_state|runtime_ms|paste started|library persisted|launchpad entry|dock pin|window mapped|compositor on' "$LOG" | tail -80
echo SHOT_INSTALL_DONE
ls -l "$OUT"/*.png 2>/dev/null || true
