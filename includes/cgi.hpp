#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <vector>
#include "config/ServerConfig.hpp"

class Request;

// The environment of the script, as "NAME=value" strings: everything the
// script must know about the request, except the body, which goes on stdin.
std::vector<std::string>	buildCgiEnv(const Request& request, const std::string& urlPath,
								const std::string& scriptPath, const ListenAddress& address);

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
