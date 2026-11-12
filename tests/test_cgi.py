#!/usr/bin/env python3
# CGI tests. Starts its own server with config/default.conf: run it from the
# repository root, with ports 8080 and 8081 free. Takes about 15 seconds:
# the timeout test waits for the 10 s CGI timeout.
import os
import socket
import subprocess
import sys
import threading
import time

results = []
UPLOADED = "www/site1/uploads/test_cgi_upload.py"


def report(name, ok, detail=""):
	results.append(ok)
	print(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}")


def send(raw, timeout=15):
	with socket.create_connection(("127.0.0.1", 8080), timeout=timeout) as sock:
		# In pieces, stopping if the server closes: it answers a body that is
		# too large (413) as soon as it has read the headers, like curl expects.
		for offset in range(0, len(raw), 65536):
			try:
				sock.sendall(raw[offset:offset + 65536])
			except OSError:
				break
		answer = b""
		while True:
			try:
				chunk = sock.recv(65536)
			except ConnectionResetError:
				break
			if not chunk:
				break
			answer += chunk
	head, _, body = answer.partition(b"\r\n\r\n")
	return int(head.split(b" ", 2)[1]), head.decode("latin1"), body


def request(method, path, body=b"", headers=None, timeout=15):
	lines = [f"{method} {path} HTTP/1.1", "Host: localhost"]
	lines += [f"{name}: {value}" for name, value in (headers or {}).items()]
	if body or method == "POST":
		lines.append(f"Content-Length: {len(body)}")
	return send(("\r\n".join(lines) + "\r\n\r\n").encode() + body, timeout)


def check(name, method, path, expected, body=b"", headers=None, contains=(), absent=()):
	try:
		status, head, answer = request(method, path, body, headers)
	except OSError as error:
		report(name, False, f"({error})")
		return
	ok = status == expected
	ok = ok and all(text in answer for text in contains)
	ok = ok and not any(text in answer for text in absent)
	report(name, ok, f"got {status}")


def children(server):
	"""Processes started by the server: (state, command line)."""
	output = subprocess.run(["ps", "-o", "stat=,args=", "--ppid", str(server.pid)],
		capture_output=True, text=True).stdout
	return [line.split(None, 1) for line in output.splitlines() if line.strip()]


def test_scripts():
	check("GET hello.py with a query string", "GET", "/cgi-bin/hello.py?name=Bob", 200,
		contains=[b"Bonjour Bob !"])
	check("POST of the form to hello.py", "POST", "/cgi-bin/hello.py", 200, body=b"name=Alice",
		headers={"Content-Type": "application/x-www-form-urlencoded"},
		contains=[b"Bonjour Alice !", b"Body recu : name=Alice"])
	chunked = b"5\r\nname=\r\n5\r\nChunk\r\n0\r\n\r\n"
	try:
		status, _, answer = send(b"POST /cgi-bin/hello.py HTTP/1.1\r\nHost: localhost\r\n"
			b"Transfer-Encoding: chunked\r\n\r\n" + chunked)
		report("Chunked POST is un-chunked for the script", status == 200 and b"Bonjour Chunk !" in answer,
			f"got {status}")
	except OSError as error:
		report("Chunked POST is un-chunked for the script", False, f"({error})")
	check("Perl script (bonus)", "GET", "/cgi-bin/hello.pl?name=Bob", 200, contains=[b"ici Perl"])
	check("Bash script (bonus)", "GET", "/cgi-bin/hello.sh?name=Bob", 200, contains=[b"ici Bash"])
	check("The script's HTML is escaped (XSS)", "GET", "/cgi-bin/hello.py?name=%3Cscript%3E", 200,
		absent=[b"<script>"])


def test_environment():
	try:
		status, _, answer = request("POST", "/cgi-bin/env.py?a=1&b=2", b"x" * 1000,
			{"Content-Type": "text/plain", "Cookie": "id=42", "X-Test": "yes",
			"Proxy": "http://evil", "X_Fake": "1"})
	except OSError as error:
		report("env.py answers", False, f"({error})")
		return
	env = dict(line.split("=", 1) for line in answer.decode().splitlines() if "=" in line)
	expected = {
		"REQUEST_METHOD": "POST", "QUERY_STRING": "a=1&b=2", "CONTENT_LENGTH": "1000",
		"CONTENT_TYPE": "text/plain", "SCRIPT_NAME": "/cgi-bin/env.py", "SCRIPT_FILENAME": "env.py",
		"SERVER_PROTOCOL": "HTTP/1.1", "SERVER_PORT": "8080", "GATEWAY_INTERFACE": "CGI/1.1",
		"HTTP_COOKIE": "id=42", "HTTP_X_TEST": "yes", "BODY_BYTES": "1000",
	}
	for name, value in expected.items():
		report(f"Environment: {name}", env.get(name) == value, f"got {env.get(name)!r}")
	report("Environment: no HTTP_PROXY (httpoxy)", "HTTP_PROXY" not in env)
	report("Environment: X_Fake is not passed", "HTTP_X_FAKE" not in env)


def test_execution_rules():
	check("Script runs in its own directory (chdir)", "GET", "/cgi-bin/readfile.py", 200,
		contains=[b"Contenu de data.txt"])
	check("Big body (1 000 000 bytes) reaches the script", "POST", "/cgi-bin/env.py", 200,
		body=b"a" * 1000000, contains=[b"BODY_BYTES=1000000"])
	check("Body above the location limit", "POST", "/cgi-bin/env.py", 413, body=b"a" * 2000000)
	check("Missing script", "GET", "/cgi-bin/absent.py", 404)
	check("DELETE is not allowed on scripts", "DELETE", "/cgi-bin/hello.py", 405)
	report("The script is still there", os.path.exists("www/site1/cgi-bin/hello.py"))

	# An uploaded .py is stored, then sent back as a file: /upload has no cgi.
	if os.path.exists(UPLOADED):
		os.unlink(UPLOADED)
	source = b'print("Content-Type: text/plain")\nprint()\nprint("EXECUTED")\n'
	check("Upload a .py file", "POST", "/upload/test_cgi_upload.py", 201, body=source)
	check("A .py outside a CGI location is not run", "GET", "/upload/test_cgi_upload.py", 200,
		contains=[b'print("EXECUTED")'])
	if os.path.exists(UPLOADED):
		os.unlink(UPLOADED)


def test_failures(server):
	check("Script that crashes: 502", "GET", "/cgi-bin/error.py", 502, absent=[b"jamais vu"])

	# The infinite loop runs in a thread; meanwhile the server must answer.
	outcome = {}

	def run_loop():
		start = time.time()
		try:
			outcome["status"] = request("GET", "/cgi-bin/loop.py", timeout=20)[0]
		except OSError as error:
			outcome["status"] = str(error)
		outcome["time"] = time.time() - start

	loop = threading.Thread(target=run_loop)
	loop.start()
	time.sleep(1)
	start = time.time()
	check("Static page while loop.py runs", "GET", "/", 200)
	report("... answered at once", time.time() - start < 1, f"({time.time() - start:.3f} s)")
	check("Another script while loop.py runs", "GET", "/cgi-bin/hello.py?name=Bob", 200)

	statuses = []

	def run_hello(number):
		try:
			statuses.append(request("GET", f"/cgi-bin/hello.py?name={number}")[0])
		except OSError:
			statuses.append(0)

	threads = [threading.Thread(target=run_hello, args=(i,)) for i in range(5)]
	for thread in threads:
		thread.start()
	for thread in threads:
		thread.join()
	report("5 scripts at the same time", statuses == [200] * 5, f"got {statuses}")

	loop.join()
	report("Infinite loop: 504", outcome.get("status") == 504, f"got {outcome.get('status')}")
	report("... after the 10 s timeout", 9 <= outcome.get("time", 0) <= 13,
		f"({outcome.get('time', 0):.1f} s)")

	# A client that leaves: its script is stopped at once.
	with socket.create_connection(("127.0.0.1", 8080), timeout=3) as sock:
		sock.sendall(b"GET /cgi-bin/loop.py HTTP/1.1\r\nHost: localhost\r\n\r\n")
		time.sleep(0.3)
	time.sleep(0.5)
	running = [args for state, args in children(server) if "loop.py" in args]
	report("Client that leaves: its script is stopped", running == [], f"({running})")

	time.sleep(0.3)
	zombies = [args for state, args in children(server) if state.startswith("Z")]
	report("No zombie process left", zombies == [], f"({zombies})")


def main():
	server = subprocess.Popen(["./webserv"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	time.sleep(0.5)
	try:
		if server.poll() is not None:
			print("webserv did not start: are ports 8080 and 8081 free?")
			sys.exit(1)
		test_scripts()
		test_environment()
		test_execution_rules()
		test_failures(server)
		report("Server still running", server.poll() is None)
	finally:
		server.terminate()
		server.wait()

	print(f"\nPassed: {sum(results)}/{len(results)}")
	sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
	main()
