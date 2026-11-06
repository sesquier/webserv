#!/usr/bin/env python3
# Starts its own server with config/default.conf: run it from the repository root,
# with ports 8080 and 8081 free.
import os
import socket
import subprocess
import sys
import time

results = []
UPLOADED = "www/site1/uploads/test_routing.txt"


def report(name, ok, detail=""):
	results.append(ok)
	print(f"[{'PASS' if ok else 'FAIL'}] {name} {detail}")


def request(method, path, port=8080, body=b""):
	head = f"{method} {path} HTTP/1.1\r\nHost: x\r\nContent-Length: {len(body)}\r\n\r\n"
	with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
		sock.sendall(head.encode() + body)
		answer = b""
		while True:
			chunk = sock.recv(65536)
			if not chunk:
				break
			answer += chunk
	status = int(answer.split(b" ", 2)[1])
	headers, _, content = answer.partition(b"\r\n\r\n")
	return status, headers.decode("latin1"), content


def check(name, method, path, expected, port=8080, body=b"", header=None, content=None):
	status, headers, answer = request(method, path, port, body)
	ok = status == expected
	if header is not None:
		ok = ok and header in headers
	if content is not None:
		ok = ok and content in answer
	report(name, ok, f"got {status}")


def main():
	if os.path.exists(UPLOADED):
		os.unlink(UPLOADED)
	server = subprocess.Popen(["./webserv"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	time.sleep(0.5)
	try:
		check("Port 8080 serves site 1", "GET", "/", 200, content=b"<h1>Site 1</h1>")
		check("Port 8081 serves site 2", "GET", "/", 200, port=8081, content=b"<h1>Site 2</h1>")
		check("Second page of site 1", "GET", "/contact.html", 200, header="Content-Type: text/html")
		check("CSS is sent as CSS", "GET", "/style.css", 200, header="Content-Type: text/css")
		check("Location with its own root", "GET", "/files/exemple.txt", 200, content=b"Premier fichier")
		check("Redirection of /old", "GET", "/old", 301, header="Location: /")
		check("POST not allowed on /", "POST", "/", 405, header="Allow: GET")
		check("DELETE not allowed on /files", "DELETE", "/files/exemple.txt", 405)
		check("Unknown method", "PUT", "/", 501)
		check("Missing page", "GET", "/page-inexistante.html", 404)
		check("Custom 404 page from error_page", "GET", "/page-inexistante.html", 404, content=b"404 : page introuvable")
		check("Generated 404 page without error_page", "GET", "/nope.html", 404, port=8081, content=b"<h1>404 Not Found</h1>")
		check("Error pages are HTML", "GET", "/nope.html", 404, port=8081, header="Content-Type: text/html")
		check("405 keeps its Allow header", "POST", "/", 405, header="Allow: GET", content=b"405 Method Not Allowed")
		check("501 page before any location", "PUT", "/", 501, content=b"<h1>501 Not Implemented</h1>")
		check("Directory without its final /", "GET", "/files", 301, header="Location: /files/")
		check("Directory listing (autoindex on)", "GET", "/files/", 200, content=b'<a href="notes.txt">')
		check("Listing shows every file", "GET", "/files/", 200, content=b'<a href="exemple.txt">')
		check("Directory with index serves it", "GET", "/", 200, content=b"<h1>Site 1</h1>")
		check("Directory without index nor autoindex", "GET", "/uploads/", 403)
		trap = "www/site1/files/<b>trap.txt"
		open(trap, "w").close()
		try:
			check("File names are escaped in the listing", "GET", "/files/", 200, content=b"&lt;b&gt;trap.txt")
			status, _, page = request("GET", "/files/")
			report("No raw HTML from a file name", b"<b>trap" not in page)
		finally:
			os.unlink(trap)
		check("Traversal refused", "GET", "/../../etc/passwd", 403)
		check("Encoded traversal refused", "GET", "/%2e%2e/%2E%2E/etc/passwd", 403)
		check("Malformed escape", "GET", "/bad%zz.html", 400)
		check("Truncated escape", "GET", "/bad%2", 400)
		check("Escaped NUL byte", "GET", "/index.html%00.txt", 400)
		check("Encoded letters", "GET", "/%63ontact.html", 200, content=b"Page de contact")
		spaced = "www/site1/files/mon fichier#1.txt"
		with open(spaced, "w") as created:
			created.write("avec espace")
		try:
			check("File name with a space and '#'", "GET", "/files/mon%20fichier%231.txt", 200, content=b"avec espace")
			check("Listing link is encoded", "GET", "/files/", 200, content=b'href="mon%20fichier%231.txt"')
		finally:
			os.unlink(spaced)
		check("Upload into upload_store", "POST", "/upload/test_routing.txt", 201, body=b"bonjour")
		report("Uploaded file is on disk", os.path.exists(UPLOADED))
		check("Uploaded file can be read back", "GET", "/upload/test_routing.txt", 200, content=b"bonjour")
		check("Uploaded file can be deleted", "DELETE", "/upload/test_routing.txt", 204)
		report("Deleted file is gone", not os.path.exists(UPLOADED))
	finally:
		server.terminate()
		server.wait()
		if os.path.exists(UPLOADED):
			os.unlink(UPLOADED)
	print(f"Passed: {sum(results)}/{len(results)}")
	sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
	main()
