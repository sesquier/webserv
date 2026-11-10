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

#endif
