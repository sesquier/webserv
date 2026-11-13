import os
import sys

# Demo: shows everything the script receives from webserv. The subject asks
# that "the full request and arguments" be available to the CGI: here they
# are, as environment variables, and the body on stdin.
body = sys.stdin.buffer.read()

print("Content-Type: text/plain")
print()
for name in sorted(os.environ):
    print(name + "=" + os.environ[name])
print("BODY_BYTES=" + str(len(body)))
