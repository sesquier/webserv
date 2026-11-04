#include "config/ServerConfig.hpp"

ListenAddress::ListenAddress()
	: port(0)
{
}

// Only initializes the plain types; real defaults are set after parsing.
ServerConfig::ServerConfig()
	: maxBodySize(BODY_SIZE_UNSET)
{
}

const LocationConfig& ServerConfig::findLocation(const std::string& path) const
{
	const LocationConfig* best = NULL;

	for (size_t i = 0; i < locations.size(); i++)
	{
		if (locations[i].matchesPath(path)
			&& (best == NULL || locations[i].path.size() > best->path.size()))
			best = &locations[i];
	}
	return *best;
}
