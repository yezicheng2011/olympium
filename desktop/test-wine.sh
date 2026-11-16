#!/bin/bash
# Copyright 2026 Development Desktop contributors
# License: daniel.ye Source License, Version 1.0. See LICENSE.
#
# Runs the compositor without a screen and launches a real Windows program
# through dev-install and Wine, three ways: Wine builds the prefix itself, the
# prefix is copied from a prebuilt one, and the prefix already exists. It
# prints the time from the launch request to the mapped window for each, and
# then copies text in both directions between a Windows program and a
# Wayland program.
#
# It then runs a 32-bit Windows program, checks that programs for a CPU this
# system cannot run are refused before Wine starts, and checks the compositor
# built without Xwayland.
#
# Needs wine, Xwayland, and x86_64-w64-mingw32-gcc. The 32-bit part also needs
# wine32 and i686-w64-mingw32-gcc and says so when they are missing. This is a host test on the
# build machine. It is not a boot of the ISO, and its times are not the ISO's.
# The stand-in for the olympium command below answers classify and plan the
# way the pinned runtime does for a PE file when Wine is installed.
set -u
cd "$(dirname "$0")"
for tool in /usr/bin/wine /usr/bin/wineserver Xwayland x86_64-w64-mingw32-gcc; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "test-wine: $tool is not installed; nothing was tested" >&2
		exit 77
	fi
done
make dev-compositor dev-install desk-note desk-paste >/dev/null || exit 1
ROOT=$(mktemp -d /tmp/test-wine.XXXXXX)
trap '[ -n "${TEST_WINE_KEEP:-}" ] && mkdir -p "$TEST_WINE_KEEP" && cp "$ROOT"/*.log "$TEST_WINE_KEEP"/ 2>/dev/null; kill $(cat "$ROOT/comp.pid" 2>/dev/null) 2>/dev/null; WINEPREFIX=$ROOT/desk/wine wineserver -k 2>/dev/null; rm -rf "$ROOT"' EXIT
x86_64-w64-mingw32-gcc -mwindows -O2 -o "$ROOT/desk-win.exe" desk-win.c || exit 1
cat > "$ROOT/olympium" <<'EOF'
#!/bin/sh
case "$2" in
classify) echo '{"ok": true, "data": {"guest": "windows", "kind": "windows_exe", "detail": "PE program"}}' ;;
plan) echo '{"ok": true, "data": {"summary": "Wine", "engine_id": "wine", "launch": {"type": "process", "program": "/usr/bin/wine"}, "surface": {"mode": "host_window", "hides_runtime_chrome": true}}}' ;;
*) echo '{"ok": false, "error": {"message": "stand-in"}}' ;;
esac
EOF
chmod 755 "$ROOT/olympium"
export OLYMPIUM_BIN=$ROOT/olympium
export OLYMPIUM_DESK_HOME=$ROOT/desk
export HOME=$ROOT/home
export XDG_RUNTIME_DIR=$ROOT/run
export WINEDEBUG=-all
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$OLYMPIUM_DESK_HOME" /run/dev-desktop
chmod 700 "$XDG_RUNTIME_DIR"
LOG=$ROOT/compositor.log

cat > "$ROOT/session.sh" <<EOF
#!/bin/sh
env | grep -E '^(DISPLAY|WAYLAND_DISPLAY)=' > "$ROOT/session.env"
EOF
chmod 755 "$ROOT/session.sh"
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 DEV_DESK_HEADLESS=1 \
	./dev-compositor -s "$ROOT/session.sh" >"$LOG" 2>&1 &
echo $! > "$ROOT/comp.pid"
for _ in $(seq 1 100); do
	[ -s "$ROOT/session.env" ] && break
	sleep 0.1
done
if [ ! -s "$ROOT/session.env" ]; then
	echo "test-wine: the compositor did not start a session" >&2
	tail -n 20 "$LOG" >&2
	exit 1
fi
set -a
. "$ROOT/session.env"
set +a
export GDK_BACKEND=wayland
echo "session $(tr '\n' ' ' < "$ROOT/session.env")"

now_ms() {
	awk '{ printf "%d\n", $1 * 1000 }' /proc/uptime
}
mapped_count() {
	grep -c "window mapped: $1" "$LOG"
}
# wait_mapped TITLE COUNT_BEFORE -> ms on the uptime clock, 10 ms steps
wait_mapped() {
	for _ in $(seq 1 6000); do
		if [ "$(mapped_count "$1")" -gt "$2" ]; then
			now_ms
			return 0
		fi
		sleep 0.01
	done
	return 1
}

fail=0
./dev-install import --consent "$ROOT/desk-win.exe" >"$ROOT/import.log" 2>&1 || { cat "$ROOT/import.log"; exit 1; }
id=$(awk -F '\t' '$6=="windows_exe" {print $1; exit}' "$OLYMPIUM_DESK_HOME/library.tsv")
[ -n "$id" ] || { echo "test-wine: no library id"; exit 1; }

launch_timed() {
	local label=$1
	local before
	before=$(mapped_count 'Desk Win')
	./dev-install launch "$id" --once >"$ROOT/launch.log" 2>&1 &
	local pid=$!
	local at
	if ! at=$(wait_mapped 'Desk Win' "$before"); then
		echo "FAIL $label: no window"
		cat "$ROOT/launch.log"
		fail=1
		return
	fi
	local start state prefix_ms server_ms
	start=$(awk '$1=="launch_start_ms" {print $2}' "$OLYMPIUM_DESK_HOME/wine-launch-marks")
	state=$(awk '$1=="wine_prefix" {print $2}' "$OLYMPIUM_DESK_HOME/wine-launch-marks")
	prefix_ms=$(awk '$1=="wine_prefix_ms" {print $2}' "$OLYMPIUM_DESK_HOME/wine-launch-marks")
	server_ms=$(awk '$1=="wine_server_ms" {print $2}' "$OLYMPIUM_DESK_HOME/wine-launch-marks")
	echo "$label: request_to_window_ms $((at - start)) prefix=$state prefix_ms=$prefix_ms server_ms=$server_ms"
	wait "$pid"
	eval "last_state=$state"
}

# 1. No prebuilt prefix. Wine builds one. This is what the 43.3 s boot did.
export OLYMPIUM_WINE_TEMPLATE=$ROOT/none
launch_timed "fresh prefix"
[ "$last_state" = fresh ] || { echo "FAIL expected fresh"; fail=1; }
wineserver_wait() { WINEPREFIX=$OLYMPIUM_DESK_HOME/wine wineserver -k 2>/dev/null; sleep 0.5; }
wineserver_wait
du -sh "$OLYMPIUM_DESK_HOME/wine" | awk '{print "prefix size " $1}'

# 2. The prefix Wine just built becomes the prebuilt one, as the image build does.
mv "$OLYMPIUM_DESK_HOME/wine" "$ROOT/template"
rm -rf "$ROOT/template/drive_c/exchange" "$ROOT/template"/drive_c/wine-*-ms
export OLYMPIUM_WINE_TEMPLATE=$ROOT/template
launch_timed "seeded prefix"
[ "$last_state" = seeded ] || { echo "FAIL expected seeded"; fail=1; }
ls "$OLYMPIUM_DESK_HOME" | grep -q 'wine.seed-' && { echo "FAIL a seed directory was left behind"; fail=1; }

# 3. Same prefix again. wineserver was asked to stay, so this reuses it.
launch_timed "existing prefix, server kept"
[ "$last_state" = existing ] || { echo "FAIL expected existing"; fail=1; }

# 4. An interrupted seed must not look like a prefix.
wineserver_wait
rm -rf "$OLYMPIUM_DESK_HOME/wine"
mkdir -p "$OLYMPIUM_DESK_HOME/wine.seed-1/drive_c"
launch_timed "after an interrupted seed"
[ "$last_state" = seeded ] || { echo "FAIL expected seeded after an interrupted seed"; fail=1; }

# 5. Prewarm is off unless asked for.
out=$(./dev-install prewarm run)
[ "$out" = "prewarm off; nothing started" ] || { echo "FAIL prewarm ran while off: $out"; fail=1; }
./dev-install prewarm on >/dev/null
./dev-install prewarm status | grep -q 'prewarm on' || { echo "FAIL prewarm status"; fail=1; }
wineserver_wait
./dev-install prewarm run | tr '\n' ' '
echo
launch_timed "after prewarm"
./dev-install prewarm off >/dev/null
./dev-install prewarm status | grep -q 'prewarm off' || { echo "FAIL prewarm off"; fail=1; }

# 6. Windows -> Wayland. Desk Win puts "Desk Win ready" on the clipboard and
# stays open. Desk Paste is a GTK Wayland client and reads it.
before=$(mapped_count 'Desk Win')
./dev-install launch "$id" >"$ROOT/win-copy.log" 2>&1 &
copy_pid=$!
wait_mapped 'Desk Win' "$before" >/dev/null || { echo "FAIL copy window"; fail=1; }
sleep 4
mkdir -p "$OLYMPIUM_DESK_HOME/exchange"
timeout 20 ./desk-paste >"$ROOT/paste.log" 2>&1 &
paste_pid=$!
for _ in $(seq 1 150); do
	grep -q '^clipboard:' "$ROOT/paste.log" 2>/dev/null && break
	sleep 0.1
done
got=$(sed -n 's/^clipboard: //p' "$ROOT/paste.log" | head -1)
if [ "$got" = "Desk Win ready" ]; then
	echo "clipboard windows->wayland: ok ($got)"
else
	echo "FAIL clipboard windows->wayland: got '${got:-nothing}'"
	fail=1
fi
kill "$paste_pid" "$copy_pid" 2>/dev/null
wineserver_wait

# 7. Wayland -> Windows. Desk Note owns "Desk Note ready". Desk Win --paste
# reads CF_TEXT and writes what it got.
printf 'note\n' > "$ROOT/hello.dnote"
timeout 40 ./desk-note "$ROOT/hello.dnote" >"$ROOT/note.log" 2>&1 &
note_pid=$!
before_note=$(mapped_count 'Desk Note')
wait_mapped 'Desk Note' "$((before_note > 0 ? before_note - 1 : 0))" >/dev/null
sleep 1
rm -f "$OLYMPIUM_DESK_HOME/exchange/desk-win-paste.txt"
before=$(mapped_count 'Desk Win')
./dev-install launch "$id" --paste >"$ROOT/win-paste.log" 2>&1 &
winpaste_pid=$!
wait_mapped 'Desk Win' "$before" >/dev/null || { echo "FAIL paste window"; fail=1; }
for _ in $(seq 1 100); do
	[ -s "$OLYMPIUM_DESK_HOME/exchange/desk-win-paste.txt" ] && break
	sleep 0.1
done
got=$(tr -d '\r\n' < "$OLYMPIUM_DESK_HOME/exchange/desk-win-paste.txt" 2>/dev/null)
if [ "$got" = "Desk Note ready" ]; then
	echo "clipboard wayland->windows: ok ($got)"
else
	echo "FAIL clipboard wayland->windows: got '${got:-nothing}'"
	fail=1
fi
kill "$note_pid" "$winpaste_pid" 2>/dev/null
wineserver_wait

# 7b. Files across systems. The shared folder is drive S: for a Windows
# program, and a Linux document path reaches a Windows program as Z:\...
ex=$OLYMPIUM_DESK_HOME/exchange
printf 'from linux\n' > "$ex/from-linux.txt"
printf 'doc\n' > "$ROOT/report one.txt"
rm -f "$ex/desk-win-open.txt"
./dev-install launch "$id" "$ROOT/report one.txt" --once >"$ROOT/open.log" 2>&1
got=$(tr -d '\r\n' < "$ex/desk-win-open.txt" 2>/dev/null)
want="opened Z:$(printf '%s' "$ROOT/report one.txt" | tr '/' '\\')"
if [ "$got" = "$want" ]; then
	echo "linux document opened by the windows program: ok"
else
	echo "FAIL linux document: got '${got:-nothing}', want '$want'"
	fail=1
fi
seen=$(WINEPREFIX=$OLYMPIUM_DESK_HOME/wine WINEDLLOVERRIDES="mscoree,mshtml=" \
	wine cmd /c 'type S:\from-linux.txt & echo from windows> S:\from-windows.txt' 2>/dev/null | tr -d '\r\n')
if [ "$seen" = "from linux" ] && [ "$(tr -d '\r\n ' < "$ex/from-windows.txt" 2>/dev/null)" = "fromwindows" ]; then
	echo "shared folder as drive S: ok, both directions"
else
	echo "FAIL shared folder: read '${seen:-nothing}', wrote '$(cat "$ex/from-windows.txt" 2>/dev/null)'"
	fail=1
fi
wineserver_wait

# 8. A 32-bit Windows program. It needs the 32-bit half of Wine.
if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
	i686-w64-mingw32-gcc -mwindows -O2 -o "$ROOT/desk-win32.exe" desk-win.c || exit 1
	action32=$(./dev-install inspect "$ROOT/desk-win32.exe" | sed -n 's/.*"action": "\([a-z]*\)".*/\1/p')
	if [ -x /usr/lib/wine/wine ] || [ -x /usr/lib/i386-linux-gnu/wine/wine ]; then
		[ "$action32" = wine ] || { echo "FAIL 32-bit program not offered: $action32"; fail=1; }
		./dev-install import --consent "$ROOT/desk-win32.exe" >"$ROOT/import32.log" 2>&1
		id64=$id
		id=$(awk -F '\t' -v p="$ROOT/desk-win32.exe" '$3==p {print $1; exit}' "$OLYMPIUM_DESK_HOME/library.tsv")
		if [ -n "$id" ]; then
			launch_timed "32-bit program"
		else
			echo "FAIL 32-bit import produced no id"
			cat "$ROOT/import32.log"
			fail=1
		fi
		id=$id64
		wineserver_wait
	else
		[ "$action32" = review ] || { echo "FAIL 32-bit program offered without wine32: $action32"; fail=1; }
		echo "32-bit program: wine32 is not installed here; refused, as it should be"
	fi
	# The same file on an image without wine32 must be refused, not started.
	out=$(OLYMPIUM_WINE32=0 ./dev-install inspect "$ROOT/desk-win32.exe")
	echo "$out" | grep -q '"action": "review"' && echo "$out" | grep -q 'wine32' ||
		{ echo "FAIL 32-bit program without wine32 was not refused"; fail=1; }
	if OLYMPIUM_WINE32=0 ./dev-install import --consent "$ROOT/desk-win32.exe" >/dev/null 2>&1; then
		echo "FAIL import without wine32 succeeded"
		fail=1
	fi
	echo "32-bit program without wine32: refused"
else
	echo "32-bit program: i686-w64-mingw32-gcc is not installed; not tested"
fi

# 9. A program for a CPU this system does not have is refused before Wine starts.
out=$(OLYMPIUM_HOST_MACHINE=aarch64 ./dev-install inspect "$ROOT/desk-win.exe")
echo "$out" | grep -q '"action": "review"' && echo "$out" | grep -q 'x86 translator' ||
	{ echo "FAIL x86 program on ARM was not refused"; fail=1; }
out=$(OLYMPIUM_HOST_MACHINE=i686 ./dev-install inspect "$ROOT/desk-win.exe")
echo "$out" | grep -q '"action": "review"' ||
	{ echo "FAIL 64-bit program on a 32-bit system was not refused"; fail=1; }
python3 - "$ROOT/desk-win.exe" "$ROOT/desk-arm64.exe" <<'PYEOF'
import struct, sys
data = bytearray(open(sys.argv[1], "rb").read())
off = struct.unpack_from("<I", data, 0x3c)[0]
struct.pack_into("<H", data, off + 4, 0xAA64)
open(sys.argv[2], "wb").write(data)
PYEOF
out=$(./dev-install inspect "$ROOT/desk-arm64.exe")
echo "$out" | grep -q '"action": "review"' && echo "$out" | grep -q '64-bit ARM' ||
	{ echo "FAIL ARM64 program on x86 was not refused"; fail=1; }
echo "wrong-CPU programs: refused"

# 10. No X display: the launch is refused and Wine is not started.
before=$(mapped_count 'Desk Win')
(unset DISPLAY; ./dev-install launch "$id" --once >"$ROOT/nodisplay.log" 2>&1)
rc=$?
sleep 1
if [ "$rc" -ne 3 ] || [ "$(mapped_count 'Desk Win')" -ne "$before" ] || ! grep -q 'no X display' "$ROOT/nodisplay.log"; then
	echo "FAIL launch without an X display: exit $rc"
	fail=1
else
	echo "launch without an X display: refused"
fi

# 11. The compositor built without Xwayland still starts a session.
cc -O2 -DDEV_DESK_NO_XWAYLAND -o "$ROOT/comp-nox" compositor.c blur.c \
	$(pkg-config --cflags --libs wlroots wayland-server cairo pango pangocairo xkbcommon libdrm pixman-1) \
	-DWLR_USE_UNSTABLE -Iprotocol -lm 2>/dev/null || { echo "FAIL build without Xwayland"; fail=1; }
if [ -x "$ROOT/comp-nox" ]; then
	mkdir -p "$ROOT/run2"
	chmod 700 "$ROOT/run2"
	printf '#!/bin/sh\necho "display=[${DISPLAY:-}]" > "%s/nox.env"\n' "$ROOT" > "$ROOT/nox.sh"
	chmod 755 "$ROOT/nox.sh"
	(unset DISPLAY; XDG_RUNTIME_DIR=$ROOT/run2 WLR_BACKENDS=headless WLR_RENDERER=pixman \
		DEV_DESK_HEADLESS=1 timeout 6 "$ROOT/comp-nox" -s "$ROOT/nox.sh" >"$ROOT/nox.log" 2>&1)
	if [ "$(cat "$ROOT/nox.env" 2>/dev/null)" = "display=[]" ]; then
		echo "compositor without Xwayland: session started, no DISPLAY"
	else
		echo "FAIL compositor without Xwayland did not start a session"
		tail -n 5 "$ROOT/nox.log"
		fail=1
	fi
fi
if [ "$fail" -ne 0 ]; then
	echo '--- compositor log ---'
	grep -E 'window mapped|xwayland|rror' "$LOG" | tail -30
	echo TEST_WINE_FAILED
	exit 1
fi
echo TEST_WINE_OK
