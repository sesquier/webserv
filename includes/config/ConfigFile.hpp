#ifndef CONFIGFILE_HPP
#define CONFIGFILE_HPP

#include <string>

// Returns the content of the configuration file; throws ConfigError.
std::string readConfigFile(const std::string& path);

#endif
