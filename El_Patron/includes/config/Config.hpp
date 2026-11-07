#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <iosfwd>
#include <string>
#include <vector>
#include "config/ConfigError.hpp"
#include "config/ServerConfig.hpp"

// Single header for the code outside the config module.
// Reads, parses and validates the configuration file.
// Throws ConfigError if the file is invalid.
std::vector<ServerConfig> loadConfig(const std::string& path);

// Prints the final configuration: every value, after defaults and inheritance.
void dumpConfig(const std::vector<ServerConfig>& servers, std::ostream& out);

#endif
