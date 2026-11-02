import socket
import sys

HOST = "127.0.0.1"
PORT = 8080

passed = 0
failed = 0


def test(name, expected, actual):
	global passed, failed

	if actual == expected:
		passed += 1
		print("[PASS] " + name)
	else:
		failed += 1
		print(
		"[FAIL] " + name
		+ " - expected HTTP " + str(expected)
		+ ", got " + str(actual)
		)


def raw_request(data):
	response = b""

	with socket.create_connection((HOST, PORT), timeout=3) as sock:
		sock.settimeout(3)
		sock.sendall(data)

		while True:
			try:
				chunk = sock.recv(4096)
			except socket.timeout:
				break

			if not chunk:
				break

			response += chunk

	if not response:
		return 0

	try:
		status_line = response.split(b"\r\n", 1)[0]
		return int(status_line.split(b" ")[1])
	except (IndexError, ValueError):
		return 0


def main():
	global passed, failed

	print("\n========== HTTP ERROR TESTS ==========\n")

# 1. Méthode valide, mais version HTTP invalide.
	status = raw_request(
		b"GET / HTTP/9.9\r\n"
		b"Host: localhost\r\n"
		b"\r\n"
	)
	test("Unsupported HTTP version", 400, status)

# 2. Requête sans ligne de requête valide.
	status = raw_request(
		b"THIS IS NOT A VALID REQUEST\r\n"
		b"Host: localhost\r\n"
		b"\r\n"
	)
	test("Malformed request line", 400, status)

# 3. En-tête sans deux-points.
	status = raw_request(
		b"GET / HTTP/1.1\r\n"
		b"Host: localhost\r\n"
		b"InvalidHeader\r\n"
		b"\r\n"
	)
	test("Malformed header", 400, status)

# 4. Content-Length invalide.
	status = raw_request(
		b"POST /upload/test.txt HTTP/1.1\r\n"
		b"Host: localhost\r\n"
		b"Content-Length: abc\r\n"
		b"\r\n"
	)
	test("Invalid Content-Length", 400, status)

# 5. Content-Length en double.
	status = raw_request(
		b"POST /upload/test.txt HTTP/1.1\r\n"
		b"Host: localhost\r\n"
		b"Content-Length: 1\r\n"
		b"Content-Length: 1\r\n"
		b"\r\n"
		b"x"
	)
	test("Duplicate Content-Length", 400, status)

	print("\n========== RESULTS ==========")
	print("Passed:", passed)
	print("Failed:", failed)
	print("Total: ", passed + failed)

	if failed:
		sys.exit(1)

	print("\nAll HTTP error tests passed!")


if __name__ == "__main__":
	main()