#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Builds the small x86_64 guest used so a 32-bit machine can run a 64-bit
# program. The kernel and the libraries are copied from the amd64 live root
# that this tree already built. Their copyright files are copied with them.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
AMD=${AMD64_ROOT:-/var/tmp/dev-live/rootfs-amd64}
OUT=${GUEST_OUT:-/var/tmp/dev-live/amd64-guest}
STAGE=$OUT/root
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/usr/bin" "$STAGE/lib/x86_64-linux-gnu" "$STAGE/lib64" \
	"$STAGE/proc" "$STAGE/sys" "$STAGE/dev" "$STAGE/usr/share/doc/olympium-amd64-guest"

gcc -static -Os -o "$STAGE/init" "$REPO/image/amd64-init.c"
gcc -static -O2 -DMSG='"hello64"' -o "$STAGE/usr/bin/hello64" "$REPO/desktop/hello.c"
gcc -static -O2 -DMSG='"payload64"' -o "$STAGE/usr/bin/payload64" "$REPO/desktop/hello.c"
gcc -static -O2 -o "$STAGE/usr/bin/highmap64" "$REPO/desktop/highmap.c"
chmod 755 "$STAGE/init" "$STAGE/usr/bin/hello64" "$STAGE/usr/bin/payload64" "$STAGE/usr/bin/highmap64"

copy() {
	local src=$1 dest=$2
	if [ ! -r "$src" ]; then
		sudo cp -a "$src" "$dest"
		sudo chown "$(id -u):$(id -g)" "$dest" 2>/dev/null || true
	else
		cp -a "$src" "$dest"
	fi
}

copy "$AMD/usr/bin/bash" "$STAGE/bin/bash"
copy "$AMD/usr/bin/uname" "$STAGE/usr/bin/uname"
copy "$AMD/lib/x86_64-linux-gnu/libc.so.6" "$STAGE/lib/x86_64-linux-gnu/libc.so.6"
copy "$AMD/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" "$STAGE/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
copy "$AMD/lib/x86_64-linux-gnu/libtinfo.so.6.4" "$STAGE/lib/x86_64-linux-gnu/libtinfo.so.6.4"
ln -s libtinfo.so.6.4 "$STAGE/lib/x86_64-linux-gnu/libtinfo.so.6"
ln -s ../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$STAGE/lib64/ld-linux-x86-64.so.2"
chmod 755 "$STAGE/bin/bash" "$STAGE/usr/bin/uname"

kver=$(basename "$(echo "$AMD"/boot/vmlinuz-*)" | sed 's/^vmlinuz-//')
copy "$AMD/boot/vmlinuz-$kver" "$OUT/vmlinuz"
for doc in \
	"$AMD/usr/share/doc/bash/copyright" \
	"$AMD/usr/share/doc/libc6/copyright" \
	"$AMD/usr/share/doc/libtinfo6/copyright" \
	"$AMD/usr/share/doc/coreutils/copyright" \
	"$AMD/usr/share/doc/linux-image-$kver/copyright"
do
	if [ -f "$doc" ] || sudo test -f "$doc"; then
		base=$(basename "$(dirname "$doc")")
		mkdir -p "$STAGE/usr/share/doc/$base"
		copy "$doc" "$STAGE/usr/share/doc/$base/copyright"
	fi
done
cat > "$STAGE/usr/share/doc/olympium-amd64-guest/NOTE" <<EOF
These files are a 64-bit guest used only so a 32-bit Olympium system can run
a 64-bit program. They are not the kernel that boots the 32-bit machine.

Kernel: Ubuntu linux-image $kver (GPL-2.0), file vmlinuz beside this guest.
bash, uname, libc, and libtinfo are the Ubuntu copies from the amd64 image.
Their copyright files are in the directories next to this note.
The init program and the sample programs are first-party and built from
image/amd64-init.c, desktop/hello.c, and desktop/highmap.c.
EOF
mkdir -p "$OUT/bin"
cp -a "$STAGE/usr/bin/hello64" "$STAGE/usr/bin/highmap64" "$STAGE/usr/bin/payload64" "$OUT/bin/"
cp -a "$STAGE/bin/bash" "$OUT/bin/bash"
cp -a "$STAGE/usr/share/doc/olympium-amd64-guest/NOTE" "$OUT/NOTE"
# Copyright files live in the image next to the guest, not only in the initrd.
rm -rf "$OUT/doc"
mkdir -p "$OUT/doc"
cp -a "$STAGE/usr/share/doc/." "$OUT/doc/"

(cd "$STAGE" && find . -print0 | cpio --null -o -H newc) > "$OUT/initrd.cpio"
ls -lh "$OUT/vmlinuz" "$OUT/initrd.cpio"
file "$OUT/vmlinuz" "$OUT/bin/hello64" "$OUT/bin/bash"

if [ "${1:-}" = "--no-test" ]; then
	exit 0
fi

run_one() {
	local cmd=$1
	local work
	work=$(mktemp -d)
	mkdir -p "$work/payload"
	printf '%s\n' "$cmd" > "$work/payload/cmd"
	if [ "$cmd" = payload ]; then
		cp "$OUT/bin/payload64" "$work/payload/program"
		chmod 755 "$work/payload/program"
	fi
	(cd "$work" && find payload -print | cpio -o -H newc) > "$work/extra.cpio" 2>/dev/null
	cat "$OUT/initrd.cpio" "$work/extra.cpio" > "$work/initrd"
	local start end
	start=$(date +%s)
	set +e
	timeout 180 qemu-system-x86_64 -machine pc,accel=tcg -m 512 \
		-kernel "$OUT/vmlinuz" -initrd "$work/initrd" \
		-append "console=ttyS0,115200 panic=1" \
		-serial file:"$work/serial" -display none -no-reboot -monitor none \
		>"$work/qemu.err" 2>&1
	local code=$?
	set -e
	end=$(date +%s)
	echo "TEST $cmd qemu=$code seconds=$((end - start))"
	if [ -f "$work/serial" ]; then
		sed -n '/OLYMPIUM64_BEGIN/,/OLYMPIUM64_END/p' "$work/serial" || true
	else
		echo "no serial"
		tail -n 15 "$work/qemu.err" || true
	fi
	rm -rf "$work"
}

run_one hello
run_one highmap
run_one bash
run_one payload
echo GUEST_OK
