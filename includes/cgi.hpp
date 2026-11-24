#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <vector>
#include <sys/types.h>
#include "config/ServerConfig.hpp"

class Request;

// The environment of the script, as "NAME=value" strings.
std::vector<std::string>	buildCgiEnv(const Request& request, const std::string& urlPath,
								const std::string& scriptPath, const ListenAddress& address);

// A request that a script must answer, filled by handleRequest.
struct CgiRequest
{
	bool						pending;		// true: no response yet, run the script
	std::string					interpreter;	// "/usr/bin/python3"
	std::string					scriptPath;		// "./www/site1/cgi-bin/hello.py"
	std::vector<std::string>	env;
	std::string					body;			// written to the script's stdin
	const LocationConfig*		location;		// for the error pages

	CgiRequest();
};

// A running script, seen from the server.
struct CgiProcess
{
	pid_t	pid;
	int		inputFd;	// write the request body here: it is the script's stdin
	int		outputFd;	// read the script's stdout here, until EOF
};

// Starts the script with two non-blocking pipes; false on failure (500).
bool						startCgi(const std::string& interpreter, const std::string& scriptPath,
								const std::vector<std::string>& env, CgiProcess& process);

// What the script wrote, split into the parts of an HTTP response.
struct CgiOutput
{
	int			status;			// 200, or what the script's "Status:" says
	std::string	reason;			// "Not Found"; empty if the script gave none
	std::string	contentType;
	std::string	headers;		// the script's other headers, each ending with "\r\n"
	std::string	body;
};

// Splits the script's output into status, headers and body; false if invalid (502).
bool						parseCgiOutput(const std::string& output, CgiOutput& result);

#endif
