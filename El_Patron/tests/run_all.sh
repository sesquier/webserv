#!/bin/bash
# Runs every test of webserv: build, configuration tests, then each Python
# suite. Some suites start their own server; for the others this script
# starts ./webserv with config/default.conf and stops it afterwards.
#
# Usage, from anywhere:  tests/run_all.sh [--valgrind]
#   --valgrind  the shared server runs under valgrind, which then reports
#               memory leaks, memory errors and file descriptors left open.
# Ports 8080, 8081 and 8180-8183 must be free.

cd "$(dirname "$0")/.." || exit 1

GREEN="\033[32m"
RED="\033[31m"
BOLD="\033[1m"
RESET="\033[0m"

USE_VALGRIND=0
if [ "$1" = "--valgrind" ]; then
	USE_VALGRIND=1
elif [ -n "$1" ]; then
	echo "usage: $0 [--valgrind]"
	exit 1
fi

LOG_DIR=$(mktemp -d)
NAMES=()
VERDICTS=()

record() {
	NAMES+=("$1")
	VERDICTS+=("$2")
}

port_in_use() {
	(echo > "/dev/tcp/127.0.0.1/$1") 2> /dev/null
}

# Runs one suite, shows its last line, keeps its full output in LOG_DIR.
run_suite() {
	local name=$1
	shift
	printf "%-22s " "$name"
	if "$@" > "$LOG_DIR/$name.log" 2>&1; then
		printf "${GREEN}OK${RESET}   %s\n" "$(tail -n 1 "$LOG_DIR/$name.log")"
		record "$name" OK
	else
		printf "${RED}FAIL${RESET} see %s\n" "$LOG_DIR/$name.log"
		grep -E "FAIL" "$LOG_DIR/$name.log" | head -n 5 | sed 's/^/                       /'
		record "$name" FAIL
	fi
}

for port in 8080 8081 8180 8181 8182 8183; do
	if port_in_use "$port"; then
		echo "Port $port is already in use: stop the server that uses it first."
		exit 1
	fi
done

echo -e "${BOLD}== Build${RESET}"
if ! make -s > "$LOG_DIR/build.log" 2>&1 || ! make -s -C tests/config >> "$LOG_DIR/build.log" 2>&1; then
	cat "$LOG_DIR/build.log"
	exit 1
fi
echo "webserv and tests/config/config_test built"

echo -e "\n${BOLD}== Suites that start their own server${RESET}"
run_suite config bash tests/config/run_tests.sh
run_suite test_routing python3 tests/test_routing.py
run_suite test_server_config python3 tests/test_server_config.py
run_suite test_robustness python3 tests/test_robustness.py
run_suite test_cgi python3 tests/test_cgi.py

echo -e "\n${BOLD}== Suites on a shared server${RESET}"
if [ "$USE_VALGRIND" -eq 1 ]; then
	echo "(webserv runs under valgrind: slower, timing tests may suffer)"
	valgrind --leak-check=full --track-fds=yes ./webserv > "$LOG_DIR/server.log" 2> "$LOG_DIR/valgrind.log" &
else
	./webserv > "$LOG_DIR/server.log" 2>&1 &
fi
SERVER=$!
# Wait until it listens (valgrind needs a few seconds to start).
for _ in $(seq 1 50); do
	port_in_use 8080 && break
	sleep 0.2
done

run_suite test_http_error python3 tests/test_http_error.py
run_suite test_webserv python3 tests/test_webserv.py
run_suite test_limits python3 tests/test_limits.py
run_suite test_slow_clients python3 tests/test_slow_clients.py

if kill -0 "$SERVER" 2> /dev/null; then
	record "server still running" OK
else
	echo -e "${RED}The shared server stopped during the tests${RESET}"
	record "server still running" FAIL
fi
kill -TERM "$SERVER" 2> /dev/null
wait "$SERVER" 2> /dev/null

if [ "$USE_VALGRIND" -eq 1 ]; then
	echo -e "\n${BOLD}== Valgrind${RESET}"
	grep -E "definitely lost|ERROR SUMMARY|FILE DESCRIPTORS" "$LOG_DIR/valgrind.log" | sed 's/^==[0-9]*== *//'
	# Stopped by a signal, the server keeps epoll and its two listening
	# sockets open: 3 + 3 standard fds. Anything more is a leak.
	if grep -q "definitely lost: 0 bytes" "$LOG_DIR/valgrind.log" \
		&& grep -q "ERROR SUMMARY: 0 errors" "$LOG_DIR/valgrind.log" \
		&& grep -q "FILE DESCRIPTORS: 6 open" "$LOG_DIR/valgrind.log"; then
		record valgrind OK
	else
		echo -e "${RED}valgrind found a problem: see $LOG_DIR/valgrind.log${RESET}"
		record valgrind FAIL
	fi
fi

FAILED=0
for i in "${!NAMES[@]}"; do
	[ "${VERDICTS[$i]}" = FAIL ] && FAILED=$((FAILED + 1))
done
echo
if [ "$FAILED" -eq 0 ]; then
	echo -e "${GREEN}${BOLD}All ${#NAMES[@]} checks passed${RESET}"
	rm -rf "$LOG_DIR"
	exit 0
fi
echo -e "${RED}${BOLD}$FAILED of ${#NAMES[@]} checks failed${RESET} (logs in $LOG_DIR)"
exit 1
