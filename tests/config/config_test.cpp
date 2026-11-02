// Standalone driver for the config module: reads a configuration file
// and prints what the module produced. Not part of the webserv binary.
// Usage: config_test [--tokens] [configuration file]

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

// Temporary summary until the real dump function exists.
static void printServers(const std::vector<ServerConfig>& servers)
{
	for (size_t i = 0; i < servers.size(); i++)
	{
		std::cout << "server #" << i + 1
				  << "  root=" << servers[i].root
				  << "  index=" << servers[i].index << std::endl;
		for (size_t j = 0; j < servers[i].locations.size(); j++)
		{
			const LocationConfig& location = servers[i].locations[j];

			std::cout << "    location " << location.path
					  << "  root=" << location.root
					  << "  hasOwnRoot=" << location.hasOwnRoot
					  << "  index=" << location.index
					  << "  autoindex=" << location.autoindex
					  << "  methods=";
			for (size_t k = 0; k < location.methods.size(); k++)
				std::cout << location.methods[k] << " ";
			std::cout << std::endl;
		}
	}
}

int main(int argc, char** argv)
{
	int		arg = 1;
	bool	showTokens = false;

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
			printServers(servers);
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
