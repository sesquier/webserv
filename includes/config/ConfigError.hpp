#ifndef CONFIGERROR_HPP
#define CONFIGERROR_HPP

#include <stdexcept>
#include <string>

// Thrown for any problem in the configuration file.
// what() returns "line <N>: <message>", or just "<message>"
// when the error is not tied to a line (missing file, no server...).
class ConfigError : public std::runtime_error
{
	public:
		// Two constructors: errors about the whole file (missing, no server...)
		// have no line, errors found in the content do.
		// 'explicit' forbids the compiler from silently turning a std::string
		// into a ConfigError (e.g. 'ConfigError e = msg;' does not compile).
		explicit ConfigError(const std::string& message);
		ConfigError(int line, const std::string& message);

	private:
		static std::string withLine(int line, const std::string& message);
};

#endif
