import html
import os
import sys
from urllib.parse import parse_qs

# GET: the parameters come from the URL, in QUERY_STRING ("name=Bob").
# POST: a form sends them in the body, which arrives on stdin.
method = os.environ.get("REQUEST_METHOD", "GET")
query = os.environ.get("QUERY_STRING", "")
body = sys.stdin.read()

params = parse_qs(query)
if method == "POST":
    params.update(parse_qs(body))

name = params.get("name", ["inconnu"])[0]

# Headers, a blank line, then the page.
print("Content-Type: text/html")
print()
print("<!DOCTYPE html>")
print("<html><head><meta charset=\"utf-8\"><title>CGI</title></head><body>")
print("<h1>Bonjour " + html.escape(name) + " !</h1>")
print("<p>Methode : " + html.escape(method) + "</p>")
if body:
    print("<p>Body recu : " + html.escape(body) + "</p>")
print("<form method=\"post\" action=\"/cgi-bin/hello.py\">")
print("<input name=\"name\" placeholder=\"Ton prenom\">")
print("<button>Envoyer</button>")
print("</form>")
print("</body></html>")
