#!/bin/sh
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
export GDK_BACKEND=wayland
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/dev-desktop}"
# The color-field demo stays installed and is not opened at startup.
# Boot tests pass these flags. A normal boot does not.
if grep -q 'olympium.openfiles=1' /proc/cmdline 2>/dev/null; then
	(sleep 12; /usr/bin/dev-files / >/dev/null 2>&1) &
fi
if grep -q 'olympium.opensettings=1' /proc/cmdline 2>/dev/null; then
	(sleep 12; /usr/bin/dev-settings >/dev/null 2>&1) &
fi
if grep -q 'olympium.openterm=1' /proc/cmdline 2>/dev/null; then
	(sleep 12; /usr/bin/foot >/dev/null 2>&1) &
fi
if grep -q 'olympium.openruntime=1' /proc/cmdline 2>/dev/null; then
	(sleep 20; /usr/libexec/dev-desktop/olympium-launch >/dev/null 2>&1) &
fi
install_disk=$(sed -n 's/.*olympium.install=\([^ ]*\).*/\1/p' /proc/cmdline 2>/dev/null || true)
if [ -n "$install_disk" ]; then
	# An unattended install for a boot test. The result line reaches the session log.
	if /usr/libexec/dev-desktop/install-to-disk --yes "$install_disk" >/tmp/install.log 2>&1; then
		echo "install-to-disk result: ok on $install_disk"
	else
		echo "install-to-disk result: failed on $install_disk"
		tail -n 5 /tmp/install.log
	fi
fi
