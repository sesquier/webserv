# Demo: a script that crashes after its headers. Its exit status is not 0,
# so webserv answers 502 Bad Gateway instead of a blank page, and keeps
# serving the other clients.
print("Content-Type: text/html")
print()
print("<p>Ce texte ne sera jamais vu : le script plante juste apres.</p>")
raise RuntimeError("erreur volontaire")
