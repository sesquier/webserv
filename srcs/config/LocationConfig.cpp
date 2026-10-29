#include "config/LocationConfig.hpp"

// Only initializes the plain types; real defaults are set after parsing.
LocationConfig::LocationConfig()
	: hasOwnRoot(false), maxBodySize(0), autoindex(false), redirectCode(0)
{
}
