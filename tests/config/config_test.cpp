// Standalone driver for the config module: reads a configuration file
// and prints what the module produced. Not part of the webserv binary.
// Usage: config_test [--tokens] [configuration file]

#include "config/ConfigError.hpp"
#include "config/ConfigFile.hpp"
#include "config/ConfigParser.hpp"
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
		std::cout << "server #" << i + 1 << std::endl;
		for (size_t j = 0; j < servers[i].locations.size(); j++)
			std::cout << "    location " << servers[i].locations[j].path << std::endl;
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
		Tokenizer			tokenizer(readConfigFile(path));
		std::vector<Token>	tokens = tokenizer.tokenize();

		if (showTokens)
			printTokens(tokens);
		ConfigParser parser(tokens);
		std::vector<ServerConfig> servers = parser.parse();
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
