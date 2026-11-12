# Same contract as hello.py: the request comes in the environment,
# the answer goes to stdout (headers, a blank line, the page).
name="inconnu"
case "$QUERY_STRING" in
    name=*) name="${QUERY_STRING#name=}" ; name="${name%%&*}" ;;
esac

# Escape HTML, as html.escape does in hello.py. The replacement is quoted:
# unquoted, bash 5.2 reads '&' in it as "the text that matched".
name="${name//&/"&amp;"}"
name="${name//</"&lt;"}"
name="${name//>/"&gt;"}"

echo "Content-Type: text/html"
echo ""
echo "<!DOCTYPE html>"
echo "<html><head><meta charset=\"utf-8\"><title>CGI Bash</title></head><body>"
echo "<h1>Bonjour $name, ici Bash !</h1>"
echo "<p>Interpreteur : /bin/bash</p>"
echo "</body></html>"
