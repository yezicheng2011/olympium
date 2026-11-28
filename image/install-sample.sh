#!/bin/sh
# Boot-test helper. A normal boot does not run this. showinstall only opens
# the review. importsample is the explicit consent for this disposable boot.
# It does not run a package the user merely inspected.
kmsg() {
	printf '<6>dev-desktop: %s\n' "$*" >/dev/kmsg 2>/dev/null || true
	printf 'dev-desktop: %s\n' "$*" >/dev/console 2>/dev/null || true
	printf '%s\n' "$*" >>/run/dev-desktop/compositor.log 2>/dev/null || true
}
DESK=${OLYMPIUM_DESK_HOME:-/var/lib/olympium/desk}
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
	if ! grep -q -e 'olympium.importwine=1' -e 'olympium.importmac=1' /proc/cmdline 2>/dev/null; then
		exit 0
	fi
else
sleep 16
kmsg "import sample with consent"
/usr/bin/dev-install import --consent "$DEB" >/tmp/import-sample.log 2>&1
kmsg "import exit $?"
id=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "$DESK/library.tsv")
if [ -z "$id" ]; then
	kmsg "import produced no id"
	exit 1
fi
kmsg "imported $id"
/usr/bin/dev-install open "$NOTE" >/tmp/note-cold.log 2>&1 &
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	if [ -s "$DESK/cold-app-ms" ]; then
		break
	fi
	sleep 1
done
cold=$(cat "$DESK/cold-app-ms" 2>/dev/null || echo missing)
kmsg "cold_app_ms $cold"
runtime_line=$(grep '^runtime_state ' /tmp/note-cold.log | head -1)
runtime_ms=$(grep '^runtime_ms ' /tmp/note-cold.log | head -1)
kmsg "${runtime_line:-runtime_state missing}"
kmsg "${runtime_ms:-runtime_ms missing}"
/usr/bin/dev-install launch "$id" >/tmp/note-warm.log 2>&1 &
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
	if [ -s "$DESK/warm-app-ms" ]; then
		break
	fi
	sleep 1
done
warm=$(cat "$DESK/warm-app-ms" 2>/dev/null || echo missing)
kmsg "warm_app_ms $warm"
warm_state=$(grep '^runtime_state ' /tmp/note-warm.log | head -1)
warm_ms=$(grep '^runtime_ms ' /tmp/note-warm.log | head -1)
kmsg "second ${warm_state:-runtime_state missing}"
kmsg "second ${warm_ms:-runtime_ms missing}"
paste=$(find "$DESK/runtimes" -type f -name desk-paste | head -1)
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
if [ -s "$DESK/library.tsv" ]; then
	kmsg "library persisted"
fi
# Leave the application windows visible before Launchpad covers them.
sleep 8
printf 'Desk\n' >/run/dev-desktop/launchpad-filter
: > /run/dev-desktop/open-launchpad
kmsg "asked launchpad to open"
fi
# A machine without KVM emulates every instruction, and everything below takes
# many times longer there. olympium.waitscale=N stretches the waits to match.
scale=$(sed -n 's/.*olympium\.waitscale=\([0-9][0-9]*\).*/\1/p' /proc/cmdline 2>/dev/null)
scale=${scale:-1}
DESK=${OLYMPIUM_DESK_HOME:-/var/lib/olympium/desk}
COMP=/run/dev-desktop/compositor.log
uptime_ms() {
	awk '{ printf "%d", $1 * 1000 }' /proc/uptime
}
# wait_mapped TITLE COUNT_BEFORE: returns once that many more have mapped, or
# fails after a minute, times the scale.
wait_mapped() {
	deadline=$(( $(uptime_ms) + 60000 * scale ))
	while [ "$(uptime_ms)" -lt "$deadline" ]; do
		have=$(grep -c "window mapped: $1" "$COMP" 2>/dev/null)
		if [ "${have:-0}" -gt "${2:-0}" ]; then
			return 0
		fi
		sleep 0.01
	done
	return 1
}
# time_wine LABEL MARKS ID [ARG...]: one launch, timed from the request to the
# mapped window on the kernel's uptime clock.
time_wine() {
	label=$1
	marks=$2
	shift 2
	was=$(grep -c 'window mapped: Desk Win' "$COMP" 2>/dev/null)
	rm -f "$marks"
	/usr/bin/dev-install launch "$@" >/tmp/wine-launch.log 2>&1 &
	if wait_mapped 'Desk Win' "$was"; then
		at=$(uptime_ms)
		from=$(awk '$1=="launch_start_ms" {print $2}' "$marks" 2>/dev/null)
		kmsg "${label}_request_to_window_ms $((at - ${from:-at}))"
	else
		kmsg "${label}_request_to_window_ms missing"
		kmsg "${label} launch said: $(tail -n 2 /tmp/wine-launch.log 2>/dev/null | tr '\n' ' ')"
	fi
	kmsg "${label}_prefix $(awk '$1=="wine_prefix" {print $2}' "$marks" 2>/dev/null)"
	kmsg "${label}_prefix_ms $(awk '$1=="wine_prefix_ms" {print $2}' "$marks" 2>/dev/null)"
	kmsg "${label}_server_ms $(awk '$1=="wine_server_ms" {print $2}' "$marks" 2>/dev/null)"
}
if grep -q 'olympium.importwine=1' /proc/cmdline 2>/dev/null && [ -f "$EXE" ]; then
	# This dwell is for the review screenshot. It is not part of any launch.
	sleep 14
	kmsg "import exe with consent"
	/usr/bin/dev-install import --consent "$EXE" >/tmp/import-wine.log 2>&1
	kmsg "wine import exit $?"
	wid=$(awk -F '\t' '$6=="windows_exe" {print $1; exit}' "$DESK/library.tsv")
	if [ -z "$wid" ]; then
		kmsg "wine import produced no id"
	else
		kmsg "imported $wid"
		kmsg "wine_prefix_in_image $([ -s "$DESK/wine/system.reg" ] && echo yes || echo no)"
		# The first launch on this boot. Its window stays for the picture.
		time_wine wine "$DESK/wine-launch-marks" "$wid"
		n=0
		while [ "$n" -lt $((30 * scale)) ] && [ ! -s "$DESK/wine-cold-ms" ]; do
			sleep 1
			n=$((n + 1))
		done
		kmsg "wine_cold_ms $(cat "$DESK/wine-cold-ms" 2>/dev/null || echo missing)"
		kmsg "wine_exchange $(tr -d '\n' < "$DESK/exchange/desk-win.txt" 2>/dev/null || echo missing)"
		# A second launch while Wine is still up from the first.
		time_wine wine_second "$DESK/wine-launch-marks" "$wid" --once
		if grep -q 'olympium.winefresh=1' /proc/cmdline 2>/dev/null; then
			# What every first launch used to cost: a prefix Wine has to
			# build. It gets its own directory, so the one the image carries
			# is left alone, and it is told there is nothing to copy from.
			fresh=/var/lib/olympium/wine-fresh
			rm -rf "$fresh"
			OLYMPIUM_WINE_PREFIX=$fresh OLYMPIUM_WINE_TEMPLATE=/nonexistent \
				time_wine wine_fresh "$DESK/wine-launch-marks" "$wid" --once
			kmsg "wine_fresh_prefix_size $(du -sm "$fresh" 2>/dev/null | cut -f1) MB"
			WINEPREFIX=$fresh wineserver -k 2>/dev/null
			rm -rf "$fresh"
		fi
	fi
	RPM=/usr/share/olympium/samples/desk-rpm.rpm
	if [ -f "$RPM" ]; then
		/usr/bin/dev-install import --consent "$RPM" >/tmp/import-rpm.log 2>&1
		rid=$(awk -F '\t' '$6=="linux_rpm" {print $1; exit}' "$DESK/library.tsv")
		if [ -n "$rid" ]; then
			/usr/bin/dev-install launch "$rid" >/tmp/rpm-launch.log 2>&1
			kmsg "rpm_exchange $(tr -d '\n' < "$DESK/exchange/desk-rpm.txt" 2>/dev/null || echo missing)"
			kmsg "rpm_$(grep '^confinement=' /tmp/import-rpm.log | head -1)"
		else
			kmsg "rpm import produced no id"
		fi
	fi
fi
# Mac files: a disk image and a package are installed, and, when the image
# carries a Mac runtime, a real Mach-O program from a disk image is started.
if grep -q 'olympium.importmac=1' /proc/cmdline 2>/dev/null; then
	S=/usr/share/olympium/samples
	kmsg "mac confinement $(/usr/bin/dev-install confinement)"
	for file in DeskMac.dmg DeskMac.pkg; do
		if [ -f "$S/$file" ]; then
			/usr/bin/dev-install import --consent "$S/$file" >/tmp/import-mac.log 2>&1
			kmsg "mac $file import exit $? $(grep -E '^(app|archs|runs_on_this_cpu)=' /tmp/import-mac.log | tr '\n' ' ')"
		fi
	done
	mid=$(awk -F '\t' '$2=="Desk Mac" && $7=="mac" {print $1; exit}' "$DESK/library.tsv")
	kmsg "mac icon $(sed -n 's/^Icon=//p' "$DESK/applications/olympium-$mid.desktop" 2>/dev/null | head -1)"
	kmsg "mac runtime $(command -v darling 2>/dev/null || echo none)"
	if [ -f "$S/DeskEcho.dmg" ] && command -v darling >/dev/null 2>&1; then
		/usr/bin/dev-install import --consent "$S/DeskEcho.dmg" >/tmp/import-echo.log 2>&1
		eid=$(awk -F '\t' '$2=="Desk Echo" && $7=="mac" {print $1; exit}' "$DESK/library.tsv")
		from=$(uptime_ms)
		said=$(HOME=${HOME:-/root} OLYMPIUM_NO_DIALOG=1 /usr/bin/dev-install launch "$eid" "said on the booted system" 2>/tmp/mac-launch.err | tail -n 1)
		kmsg "mac_program_said [$said] after $(( $(uptime_ms) - from )) ms"
		# cf fa ed fe is how a 64-bit Mach-O file starts. 7f 45 4c 46 would be
		# this system's own kind of program.
		kmsg "mac_program_magic$(od -An -tx1 -N4 "$DESK/mac/Applications/Desk Echo.app/Contents/MacOS/Desk Echo" 2>/dev/null)"
	fi
	# With no runtime for it, opening the application shows the window that
	# says so. It stays for the picture.
	if [ -n "$mid" ] && ! command -v darling >/dev/null 2>&1; then
		was=$(grep -c 'window mapped: Desk Mac' "$COMP" 2>/dev/null)
		/usr/bin/dev-install launch "$mid" >/tmp/mac-launch.log 2>&1 &
		wait_mapped 'Desk Mac' "$was" && kmsg "mac refusal window mapped"
	fi
fi
if grep -q -e 'olympium.importwine=1' -e 'olympium.importmac=1' /proc/cmdline 2>/dev/null; then
	sleep 12
	printf 'Desk\n' >/run/dev-desktop/launchpad-filter
	: > /run/dev-desktop/open-launchpad
	kmsg "asked launchpad to open"
fi
kmsg "install-sample done"
exit 0
