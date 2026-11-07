
import http.client
import os
import socket
import sys
import time
import concurrent.futures

HOST = "127.0.0.1"
PORT = 8080
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UPLOAD_DIR = os.path.join(BASE_DIR, "www", "site1", "uploads")

passed = 0
failed = 0


def test(name, condition, details=""):
	global passed, failed

	if condition:
		passed += 1
		print("[PASS] " + name)
	else:
		failed += 1
		print("[FAIL] " + name)
		if details:
			print(" " + details)


def request(method, path, body=None, headers=None):
	conn = http.client.HTTPConnection(HOST, PORT, timeout=3)

	try:
		conn.request(method, path, body=body, headers=headers or {})
		response = conn.getresponse()
		result = (
			response.status,
			response.read()
		)
		conn.close()
		return result
	except Exception:
		conn.close()
		raise

def test_concurrent_requests():
    def worker(_):
        status, body = request("GET", "/index.html")
        return status

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
            statuses = list(executor.map(worker, range(50)))

        success_count = sum(1 for status in statuses if status == 200)

        test(
            "50 concurrent requests",
            success_count == 50,
            str(success_count) + "/50 requests returned HTTP 200"
        )

    except Exception as error:
        test("50 concurrent requests", False, str(error))

def main():
	global passed, failed

	print("\n========== WEBSERV AUTOMATED TESTS ==========\n")

# Vérifier que le serveur est accessible.
	try:
		request("GET", "/")
		server_available = True
	except Exception as error:
		print("[ERROR] Cannot connect to webserv:", error)
		print("Start your server with ./webserv before running tests.")
		sys.exit(1)

# GET : fichier existant.
	try:
		status, body = request("GET", "/index.html")
		test("GET existing file returns 200", status == 200,
			"Received HTTP " + str(status))
	except Exception as error:
		test("GET existing file returns 200", False, str(error))

# GET : fichier inexistant.
	try:
		status, body = request("GET", "/this-file-does-not-exist.txt")
		test("GET missing file returns 404", status == 404,
			"Received HTTP " + str(status))
	except Exception as error:
		test("GET missing file returns 404", False, str(error))

# GET : tentative de traversal.
	try:
		status, body = request(
		"GET",
		"/../../etc/passwd"
		)
		test("GET path traversal is rejected",
			status in (400, 403, 404),
			"Received HTTP " + str(status))
	except Exception as error:
		test("GET path traversal is rejected", False, str(error))

# POST : upload valide.
	filename = "automated-test.txt"
	filepath = os.path.join(UPLOAD_DIR, filename)
	content = b"Automated POST test\n"

	try:
		if os.path.exists(filepath):
			os.remove(filepath)

		status, body = request(
			"POST",
			"/upload/" + filename,
			body=content
		)

		test("POST creates a file", status == 201,
			"Received HTTP " + str(status))

		exists = os.path.isfile(filepath)
		test("POST file exists on disk", exists)

		if exists:
			with open(filepath, "rb") as uploaded:
				actual_content = uploaded.read()

			test("POST preserves file content",
				actual_content == content)
	except Exception as error:
		test("POST upload", False, str(error))

# POST : nom de fichier invalide.
	try:
		status, body = request(
			"POST",
			"/upload/../invalid.txt",
			body=b"invalid"
		)
		test("POST invalid filename is rejected",
			status in (400, 404),
			"Received HTTP " + str(status))
	except Exception as error:
		test("POST invalid filename is rejected", False, str(error))

# DELETE : fichier existant.
	try:
		status, body = request(
			"DELETE",
			"/upload/" + filename
		)

		test("DELETE existing file returns 204",
			status == 204,
			"Received HTTP " + str(status))

		test("DELETE removes file from disk",
			not os.path.exists(filepath))
	except Exception as error:
		test("DELETE existing file", False, str(error))

# DELETE : fichier inexistant.
	try:
		status, body = request(
			"DELETE",
			"/upload/does-not-exist.txt"
		)

		test("DELETE missing file returns 404",
			status == 404,
			"Received HTTP " + str(status))
	except Exception as error:
		test("DELETE missing file returns 404", False, str(error))

# Méthode non implémentée.
	try:
		status, body = request("PUT", "/index.html")
		test("Unsupported method returns 501",
			status == 501,
			"Received HTTP " + str(status))
	except Exception as error:
		test("Unsupported method returns 501", False, str(error))

# Plusieurs requêtes successives.
	try:
		statuses = []
		for _ in range(5):
			status, body = request("GET", "/index.html")
			statuses.append(status)

		test("Multiple successive requests",
			all(status == 200 for status in statuses),
			"Received statuses: " + str(statuses))
	except Exception as error:
		test("Multiple successive requests", False, str(error))

	test_concurrent_requests()
# Résultat final.
	total = passed + failed

	print("\n========== TEST RESULTS ==========")
	print("Passed:", passed)
	print("Failed:", failed)
	print("Total: ", total)

	if failed:
		print("\nSome tests failed.")
		sys.exit(1)

	print("\nAll tests passed!")


if __name__ == "__main__":
	main()
