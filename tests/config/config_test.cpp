// Standalone driver for the config module: reads a configuration file
// and prints what the module produced. Not part of the webserv binary.

#include "config/ConfigError.hpp"
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

int main(int argc, char** argv)
{
	if (argc > 2)
	{
		std::cerr << "usage: " << argv[0] << " [configuration file]" << std::endl;
		return 1;
	}
	std::string path = (argc == 2) ? argv[1] : "config/default.conf";
	try
	{
		Tokenizer tokenizer(readConfigFile(path));
		printTokens(tokenizer.tokenize());
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
