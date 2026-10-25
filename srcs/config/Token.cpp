#include "config/Token.hpp"

Token::Token(Type type, const std::string& value, int line)
	: type(type), value(value), line(line)
{
}
