#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <vector>
#include <sys/types.h>
#include "config/ServerConfig.hpp"

class Request;

// The environment of the script, as "NAME=value" strings: everything the
// script must know about the request, except the body, which goes on stdin.
std::vector<std::string>	buildCgiEnv(const Request& request, const std::string& urlPath,
								const std::string& scriptPath, const ListenAddress& address);

// A running script, seen from the server.
struct CgiProcess
{
	pid_t	pid;
	int		inputFd;	// write the request body here: it is the script's stdin
	int		outputFd;	// read the script's stdout here, until EOF
};

// Starts "interpreter script" in the script's directory, with env as its
// environment. Both fds are non-blocking, for epoll. Returns false if the
// pipes or the process cannot be created (the server then answers 500).
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

// Reads the output of a script: headers, a blank line, the body.
// Returns false if it does not follow CGI (the server then answers 502).
bool						parseCgiOutput(const std::string& output, CgiOutput& result);

#endif
