#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "config/LocationConfig.hpp"

// One "listen" directive.
struct ListenAddress
{
	std::string	host;	// "0.0.0.0" when no IP is given
	int			port;

	ListenAddress();
};

// Settings of one "server { ... }" block.
struct ServerConfig
{
	std::vector<ListenAddress>	listens;
	std::string					root;
	std::string					index;
	size_t						maxBodySize;	// in bytes
	std::map<int, std::string>	errorPages;		// 404 -> path on disk
	std::vector<LocationConfig>	locations;

	ServerConfig();

	// Not implemented yet.
	// Longest matching prefix for a request path (without the query string).
	// Never fails: every server has a "/" location after parsing.
	const LocationConfig&	findLocation(const std::string& path) const;
};

#endif
