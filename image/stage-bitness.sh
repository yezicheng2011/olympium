# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Installed by build-iso.sh and build-iso-i386.sh. ROOT, ARCH, and REPO are set.

stage_bitness() {
	local bins=$REPO/desktop
	if [ -n "${DESKTOP_BINS:-}" ]; then
		bins=$DESKTOP_BINS
	fi
	install -d "$ROOT/usr/bin" "$ROOT/usr/libexec/dev-desktop" \
		"$ROOT/usr/share/olympium/samples" "$ROOT/usr/share/applications" \
		"$ROOT/root/.local/share/olympium"
	install -m 755 "$bins/olympium-run" "$ROOT/usr/libexec/dev-desktop/olympium-run"
	install -m 755 "$bins/olympium-testapp" "$ROOT/usr/bin/olympium-testapp"
	install -m 755 "$bins/dev-install" "$ROOT/usr/bin/dev-install"
	install -m 755 "$REPO/image/olympium-run64" "$ROOT/usr/libexec/dev-desktop/olympium-run64"
	install -m 755 "$REPO/image/olympium-hello32" "$ROOT/usr/bin/olympium-hello32"
	install -m 644 "$REPO/image/olympium-hello32.desktop" \
		"$REPO/image/olympium-testapp.desktop" \
		"$ROOT/usr/share/applications/"
	python3 - "$ROOT/usr/share/olympium/samples/macho-header-fixture" <<'PY'
import sys
# Little-endian MH_MAGIC_64, cputype x86_64, filetype execute. A header only.
raw = bytes([0xCF, 0xFA, 0xED, 0xFE, 0x07, 0x00, 0x00, 0x01, 0, 0, 0, 0, 2, 0, 0, 0])
raw += bytes(16)
open(sys.argv[1], "wb").write(raw)
PY
	cat > "$ROOT/root/.local/share/olympium/catalog.json" <<'EOF'
{
  "version": 1,
  "programs": [
    {
      "id": "lawful-test-app",
      "name": "Lawful test app",
      "guest": "linux",
      "kind": "linux_executable",
      "target": {"type": "file", "path": "/usr/bin/olympium-testapp"},
      "args": [],
      "added_unix_ms": 1763200000000
    },
    {
      "id": "macho-header-fixture",
      "name": "Mach-O header fixture",
      "guest": "macos",
      "kind": "macos_binary",
      "target": {"type": "file", "path": "/usr/share/olympium/samples/macho-header-fixture"},
      "args": [],
      "added_unix_ms": 1763200000000
    }
  ]
}
EOF
	if [ "$ARCH" = amd64 ]; then
		mount_chroot
		chroot "$ROOT" dpkg --add-architecture i386
		chroot "$ROOT" apt-get update
		DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
			libc6:i386 libgcc-s1:i386
		gcc -m32 -O2 -DMSG='"hello32"' -o "$ROOT/usr/share/olympium/samples/hello32" \
			"$REPO/desktop/hello.c"
		chmod 755 "$ROOT/usr/share/olympium/samples/hello32"
		local shown
		shown=$(chroot "$ROOT" /usr/share/olympium/samples/hello32)
		echo "hello32: $shown"
		printf '%s\n' "$shown" | grep -q 'bits=32 machine=i386'
	elif [ "$ARCH" = arm64 ]; then
		mount_chroot
		chroot "$ROOT" dpkg --add-architecture armhf
		chroot "$ROOT" apt-get update
		DEBIAN_FRONTEND=noninteractive chroot "$ROOT" apt-get install -y --no-install-recommends \
			libc6:armhf libgcc-s1:armhf gcc-arm-linux-gnueabihf libc6-dev-armhf-cross
		cp "$REPO/desktop/hello.c" "$ROOT/tmp/hello.c"
		chroot "$ROOT" arm-linux-gnueabihf-gcc -O2 -DMSG='"hello32"' -o /usr/share/olympium/samples/hello32 /tmp/hello.c
		file "$ROOT/usr/share/olympium/samples/hello32" | grep -q 'ARM, EABI5'
		if [ -x /usr/bin/qemu-arm-static ]; then
			echo "hello32 armhf (build host qemu-arm, not the image path): $(qemu-arm-static -L "$ROOT" "$ROOT/usr/share/olympium/samples/hello32" || true)"
		fi
	elif [ "$ARCH" = i386 ]; then
		mount_chroot
		cp "$REPO/desktop/hello.c" "$ROOT/tmp/hello.c"
		chroot "$ROOT" gcc -O2 -DMSG='"hello32"' -o /usr/share/olympium/samples/hello32 /tmp/hello.c
		chroot "$ROOT" /usr/share/olympium/samples/hello32 | grep -q 'bits=32 machine=i386'
		local guest=${GUEST_OUT:-/var/tmp/dev-live/amd64-guest}
		if [ ! -f "$guest/vmlinuz" ] || [ ! -f "$guest/initrd.cpio" ]; then
			echo "64-bit guest is missing; run image/build-amd64-guest.sh" >&2
			exit 1
		fi
		install -d "$ROOT/usr/lib/olympium/amd64/bin" "$ROOT/usr/share/doc/olympium-amd64-guest"
		install -m 644 "$guest/vmlinuz" "$ROOT/usr/lib/olympium/amd64/vmlinuz"
		install -m 644 "$guest/initrd.cpio" "$ROOT/usr/lib/olympium/amd64/initrd.cpio"
		install -m 755 "$guest/bin/hello64" "$guest/bin/highmap64" "$guest/bin/payload64" \
			"$guest/bin/bash" "$ROOT/usr/lib/olympium/amd64/bin/"
		cp -a "$guest/doc/." "$ROOT/usr/share/doc/olympium-amd64-guest/"
		install -m 755 "$REPO/image/olympium-hello64" "$REPO/image/olympium-bash64" \
			"$REPO/image/olympium-64demo" "$ROOT/usr/bin/"
		install -m 644 "$REPO/image/olympium-hello64.desktop" \
			"$REPO/image/olympium-bash64.desktop" \
			"$ROOT/usr/share/applications/"
		if ! file "$ROOT/usr/bin/qemu-system-x86_64" | grep -q '80386'; then
			echo "qemu-system-x86_64 is not a 32-bit program" >&2
			file "$ROOT/usr/bin/qemu-system-x86_64" >&2
			exit 1
		fi
	fi
	file "$ROOT/usr/share/olympium/samples/hello32" "$ROOT/usr/bin/olympium-testapp" \
		"$ROOT/usr/libexec/dev-desktop/olympium-run"
}
