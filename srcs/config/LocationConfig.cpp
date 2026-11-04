#include "config/LocationConfig.hpp"
#include <algorithm>

// Only initializes the plain types; real defaults are set after parsing.
LocationConfig::LocationConfig()
	: hasOwnRoot(false), maxBodySize(BODY_SIZE_UNSET), autoindex(false), redirectCode(0)
{
}

bool LocationConfig::isMethodAllowed(const std::string& method) const
{
	return std::find(methods.begin(), methods.end(), method) != methods.end();
}

// Compares whole segments: "/files" matches "/files" and "/files/a", not "/filesxyz".
bool LocationConfig::matchesPath(const std::string& requestPath) const
{
	if (path == "/")
		return true;
	if (requestPath.compare(0, path.size(), path) != 0)
		return false;
	return requestPath.size() == path.size() || requestPath[path.size()] == '/';
}

// "a..b" and "..." are ordinary names: only a whole ".." segment goes up a directory.
static bool hasParentSegment(const std::string& path)
{
	size_t pos = path.find("..");

	while (pos != std::string::npos)
	{
		bool startsSegment = (pos == 0 || path[pos - 1] == '/');
		bool endsSegment = (pos + 2 == path.size() || path[pos + 2] == '/');

		if (startsSegment && endsSegment)
			return true;
		pos = path.find("..", pos + 1);
	}
	return false;
}

// Own root: it replaces the location prefix. Inherited root: the whole path is added.
std::string resolvePath(const LocationConfig& location, const std::string& path)
{
	if (path.empty() || path[0] != '/' || hasParentSegment(path) || !location.matchesPath(path))
		return "";
	if (location.hasOwnRoot && location.path != "/")
		return location.root + path.substr(location.path.size());
	return location.root + path;
}
