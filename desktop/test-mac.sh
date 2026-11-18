#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Opens Mac disk images, installer packages and application folders the way
# the desktop does, and checks what ends up on disk.
#
# The files it opens are written by mac-fixtures.py. They are real in form:
# fsck.hfsplus accepts the HFS+ volumes and 7-Zip verifies the disk images.
# The application inside is a Mach-O header with no code after it, so
# nothing in this test runs a Mac program, and no Mac runtime is installed
# here. Starting an application is tested as far as this desktop goes: which
# program it hands to the runtime, and what it says when there is none.
#
# Needs 7z and cpio. With Xwayland and wlroots it also checks the Install
# window, Launchpad, and the window that explains a refusal.
set -u
cd "$(dirname "$0")"
if ! command -v 7z >/dev/null 2>&1 && ! command -v 7zz >/dev/null 2>&1; then
	echo "test-mac: 7-Zip is not installed; nothing was tested" >&2
	exit 77
fi
if ! command -v cpio >/dev/null 2>&1; then
	echo "test-mac: cpio is not installed; nothing was tested" >&2
	exit 77
fi
make dev-install >/dev/null || exit 1
ROOT=$(mktemp -d /var/tmp/test-mac.XXXXXX)
# The fallback confinement unpacks as the user "nobody", who has to be able
# to reach the files. A temporary directory is private by default.
chmod 755 "$ROOT"
trap '[ -n "${TEST_MAC_KEEP:-}" ] && mkdir -p "$TEST_MAC_KEEP" && cp "$ROOT"/*.log "$TEST_MAC_KEEP"/ 2>/dev/null; kill $(cat "$ROOT/comp.pid" 2>/dev/null) 2>/dev/null; rm -rf "$ROOT"' EXIT
FX=$ROOT/fx
python3 mac-fixtures.py "$FX" >/dev/null || exit 1
export OLYMPIUM_DESK_HOME=$ROOT/desk
export OLYMPIUM_BIN=$ROOT/no-olympium-runtime
export OLYMPIUM_MAC_RUNTIME=$ROOT/no-mac-runtime
export OLYMPIUM_NO_DIALOG=1
export HOME=$ROOT/home
mkdir -p "$OLYMPIUM_DESK_HOME" "$HOME"
chmod 755 "$OLYMPIUM_DESK_HOME"
APPS=$OLYMPIUM_DESK_HOME/mac/Applications
LIB=$OLYMPIUM_DESK_HOME/library.tsv
fail=0
ok() { echo "$1: ok"; }
bad() { echo "FAIL $1"; fail=1; }
field() { sed -n "s/.*\"$1\": \"\\([^\"]*\\)\".*/\\1/p" | head -1; }
mac_id() { awk -F '\t' -v n="$1" '$2==n && $7=="mac" {print $1; exit}' "$LIB" 2>/dev/null; }
reset_desk() {
	rm -rf "$OLYMPIUM_DESK_HOME"
	mkdir -p "$OLYMPIUM_DESK_HOME"
	chmod 755 "$OLYMPIUM_DESK_HOME"
}
same_as_plain() {
	diff -r "$FX/plain/Desk Mac.app" "$APPS/Desk Mac.app" >/dev/null 2>&1
}
# The packages carry the same application with its Info.plist in the binary
# form, so that file is compared by what was read out of it.
same_as_packaged() {
	diff -r -x Info.plist "$FX/plain/Desk Mac.app" "$APPS/Desk Mac.app" >/dev/null 2>&1 &&
		[ "$(head -c 8 "$APPS/Desk Mac.app/Contents/Info.plist")" = bplist00 ] &&
		grep -q '^app=Desk Mac$' "$1" && grep -q '^version=1.2$' "$1"
}

echo "confinement $(./dev-install confinement)"

# 0. The fixtures are what they claim to be, by tools that are not ours.
if command -v fsck.hfsplus >/dev/null 2>&1; then
	if fsck.hfsplus -n -f "$FX/DeskMac.hfs" 2>&1 | grep -q 'appears to be OK'; then
		ok "fsck.hfsplus accepts the HFS+ volume"
	else
		bad "fsck.hfsplus rejects the HFS+ volume"
	fi
else
	echo "fsck.hfsplus is not installed; the volume was not checked by it"
fi
SEVENZ=$(command -v 7zz || command -v 7z)
for image in DeskMac DeskMacApm DeskMacBz DeskMacTwo DeskArm DeskBig DeskCjk; do
	"$SEVENZ" t -tdmg "$FX/$image.dmg" 2>&1 | grep -q 'Everything is Ok' || bad "7-Zip does not verify $image.dmg"
done
[ "$fail" = 0 ] && ok "7-Zip verifies the checksums of all seven disk images"

# 1. The readers, on good and on damaged input, under the sanitizers.
if cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o "$ROOT/test-macbundle" \
		test_macbundle.c macbundle.c macho.c 2>/dev/null; then
	python3 - "$FX/wide.bplist" <<'PYEOF'
import plistlib, sys
with open(sys.argv[1], "wb") as handle:
    handle.write(plistlib.dumps({
        "CFBundleExecutable": "Desk Mac", "CFBundleName": "Desk Mac",
        "CFBundleShortVersionString": "1.2", "Wide": "\u8ba1\u7b97\u5668",
        "Many": list(range(40)), "Nested": {"CFBundleExecutable": "inner"},
    }, fmt=plistlib.FMT_BINARY))
PYEOF
	"$ROOT/test-macbundle" "$FX/plain/Desk Mac.app" "$FX/wide.bplist" || fail=1
else
	echo "the sanitizers are not available; the reader test was not built"
fi

# 1b. The confinement itself: each thing an unpacker must not be able to do
# is tried from inside it.
if cc -O2 -o "$ROOT/test-sandbox" test_sandbox.c sandbox.c 2>/dev/null; then
	mkdir -p "$ROOT/sbx/in" "$ROOT/sbx/out" "$ROOT/sbx/open" "$ROOT/sbx/closed"
	chmod 755 "$ROOT/sbx" "$ROOT/sbx/in" "$ROOT/sbx/closed"
	chmod 777 "$ROOT/sbx/open"
	echo data > "$ROOT/sbx/in/file"
	echo secret > "$ROOT/sbx/open/secret.txt"
	cp /bin/true "$ROOT/sbx/out/true-copy"
	"$ROOT/test-sandbox" "$ROOT/sbx/in/file" "$ROOT/sbx/out" "$ROOT/sbx/open" >"$ROOT/sandbox.log" 2>&1
	code=$?
	if [ "$code" -eq 0 ] && [ "$(ls "$ROOT/sbx/open")" = secret.txt ]; then
		ok "$(sed -n 's/^confinement //p' "$ROOT/sandbox.log") confinement held on $(grep -c '^  held:' "$ROOT/sandbox.log") points"
	elif [ "$code" -eq 77 ]; then
		echo "this user gets no confinement on this kernel; it was not tested"
	else
		bad "confinement"
		cat "$ROOT/sandbox.log"
	fi
	if [ "$(id -u)" -eq 0 ]; then
		rm -rf "$ROOT/sbx/out"
		mkdir "$ROOT/sbx/out"
		OLYMPIUM_CONFINE=nobody "$ROOT/test-sandbox" "$ROOT/sbx/in/file" "$ROOT/sbx/out" "$ROOT/sbx/closed" \
			>"$ROOT/sandbox-nobody.log" 2>&1
		if [ $? -eq 0 ] && [ -z "$(ls -A "$ROOT/sbx/closed")" ]; then
			ok "the fallback, as the user nobody, held on $(grep -c '^  held:' "$ROOT/sandbox-nobody.log") points"
		else
			bad "fallback confinement"
			cat "$ROOT/sandbox-nobody.log"
		fi
	fi
else
	echo "the confinement test was not built"
fi

# 1c. A second reader agrees about the disk image, byte for byte.
if command -v dmg2img >/dev/null 2>&1; then
	if dmg2img -s "$FX/DeskMac.dmg" "$ROOT/back.img" >/dev/null 2>&1 && cmp -s "$ROOT/back.img" "$FX/DeskMac.hfs"; then
		ok "dmg2img turns the disk image back into the exact volume it was made from"
	else
		bad "dmg2img does not reproduce the volume"
	fi
	rm -f "$ROOT/back.img"
else
	echo "dmg2img is not installed; the disk image was not checked by it"
fi

# 2. A disk image. Looking at it installs nothing.
out=$(./dev-install inspect "$FX/DeskMac.dmg")
[ "$(echo "$out" | field action)" = mac ] && [ "$(echo "$out" | field kind)" = mac_dmg ] &&
	echo "$out" | grep -q 'Desk Mac 1.2' && echo "$out" | grep -q '"ran": false' ||
	bad "inspect of the disk image: $out"
[ ! -e "$APPS" ] && [ ! -e "$LIB" ] || bad "inspect installed something"
./dev-install import "$FX/DeskMac.dmg" >/dev/null 2>&1
[ $? -eq 2 ] && [ ! -e "$APPS" ] || bad "install without consent"
ok "disk image is read, and nothing is installed without consent"

./dev-install import --consent "$FX/DeskMac.dmg" >"$ROOT/import.log" 2>&1 || { bad "install of the disk image"; cat "$ROOT/import.log"; }
id=$(mac_id "Desk Mac")
icon=$(sed -n 's/^icon=//p' "$ROOT/import.log")
entry=$OLYMPIUM_DESK_HOME/applications/olympium-$id.desktop
if [ -n "$id" ] && same_as_plain && [ -s "$icon" ] && head -c 4 "$icon" | grep -q PNG &&
		grep -q "^Icon=$icon\$" "$entry" && grep -q '^Name=Desk Mac$' "$entry" &&
		grep -q '^archs=x86_64, arm64$' "$ROOT/import.log" &&
		[ -f "$APPS/Desk Mac.app/Contents/Resources/中文说明.txt" ]; then
	ok "disk image installs the application whole, with its icon and its Chinese-named file"
else
	bad "disk image install: id=$id icon=$icon"
	cat "$ROOT/import.log"
fi
# The picture the desktop keeps is one it drew itself: 256 by 256, from the
# pixels a confined child handed back. It is not the application's own bytes.
if python3 - "$icon" "$APPS/Desk Mac.app/Contents/Resources/DeskMac.icns" <<'PYEOF'
import struct, sys
kept = open(sys.argv[1], "rb").read()
original = open(sys.argv[2], "rb").read()
width, height = struct.unpack(">II", kept[16:24])
sys.exit(0 if (width, height) == (256, 256) and kept[8:] not in original and kept not in original else 1)
PYEOF
then
	ok "the kept icon is a 256 by 256 picture drawn here, not bytes from the application"
else
	bad "the kept icon"
fi
[ ! -e "$OLYMPIUM_DESK_HOME/pinned" ] || bad "a Mac application was pinned to the dock"
[ -z "$(find "$OLYMPIUM_DESK_HOME/mac" -maxdepth 1 -name '.stage-*' -o -maxdepth 1 -name '.old-*')" ] ||
	bad "a staging folder was left behind"

# 3. Every other form the same application arrives in.
for file in DeskMacApm.dmg DeskMacBz.dmg DeskMacTwo.dmg DeskMac.pkg DeskMacPbzx.pkg; do
	reset_desk
	./dev-install import --consent "$FX/$file" >"$ROOT/import.log" 2>&1
	code=$?
	case "$file" in
		*.pkg) same_as_packaged "$ROOT/import.log"; same=$? ;;
		*) same_as_plain; same=$? ;;
	esac
	if [ "$code" -ne 0 ] || [ "$same" -ne 0 ] || [ -z "$(mac_id "Desk Mac")" ]; then
		bad "install of $file"
		tail -n 4 "$ROOT/import.log"
	fi
done
reset_desk
if ./dev-install import --consent "$FX/plain/Desk Mac.app" >"$ROOT/import.log" 2>&1 && same_as_plain; then
	:
else
	bad "install of the application folder"
fi
[ "$fail" = 0 ] && ok "partition map, bzip2, two volumes, gzip package, pbzx package, and a bare .app all install the same files"
[ ! -e "$FX/postinstall-ran" ] && ok "the package's installer script was not run" || bad "the installer script ran"

# 4. A package with no application is read and not installed.
reset_desk
out=$(./dev-install inspect "$FX/DeskTool.pkg")
if [ "$(echo "$out" | field action)" = review ] &&
		! ./dev-install import --consent "$FX/DeskTool.pkg" >/dev/null 2>&1 && [ ! -e "$APPS/desk-tool" ]; then
	ok "a package with no application is not installed"
else
	bad "package with no application: $out"
fi

# 5. Hostile files. Each tries to write outside the folder it is unpacked into.
reset_desk
rm -rf "$FX/escape"
mkdir "$FX/escape"
chmod 777 "$FX/escape"
./dev-install import --consent "$FX/Evil.dmg" >"$ROOT/evil-dmg.log" 2>&1
evil_dmg=$?
./dev-install import --consent "$FX/Evil.pkg" >"$ROOT/evil-pkg.log" 2>&1
evil_pkg=$?
if [ -z "$(ls -A "$FX/escape")" ] && [ -z "$(find "$APPS" -type l 2>/dev/null)" ] &&
		[ -z "$(find "$OLYMPIUM_DESK_HOME" -name 'planted.txt' -o -name 'dotdot.txt' -o -name 'absolute.txt')" ]; then
	ok "hostile image and package wrote nothing outside (image exit $evil_dmg, package exit $evil_pkg)"
else
	bad "a hostile file escaped: $(ls -A "$FX/escape") $(find "$APPS" -type l 2>/dev/null)"
fi
[ "$evil_pkg" -ne 0 ] || bad "the hostile package was installed"
if [ "$evil_dmg" -eq 0 ]; then
	# Installed, without the link that pointed at /etc/hostname.
	[ ! -e "$APPS/Evil.app/Contents/Resources/secret" ] || bad "a link out of the application was kept"
fi

# 6. A damaged download does not replace a good install.
reset_desk
./dev-install import --consent "$FX/DeskMac.dmg" >/dev/null 2>&1
before=$(sha256sum "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac" | cut -d' ' -f1)
python3 - "$FX/DeskMac.dmg" "$FX/Broken.dmg" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
for at in range(600, 900):
    data[at] ^= 0x5A
open(sys.argv[2], "wb").write(data)
PYEOF
./dev-install import --consent "$FX/Broken.dmg" >"$ROOT/broken.log" 2>&1
broken=$?
after=$(sha256sum "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac" 2>/dev/null | cut -d' ' -f1)
if [ "$broken" -ne 0 ] && [ "$before" = "$after" ] && same_as_plain &&
		[ -z "$(find "$OLYMPIUM_DESK_HOME/mac" -maxdepth 1 -name '.stage-*')" ]; then
	ok "a damaged disk image is refused and the installed copy is untouched"
else
	bad "damaged disk image: exit $broken"
	tail -n 3 "$ROOT/broken.log"
fi
head -c 3000 "$FX/DeskMac.pkg" > "$FX/Short.pkg"
if ! ./dev-install import --consent "$FX/Short.pkg" >/dev/null 2>&1 && same_as_plain; then
	ok "a cut-off package is refused and the installed copy is untouched"
else
	bad "cut-off package"
fi

# 7. Opening. No Mac runtime is installed here, and none is pretended.
id=$(mac_id "Desk Mac")
./dev-install launch "$id" >"$ROOT/launch.log" 2>&1
[ $? -eq 3 ] && grep -q 'Darling is not installed' "$ROOT/launch.log" &&
	ok "without a Mac runtime, opening is refused and says why" || bad "launch without a runtime"
# A stand-in for the runtime records what it was handed.
cat > "$ROOT/runtime" <<EOF
#!/bin/sh
[ "\$1" = "--check" ] && exit 0
printf '%s\n' "\$@" > "$ROOT/runtime.args"
EOF
chmod 755 "$ROOT/runtime"
OLYMPIUM_MAC_RUNTIME=$ROOT/runtime ./dev-install launch "$id" one "two words" >"$ROOT/launch.log" 2>&1
if [ $? -eq 0 ] && [ "$(sed -n 1p "$ROOT/runtime.args")" = "$APPS/Desk Mac.app" ] &&
		[ "$(sed -n 2p "$ROOT/runtime.args")" = "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac" ] &&
		[ "$(sed -n 4p "$ROOT/runtime.args")" = "two words" ]; then
	ok "with a runtime, it is handed the application, its program, and the arguments"
else
	bad "launch through the stand-in runtime"
	cat "$ROOT/launch.log"
fi
OLYMPIUM_MAC_RUNTIME=$ROOT/runtime OLYMPIUM_HOST_MACHINE=riscv64 ./dev-install launch "$id" >"$ROOT/launch.log" 2>&1
[ $? -eq 3 ] && grep -q 'built for x86_64, arm64' "$ROOT/launch.log" &&
	ok "an application with no code for this processor is not opened" || bad "launch on another processor"
./dev-install import --consent "$FX/DeskArm.dmg" >/dev/null 2>&1
arm=$(mac_id "Desk Arm")
OLYMPIUM_MAC_RUNTIME=$ROOT/runtime OLYMPIUM_HOST_MACHINE=x86_64 ./dev-install launch "$arm" >"$ROOT/launch.log" 2>&1
[ $? -eq 3 ] && grep -q 'built for arm64, and this computer is x86_64' "$ROOT/launch.log" &&
	ok "an arm64-only application is not opened on x86_64" || bad "arm64 application on x86_64"
printf 'x' >> "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac"
rm -f "$ROOT/runtime.args"
OLYMPIUM_MAC_RUNTIME=$ROOT/runtime ./dev-install launch "$id" >"$ROOT/launch.log" 2>&1
[ $? -eq 4 ] && [ ! -e "$ROOT/runtime.args" ] &&
	ok "a program changed after install is not opened" || bad "launch of a changed program"

# 7a. An application whose icon file is damaged still installs, without a picture.
rm -rf "$ROOT/damaged"
mkdir -p "$ROOT/damaged"
cp -a "$FX/plain/Desk Mac.app" "$ROOT/damaged/Bad Icon.app"
python3 - "$ROOT/damaged/Bad Icon.app/Contents/Resources/DeskMac.icns" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
for at in range(40, len(data), 7):
    data[at] ^= 0xA5
open(sys.argv[1], "wb").write(data)
PYEOF
reset_desk
./dev-install import --consent "$ROOT/damaged/Bad Icon.app" >"$ROOT/badicon.log" 2>&1
if [ $? -eq 0 ] && grep -q '^icon=none$' "$ROOT/badicon.log" && [ -d "$APPS/Bad Icon.app" ] &&
		! grep -q '^Icon=' "$OLYMPIUM_DESK_HOME"/applications/olympium-mac-*.desktop; then
	ok "an application with a damaged icon installs without a picture"
else
	bad "damaged icon"
	tail -n 4 "$ROOT/badicon.log"
fi

# 7b. A double-click on an application that is already installed opens it.
reset_desk
./dev-install import --consent "$FX/DeskMac.dmg" >/dev/null 2>&1
./dev-install import --consent "$FX/DeskArm.dmg" >/dev/null 2>&1
rm -f "$ROOT/runtime.args"
OLYMPIUM_MAC_RUNTIME=$ROOT/runtime ./dev-install open --no-ui "$APPS/Desk Mac.app" >"$ROOT/open.log" 2>&1
if [ "$(sed -n 2p "$ROOT/runtime.args" 2>/dev/null)" = "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac" ]; then
	ok "opening an application that is already in Applications starts it"
else
	bad "open of an installed application"
	cat "$ROOT/open.log"
fi

# 7c. Chinese names: the file, its folder, the volume, the application, its program.
mkdir -p "$ROOT/下载 文件"
cp "$FX/DeskCjk.dmg" "$ROOT/下载 文件/计算器 安装.dmg"
out=$(./dev-install inspect "$ROOT/下载 文件/计算器 安装.dmg")
./dev-install import --consent "$ROOT/下载 文件/计算器 安装.dmg" >"$ROOT/cjk.log" 2>&1
cjk=$(mac_id "计算器")
if echo "$out" | grep -q '计算器 1.2' && [ -n "$cjk" ] &&
		[ -f "$APPS/计算器.app/Contents/MacOS/计算器" ] &&
		grep -q '^Name=计算器$' "$OLYMPIUM_DESK_HOME/applications/olympium-$cjk.desktop" &&
		grep -q '^archs=x86_64, arm64$' "$ROOT/cjk.log"; then
	ok "a Chinese-named application from a Chinese-named folder installs under its own name"
else
	bad "Chinese names: id=$cjk"
	tail -n 5 "$ROOT/cjk.log"
fi

# 7d. Removing one application takes it and only it.
id=$(mac_id "Desk Mac")
arm=$(mac_id "Desk Arm")
icon=$(sed -n 's/^Icon=//p' "$OLYMPIUM_DESK_HOME/applications/olympium-$id.desktop")
./dev-install remove "$id" >"$ROOT/remove.log" 2>&1
if [ $? -eq 0 ] && [ ! -e "$APPS/Desk Mac.app" ] && [ -z "$(mac_id "Desk Mac")" ] &&
		[ ! -e "$OLYMPIUM_DESK_HOME/applications/olympium-$id.desktop" ] &&
		[ ! -e "$OLYMPIUM_DESK_HOME/launch/$id" ] && [ ! -e "$icon" ] &&
		[ -d "$APPS/Desk Arm.app" ] && [ -n "$(mac_id "Desk Arm")" ] && [ -d "$APPS/计算器.app" ]; then
	ok "remove takes the application, its entry and its icon, and leaves the others"
else
	bad "remove"
	cat "$ROOT/remove.log"
fi
# A library row that was edited to point elsewhere deletes nothing.
mkdir -p "$ROOT/decoy/keep"
python3 - "$LIB" "$APPS/Desk Arm.app" "$ROOT/decoy" <<'PYEOF'
import sys
path, old, new = sys.argv[1:4]
text = open(path, encoding="utf-8").read().replace("\t" + old + "\t", "\t" + new + "\t")
open(path, "w", encoding="utf-8").write(text)
PYEOF
./dev-install remove "$arm" >"$ROOT/remove.log" 2>&1
[ $? -eq 3 ] && [ -d "$ROOT/decoy/keep" ] && [ -d "$APPS/Desk Arm.app" ] &&
	ok "remove refuses a row that points outside Applications" || bad "remove followed an edited row"

# 8. The fallback confinement, where the kernel has no Landlock.
if [ "$(id -u)" -eq 0 ]; then
	reset_desk
	if OLYMPIUM_CONFINE=nobody ./dev-install import --consent "$FX/DeskMac.dmg" >"$ROOT/nobody.log" 2>&1 &&
			grep -q '^confinement=nobody$' "$ROOT/nobody.log" && same_as_plain &&
			[ "$(stat -c %u "$APPS/Desk Mac.app/Contents/Info.plist")" = 0 ]; then
		reset_desk
		if OLYMPIUM_CONFINE=nobody ./dev-install import --consent "$FX/DeskMac.pkg" >"$ROOT/nobody.log" 2>&1 &&
				same_as_packaged "$ROOT/nobody.log"; then
			ok "unpacking as the user nobody installs the same files, owned by this user"
		else
			bad "package install as nobody"
		fi
	else
		bad "disk image install as nobody"
		tail -n 3 "$ROOT/nobody.log"
	fi
	cp "$FX/DeskMac.dmg" "$FX/Private.dmg"
	chmod 600 "$FX/Private.dmg"
	reset_desk
	if ! OLYMPIUM_CONFINE=nobody ./dev-install import --consent "$FX/Private.dmg" >"$ROOT/nobody.log" 2>&1 &&
			[ ! -e "$APPS/Desk Mac.app" ]; then
		ok "a file that user cannot read is refused, not unpacked without limits"
	else
		bad "private file as nobody"
	fi
else
	echo "not root; the fallback confinement was not tested"
fi

# 9. On the desktop: the Install window, its buttons, Launchpad, and the
# window that explains a refusal.
if make dev-compositor >/dev/null 2>&1 && [ -x ./dev-compositor ]; then
	reset_desk
	export XDG_RUNTIME_DIR=$ROOT/run
	mkdir -p "$XDG_RUNTIME_DIR" /run/dev-desktop
	chmod 700 "$XDG_RUNTIME_DIR"
	LOG=$ROOT/compositor.log
	printf '#!/bin/sh\nenv | grep -E "^(DISPLAY|WAYLAND_DISPLAY)=" > "%s/session.env"\n' "$ROOT" > "$ROOT/session.sh"
	chmod 755 "$ROOT/session.sh"
	# What Launchpad starts inherits the desktop's environment. Start with
	# dialogs enabled and a runtime path that does not exist yet.
	env -u OLYMPIUM_NO_DIALOG OLYMPIUM_MAC_RUNTIME="$ROOT/launchpad-runtime" \
		WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 DEV_DESK_HEADLESS=1 \
		./dev-compositor -s "$ROOT/session.sh" >"$LOG" 2>&1 &
	echo $! > "$ROOT/comp.pid"
	for _ in $(seq 1 100); do
		[ -s "$ROOT/session.env" ] && break
		sleep 0.1
	done
	if [ -s "$ROOT/session.env" ]; then
		set -a
		. "$ROOT/session.env"
		set +a
		export GDK_BACKEND=wayland
		ctl() {
			python3 - "$1" <<'PYEOF'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/run/dev-desktop/control")
s.sendall(sys.argv[1].encode())
s.close()
PYEOF
		}
		mapped() { grep -c "window mapped: $1" "$LOG"; }
		# Stop, pressed while the image is being read.
		OLYMPIUM_INSTALL_AUTO=stop OLYMPIUM_INSTALL_PAUSE_MS=1500 OLYMPIUM_INSTALL_AUTO_STOP_MS=300 \
			timeout 30 ./dev-install open "$FX/DeskBig.dmg" >"$ROOT/auto-stop.log" 2>&1
		if grep -q '^auto: Stopped. Nothing was installed.$' "$ROOT/auto-stop.log" &&
				[ ! -e "$APPS/Desk Big.app" ] && [ ! -e "$LIB" ] &&
				[ -z "$(find "$OLYMPIUM_DESK_HOME/mac" -maxdepth 1 -name '.stage-*' 2>/dev/null)" ]; then
			ok "Stop in the Install window ends the install and leaves nothing behind"
		else
			bad "Stop in the Install window"
			tail -n 4 "$ROOT/auto-stop.log"
		fi
		# Install, pressed.
		before=$(mapped Install)
		OLYMPIUM_INSTALL_AUTO=install timeout 60 ./dev-install open "$FX/DeskBig.dmg" >"$ROOT/auto.log" 2>&1
		big=$(sed -n 's/^auto_id: //p' "$ROOT/auto.log")
		if [ "$(mapped Install)" -gt "$before" ] && grep -q '^auto: Installed in Applications' "$ROOT/auto.log" &&
				[ -n "$big" ] && [ "$big" != none ] &&
				[ "$(stat -c %s "$APPS/Desk Big.app/Contents/Resources/bulk.bin")" = 25165824 ]; then
			ok "the Install button installs a 24 MB application and reports it"
		else
			bad "Install button"
			tail -n 4 "$ROOT/auto.log"
		fi
		./dev-install import --consent "$FX/DeskMac.dmg" >"$ROOT/import.log" 2>&1
		icon=$(sed -n 's/^icon=//p' "$ROOT/import.log")
		./dev-install import --consent "$FX/DeskCjk.dmg" >/dev/null 2>&1
		ctl "launchpad list"
		sleep 0.5
		if grep -q "launchpad has: Desk Mac \[installed\] icon $icon" "$LOG" &&
				grep -q 'launchpad has: Desk Big \[installed\] icon /' "$LOG" &&
				grep -q 'launchpad has: 计算器 \[installed\] icon /' "$LOG"; then
			ok "Launchpad lists the three Mac applications with their own icons"
		else
			bad "Launchpad listing"
			grep 'launchpad has: Desk' "$LOG" | tail -n 3
		fi
		# Search finds them by name, the Chinese one too, and names that
		# start with what was typed come before names that only contain it.
		ctl "search open"
		ctl "search type 计算"
		ctl "search type mac"
		sleep 0.5
		if grep -q 'search result 1: 计算器' "$LOG" && grep -q 'search for "计算": 1 result$' "$LOG" &&
				grep -q 'search result 1: Desk Mac' "$LOG"; then
			ok "search finds Mac applications by name, in Chinese too"
		else
			bad "search results"
			grep 'search' "$LOG" | tail -n 5
		fi
		ctl "search close"
		# Quick settings, under the sliders glyph: each line does what it says.
		ctl "menu open 8"
		ctl "menu pick 1"
		ctl "menu open 8"
		ctl "menu pick 0"
		sleep 0.3
		state=$(cat "$OLYMPIUM_DESK_HOME/widgets" 2>/dev/null)
		ctl "menu open 8"
		ctl "menu pick 1"
		ctl "menu open 8"
		ctl "menu pick 0"
		sleep 0.3
		if grep -q 'widgets off' "$LOG" && grep -q 'widgets on' "$LOG" && [ "$state" = off ] &&
				[ "$(cat "$OLYMPIUM_DESK_HOME/widgets")" = on ] &&
				grep -q 'reduce transparency on' "$LOG" && grep -q 'reduce transparency off' "$LOG"; then
			ok "quick settings turns widgets and transparency off and on, and the choice is kept"
		else
			bad "quick settings"
			grep -E 'widgets o|transparency' "$LOG" | tail -n 4
		fi
		# From Launchpad with no runtime: a window says why.
		unset OLYMPIUM_NO_DIALOG
		before=$(mapped 'Desk Mac')
		ctl "launchpad start Desk Mac"
		for _ in $(seq 1 60); do
			[ "$(mapped 'Desk Mac')" -gt "$before" ] && break
			sleep 0.1
		done
		if [ "$(mapped 'Desk Mac')" -gt "$before" ]; then
			ok "started from Launchpad with no Mac runtime, a window explains the refusal"
		else
			bad "no window explained the refusal"
		fi
		pkill -f "dev-install launch" 2>/dev/null
		# The same from the search panel: Return starts the selected result.
		before=$(mapped 'Desk Mac')
		ctl "search open"
		ctl "search type desk m"
		ctl "search enter"
		for _ in $(seq 1 60); do
			[ "$(mapped 'Desk Mac')" -gt "$before" ] && break
			sleep 0.1
		done
		if [ "$(mapped 'Desk Mac')" -gt "$before" ] && grep -q 'search start: Desk Mac' "$LOG"; then
			ok "Return in the search panel starts the selected application"
		else
			bad "search did not start the application"
		fi
		pkill -f "dev-install launch" 2>/dev/null
		export OLYMPIUM_NO_DIALOG=1
		# Install the stand-in at the path the running compositor inherited.
		if cp "$ROOT/runtime" "$ROOT/launchpad-runtime"; then
			rm -f "$ROOT/runtime.args"
			ctl "launchpad start Desk Mac"
			for _ in $(seq 1 60); do
				[ -s "$ROOT/runtime.args" ] && break
				sleep 0.1
			done
			if [ "$(sed -n 2p "$ROOT/runtime.args" 2>/dev/null)" = "$APPS/Desk Mac.app/Contents/MacOS/Desk Mac" ]; then
				ok "started from Launchpad with a runtime, the runtime is handed the program"
			else
				bad "Launchpad did not reach the runtime"
			fi
		else
			bad "could not install the test Launchpad runtime"
		fi
	else
		echo "the compositor did not start; the windows were not tested"
	fi
else
	echo "the compositor is not built here; the windows were not tested"
fi

# 10. With a real Mac runtime on this machine: one of its own command-line
# programs, a real Mach-O executable, wrapped in a disk image and in a package
# and taken through the whole path: read, unpacked confined, installed,
# checksummed, checked for this CPU, and started by the helper the image
# ships. Without a runtime, no Mac program is run, and this says so.
if command -v darling >/dev/null 2>&1; then
	echo_program=$(dirname "$(dirname "$(command -v darling)")")/libexec/darling/bin/echo
	if [ -f "$echo_program" ]; then
		for form in dmg pkg; do
			reset_desk
			python3 mac-fixtures.py --wrap "$echo_program" "Desk Echo" "$FX/DeskEcho.$form" >/dev/null
			./dev-install import --consent "$FX/DeskEcho.$form" >"$ROOT/echo-import.log" 2>&1
			id=$(mac_id "Desk Echo")
			archs=$(sed -n 's/^archs=//p' "$ROOT/echo-import.log")
			said=$(OLYMPIUM_MAC_RUNTIME=$PWD/../image/olympium-mac-run timeout 300 \
				./dev-install launch "$id" "said by a Mac program" "from a .$form" 2>"$ROOT/echo-launch.err" | tail -n 1)
			if [ "$said" = "said by a Mac program from a .$form" ] &&
					cmp -s "$echo_program" "$APPS/Desk Echo.app/Contents/MacOS/Desk Echo"; then
				ok "a Mach-O program ($archs) from a .$form ran through Darling and printed its arguments"
			else
				bad "the Mac program from a .$form did not run: got '$said'"
				tail -n 5 "$ROOT/echo-launch.err"
			fi
		done
		# The same program, changed by one byte after install, is not handed over.
		printf 'x' >> "$APPS/Desk Echo.app/Contents/MacOS/Desk Echo"
		said=$(OLYMPIUM_MAC_RUNTIME=$PWD/../image/olympium-mac-run timeout 60 \
			./dev-install launch "$id" "must not be said" 2>/dev/null | tail -n 1)
		[ "$said" != "must not be said" ] && ok "with the runtime present, a changed program is still not started" ||
			bad "a changed program was started"
		darling shutdown >/dev/null 2>&1
	else
		echo "Darling is installed but its echo was not found at $echo_program; no Mac program was run"
	fi
else
	echo "no Mac runtime is installed; no Mac program was run"
fi

if [ "$fail" -ne 0 ]; then
	echo TEST_MAC_FAILED
	exit 1
fi
echo TEST_MAC_OK
