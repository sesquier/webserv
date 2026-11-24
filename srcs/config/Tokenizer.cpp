#include "config/Tokenizer.hpp"
#include "config/ConfigError.hpp"
#include <cctype>

Tokenizer::Tokenizer(const std::string& content)
	: _content(content), _pos(0), _line(1)
{
}

Tokenizer::~Tokenizer()
{
}

std::vector<Token> Tokenizer::tokenize()
{
	_tokens.clear();
	_pos = 0;
	_line = 1;
	while (_pos < _content.size())
	{
		char c = _content[_pos];

		if (c == '\n')
		{
			_line++;
			_pos++;
		}
		else if (std::isspace(static_cast<unsigned char>(c)))
			_pos++;
		else if (c == '#')
			skipComment();
		else if (isSpecial(c))
			readSpecial();
		else if (isWordChar(c))
			readWord();
		else
			throw ConfigError(_line, "invalid character in configuration file");
	}
	return _tokens;
}

bool Tokenizer::isSpecial(char c)
{
	return c == '{' || c == '}' || c == ';';
}

// A word: printable characters, no separator, special character or '#'.
bool Tokenizer::isWordChar(char c)
{
	unsigned char uc = static_cast<unsigned char>(c);

	return std::isgraph(uc) && !isSpecial(c) && c != '#';
}

// Stops on the '\n' without consuming it, so the main loop counts the line.
void Tokenizer::skipComment()
{
	while (_pos < _content.size() && _content[_pos] != '\n')
		_pos++;
}

void Tokenizer::readSpecial()
{
	char		c = _content[_pos];
	Token::Type	type = Token::SEMICOLON;

	if (c == '{')
		type = Token::OPEN_BRACE;
	else if (c == '}')
		type = Token::CLOSE_BRACE;
	_tokens.push_back(Token(type, std::string(1, c), _line));
	_pos++;
}

void Tokenizer::readWord()
{
	size_t start = _pos;

	while (_pos < _content.size() && isWordChar(_content[_pos]))
		_pos++;
	_tokens.push_back(Token(Token::WORD, _content.substr(start, _pos - start), _line));
}
