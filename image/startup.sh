#!/bin/sh
# Copyright 2026 Development Desktop contributors
# SPDX-License-Identifier: Apache-2.0
export GDK_BACKEND=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/dev-desktop}"
if [ -x /usr/bin/dev-swatch ]; then
	/usr/bin/dev-swatch &
fi
