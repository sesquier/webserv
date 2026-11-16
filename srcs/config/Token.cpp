#include "config/Token.hpp"

// Constructeur avec liste d'initialisation : chaque membre est construit
// directement avec sa valeur (pas de construction par défaut puis copie).
Token::Token(Type type, const std::string& value, int line)
	: type(type), value(value), line(line)
{
}
