#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include "config/ConfigError.hpp"
#include "config/ServerConfig.hpp"

// Single header for the code outside the config module.
// Reads, parses and validates the configuration file.
// Throws ConfigError if the file is invalid.
std::vector<ServerConfig> loadConfig(const std::string& path);

#endif
