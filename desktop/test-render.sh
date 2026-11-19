#!/bin/bash
# Regression checks for idle rendering, shell damage and Xwayland lifetimes.
set -euo pipefail
cd "$(dirname "$0")"
umask 022
make dev-compositor test-xremap test-animate >/dev/null
ROOT=$(mktemp -d)
comp=
trap '[ -z "$comp" ] || kill "$comp" 2>/dev/null; rm -rf "$ROOT"' EXIT
mkdir "$ROOT/run"
chmod 700 "$ROOT/run"
export XDG_RUNTIME_DIR=$ROOT/run
export OLYMPIUM_DESK_HOME=$ROOT/desk
export WLR_BACKENDS=headless WLR_RENDERER=pixman DEV_DESK_HEADLESS=1
printf '#!/bin/sh\nprintf "DISPLAY=%%s\\n" "$DISPLAY" > "%s/display"\n' "$ROOT" > "$ROOT/session"
chmod +x "$ROOT/session"
"${COMPOSITOR:-./dev-compositor}" -s "$ROOT/session" > "$ROOT/compositor.log" 2>&1 &
comp=$!
for _ in $(seq 1 100); do
    [ ! -s "$ROOT/display" ] || break
    kill -0 "$comp" || { cat "$ROOT/compositor.log"; exit 1; }
    sleep 0.1
done
[ -s "$ROOT/display" ] || { cat "$ROOT/compositor.log"; exit 1; }
. "$ROOT/display"
export DISPLAY
# Allow initial output/cursor commits to settle. At most one widget update
# and one clock update may occur during this sample, irrespective of vblank.
sleep 1
frames() { sed -n 's/^frames=//p' /run/dev-desktop/status; }
before=$(frames)
sleep 4
after=$(frames)
[ $((after-before)) -le 4 ] || { echo "FAIL: idle rendered $((after-before)) frames"; exit 1; }
echo "idle: $((after-before)) frames in four seconds"
# Chrome is not in the wlroots scene graph: an explicit shell change must
# still cause a real render once the desktop has gone idle.
python3 - <<'PY'
import socket
with socket.socket(socket.AF_UNIX) as s:
    s.connect('/run/dev-desktop/control')
    s.sendall(b'contrast toggle')
PY
sleep 0.5
[ "$(frames)" -gt "$after" ] || { echo 'FAIL: chrome change did not redraw'; exit 1; }
echo 'shell change redraws: ok'
./test-xremap "${REMAP_ROUNDS:-12}"
kill -0 "$comp"
[ "$(grep -c 'window mapped: Desk Remap' "$ROOT/compositor.log")" -eq "$(( ${REMAP_ROUNDS:-12} * 2 ))" ]
if grep -E 'AddressSanitizer|runtime error:|corrupted|double free' "$ROOT/compositor.log"; then
    cat "$ROOT/compositor.log"
    exit 1
fi
echo 'Xwayland unmap/remap/destroy: ok'
kill "$comp"
wait "$comp" || [ "$?" -eq 143 ]
comp=

# The glass behind the menu bar and the dock is remembered between frames and
# is made again only when the pixels behind it change. With an X window
# repainting all the time, the cache has to be used, and in verify mode every
# use is checked against a fresh blur, which aborts the compositor on a
# difference.
mkdir "$ROOT/run2"
chmod 700 "$ROOT/run2"
rm -f "$ROOT/display"
XDG_RUNTIME_DIR=$ROOT/run2 OLYMPIUM_BLUR_CACHE=verify "${COMPOSITOR:-./dev-compositor}" \
    -s "$ROOT/session" > "$ROOT/compositor2.log" 2>&1 &
comp=$!
for _ in $(seq 1 100); do
    [ ! -s "$ROOT/display" ] || break
    sleep 0.1
done
. "$ROOT/display"
export DISPLAY
./test-animate 16 &
anim=$!
sleep 5
kill "$anim" 2>/dev/null || true
wait "$anim" 2>/dev/null || true
kill -0 "$comp" || { echo 'FAIL: the compositor stopped while the glass cache was verified'; tail -5 "$ROOT/compositor2.log"; exit 1; }
if grep -q 'cache mismatch' "$ROOT/compositor2.log"; then
    echo 'FAIL: the glass cache served a picture that differs from a fresh blur'
    exit 1
fi
hits=$(sed -n 's/^glass_cache_hits=//p' /run/dev-desktop/status)
misses=$(sed -n 's/^glass_cache_misses=//p' /run/dev-desktop/status)
[ "${hits:-0}" -gt 20 ] || { echo "FAIL: the glass cache was not used: $hits hits, $misses misses"; exit 1; }
echo "glass cache while a window animates: $hits uses, $misses blurs, every use equal to a fresh blur"
kill "$comp"
wait "$comp" || [ "$?" -eq 143 ]
comp=
