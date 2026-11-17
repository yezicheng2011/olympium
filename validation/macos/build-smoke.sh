#!/bin/sh
# Build the independent Cocoa probe against Darling's open-source libraries.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 DARWIN_LD DARLING_SYSROOT OUTPUT_APP" >&2
    exit 2
fi
darwin_ld=$1
sysroot=$2
app=$3
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$app/Contents/MacOS"
object=$(mktemp)
trap 'rm -f "$object"' EXIT
"${CLANG:-clang}" -target x86_64-apple-macos10.13 -O2 -c "$src/hello-gui.c" -o "$object"
# ld64 otherwise defaults the SDK metadata to 10.6. That activates legacy
# Objective-C raw-isa compatibility and crashes this modern Darling runtime.
"$darwin_ld" -arch x86_64 -macosx_version_min 10.13 -sdk_version 10.15 \
    -syslibroot "$sysroot" -o "$app/Contents/MacOS/HelloGui" "$object" \
    -lobjc -lSystem -framework AppKit -framework Foundation
chmod 755 "$app/Contents/MacOS/HelloGui"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.olympium.validation.cocoa</string>
<key>CFBundleName</key><string>Olympium Cocoa Test</string>
<key>CFBundleExecutable</key><string>HelloGui</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
sha256sum "$app/Contents/MacOS/HelloGui"
