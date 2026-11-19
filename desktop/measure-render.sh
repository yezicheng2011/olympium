#!/bin/bash
# Measures what the compositor costs while a window keeps changing: CPU used
# per second and per frame, with the headless backend and the pixman renderer,
# one X window repainted every 16 ms (test-animate). Run it with and without
# the glass cache to see what the cache saves:
#
#   desktop/measure-render.sh            cache on
#   OLYMPIUM_BLUR_CACHE=0 desktop/measure-render.sh
#
# The numbers belong to the machine they were taken on. Only compare runs
# made on the same one.
set -euo pipefail
cd "$(dirname "$0")"
make dev-compositor test-animate >/dev/null
SECS=${1:-10}
ROOT=$(mktemp -d)
comp=
anim=
trap '[ -z "$anim" ] || kill "$anim" 2>/dev/null; [ -z "$comp" ] || { kill "$comp" 2>/dev/null; wait "$comp" 2>/dev/null || true; }; rm -rf "$ROOT"; true' EXIT
mkdir "$ROOT/run"
chmod 700 "$ROOT/run"
export XDG_RUNTIME_DIR=$ROOT/run OLYMPIUM_DESK_HOME=$ROOT/desk
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 DEV_DESK_HEADLESS=1
printf '#!/bin/sh\nprintf "DISPLAY=%%s\\n" "$DISPLAY" > "%s/display"\n' "$ROOT" > "$ROOT/session"
chmod +x "$ROOT/session"
# Its own status file: the usual one is shared with any other compositor.
export OLYMPIUM_STATUS=$ROOT/status
./dev-compositor -s "$ROOT/session" > "$ROOT/compositor.log" 2>&1 &
comp=$!
for _ in $(seq 1 100); do
    [ ! -s "$ROOT/display" ] || break
    sleep 0.1
done
. "$ROOT/display"
export DISPLAY
./test-animate 16 &
anim=$!
sleep 3
for _ in $(seq 1 100); do
    [ -s "$OLYMPIUM_STATUS" ] && break
    sleep 0.2
done
sleep 2
status() { sed -n "s/^$1=//p" "$OLYMPIUM_STATUS"; }
ticks() { awk '{print $14 + $15}' "/proc/$comp/stat"; }
f0=$(status frames); t0=$(ticks); h0=$(status glass_cache_hits); m0=$(status glass_cache_misses)
sleep "$SECS"
f1=$(status frames); t1=$(ticks); h1=$(status glass_cache_hits); m1=$(status glass_cache_misses)
hz=$(getconf CLK_TCK)
frames=$((f1 - f0))
cpu=$((t1 - t0))
echo "cache ${OLYMPIUM_BLUR_CACHE:-on}: $frames frames in ${SECS} s ($((frames / SECS)) a second)"
echo "compositor CPU: $((cpu * 100 / hz / SECS))% of one core, $((cpu * 1000000 / hz / (frames > 0 ? frames : 1))) us per frame"
echo "glass cache: $((h1 - h0)) uses, $((m1 - m0)) blurs"
