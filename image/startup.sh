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
if grep -q 'olympium.open32=1' /proc/cmdline 2>/dev/null; then
	(sleep 14; /usr/bin/foot -T "32-bit Hello" -e /usr/bin/olympium-hello32 >/tmp/hello32.log 2>&1) &
fi
if grep -q 'olympium.open64=1' /proc/cmdline 2>/dev/null && [ -x /usr/bin/olympium-64demo ]; then
	(sleep 18; /usr/bin/foot -T "64-bit programs" -e /usr/bin/olympium-64demo >/tmp/hello64.log 2>&1) &
fi
if grep -q 'olympium.opentest=1' /proc/cmdline 2>/dev/null; then
	(sleep 12; /usr/bin/olympium-testapp >/tmp/testapp.log 2>&1) &
fi
if grep -q -e 'olympium.showinstall=1' -e 'olympium.importsample=1' \
		-e 'olympium.showwine=1' -e 'olympium.importwine=1' -e 'olympium.importmac=1' \
		/proc/cmdline 2>/dev/null; then
	(sleep 8; /usr/libexec/dev-desktop/install-sample.sh >/tmp/install-sample.log 2>&1) &
fi
if grep -q 'olympium.openbrowser=1' /proc/cmdline 2>/dev/null; then
	(sleep 10; /usr/bin/dev-settings --page=appearance >/dev/null 2>&1) &
	(
		sleep 16
		/usr/libexec/dev-desktop/firefox-launch /usr/share/olympium/browser-home.html >/tmp/firefox.log 2>&1
		echo "firefox: exit $? (log follows)"
		tail -n 20 /tmp/firefox.log 2>/dev/null || true
	) &
fi
# First launch: an account and the administrator password are asked for until
# they have been set. A boot test that opens or imports something
# (olympium.open*, show*, import*, install) is not asked, unless it also
# passes olympium.setup=1. Options a person chooses at the boot menu, such as
# olympium.contrast=1, do not skip it.
if [ ! -e /var/lib/olympium/setup-done ] && [ -x /usr/bin/dev-settings ]; then
	if grep -q 'olympium.setup=1' /proc/cmdline 2>/dev/null ||
			! grep -q -E 'olympium\.(open|show|import|install)[a-z0-9]*=' /proc/cmdline 2>/dev/null; then
		(/usr/bin/dev-settings --first-run 2>&1 | sed 's/^/session: /') &
	fi
fi
# Prewarm is off unless the user turned it on with "dev-install prewarm on".
# While it is off this starts nothing and exits at once.
if [ -x /usr/bin/dev-install ]; then
	if grep -q 'olympium.prewarm=1' /proc/cmdline 2>/dev/null; then
		/usr/bin/dev-install prewarm on >/dev/null 2>&1
	fi
	(/usr/bin/dev-install prewarm run 2>&1 | sed 's/^/session: /') &
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
