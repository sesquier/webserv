# Demo: opens a file by a relative path. It only works because webserv runs
# the script in its own directory (chdir), as the subject asks.
print("Content-Type: text/plain")
print()
with open("data.txt") as data:
    print(data.read().strip())
