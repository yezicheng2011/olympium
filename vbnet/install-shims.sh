#!/bin/bash
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
#
# Installs the shared VB.NET (.NET 8) Olympium assembly once under
# /usr/lib/olympium/vbnet and creates tiny launcher shims for every desktop
# application and system tool, minimizing ISO footprint while keeping a
# single shared .NET 8 runtime.
set -euo pipefail

ROOT=${1:?target rootfs directory}
VBNET_OUT=${2:?built vbnet/out directory}
LIB_DEST="$ROOT/usr/lib/olympium/vbnet"

install -d "$LIB_DEST" "$ROOT/usr/bin" "$ROOT/usr/libexec/dev-desktop"
install -m 644 "$VBNET_OUT/Olympium.dll" "$VBNET_OUT/Olympium.runtimeconfig.json" "$LIB_DEST/"
if [ -f "$VBNET_OUT/Olympium.deps.json" ]; then
	install -m 644 "$VBNET_OUT/Olympium.deps.json" "$LIB_DEST/"
fi

make_shim() {
	local dest=$1
	local app=$2
	cat > "$dest" <<EOF
#!/bin/sh
export OLYMPIUM_APP=$app
export LD_LIBRARY_PATH="/usr/lib/olympium:/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
exec /usr/bin/dotnet /usr/lib/olympium/vbnet/Olympium.dll "\$@"
EOF
	chmod 755 "$dest"
}

for app in dev-settings dev-files dev-install dev-swatch dev-compat olympium-testapp \
		driver-report driver-install olympium-activate install-to-disk olympium-interop; do
	make_shim "$ROOT/usr/bin/$app" "$app"
done

for app in run-session olympium-launch install-to-disk olympium-mac-run guest-fetch \
		driver-report driver-install firefox-launch olympium-android olympium-openharmony \
		olympium-wine-exec olympium-interop Xwayland-software; do
	make_shim "$ROOT/usr/libexec/dev-desktop/$app" "$app"
done
