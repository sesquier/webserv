// CGI: what webserv needs to run a script and send back what it writes.
// The variables follow RFC 3875, the CGI standard.

#include "cgi.hpp"
#include "request.hpp"
#include <sstream>

static std::string numberToString(size_t number)
{
	std::ostringstream text;

	text << number;
	return (text.str());
}

// "/cgi-bin/hello.py?name=Bob" -> "name=Bob". Empty without a '?'.
static std::string queryString(const std::string& target)
{
	std::string::size_type mark = target.find('?');

	if (mark == std::string::npos)
		return ("");
	return (target.substr(mark + 1));
}

// "./www/site1/cgi-bin/hello.py" -> "hello.py".
static std::string fileName(const std::string& path)
{
	return (path.substr(path.find_last_of('/') + 1));
}

std::vector<std::string> buildCgiEnv(const Request& request, const std::string& urlPath,
	const std::string& scriptPath, const ListenAddress& address)
{
	std::vector<std::string> env;

	// The request: method, protocol, and what follows the '?' in the URL.
	env.push_back("REQUEST_METHOD=" + request.getMethod());
	env.push_back("QUERY_STRING=" + queryString(request.getTarget()));
	env.push_back("SERVER_PROTOCOL=" + request.getVersion());

	// The script: its path in the URL, and its file. The script runs in its own
	// directory, so the file is named from there: "hello.py".
	env.push_back("SCRIPT_NAME=" + urlPath);
	env.push_back("SCRIPT_FILENAME=" + fileName(scriptPath));

	// The body arrives on stdin; its size tells the script how much to read.
	if (request.getMethod() == "POST")
		env.push_back("CONTENT_LENGTH=" + numberToString(request.getBody().size()));

	// The server that runs the script.
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_SOFTWARE=webserv");
	env.push_back("SERVER_NAME=" + address.host);
	env.push_back("SERVER_PORT=" + numberToString(address.port));

	// php-cgi refuses to run a script without it.
	env.push_back("REDIRECT_STATUS=200");
	return (env);
}
