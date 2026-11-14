#!/bin/sh
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
export GDK_BACKEND=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/dev-desktop}"
# The color-field demo stays installed and is not opened at startup.
# A boot test can pass olympium.openfiles=1 to open Files after the empty desktop.
if grep -q 'olympium.openfiles=1' /proc/cmdline 2>/dev/null; then
	(sleep 8; /usr/bin/dev-files >/dev/null 2>&1) &
fi
