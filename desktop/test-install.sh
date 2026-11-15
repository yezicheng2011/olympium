#!/bin/sh
# Isolated checks for the install window. Does not touch a real home library.
set -eu
cd "$(dirname "$0")"
make dev-install olympium-run >/dev/null
BIN=$PWD/dev-install
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
export HOME=$ROOT/home
export OLYMPIUM_HOME=$ROOT/runtime
export OLYMPIUM_DESK_HOME=$ROOT/desk
export OLYMPIUM_BIN=/var/tmp/aaa-out/olympium
export OLYMPIUM_RUN=$PWD/olympium-run
export XDG_DATA_HOME=$ROOT/xdg
mkdir -p "$HOME" "$OLYMPIUM_HOME" "$OLYMPIUM_DESK_HOME" "$XDG_DATA_HOME"

fail() {
	echo "FAIL: $*" >&2
	exit 1
}

json_get() {
	python3 -c 'import json,sys; v=json.load(sys.stdin)[sys.argv[1]]; print(v if isinstance(v, str) else str(v).lower())' "$1"
}

echo "inspect /usr/bin/true"
one=$("$BIN" inspect /usr/bin/true)
printf '%s\n' "$one" | json_get launchable | grep -qx true
	printf '%s\n' "$one" | json_get ran | grep -qx false
printf '%s\n' "$one" | json_get cache | grep -qx miss
printf '%s\n' "$one" | json_get action | grep -qx import
test ! -e "$OLYMPIUM_DESK_HOME/library.tsv"

two=$("$BIN" inspect /usr/bin/true)
printf '%s\n' "$two" | json_get cache | grep -qx hit

echo "import without consent"
if "$BIN" import /usr/bin/true >/dev/null 2>&1; then
	fail "import without consent succeeded"
fi
test ! -e "$OLYMPIUM_DESK_HOME/library.tsv"

cp /usr/bin/true "$ROOT/true-copy"
chmod 755 "$ROOT/true-copy"
echo "import true-copy"
"$BIN" import --consent "$ROOT/true-copy" | tee "$ROOT/import-true.txt" | grep -q "download: none"
grep -q "progress 4/4 desktop" "$ROOT/import-true.txt"
test -s "$OLYMPIUM_DESK_HOME/library.tsv"
test -s "$OLYMPIUM_DESK_HOME/pinned"
id=$(awk -F '\t' -v p="$ROOT/true-copy" '$3==p {print $1}' "$OLYMPIUM_DESK_HOME/library.tsv")
test -n "$id"
test -x "$OLYMPIUM_DESK_HOME/launch/$id"
test -s "$OLYMPIUM_DESK_HOME/applications/olympium-$id.desktop"
grep -q "Exec=$OLYMPIUM_DESK_HOME/launch/$id" "$OLYMPIUM_DESK_HOME/applications/olympium-$id.desktop"
"$OLYMPIUM_BIN" --json list | grep -q "$ROOT/true-copy"
echo "launch true-copy"
"$BIN" launch "$id"

echo "checksum blocks a changed file"
printf 'x' >> "$ROOT/true-copy"
if "$BIN" launch "$id"; then
	fail "changed file was launched"
fi
code=$?
# The shell if already consumed the status. Re-run for the code.
set +e
"$BIN" launch "$id" >/dev/null 2>"$ROOT/tamper.err"
code=$?
set -e
test "$code" = 4
grep -q "checksum changed" "$ROOT/tamper.err"

ROOT=$ROOT python3 - <<'PY'
import os, zipfile
root = os.environ["ROOT"]
desk = os.environ["OLYMPIUM_DESK_HOME"]
open(root+"/macho","wb").write(b"\xcf\xfa\xed\xfe" + b"\x00"*28)
z = zipfile.ZipFile(root+"/sample.ipa","w")
z.writestr("Payload/App.app/Info.plist","x")
z.close()
z = zipfile.ZipFile(root+"/sample.apk","w")
z.writestr("AndroidManifest.xml","x")
z.close()
z = zipfile.ZipFile(root+"/sample.hap","w")
z.writestr("module.json","{}")
z.close()
open(root+"/sample.deb","wb").write(b"!<arch>\ndebian-binary   0           0     0     644     4         `\n2.0\n")
open(root+"/sample.exe","wb").write(b"MZ" + b"\x00"*64)
open(root+"/sample.rpm","wb").write(b"\xed\xab\xee\xdb" + b"\x00"*16)
open(root+"/sample.sh","w").write("#!/bin/sh\necho \"$1\" > \"" + desk + "/opened\"\n")
os.chmod(root+"/sample.sh", 0o755)
open(root+"/note.note","w").write("hello note\n")
PY

echo "mach-o is kept and not launched"
"$BIN" import --consent "$ROOT/macho" >"$ROOT/macho-import.txt"
grep -q "launch=refused" "$ROOT/macho-import.txt"
test ! -e "$OLYMPIUM_DESK_HOME/applications/olympium-"*macho*.desktop 2>/dev/null || true
mid=$(awk -F '\t' -v p="$ROOT/macho" '$3==p {print $1}' "$OLYMPIUM_DESK_HOME/library.tsv")
test -n "$mid"
set +e
"$BIN" launch "$mid" >/dev/null 2>"$ROOT/macho.err"
mcode=$?
set -e
test "$mcode" = 3
grep -q "not started" "$ROOT/macho.err"
# No desktop entry was written for the Mach-O id.
test ! -e "$OLYMPIUM_DESK_HOME/applications/olympium-$mid.desktop"
"$OLYMPIUM_BIN" --json list | grep -q "$ROOT/macho"

echo "ipa is refused and not added"
before=$("$OLYMPIUM_BIN" --json list)
set +e
"$BIN" import --consent "$ROOT/sample.ipa" >"$ROOT/ipa.txt" 2>"$ROOT/ipa.err"
icode=$?
set -e
test "$icode" != 0
grep -q "iOS is not part of this system" "$ROOT/ipa.txt"
after=$("$OLYMPIUM_BIN" --json list)
test "$before" = "$after"

echo "packages are recognized and not installed"
for f in sample.apk sample.hap sample.deb sample.exe sample.rpm; do
	view=$("$BIN" inspect "$ROOT/$f")
	printf '%s\n' "$view" | json_get launchable | grep -qx false
	printf '%s\n' "$view" | json_get ran | grep -qx false
	printf '%s\n' "$view" | json_get action | grep -qx review
	set +e
	"$BIN" import --consent "$ROOT/$f" >"$ROOT/$f.out" 2>"$ROOT/$f.err"
	pcode=$?
	set -e
	test "$pcode" != 0
	grep -q "not installed" "$ROOT/$f.err"
	grep -q "$f" "$OLYMPIUM_DESK_HOME/library.tsv" && fail "$f was recorded" || true
done
again=$("$BIN" inspect "$ROOT/sample.deb")
printf '%s\n' "$again" | json_get cache | grep -qx hit
printf '%s\n' "$again" | json_get launchable | grep -qx false
# The runtime's own sentence may name a download command. This desktop
# still reports that it downloaded nothing and did not run the file.
grep -q '"download": "none"' "$ROOT/sample.hap.out"
grep -q '"ran": false' "$ROOT/sample.hap.out"

echo "open a program does not run it"
before_open=$(wc -c < "$OLYMPIUM_DESK_HOME/library.tsv")
"$BIN" open --no-ui "$ROOT/sample.sh" >"$ROOT/open-sh.txt"
test ! -e "$OLYMPIUM_DESK_HOME/opened"
printf '%s\n' "$(cat "$ROOT/open-sh.txt")" | json_get ran | grep -qx false
after_open=$(wc -c < "$OLYMPIUM_DESK_HOME/library.tsv")
test "$before_open" = "$after_open"

echo "open-with after a consented script import"
"$BIN" import --consent --mime note "$ROOT/sample.sh" >"$ROOT/script-import.txt"
sid=$(awk -F '\t' -v p="$ROOT/sample.sh" '$3==p {print $1}' "$OLYMPIUM_DESK_HOME/library.tsv")
test -n "$sid"
test ! -e "$OLYMPIUM_DESK_HOME/opened"
"$BIN" open --no-ui "$ROOT/note.note"
grep -q "$ROOT/note.note" "$OLYMPIUM_DESK_HOME/opened"

echo "exchange directory"
ex=$("$BIN" exchange)
printf '%s\n' "$ex" | grep -q "^exchange=$OLYMPIUM_DESK_HOME/exchange$"
printf 'roundtrip\n' > "$OLYMPIUM_DESK_HOME/exchange/hello.txt"
grep -qx roundtrip "$OLYMPIUM_DESK_HOME/exchange/hello.txt"

echo "timings"
"$BIN" timings | tee "$ROOT/timings.txt"
grep -qE '^cold_runtime_ms [0-9]+$' "$ROOT/timings.txt"
grep -qE '^warm_runtime_ms [0-9]+$' "$ROOT/timings.txt"
grep -qE '^cold_exec_ms [0-9]+$' "$ROOT/timings.txt"
grep -qE '^warm_exec_ms [0-9]+$' "$ROOT/timings.txt"

echo "preview"
GDK_BACKEND=x11 xvfb-run -a "$BIN" preview "$ROOT/sample.deb" "$ROOT/install.png"
file "$ROOT/install.png" | grep -q PNG
test "$(wc -c < "$ROOT/install.png")" -gt 1000
cp "$ROOT/install.png" /tmp/olympium-install-preview.png
cp "$ROOT/timings.txt" /tmp/olympium-install-timings.txt

echo "real deb extracts and launches only after consent"
STAGE=$(mktemp -d)
sh "$(dirname "$0")/../image/build-sample-deb.sh" "$STAGE" "$PWD"
DEB=$STAGE/usr/share/olympium/samples/desk-note.deb
view=$("$BIN" inspect "$DEB")
printf '%s\n' "$view" | json_get action | grep -qx install
printf '%s\n' "$view" | json_get ran | grep -qx false
if "$BIN" import "$DEB" >/dev/null 2>&1; then
	fail "deb import without consent succeeded"
fi
test ! -d "$OLYMPIUM_DESK_HOME/runtimes"
"$BIN" import --consent "$DEB" >"$ROOT/deb-import.txt"
grep -q '^extracted=' "$ROOT/deb-import.txt"
grep -q 'download: none' "$ROOT/deb-import.txt"
test ! -e "$OLYMPIUM_DESK_HOME/cold-app-ms"
did=$(awk -F '\t' '$6=="linux_deb" {print $1; exit}' "$OLYMPIUM_DESK_HOME/library.tsv")
test -n "$did"
test -x "$OLYMPIUM_DESK_HOME/runtimes/linux/$did/root/usr/bin/desk-note"
printf 'hello from a note\n' > "$ROOT/hello.dnote"
set +e
"$BIN" open --no-ui "$ROOT/hello.dnote" >/dev/null 2>&1
set -e
grep -qx "$ROOT/hello.dnote" "$OLYMPIUM_DESK_HOME/opened-path"
rm -f "$OLYMPIUM_DESK_HOME/cold-app-ms" "$OLYMPIUM_DESK_HOME/warm-app-ms" "$OLYMPIUM_DESK_HOME/linux-runtime.sock"
GDK_BACKEND=x11 xvfb-run -a "$BIN" launch "$did" --once >"$ROOT/deb-cold.txt"
GDK_BACKEND=x11 xvfb-run -a "$BIN" launch "$did" --once >"$ROOT/deb-warm.txt"
grep -q '^runtime_state cold' "$ROOT/deb-cold.txt"
grep -q '^runtime_state warm' "$ROOT/deb-warm.txt"
grep -q '^runtime_ms 0' "$ROOT/deb-warm.txt"
test -s "$OLYMPIUM_DESK_HOME/cold-app-ms"
test -s "$OLYMPIUM_DESK_HOME/warm-app-ms"
paste=$(find "$OLYMPIUM_DESK_HOME/runtimes" -type f -name desk-paste | head -1)
GDK_BACKEND=x11 xvfb-run -a bash -c "\"$BIN\" launch \"$did\" >/tmp/hold-note.txt 2>&1 & sleep 0.8; \"$paste\" --once" >"$ROOT/deb-paste.txt"
grep -q 'clipboard: Desk Note ready' "$ROOT/deb-paste.txt"
grep -q 'exchange: Desk Note ready' "$ROOT/deb-paste.txt"
rm -rf "$STAGE"

echo "runtime absent still imports a Linux ELF and refuses to invent iOS"
export OLYMPIUM_BIN=/nonexistent
export OLYMPIUM_DESK_HOME=$ROOT/desk2
export OLYMPIUM_HOME=$ROOT/runtime2
mkdir -p "$OLYMPIUM_DESK_HOME" "$OLYMPIUM_HOME"
none=$("$BIN" inspect /usr/bin/true)
printf '%s\n' "$none" | json_get runtime | grep -qx absent
printf '%s\n' "$none" | json_get launchable | grep -qx true
"$BIN" import --consent /usr/bin/true >"$ROOT/absent-import.txt"
grep -q "launch=native" "$ROOT/absent-import.txt"
# A zip named .ipa is not classified as iOS without the runtime.
ROOT=$ROOT python3 - <<'PY'
import os, zipfile
z = zipfile.ZipFile(os.environ["ROOT"]+"/desk2-ipa.ipa","w")
z.writestr("Payload/App.app/Info.plist","x")
z.close()
PY
ipa2=$("$BIN" inspect "$ROOT/desk2-ipa.ipa")
printf '%s\n' "$ipa2" | json_get guest | grep -qx '' || printf '%s\n' "$ipa2" | json_get action | grep -qx refuse
printf '%s\n' "$ipa2" | json_get launchable | grep -qx false

echo "PASS"
