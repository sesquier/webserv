// CGI: what webserv needs to run a script and send back what it writes.
// The variables follow RFC 3875, the CGI standard.

#include "cgi.hpp"
#include "request.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

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

// "./www/site1/cgi-bin/hello.py" -> "./www/site1/cgi-bin".
static std::string directoryOf(const std::string& path)
{
	std::string::size_type slash = path.find_last_of('/');

	if (slash == std::string::npos)
		return (".");
	if (slash == 0)
		return ("/");
	return (path.substr(0, slash));
}

static void closePipe(int fds[2])
{
	close(fds[0]);
	close(fds[1]);
}

// In the child, which never returns from here: plugs the pipes on stdin and
// stdout, moves to the script's directory, then becomes the interpreter.
// If anything fails, the child exits without writing: the server sees an
// empty output and answers 502.
static void runScript(int toScript[2], int fromScript[2], const std::string& interpreter,
	const std::string& scriptPath, const std::vector<std::string>& env)
{
	if (dup2(toScript[0], STDIN_FILENO) == -1 || dup2(fromScript[1], STDOUT_FILENO) == -1)
		std::exit(1);
	// stdin and stdout are the pipes now: the original fds are not needed, and
	// a write end left open here would keep the server from ever seeing EOF.
	closePipe(toScript);
	closePipe(fromScript);
	if (chdir(directoryOf(scriptPath).c_str()) == -1)
		std::exit(1);

	// execve wants NULL-terminated arrays of C strings:
	// argv = { "/usr/bin/python3", "hello.py", NULL }, envp = { "NAME=value", ..., NULL }.
	std::string			script = fileName(scriptPath);
	std::vector<char*>	argv;
	std::vector<char*>	envp;

	argv.push_back(const_cast<char*>(interpreter.c_str()));
	argv.push_back(const_cast<char*>(script.c_str()));
	argv.push_back(NULL);
	for (size_t i = 0; i < env.size(); i++)
		envp.push_back(const_cast<char*>(env[i].c_str()));
	envp.push_back(NULL);

	execve(interpreter.c_str(), &argv[0], &envp[0]);
	std::exit(1);	// only reached if execve failed
}

bool startCgi(const std::string& interpreter, const std::string& scriptPath,
	const std::vector<std::string>& env, CgiProcess& process)
{
	int toScript[2];	// server -> script: the body, on the script's stdin
	int fromScript[2];	// script -> server: the output, from the script's stdout

	if (pipe(toScript) == -1)
		return (false);
	if (pipe(fromScript) == -1)
	{
		closePipe(toScript);
		return (false);
	}

	pid_t pid = fork();

	if (pid == -1)
	{
		closePipe(toScript);
		closePipe(fromScript);
		return (false);
	}
	if (pid == 0)
		runScript(toScript, fromScript, interpreter, scriptPath, env);

	// The parent keeps one end of each pipe: it writes the body, reads the output.
	close(toScript[0]);
	close(fromScript[1]);
	if (fcntl(toScript[1], F_SETFL, O_NONBLOCK) == -1
		|| fcntl(fromScript[0], F_SETFL, O_NONBLOCK) == -1)
	{
		close(toScript[1]);
		close(fromScript[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return (false);
	}
	process.pid = pid;
	process.inputFd = toScript[1];
	process.outputFd = fromScript[0];
	return (true);
}

static bool isDigit(char c)
{
	return (std::isdigit(static_cast<unsigned char>(c)) != 0);
}

// Header names ignore case: "Content-Type" and "content-type" are the same.
static std::string toLower(const std::string& text)
{
	std::string lower = text;

	for (std::string::size_type i = 0; i < lower.size(); i++)
		lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
	return (lower);
}

// "Status: 404 Not Found" -> 404 and "Not Found". Three digits, then the
// reason (optional), as on the first line of an HTTP response.
static bool parseStatus(const std::string& value, CgiOutput& result)
{
	if (value.size() < 3 || !isDigit(value[0]) || !isDigit(value[1])
		|| !isDigit(value[2]) || (value.size() > 3 && value[3] != ' '))
		return (false);

	int code = (value[0] - '0') * 100 + (value[1] - '0') * 10 + (value[2] - '0');

	if (code < 100 || code > 599)
		return (false);
	result.status = code;
	result.reason = (value.size() > 4) ? value.substr(4) : "";
	return (true);
}

// "Content-Length: 42" -> 42. Digits only; refuses a length that would overflow.
static bool parseLength(const std::string& value, std::string::size_type& length)
{
	if (value.empty() || value.size() > 18)
		return (false);
	length = 0;
	for (std::string::size_type i = 0; i < value.size(); i++)
	{
		if (!isDigit(value[i]))
			return (false);
		length = length * 10 + (value[i] - '0');
	}
	return (true);
}

bool parseCgiOutput(const std::string& output, CgiOutput& result)
{
	// The headers end at the first blank line. Scripts often write "\n" only
	// (Python's print), so "\n\n" is accepted as well as "\r\n\r\n".
	std::string::size_type	headersEnd = output.find("\r\n\r\n");
	std::string::size_type	lfHeadersEnd = output.find("\n\n");
	std::string::size_type	separatorSize = 4;

	if (lfHeadersEnd < headersEnd)
	{
		headersEnd = lfHeadersEnd;
		separatorSize = 2;
	}
	if (headersEnd == std::string::npos)
		return (false);

	result.status = 200;
	result.reason = "";
	result.contentType = "";
	result.headers = "";
	result.body = output.substr(headersEnd + separatorSize);

	std::istringstream	lines(output.substr(0, headersEnd));
	std::string			line;
	bool				hasStatus = false;
	bool				hasLocation = false;

	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		std::string::size_type colon = line.find(':');

		if (colon == std::string::npos || colon == 0)
			return (false);

		std::string				name = line.substr(0, colon);
		std::string				value = line.substr(colon + 1);
		std::string::size_type	start = value.find_first_not_of(" \t");

		value = (start == std::string::npos) ? "" : value.substr(start);
		if (toLower(name) == "status")
		{
			if (!parseStatus(value, result))
				return (false);
			hasStatus = true;
		}
		else if (toLower(name) == "content-type")
			result.contentType = value;
		else if (toLower(name) == "content-length")
		{
			// Without it, the end of the output (EOF) marks the end of the body.
			std::string::size_type length;

			if (!parseLength(value, length) || length > result.body.size())
				return (false);
			result.body.erase(length);
		}
		else if (toLower(name) != "connection")	// webserv closes every connection itself
		{
			if (toLower(name) == "location")
				hasLocation = true;
			result.headers += name + ": " + value + "\r\n";
		}
	}
	// A script that only gives "Location:" asks for a redirection.
	if (hasLocation && !hasStatus)
	{
		result.status = 302;
		result.reason = "Found";
	}
	// A body must say what it is.
	if (result.contentType.empty() && !result.body.empty())
		return (false);
	return (true);
}
