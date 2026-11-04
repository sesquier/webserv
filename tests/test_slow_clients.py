import socket
import threading
import time
import sys

HOST = "127.0.0.1"
PORT = 8080

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
            print("       " + details)

def send_request():
    with socket.create_connection((HOST, PORT), timeout=3) as sock:
        sock.settimeout(3)

        sock.sendall(
            b"GET /index.html HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Connection: close\r\n"
            b"\r\n"
        )

        response = b""

        while True:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                break

            if not data:
                break

            response += data

        return response

def slow_client_test():
    try:
        slow_socket = socket.create_connection(
            (HOST, PORT),
            timeout=3
        )

        slow_socket.sendall(
            b"GET /index.html HTTP/1.1\r\n"
            b"Host: localhost\r\n"
        )

        # Le premier client ne termine pas sa requête.
        time.sleep(2)

        # Le deuxième client doit pouvoir obtenir sa réponse.
        response = send_request()

        success = b"200 OK" in response

        test(
            "Normal client responds while another client is slow",
            success,
            "The second client did not receive HTTP 200"
        )

        slow_socket.close()

    except Exception as error:
        test(
            "Normal client responds while another client is slow",
            False,
            str(error)
        )

def main():
    global passed, failed

    print("\n========== SLOW CLIENT TESTS ==========\n")

    slow_client_test()

    print("\n========== RESULTS ==========")
    print("Passed:", passed)
    print("Failed:", failed)
    print("Total: ", passed + failed)

    if failed:
        sys.exit(1)

    print("\nAll slow client tests passed!")


if __name__ == "__main__":
    main()