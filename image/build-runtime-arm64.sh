#!/bin/bash
# Cross-builds the Olympium runtime for arm64 on an x86_64 host.
#
# The runtime source is a separate repository and is not part of this tree.
# SRC must already hold an unmodified copy of it. Nothing is written into SRC
# except Cargo's own lock check; build output goes to a separate directory.
#
# The arm64 headers and libraries come from a throwaway overlay on top of the
# arm64 image root, so the GTK development packages never enter the image.
set -euo pipefail
if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E bash "$0" "$@"
fi
SRC=${SRC:-/var/tmp/aaa-src}
OUT=${OUT:-/var/tmp/aaa-out-arm64}
ROOT=${ROOT:-/var/tmp/dev-live/rootfs-arm64}
SYS=${SYS:-/var/tmp/arm64-sysroot}
TARGET_DIR=${TARGET_DIR:-/var/tmp/aaa-target-arm64}
TRIPLE=aarch64-unknown-linux-gnu

if [ ! -f "$SRC/Cargo.toml" ]; then
	echo "no runtime source at $SRC" >&2
	exit 1
fi

cleanup() {
	for dir in dev/pts dev proc sys; do
		umount "$SYS/merged/$dir" 2>/dev/null || true
	done
	umount "$SYS/merged" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$SYS/upper" "$SYS/work" "$SYS/merged" "$OUT"
if ! mountpoint -q "$SYS/merged"; then
	mount -t overlay overlay \
		-o "lowerdir=$ROOT,upperdir=$SYS/upper,workdir=$SYS/work" "$SYS/merged"
fi
mount --bind /proc "$SYS/merged/proc"
mount --bind /sys "$SYS/merged/sys"
mount --bind /dev "$SYS/merged/dev"
mount --bind /dev/pts "$SYS/merged/dev/pts"
chroot "$SYS/merged" apt-get update
DEBIAN_FRONTEND=noninteractive chroot "$SYS/merged" apt-get install -y --no-install-recommends \
	libgtk-4-dev libadwaita-1-dev

DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-aarch64-linux-gnu
export PATH=/usr/local/cargo/bin:$PATH
export RUSTUP_HOME=${RUSTUP_HOME:-/usr/local/rustup}
export CARGO_HOME=${CARGO_HOME:-/usr/local/cargo}
rustup target add --toolchain stable "$TRIPLE"

export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYS/merged"
export PKG_CONFIG_LIBDIR="$SYS/merged/usr/lib/aarch64-linux-gnu/pkgconfig:$SYS/merged/usr/share/pkgconfig"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS="-C link-arg=--sysroot=$SYS/merged"
export CARGO_TARGET_DIR="$TARGET_DIR"
cd "$SRC"
cargo +stable build --locked --release --target "$TRIPLE" -p linkspan-linux -p linkspan-cli

REL="$TARGET_DIR/$TRIPLE/release"
for name in olympium-linux linkspan-linux olympium linkspan; do
	if [ -f "$REL/$name" ]; then
		install -m 755 "$REL/$name" "$OUT/$name"
	fi
done
cp "$SRC/LICENSE" "$SRC/NOTICE" "$OUT/"
if [ -f "$SRC/LICENSE.zh.txt" ]; then
	cp "$SRC/LICENSE.zh.txt" "$OUT/"
fi
rm -rf "$OUT/share"
cp -a "$SRC/share" "$OUT/share"
file "$OUT/olympium-linux"
sha256sum "$OUT"/olympium-linux "$OUT"/linkspan-linux "$OUT"/olympium "$OUT"/linkspan
echo RUNTIME-ARM64-DONE
