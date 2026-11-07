#include "config/ConfigFile.hpp"
#include "config/ConfigError.hpp"
#include <sys/stat.h>
#include <fstream>
#include <sstream>

std::string readConfigFile(const std::string& path)
{
	struct stat info;

	// ifstream happily "opens" a directory on Linux, so check the type first.
	if (stat(path.c_str(), &info) != 0)
		throw ConfigError("cannot access '" + path + "'");
	if (!S_ISREG(info.st_mode))
		throw ConfigError("'" + path + "' is not a regular file");

	std::ifstream file;
	file.open(path.c_str());
	if (!file.is_open())
		throw ConfigError("cannot open '" + path + "'");

	std::ostringstream content;
	content << file.rdbuf();
	if (file.bad())
		throw ConfigError("cannot read '" + path + "'");
	return content.str();
}
