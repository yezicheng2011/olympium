#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Installs a real .deb and a real .rpm through dev-install and checks what
# ends up on disk, then gives it packages built to misbehave.
#
# The Olympium runtime is not needed: a stand-in for the olympium command
# answers classify the way the pinned runtime does for these two formats.
# That is the only part the stand-in plays. The unpacking, the confinement
# and the checks are dev-install's own.
#
# Needs dpkg-deb. The .rpm part also needs rpm, rpm2cpio and rpmbuild, and
# says so when they are missing.
set -u
cd "$(dirname "$0")"
if ! command -v dpkg-deb >/dev/null 2>&1; then
	echo "test-packages: dpkg-deb is not installed; nothing was tested" >&2
	exit 77
fi
make dev-install >/dev/null || exit 1
ROOT=$(mktemp -d /var/tmp/test-packages.XXXXXX)
chmod 755 "$ROOT"
trap 'rm -rf "$ROOT"' EXIT
cat > "$ROOT/olympium" <<'EOF'
#!/bin/sh
case "$2" in
classify)
	magic=$(head -c 8 "$3" | od -An -tx1 | tr -d ' \n')
	case "$magic" in
	213c617263683e0a) echo '{"ok": true, "data": {"guest": "linux", "kind": "linux_deb", "detail": "Debian package"}}' ;;
	edabeedb*) echo '{"ok": true, "data": {"guest": "linux", "kind": "linux_rpm", "detail": "RPM package"}}' ;;
	*) echo '{"ok": false, "error": {"message": "not a package the stand-in knows"}}' ;;
	esac ;;
plan) echo '{"ok": true, "data": {"summary": "Extract, then run.", "launch": {"type": "extract_then_run"}, "surface": {"mode": "native", "hides_runtime_chrome": true}}}' ;;
*) echo '{"ok": false, "error": {"message": "stand-in"}}' ;;
esac
EOF
chmod 755 "$ROOT/olympium"
export OLYMPIUM_BIN=$ROOT/olympium
export OLYMPIUM_DESK_HOME=$ROOT/desk
export OLYMPIUM_RUN=$PWD/olympium-run
export HOME=$ROOT/home
mkdir -p "$OLYMPIUM_DESK_HOME" "$HOME" "$ROOT/escape"
chmod 755 "$OLYMPIUM_DESK_HOME"
chmod 777 "$ROOT/escape"
LIB=$OLYMPIUM_DESK_HOME/library.tsv
fail=0
ok() { echo "$1: ok"; }
bad() { echo "FAIL $1"; fail=1; }
field() { sed -n "s/.*\"$1\": \"\\([^\"]*\\)\".*/\\1/p" | head -1; }
reset_desk() {
	rm -rf "$OLYMPIUM_DESK_HOME"
	mkdir -p "$OLYMPIUM_DESK_HOME"
	chmod 755 "$OLYMPIUM_DESK_HOME"
}

echo "confinement $(./dev-install confinement)"

# 1. The sample .deb, built here as the image build builds it.
sh ../image/build-sample-deb.sh "$ROOT/s" "$PWD" >"$ROOT/build-deb.log" 2>&1 || { cat "$ROOT/build-deb.log"; exit 1; }
DEB=$ROOT/s/usr/share/olympium/samples/desk-note.deb
out=$(./dev-install inspect "$DEB")
[ "$(echo "$out" | field action)" = install ] || bad "inspect of the .deb: $out"
./dev-install import --consent "$DEB" >"$ROOT/deb.log" 2>&1
code=$?
id=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "$LIB" 2>/dev/null)
tree=$OLYMPIUM_DESK_HOME/runtimes/linux/$id/root
if [ "$code" -eq 0 ] && [ -n "$id" ] && [ -x "$tree/usr/bin/desk-note" ] && [ -x "$tree/usr/bin/desk-paste" ] &&
		cmp -s "$tree/usr/bin/desk-note" <(dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/bin/desk-note) &&
		grep -q "^program=$tree/usr/bin/desk-note\$" "$ROOT/deb.log" &&
		grep -q "^confinement=$(./dev-install confinement)\$" "$ROOT/deb.log"; then
	ok "the .deb is unpacked confined and its program is byte for byte the one in the package"
else
	bad ".deb install (exit $code, id $id)"
	tail -n 6 "$ROOT/deb.log"
fi

# 2. The sample .rpm.
if command -v rpmbuild >/dev/null 2>&1 && command -v rpm2cpio >/dev/null 2>&1; then
	top=$ROOT/rpm
	mkdir -p "$top/SOURCES" "$top/SPECS" "$top/BUILD" "$top/RPMS" "$top/BUILDROOT"
	cp ../image/desk-rpm "$top/SOURCES/desk-rpm"
	cp ../image/desk-rpm.spec "$top/SPECS/desk-rpm.spec"
	rpmbuild -bb --define "_topdir $top" "$top/SPECS/desk-rpm.spec" >"$ROOT/rpmbuild.log" 2>&1 || { tail -n 5 "$ROOT/rpmbuild.log"; bad "rpmbuild"; }
	RPM=$top/RPMS/noarch/desk-rpm-1.0-1.noarch.rpm
	./dev-install import --consent "$RPM" >"$ROOT/rpm.log" 2>&1
	code=$?
	rid=$(awk -F '\t' '$6=="linux_rpm" {print $1; exit}' "$LIB" 2>/dev/null)
	./dev-install launch "$rid" >"$ROOT/rpm-launch.log" 2>&1
	if [ "$code" -eq 0 ] && [ -n "$rid" ] &&
			[ "$(tr -d '\n' < "$OLYMPIUM_DESK_HOME/exchange/desk-rpm.txt" 2>/dev/null)" = desk-rpm-ready ] &&
			grep -q "^confinement=$(./dev-install confinement)\$" "$ROOT/rpm.log"; then
		ok "the .rpm is unpacked confined and its program runs"
	else
		bad ".rpm install (exit $code, id $rid)"
		tail -n 6 "$ROOT/rpm.log" "$ROOT/rpm-launch.log"
	fi
	# Exercise the generated shell wrapper and the desktop-entry parser, not
	# just a textual Exec match. Percent codes and shell quotes have different
	# escaping rules and both must preserve the exact desktop data path.
	if command -v gio >/dev/null 2>&1; then
		old_desk=$OLYMPIUM_DESK_HOME
		export OLYMPIUM_DESK_HOME="$ROOT/desk with 'quotes' and % field"' $cash `tick` \slash "double"'
		mkdir -p "$OLYMPIUM_DESK_HOME"
		./dev-install import --consent "$RPM" >"$ROOT/quoted-import.log" 2>&1
		qid=$(awk -F '\t' '$6=="linux_rpm" {print $1; exit}' "$OLYMPIUM_DESK_HOME/library.tsv" 2>/dev/null)
		gio launch "$OLYMPIUM_DESK_HOME/applications/olympium-$qid.desktop" >"$ROOT/quoted-launch.log" 2>&1
		qcode=$?
		for attempt in {1..20}; do
			[ ! -s "$OLYMPIUM_DESK_HOME/exchange/desk-rpm.txt" ] || break
			sleep .1
		done
		if [ "$qcode" -eq 0 ] && [ "$(cat "$OLYMPIUM_DESK_HOME/exchange/desk-rpm.txt" 2>/dev/null)" = desk-rpm-ready ]; then
			ok "desktop launcher preserves spaces, quotes, percent signs and shell metacharacters"
		else
			bad "desktop launcher with special characters"
			tail -n 6 "$ROOT/quoted-import.log" "$ROOT/quoted-launch.log"
		fi
		export OLYMPIUM_DESK_HOME=$old_desk
	else
		echo "gio is not installed; desktop Exec parsing was not tested"
	fi
else
	echo "rpm, rpm2cpio or rpmbuild is not installed; the .rpm was not tested"
fi

# 2b. An image built without the Olympium runtime. The desktop reads both
# formats itself, and an .rpm installs and runs the same way.
if [ ! -x /usr/bin/olympium ]; then
	norun=$ROOT/desk-norun
	mkdir -p "$norun"
	chmod 755 "$norun"
	out=$(OLYMPIUM_BIN=/nonexistent OLYMPIUM_DESK_HOME=$norun ./dev-install inspect "$DEB")
	[ "$(echo "$out" | field action)" = install ] || bad "without the runtime, inspect of the .deb: $out"
	if [ -n "${RPM:-}" ] && [ -f "$RPM" ]; then
		out=$(OLYMPIUM_BIN=/nonexistent OLYMPIUM_DESK_HOME=$norun ./dev-install inspect "$RPM")
		[ "$(echo "$out" | field action)" = rpm ] || bad "without the runtime, inspect of the .rpm: $out"
		OLYMPIUM_BIN=/nonexistent OLYMPIUM_DESK_HOME=$norun ./dev-install import --consent "$RPM" >"$ROOT/norun-rpm.log" 2>&1
		code=$?
		nrid=$(awk -F '\t' '$6=="linux_rpm" {print $1; exit}' "$norun/library.tsv" 2>/dev/null)
		OLYMPIUM_BIN=/nonexistent OLYMPIUM_DESK_HOME=$norun ./dev-install launch "$nrid" >"$ROOT/norun-rpm-launch.log" 2>&1
		if [ "$code" -eq 0 ] && [ -n "$nrid" ] &&
				[ "$(tr -d '\n' < "$norun/exchange/desk-rpm.txt" 2>/dev/null)" = desk-rpm-ready ]; then
			ok "without the Olympium runtime, the .deb and the .rpm are recognized and the .rpm runs"
		else
			bad "without the runtime, .rpm install (exit $code, id $nrid)"
			tail -n 6 "$ROOT/norun-rpm.log" "$ROOT/norun-rpm-launch.log"
		fi
	else
		ok "without the Olympium runtime, the .deb is recognized"
	fi
else
	echo "without the Olympium runtime: /usr/bin/olympium is installed here; not tested"
fi

# 3. Packages built to misbehave. A .deb is an ar archive of three files, and
# the third is a tar archive, so both are written here by hand.
python3 - "$ROOT" <<'PYEOF'
import gzip, io, os, sys, tarfile, time

root = sys.argv[1]
escape = os.path.join(root, "escape")

def member(name, data=None, mode=0o644, kind=tarfile.REGTYPE, link="", major=0, minor=0):
    info = tarfile.TarInfo(name)
    info.mode = mode
    info.type = kind
    info.linkname = link
    info.devmajor, info.devminor = major, minor
    info.mtime = 1794787200
    info.size = len(data) if data is not None else 0
    return info, data

def deb(path, package, members):
    control = ("Package: %s\nVersion: 1.0\nArchitecture: all\nMaintainer: Test <test@localhost>\n"
               "Description: %s\n A package built to misbehave.\n" % (package, package)).encode()
    def tar(entries):
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w", format=tarfile.GNU_FORMAT) as out:
            for info, data in entries:
                out.addfile(info, io.BytesIO(data) if data is not None else None)
        return gzip.compress(buf.getvalue(), mtime=0)
    parts = [("debian-binary", b"2.0\n"),
             ("control.tar.gz", tar([member("./control", control)])),
             ("data.tar.gz", tar(members))]
    with open(path, "wb") as out:
        out.write(b"!<arch>\n")
        for name, data in parts:
            out.write(("%-16s%-12d%-6d%-6d%-8s%-10d`\n" % (name, 0, 0, 0, "100644", len(data))).encode())
            out.write(data)
            if len(data) % 2:
                out.write(b"\n")

desktop = b"[Desktop Entry]\nType=Application\nName=Tool\nExec=/usr/bin/tool\nTerminal=false\n"
script = b"#!/bin/sh\nid -u\n"
common = [member("./usr", kind=tarfile.DIRTYPE, mode=0o755),
          member("./usr/bin", kind=tarfile.DIRTYPE, mode=0o755),
          member("./usr/share", kind=tarfile.DIRTYPE, mode=0o755),
          member("./usr/share/applications", kind=tarfile.DIRTYPE, mode=0o755),
          member("./usr/share/applications/tool.desktop", desktop)]

# Set-user-ID and set-group-ID programs, and a world-writable sticky folder.
deb(os.path.join(root, "setid.deb"), "setid-tool", common + [
    member("./usr/bin/tool", script, mode=0o6755),
    member("./usr/share/open", kind=tarfile.DIRTYPE, mode=0o1777),
])

# A device node.
deb(os.path.join(root, "device.deb"), "device-tool", common + [
    member("./usr/bin/tool", script, mode=0o755),
    member("./usr/share/null", kind=tarfile.CHRTYPE, mode=0o666, major=1, minor=3),
])

# A link out, a file to be written through it, a path of ../, an absolute path.
deb(os.path.join(root, "escape.deb"), "escape-tool", common + [
    member("./usr/bin/tool", script, mode=0o755),
    member("./usr/share/way-out", kind=tarfile.SYMTYPE, link=escape, mode=0o777),
    member("./usr/share/way-out/planted.txt", b"through a link\n"),
    member("../../../../../../../.." + escape + "/dotdot.txt", b"dot dot\n"),
    member(escape + "/absolute.txt", b"absolute\n"),
])
PYEOF

reset_desk
./dev-install import --consent "$ROOT/setid.deb" >"$ROOT/setid.log" 2>&1
code=$?
sid=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "$LIB" 2>/dev/null)
tree=$OLYMPIUM_DESK_HOME/runtimes/linux/$sid/root
if [ "$code" -eq 0 ] && [ -x "$tree/usr/bin/tool" ] &&
		[ -z "$(find "$tree" -perm /7000)" ] && [ "$(stat -c %a "$tree/usr/bin/tool")" = 755 ]; then
	ok "a package's set-user-ID and set-group-ID bits are cleared"
else
	bad "set-ID package (exit $code): $(find "$tree" -perm /7000 2>/dev/null)"
	tail -n 4 "$ROOT/setid.log"
fi

reset_desk
./dev-install import --consent "$ROOT/device.deb" >"$ROOT/device.log" 2>&1
code=$?
if [ -z "$(find "$OLYMPIUM_DESK_HOME" \( -type c -o -type b -o -type p -o -type s \) 2>/dev/null)" ]; then
	ok "a package with a device in it leaves no device behind (exit $code)"
else
	bad "a device was created: $(find "$OLYMPIUM_DESK_HOME" -type c -o -type b)"
fi

reset_desk
./dev-install import --consent "$ROOT/escape.deb" >"$ROOT/escape.log" 2>&1
code=$?
if [ -z "$(ls -A "$ROOT/escape")" ]; then
	ok "a package that tries to write outside its folder writes nothing there (exit $code)"
else
	bad "a package escaped: $(ls -A "$ROOT/escape")"
fi

# 4. The fallback confinement.
if [ "$(id -u)" -eq 0 ]; then
	reset_desk
	OLYMPIUM_CONFINE=nobody ./dev-install import --consent "$DEB" >"$ROOT/nobody.log" 2>&1
	code=$?
	id=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "$LIB" 2>/dev/null)
	tree=$OLYMPIUM_DESK_HOME/runtimes/linux/$id/root
	if [ "$code" -eq 0 ] && grep -q '^confinement=nobody$' "$ROOT/nobody.log" && [ -x "$tree/usr/bin/desk-note" ] &&
			[ "$(stat -c %u "$tree/usr/bin/desk-note")" = 0 ]; then
		ok "unpacked as the user nobody, the .deb installs the same and ends up owned by this user"
	else
		bad ".deb as nobody (exit $code)"
		tail -n 4 "$ROOT/nobody.log"
	fi
fi

if [ "$fail" -ne 0 ]; then
	echo TEST_PACKAGES_FAILED
	exit 1
fi
echo TEST_PACKAGES_OK
