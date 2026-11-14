#!/bin/sh
# Copyright 2026 Development Desktop contributors
# License: daniel.ye 源码许可证（第 1.0 版）。全文见 LICENSE。
export GDK_BACKEND=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/dev-desktop}"
if [ -x /usr/bin/dev-swatch ]; then
	/usr/bin/dev-swatch &
fi
