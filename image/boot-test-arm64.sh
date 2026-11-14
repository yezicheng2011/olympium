#!/bin/bash
# Boots the arm64 ISO on QEMU virt + AAVMF and records the serial log.
# This is not a Raspberry Pi, SBSA, or Apple Silicon boot.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$REPO/build/dev-live-arm64.iso}
OUT=${2:-/opt/cursor/artifacts}
STAGE=${3:-/var/tmp/dev-live/iso-arm64}
mkdir -p "$OUT"
LOG=$OUT/olympium-arm64-serial.log
rm -f "$LOG"
if [ ! -f "$STAGE/boot/vmlinuz" ] || [ ! -f "$STAGE/boot/initrd.img" ]; then
	echo "no staged arm64 kernel" >&2
	exit 1
fi
dd if=/dev/zero of=/tmp/olympium-arm64-disk.img bs=1M count=8 status=none
qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 2048 -smp 2 \
	-bios /usr/share/AAVMF/AAVMF_CODE.fd \
	-kernel "$STAGE/boot/vmlinuz" \
	-initrd "$STAGE/boot/initrd.img" \
	-append "boot=live console=ttyAMA0,115200" \
	-cdrom "$ISO" \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
	-drive file=/tmp/olympium-arm64-disk.img,if=none,id=vd0,format=raw \
	-device virtio-blk-pci,drive=vd0 \
	-device virtio-gpu-pci \
	-serial file:"$LOG" \
	-display none -no-reboot &
qemu=$!
for i in $(seq 1 40); do
	if grep -a -E 'olympium-drivers:|olympium login:' "$LOG" >/dev/null 2>&1; then
		break
	fi
	if ! kill -0 "$qemu" 2>/dev/null; then
		break
	fi
	sleep 3
done
sleep 2
kill "$qemu" 2>/dev/null || true
wait "$qemu" 2>/dev/null || true
echo "--- driver lines ---"
grep -a 'olympium-drivers:\|virtio' "$LOG" | head -40 || true
sha256sum "$ISO" | tee "$OUT/dev-live-arm64.iso.sha256"
