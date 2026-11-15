#!/bin/sh
# Boot-test helper. A normal boot does not run this. showinstall only opens
# the review. importsample is the explicit consent for this disposable boot.
# It does not run a package the user merely inspected.
kmsg() {
	printf '<6>dev-desktop: %s\n' "$*" >/dev/kmsg 2>/dev/null || true
	printf 'dev-desktop: %s\n' "$*" >/dev/console 2>/dev/null || true
	printf '%s\n' "$*" >>/run/dev-desktop/compositor.log 2>/dev/null || true
}
DEB=/usr/share/olympium/samples/desk-note.deb
NOTE=/usr/share/olympium/samples/hello.dnote
if [ ! -f "$DEB" ]; then
	kmsg "sample package missing"
	exit 1
fi
if grep -q 'olympium.showinstall=1' /proc/cmdline 2>/dev/null; then
	/usr/bin/dev-install open "$DEB" >/tmp/install-window.log 2>&1 &
	kmsg "install window opened; package not run"
fi
EXE=/usr/share/olympium/samples/desk-win.exe
if grep -q 'olympium.showwine=1' /proc/cmdline 2>/dev/null && [ -f "$EXE" ]; then
	/usr/bin/dev-install open "$EXE" >/tmp/wine-window.log 2>&1 &
	kmsg "wine install window opened; program not run"
fi
if ! grep -q 'olympium.importsample=1' /proc/cmdline 2>/dev/null; then
	if ! grep -q 'olympium.importwine=1' /proc/cmdline 2>/dev/null; then
		exit 0
	fi
else
sleep 16
kmsg "import sample with consent"
/usr/bin/dev-install import --consent "$DEB" >/tmp/import-sample.log 2>&1
kmsg "import exit $?"
id=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "${OLYMPIUM_DESK_HOME:-/var/lib/olympium/desk}/library.tsv")
if [ -z "$id" ]; then
	kmsg "import produced no id"
	exit 1
fi
kmsg "imported $id"
/usr/bin/dev-install open "$NOTE" >/tmp/note-cold.log 2>&1 &
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	if [ -s "${OLYMPIUM_DESK_HOME}/cold-app-ms" ]; then
		break
	fi
	sleep 1
done
cold=$(cat "${OLYMPIUM_DESK_HOME}/cold-app-ms" 2>/dev/null || echo missing)
kmsg "cold_app_ms $cold"
runtime_line=$(grep '^runtime_state ' /tmp/note-cold.log | head -1)
runtime_ms=$(grep '^runtime_ms ' /tmp/note-cold.log | head -1)
kmsg "${runtime_line:-runtime_state missing}"
kmsg "${runtime_ms:-runtime_ms missing}"
/usr/bin/dev-install launch "$id" >/tmp/note-warm.log 2>&1 &
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
	if [ -s "${OLYMPIUM_DESK_HOME}/warm-app-ms" ]; then
		break
	fi
	sleep 1
done
warm=$(cat "${OLYMPIUM_DESK_HOME}/warm-app-ms" 2>/dev/null || echo missing)
kmsg "warm_app_ms $warm"
warm_state=$(grep '^runtime_state ' /tmp/note-warm.log | head -1)
warm_ms=$(grep '^runtime_ms ' /tmp/note-warm.log | head -1)
kmsg "second ${warm_state:-runtime_state missing}"
kmsg "second ${warm_ms:-runtime_ms missing}"
paste=$(find "${OLYMPIUM_DESK_HOME}/runtimes" -type f -name desk-paste | head -1)
if [ -n "$paste" ]; then
	"$paste" >/tmp/desk-paste.log 2>&1 &
	kmsg "paste started"
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
		if grep -q '^clipboard:' /tmp/desk-paste.log 2>/dev/null; then
			break
		fi
		sleep 1
	done
	clip_line=$(grep '^clipboard:' /tmp/desk-paste.log 2>/dev/null | head -1)
	file_line=$(grep '^exchange:' /tmp/desk-paste.log 2>/dev/null | head -1)
	kmsg "paste ${clip_line:-clipboard: missing}"
	kmsg "paste ${file_line:-exchange: missing}"
fi
if [ -s "${OLYMPIUM_DESK_HOME}/library.tsv" ]; then
	kmsg "library persisted"
fi
# Leave the application windows visible before Launchpad covers them.
sleep 8
printf 'Desk\n' >/run/dev-desktop/launchpad-filter
: > /run/dev-desktop/open-launchpad
kmsg "asked launchpad to open"
fi
if grep -q 'olympium.importwine=1' /proc/cmdline 2>/dev/null && [ -f "$EXE" ]; then
	sleep 14
	kmsg "import exe with consent"
	/usr/bin/dev-install import --consent "$EXE" >/tmp/import-wine.log 2>&1
	kmsg "wine import exit $?"
	wid=$(awk -F '\t' '$6=="windows_exe" {print $1; exit}' "${OLYMPIUM_DESK_HOME:-/var/lib/olympium/desk}/library.tsv")
	if [ -z "$wid" ]; then
		kmsg "wine import produced no id"
	else
		kmsg "imported $wid"
		# The dwell before this point is for the review screenshot. It is
		# not part of the launch. The launch itself is timed from the
		# request to the mapped window, on the kernel's uptime clock.
		was=$(grep -c 'window mapped: Desk Win' /run/dev-desktop/compositor.log 2>/dev/null)
		/usr/bin/dev-install launch "$wid" >/tmp/wine-launch.log 2>&1 &
		i=0
		while [ "$i" -lt 6000 ]; do
			now=$(grep -c 'window mapped: Desk Win' /run/dev-desktop/compositor.log 2>/dev/null)
			if [ "${now:-0}" -gt "${was:-0}" ]; then
				break
			fi
			sleep 0.01
			i=$((i + 1))
		done
		at=$(awk '{ printf "%d", $1 * 1000 }' /proc/uptime)
		marks="${OLYMPIUM_DESK_HOME}/wine-launch-marks"
		from=$(awk '$1=="launch_start_ms" {print $2}' "$marks" 2>/dev/null)
		if [ -n "$from" ] && [ "${now:-0}" -gt "${was:-0}" ]; then
			kmsg "wine_request_to_window_ms $((at - from))"
		else
			kmsg "wine_request_to_window_ms missing"
		fi
		kmsg "wine_prefix $(awk '$1=="wine_prefix" {print $2}' "$marks" 2>/dev/null)"
		kmsg "wine_prefix_ms $(awk '$1=="wine_prefix_ms" {print $2}' "$marks" 2>/dev/null)"
		kmsg "wine_server_ms $(awk '$1=="wine_server_ms" {print $2}' "$marks" 2>/dev/null)"
		for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
			if [ -s "${OLYMPIUM_DESK_HOME}/wine-cold-ms" ]; then
				break
			fi
			sleep 1
		done
		kmsg "wine_cold_ms $(cat "${OLYMPIUM_DESK_HOME}/wine-cold-ms" 2>/dev/null || echo missing)"
		kmsg "wine_exchange $(tr -d '\n' < "${OLYMPIUM_DESK_HOME}/exchange/desk-win.txt" 2>/dev/null || echo missing)"
	fi
	RPM=/usr/share/olympium/samples/desk-rpm.rpm
	if [ -f "$RPM" ]; then
		/usr/bin/dev-install import --consent "$RPM" >/tmp/import-rpm.log 2>&1
		rid=$(awk -F '\t' '$6=="linux_rpm" {print $1; exit}' "${OLYMPIUM_DESK_HOME}/library.tsv")
		if [ -n "$rid" ]; then
			/usr/bin/dev-install launch "$rid" >/tmp/rpm-launch.log 2>&1
			kmsg "rpm_exchange $(tr -d '\n' < "${OLYMPIUM_DESK_HOME}/exchange/desk-rpm.txt" 2>/dev/null || echo missing)"
		else
			kmsg "rpm import produced no id"
		fi
	fi
	sleep 12
	printf 'Desk\n' >/run/dev-desktop/launchpad-filter
	: > /run/dev-desktop/open-launchpad
	kmsg "asked launchpad to open"
fi
exit 0
