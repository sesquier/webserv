#include "config/LocationConfig.hpp"
#include <algorithm>

// Only initializes the plain types; real defaults are set after parsing.
LocationConfig::LocationConfig()
	: hasOwnRoot(false), maxBodySize(0), autoindex(false), redirectCode(0)
{
}

bool LocationConfig::isMethodAllowed(const std::string& method) const
{
	return std::find(methods.begin(), methods.end(), method) != methods.end();
}
