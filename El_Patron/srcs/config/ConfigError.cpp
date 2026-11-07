#include "config/ConfigError.hpp"
#include <sstream>

ConfigError::ConfigError(const std::string& message)
	: std::runtime_error(message)
{
}

ConfigError::ConfigError(int line, const std::string& message)
	: std::runtime_error(withLine(line, message))
{
}

std::string ConfigError::withLine(int line, const std::string& message)
{
	std::ostringstream oss;

	oss << "line " << line << ": " << message;
	return oss.str();
}
