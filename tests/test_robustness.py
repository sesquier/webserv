#!/usr/bin/env python3
# Starts its own server: run it from the repository root, with port 8080 free.
import os
import signal
import socket
import subprocess
import sys
import time

PORT = 8080
FIFO = "www/site1/test_fifo"
results = []


def report(name, ok, detail=""):
	results.append(ok)
	print(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}")


def request(data, timeout=4):
	with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as sock:
		sock.sendall(data)
		answer = b""
		while True:
			chunk = sock.recv(65536)
			if not chunk:
				return answer
			answer += chunk


def has_status(answer, code):
	return answer.startswith(b"HTTP/1.1 %d " % code)


def test_requests():
	report("HTTP/1.0 request without headers", has_status(request(b"GET / HTTP/1.0\r\n\r\n"), 200))
	report("Request line alone without a space gets 400", has_status(request(b"BONJOUR\r\n\r\n"), 400))
	body = b"a\r\n\r\nb"
	head = b"POST /upload/test_robustness.bin HTTP/1.1\nHost: x\nContent-Length: %d\n\n" % len(body)
	report("LF headers with CRLFCRLF in the body", has_status(request(head + body), 201))
	path = "www/site1/uploads/test_robustness.bin"
	with open(path, "rb") as saved:
		report("Body saved without alteration", saved.read() == body)
	os.unlink(path)


def test_stop_and_continue(server):
	os.kill(server.pid, signal.SIGSTOP)
	time.sleep(0.3)
	os.kill(server.pid, signal.SIGCONT)
	time.sleep(1.5)
	report("Server survives Ctrl-Z then fg", server.poll() is None and has_status(request(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"), 200))


def test_fifo():
	os.mkfifo(FIFO)
	try:
		answer = request(b"GET /test_fifo HTTP/1.1\r\nHost: x\r\n\r\n")
		report("GET on a FIFO answers 404 without blocking", has_status(answer, 404))
	except OSError as error:
		report("GET on a FIFO answers 404 without blocking", False, str(error))
	finally:
		os.unlink(FIFO)


def main():
	server = subprocess.Popen(["./webserv"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	time.sleep(0.5)
	try:
		test_requests()
		test_stop_and_continue(server)
		test_fifo()
	finally:
		server.terminate()
		server.wait()
	print(f"Passed: {sum(results)}/{len(results)}")
	sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
	main()
