#!/bin/bash
# First-launch account setup and password changes, against a throwaway root
# directory (OLYMPIUM_ACCT_ROOT), with the real useradd and chpasswd.
#   - the name and password rules
#   - the first-launch window, filled in the way a boot test fills it, makes
#     the account and sets the administrator password
#   - a right password verifies, a wrong one does not, and a change takes
# Needs root, useradd, usermod, chpasswd, and Xwayland-free headless Wayland.
set -euo pipefail
cd "$(dirname "$0")"
for tool in /usr/sbin/useradd /usr/sbin/usermod /usr/sbin/chpasswd; do
    [ -x "$tool" ] || { echo "test-accounts: $tool is not installed; nothing was tested" >&2; exit 77; }
done
[ "$(id -u)" -eq 0 ] || { echo "test-accounts: needs root; nothing was tested" >&2; exit 77; }
make dev-compositor dev-settings test_accounts >/dev/null
ROOT=$(mktemp -d)
comp=
mounted=
cleanup() {
    [ -z "$comp" ] || kill "$comp" 2>/dev/null || true
    if [ -n "$mounted" ]; then
        # Never remove the directory while /usr is still mounted in it.
        umount "$mounted" 2>/dev/null || {
            echo "test-accounts: $mounted is still mounted; $ROOT was not removed" >&2
            return 0
        }
    fi
    rm -rf "$ROOT"
}
trap cleanup EXIT
./test_accounts names

# A root with what useradd and chpasswd need. chpasswd goes into the root with
# chroot, so it has to find the programs, libraries and PAM files there: /usr
# is mounted into it read-only, and the rest is copied or made.
A=$ROOT/acct
mkdir -p "$A/etc/skel" "$A/etc/default" "$A/home" "$A/var/lib/olympium" "$A/usr" "$A/dev"
if ! mount --bind /usr "$A/usr" 2>/dev/null; then
    echo "test-accounts: /usr cannot be mounted into a test root; nothing was tested" >&2
    exit 77
fi
mounted=$A/usr
mount -o remount,bind,ro "$A/usr"
for link in bin sbin lib lib64; do
    [ -e "/$link" ] && ln -s "usr/$link" "$A/$link" 2>/dev/null || true
done
mknod -m 666 "$A/dev/null" c 1 3
cp -a /etc/pam.d /etc/security /etc/nsswitch.conf "$A/etc/"
[ ! -e /etc/ld.so.cache ] || cp /etc/ld.so.cache "$A/etc/"
printf 'root:x:0:0:root:/root:/bin/bash\n' > "$A/etc/passwd"
printf 'root:x:0:\n' > "$A/etc/group"
printf 'root:!:19000:0:99999:7:::\n' > "$A/etc/shadow"
printf 'root:!::\n' > "$A/etc/gshadow"
cp /etc/login.defs "$A/etc/login.defs"
chmod 640 "$A/etc/shadow" "$A/etc/gshadow"
export OLYMPIUM_ACCT_ROOT=$A

mkdir "$ROOT/run"
chmod 700 "$ROOT/run"
export XDG_RUNTIME_DIR=$ROOT/run OLYMPIUM_STATUS=$ROOT/status GDK_BACKEND=wayland
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 DEV_DESK_HEADLESS=1
cat > "$ROOT/session" <<SESSION
#!/bin/sh
OLYMPIUM_SETUP_AUTO='Dana Example|dana|first password 1|root password 9' \\
    $PWD/dev-settings --first-run > "$ROOT/setup.log" 2>&1
echo "exit \$?" >> "$ROOT/setup.log"
SESSION
chmod +x "$ROOT/session"
./dev-compositor -s "$ROOT/session" > "$ROOT/compositor.log" 2>&1 &
comp=$!
for _ in $(seq 1 150); do
    grep -q '^exit ' "$ROOT/setup.log" 2>/dev/null && break
    sleep 0.2
done
grep -q '^exit 0' "$ROOT/setup.log" || { echo 'FAIL: the first-launch window did not finish'; cat "$ROOT/setup.log"; exit 1; }
grep -q 'setup: account dana created' "$ROOT/setup.log" || { echo 'FAIL: no account was reported'; exit 1; }
grep -q '^dana:x:[0-9]*:[0-9]*:Dana Example:/home/dana:/bin/bash$' "$A/etc/passwd" ||
    { echo 'FAIL: the account is not in passwd with its full name'; cat "$A/etc/passwd"; exit 1; }
[ "$(cat "$A/var/lib/olympium/setup-done")" = dana ] || { echo 'FAIL: the first-launch marker is wrong'; exit 1; }
echo 'first launch: the account was made'

./test_accounts check dana 'first password 1' || { echo 'FAIL: the account password does not verify'; exit 1; }
./test_accounts check root 'root password 9' || { echo 'FAIL: the administrator password does not verify'; exit 1; }
if ./test_accounts check dana 'not the password'; then echo 'FAIL: a wrong password verified'; exit 1; fi
if ./test_accounts check root 'first password 1'; then echo 'FAIL: the account password opened root'; exit 1; fi
if grep -q -e 'first password 1' -e 'root password 9' "$ROOT/setup.log" "$ROOT/compositor.log" "$A/etc/shadow"; then
    echo 'FAIL: a password was written in clear'
    exit 1
fi
echo 'passwords: right ones verify, wrong ones do not, none in clear'

./test_accounts set dana 'a second password' || { echo 'FAIL: the change was refused'; exit 1; }
./test_accounts check dana 'a second password' || { echo 'FAIL: the changed password does not verify'; exit 1; }
if ./test_accounts check dana 'first password 1'; then echo 'FAIL: the old password still works'; exit 1; fi
if ./test_accounts set dana 'short'; then echo 'FAIL: a short password was accepted'; exit 1; fi
./test_accounts check dana 'a second password' || { echo 'FAIL: a refused change altered the password'; exit 1; }
echo 'change: taken, the old one stops working, a short one is refused'
kill "$comp"
wait "$comp" || [ "$?" -eq 143 ]
comp=
echo TEST_ACCOUNTS_OK
