#!/bin/sh
# Build the Desk Note package with the compiler of the system this script
# runs on. The result is a real .deb. It is extracted for one user. It has
# no maintainer scripts.
set -eu
ROOT=${1:-}
SRC=${2:-/tmp/desktop}
if [ -z "$ROOT" ]; then
	echo "usage: $0 ROOT [source-dir]" >&2
	exit 1
fi
ARCH=$(dpkg --print-architecture)
GTK=libgtk-3-0
if dpkg-query -W libgtk-3-0t64 >/dev/null 2>&1; then
	GTK=libgtk-3-0t64
fi
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/bin" "$STAGE/usr/share/applications"
cc -O2 -Wall -o "$STAGE/usr/bin/desk-note" "$SRC/desk-note.c" $(pkg-config --cflags --libs gtk+-3.0)
cc -O2 -Wall -o "$STAGE/usr/bin/desk-paste" "$SRC/desk-paste.c" $(pkg-config --cflags --libs gtk+-3.0)
cat > "$STAGE/usr/share/applications/desk-note.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Desk Note
Exec=/usr/bin/desk-note
Terminal=false
X-Olympium-Extension=dnote
EOF
cat > "$STAGE/usr/share/applications/desk-paste.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Desk Paste
Exec=/usr/bin/desk-paste
Terminal=false
EOF
cat > "$STAGE/DEBIAN/control" <<EOF
Package: desk-note
Version: 1.0
Architecture: $ARCH
Maintainer: Olympium Desktop <desktop@localhost>
Depends: libc6, $GTK
Description: Desk Note
 A Linux window used to test package installation on this desktop.
EOF
mkdir -p "$ROOT/usr/share/olympium/samples"
dpkg-deb --root-owner-group --build "$STAGE" "$ROOT/usr/share/olympium/samples/desk-note.deb"
dpkg-deb -I "$ROOT/usr/share/olympium/samples/desk-note.deb"
