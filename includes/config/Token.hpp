#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <string>

// Smallest unit of the configuration file, produced by the Tokenizer.
struct Token
{
	enum Type
	{
		WORD,
		OPEN_BRACE,
		CLOSE_BRACE,
		SEMICOLON
	};

	Type		type;
	std::string	value;
	int			line;

	Token(Type type, const std::string& value, int line);
};

#endif
