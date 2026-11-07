#ifndef CONFIGFILE_HPP
#define CONFIGFILE_HPP

#include <string>

// Returns the whole content of the configuration file.
// Throws ConfigError if the path is missing, not a regular file or unreadable.
std::string readConfigFile(const std::string& path);

#endif
