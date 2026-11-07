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

# Prints "<location> <path on disk>", or "<location> REFUSED".
expect_route()
{
	result=$("$BIN" --route "$1" "$2" 2> "$TMP/err")
	if [ "$result" = "$3" ]; then ok "$2  -> $result"; else ko "$2" "got '$result', expected '$3'"; fi
}

# Compares the whole dump with a reference file reviewed by hand.
expect_dump()
{
	"$BIN" "$1" > "$TMP/dump" 2> "$TMP/err"
	if cmp -s "$TMP/dump" "$2"; then ok "$1"; else ko "$1" "differs from $2"; fi
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

echo "== routing"
R=$DIR/valid/routing.conf
expect_route "$R" "/" "/ ./www/site1/"
expect_route "$R" "/index.html" "/ ./www/site1/index.html"
expect_route "$R" "/autre/page.html" "/ ./www/site1/autre/page.html"
expect_route "$R" "/files" "/files ./data/files"
expect_route "$R" "/files/" "/files ./data/files/"
expect_route "$R" "/files/doc.pdf" "/files ./data/files/doc.pdf"
expect_route "$R" "/filesxyz" "/ ./www/site1/filesxyz"
expect_route "$R" "/files/img/a.png" "/files/img ./www/site1/files/img/a.png"
expect_route "$R" "/files/imgx" "/files ./data/files/imgx"
expect_route "$R" "/cgi-bin/hello.py" "/cgi-bin ./www/site1/cgi-bin/hello.py"
expect_route "$R" "/a..b" "/ ./www/site1/a..b"
expect_route "$R" "/..." "/ ./www/site1/..."
expect_route "$R" "/.." "/ REFUSED"
expect_route "$R" "/../etc/passwd" "/ REFUSED"
expect_route "$R" "/files/.." "/files REFUSED"
expect_route "$R" "/files/../../secret" "/files REFUSED"
expect_route "$R" "/a/../b" "/ REFUSED"
expect_route "$R" "relative" "/ REFUSED"
expect_route "$R" "" "/ REFUSED"
expect_route "$DIR/valid/routing_own_root.conf" "/page.html" "/ ./data/page.html"
expect_route "$DIR/valid/post_parsing_defaults.conf" "/nothing" "/ ./www/site1/nothing"
expect_route "$DIR/valid/routing_implicit_root.conf" "/page.html" "/ ./www/site1/page.html"
expect_route config/default.conf "/upload/a.txt" "/upload ./www/site1/uploads/a.txt"
expect_route config/default.conf "/files" "/files ./www/site1/files"
expect_route config/default.conf "/cgi-bin/hello.py" "/cgi-bin ./www/site1/cgi-bin/hello.py"

echo "== dump"
expect_dump config/default.conf "$DIR/expected/default.dump"
expect_dump "$DIR/valid/post_parsing_inheritance.conf" "$DIR/expected/post_parsing_inheritance.dump"
expect_dump "$DIR/valid/post_parsing_defaults.conf" "$DIR/expected/post_parsing_defaults.dump"

echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
