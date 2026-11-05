#!/usr/bin/env python3
# Starts its own servers: run it from the repository root, with ports 8180-8183 free.
import os
import socket
import subprocess
import sys
import tempfile
import time

results = []
CONFIG = """
server {
    listen 127.0.0.1:8180;
    listen 127.0.0.1:8181;
    root ./www/first;
}
server {
    listen 8182;
    root ./www/second;
}
"""


def report(name, ok, detail=""):
	results.append(ok)
	print(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}")


def answers(port):
	try:
		with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
			sock.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
			return sock.recv(20).startswith(b"HTTP/1.1 200 ")
	except OSError:
		return False


def write_config(text):
	handle, path = tempfile.mkstemp(suffix=".conf")
	os.write(handle, text.encode())
	os.close(handle)
	return path


def run_and_exit(arguments):
	return subprocess.run(["./webserv"] + arguments, capture_output=True, text=True, timeout=5)


def test_listeners_and_servers():
	path = write_config(CONFIG)
	log = tempfile.TemporaryFile(mode="w+")
	server = subprocess.Popen(["./webserv", path], stdout=log, stderr=subprocess.DEVNULL)
	time.sleep(0.5)
	try:
		for port in (8180, 8181, 8182):
			report(f"Port {port} from the configuration answers", answers(port))
		report("Port 8183, absent from the configuration, is closed", not answers(8183))
		report("Port 8080 is no longer hard-coded", not answers(8080))
		time.sleep(0.2)
		log.seek(0)
		roots = [line for line in log.read().splitlines() if "server root" in line]
		expected = ["./www/first", "./www/first", "./www/second"]
		report("Each client is attached to the server of its port",
			len(roots) == 3 and all(root in line for root, line in zip(expected, roots)))
		conflict = run_and_exit([path])
		report("Address already in use is a clean error",
			conflict.returncode == 1 and "cannot listen on" in conflict.stderr, conflict.stderr.strip())
	finally:
		server.terminate()
		server.wait()
		log.close()
		os.unlink(path)


def test_invalid_starts():
	missing = run_and_exit(["does_not_exist.conf"])
	report("Missing file is refused", missing.returncode == 1 and "config error" in missing.stderr, missing.stderr.strip())
	path = write_config("server {\n    listen 99999;\n    root ./www;\n}\n")
	invalid = run_and_exit([path])
	os.unlink(path)
	report("Invalid file is refused with its line", invalid.returncode == 1 and "line 2" in invalid.stderr, invalid.stderr.strip())
	usage = run_and_exit(["a.conf", "b.conf"])
	report("Two arguments give the usage", usage.returncode == 1 and "usage" in usage.stderr)


def test_default_path():
	server = subprocess.Popen(["./webserv"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	time.sleep(0.5)
	try:
		report("Without argument, config/default.conf is used", answers(8080) and answers(8081))
	finally:
		server.terminate()
		server.wait()


def main():
	test_listeners_and_servers()
	test_invalid_starts()
	test_default_path()
	print(f"Passed: {sum(results)}/{len(results)}")
	sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
	main()
