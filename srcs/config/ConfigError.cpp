#include "config/ConfigError.hpp"
#include <sstream>

// Le message est donné au constructeur de la classe mère : c'est lui que
// renverra what().
ConfigError::ConfigError(const std::string& message)
	: std::runtime_error(message)
{
}

ConfigError::ConfigError(int line, const std::string& message)
	: std::runtime_error(withLine(line, message))
{
}

// ostringstream : la façon C++98 de construire une chaîne avec un nombre
// (std::to_string n'existe qu'à partir de C++11).
std::string ConfigError::withLine(int line, const std::string& message)
{
	std::ostringstream oss;

	oss << "line " << line << ": " << message;
	return oss.str();
}
