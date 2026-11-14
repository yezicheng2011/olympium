#!/bin/sh
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
export GDK_BACKEND=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/dev-desktop}"
if [ -x /usr/bin/dev-swatch ]; then
	/usr/bin/dev-swatch &
fi
