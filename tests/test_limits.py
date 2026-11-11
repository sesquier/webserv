import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 8080

# client_max_body_size of the /upload location in config/default.conf
UPLOAD_MAX_BODY_SIZE = 10 * 1024 * 1024
MAX_HEADERS_SIZE = 16 * 1024

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

	sock = socket.create_connection((HOST, PORT), timeout=5)
	sock.settimeout(5)

	try:
# Envoi par morceaux pour permettre au serveur
# de traiter la requête pendant sa réception.
		chunk_size = 4096

		for offset in range(0, len(data), chunk_size):
			try:
				sock.sendall(data[offset:offset + chunk_size])
			except (ConnectionResetError, BrokenPipeError, OSError):
				break

# Récupération de la réponse.
		while True:
			try:
				chunk = sock.recv(4096)
			except (socket.timeout, ConnectionResetError, OSError):
				break

			if not chunk:
				break

			response += chunk

	finally:
		sock.close()

	if not response:
		return 0

	try:
		status_line = response.split(b"\r\n", 1)[0]
		return int(status_line.split(b" ")[1])
	except (IndexError, ValueError):
		return 0


def main():
	global passed, failed

	print("\n========== HTTP SIZE LIMIT TESTS ==========\n")

# Test 1 : corps trop volumineux.
	body = b"A" * (UPLOAD_MAX_BODY_SIZE + 1)

	request = (
		b"POST /upload/oversized-test.txt HTTP/1.1\r\n"
		b"Host: localhost\r\n"
		b"Content-Length: "
		+ str(len(body)).encode("ascii")
		+ b"\r\n\r\n"
		+ body
	)

	try:
		status = raw_request(request)
		test("Oversized request body returns 413", 413, status)
	except OSError as error:
		test("Oversized request body returns 413", 413, 0)
		print(" Error:", error)

# Test 2 : en-têtes trop volumineux.
	large_header = b"X-Test: " + b"A" * MAX_HEADERS_SIZE

	request = (
		b"GET / HTTP/1.1\r\n"
		b"Host: localhost\r\n"
		+ large_header
		+ b"\r\n\r\n"
	)

	try:
		status = raw_request(request)
		test("Oversized headers return 431", 431, status)
	except OSError as error:
		test("Oversized headers return 431", 431, 0)
		print(" Error:", error)

	print("\n========== RESULTS ==========")
	print("Passed:", passed)
	print("Failed:", failed)
	print("Total: ", passed + failed)

	if failed:
		sys.exit(1)

	print("\nAll size limit tests passed!")


if __name__ == "__main__":
	main()