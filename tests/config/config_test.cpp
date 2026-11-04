// Standalone driver for the config module: reads a configuration file
// and prints what the module produced. Not part of the webserv binary.
// Usage: config_test [--tokens] [configuration file]
//        config_test --route <configuration file> <request path>

#include "config/Config.hpp"
#include "config/ConfigFile.hpp"
#include "config/Tokenizer.hpp"
#include <iostream>
#include <iomanip>

static const char* typeName(Token::Type type)
{
	switch (type)
	{
		case Token::WORD:			return "WORD";
		case Token::OPEN_BRACE:		return "OPEN_BRACE";
		case Token::CLOSE_BRACE:	return "CLOSE_BRACE";
		case Token::SEMICOLON:		return "SEMICOLON";
	}
	return "UNKNOWN";
}

static void printTokens(const std::vector<Token>& tokens)
{
	for (size_t i = 0; i < tokens.size(); i++)
	{
		std::cout << std::setw(4) << tokens[i].line << "  "
				  << std::left << std::setw(12) << typeName(tokens[i].type)
				  << std::right << tokens[i].value << std::endl;
	}
}

// Shows which location and which file the first server picks for a path.
static int printRoute(const std::string& file, const std::string& path)
{
	try
	{
		std::vector<ServerConfig>	servers = loadConfig(file);
		const LocationConfig&		location = servers[0].findLocation(path);
		std::string					disk = resolvePath(location, path);

		std::cout << location.path << " " << (disk.empty() ? "REFUSED" : disk) << std::endl;
	}
	catch (const ConfigError& e)
	{
		std::cerr << "webserv: config error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}

int main(int argc, char** argv)
{
	int		arg = 1;
	bool	showTokens = false;

	if (argc == 4 && std::string(argv[1]) == "--route")
		return printRoute(argv[2], argv[3]);

	if (arg < argc && std::string(argv[arg]) == "--tokens")
	{
		showTokens = true;
		arg++;
	}
	if (argc - arg > 1)
	{
		std::cerr << "usage: " << argv[0] << " [--tokens] [configuration file]" << std::endl;
		return 1;
	}
	std::string path = (arg < argc) ? argv[arg] : "config/default.conf";
	try
	{
		if (showTokens)
		{
			Tokenizer tokenizer(readConfigFile(path));
			printTokens(tokenizer.tokenize());
		}
		std::vector<ServerConfig> servers = loadConfig(path);
		if (!showTokens)
			dumpConfig(servers, std::cout);
	}
	catch (const ConfigError& e)
	{
		std::cerr << "webserv: config error: " << e.what() << std::endl;
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << "webserv: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
