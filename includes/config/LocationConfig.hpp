#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// Value of maxBodySize until the directive is read or inherited. 0 cannot play
// this role: it is a real limit. The parser never produces this value.
const size_t BODY_SIZE_UNSET = static_cast<size_t>(-1);

// Settings of one "location <prefix> { ... }" block.
// Read-only for the HTTP side: every value is validated, converted
// and inherited from the server by the config module.
struct LocationConfig
{
	std::string							path;			// "/upload", never a trailing '/' except "/"
	std::string							root;
	bool								hasOwnRoot;		// false if root is inherited from the server
	std::string							index;
	size_t								maxBodySize;	// in bytes
	std::map<int, std::string>			errorPages;		// 404 -> path on disk
	std::vector<std::string>			methods;		// "GET", "POST", "DELETE"
	bool								autoindex;
	int									redirectCode;	// 0 = no redirection
	std::string							redirectUrl;
	std::string							uploadStore;	// empty = upload disabled
	std::map<std::string, std::string>	cgi;			// ".py" -> "/usr/bin/python3"

	LocationConfig();

	bool	isMethodAllowed(const std::string& method) const;
	bool	matchesPath(const std::string& requestPath) const;
};

// Path on disk for a request path: without the query string and percent-decoded,
// because "%2e%2e" must already be ".." when it is checked here.
// Returns an empty string if the path must not be served: it contains a ".."
// segment, does not start with '/' or does not belong to this location.
std::string resolvePath(const LocationConfig& location, const std::string& path);

#endif
