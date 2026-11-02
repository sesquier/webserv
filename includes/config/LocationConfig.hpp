#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

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
};

// Not implemented yet.
// Path on disk for a request path (without the query string).
// Returns an empty string if the path contains a ".." segment.
std::string resolvePath(const LocationConfig& location, const std::string& path);

#endif
