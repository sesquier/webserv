#!/bin/sh
# Runs the config module on every test file.
#   valid/*.conf   must exit with 0
#   invalid/*.conf must exit with 1 and print "config error" (no crash)
# Usage from the repository root: sh tests/config/run_tests.sh [binary]

BIN=${1:-tests/config/config_test}
DIR=tests/config
TMP=$(mktemp -d)
pass=0
fail=0

trap 'chmod -R u+rwx "$TMP"; rm -rf "$TMP"' EXIT

ok()  { pass=$((pass + 1)); printf '  \033[32mOK\033[0m   %s\n' "$1"; }
ko()  { fail=$((fail + 1)); printf '  \033[31mKO\033[0m   %s (%s)\n' "$1" "$2"; }

expect_valid()
{
	"$BIN" "$1" > /dev/null 2> "$TMP/err"
	code=$?
	if [ "$code" -eq 0 ]; then ok "$2"; else ko "$2" "exit $code: $(head -n 1 "$TMP/err")"; fi
}

# Exit code 1 + our message. A crash gives 128+signal, so it fails here.
expect_invalid()
{
	"$BIN" "$1" > /dev/null 2> "$TMP/err"
	code=$?
	if [ "$code" -eq 1 ] && grep -q "config error" "$TMP/err"; then
		ok "$2  -> $(head -n 1 "$TMP/err")"
	else
		ko "$2" "exit $code"
	fi
}

if [ ! -x "$BIN" ]; then
	echo "binary '$BIN' not found, build it first (make -C tests/config)"
	exit 1
fi

echo "== valid"
for f in "$DIR"/valid/*.conf config/default.conf; do
	[ -e "$f" ] && expect_valid "$f" "$f"
done

echo "== invalid"
for f in "$DIR"/invalid/*.conf; do
	[ -e "$f" ] && expect_invalid "$f" "$f"
done

# Cases that cannot be stored in git as plain files.
expect_invalid "$TMP/does_not_exist.conf" "missing file"
expect_invalid "$DIR" "directory instead of file"
cp config/default.conf "$TMP/unreadable.conf" && chmod 000 "$TMP/unreadable.conf"
expect_invalid "$TMP/unreadable.conf" "unreadable file"

echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
