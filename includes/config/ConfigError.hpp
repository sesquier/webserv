#ifndef CONFIGERROR_HPP
#define CONFIGERROR_HPP

#include <stdexcept>
#include <string>

// Thrown for any problem in the configuration file: "line <N>: <message>".
class ConfigError : public std::runtime_error
{
	public:
		// Without a line for errors about the whole file (missing, no server).
		explicit ConfigError(const std::string& message);
		ConfigError(int line, const std::string& message);

	private:
		static std::string withLine(int line, const std::string& message);
};

#endif
