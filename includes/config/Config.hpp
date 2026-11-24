#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <iosfwd>
#include <string>
#include <vector>
#include "config/ConfigError.hpp"
#include "config/ServerConfig.hpp"

// Reads, parses and validates the configuration file; throws ConfigError.
std::vector<ServerConfig> loadConfig(const std::string& path);

// Prints the final configuration: every value, after defaults and inheritance.
void dumpConfig(const std::vector<ServerConfig>& servers, std::ostream& out);

#endif
