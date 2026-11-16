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

// Boucle principale : on regarde le caractère courant et on décide quoi en
// faire. Chaque fonction appelée avance _pos elle-même : on ne peut donc
// jamais boucler sur place.
std::vector<Token> Tokenizer::tokenize()
{
	// Remise à zéro : tokenize() peut être appelé deux fois sans surprise.
	_tokens.clear();
	_pos = 0;
	_line = 1;
	while (_pos < _content.size())
	{
		char c = _content[_pos];

		// Le '\n' est traité à part pour compter les lignes.
		if (c == '\n')
		{
			_line++;
			_pos++;
		}
		// Le cast en unsigned char est obligatoire : passer un char négatif
		// (accent en UTF-8 par exemple) à isspace est un comportement indéfini.
		else if (std::isspace(static_cast<unsigned char>(c)))
			_pos++;
		else if (c == '#')
			skipComment();
		else if (isSpecial(c))
			readSpecial();
		else if (isWordChar(c))
			readWord();
		else
			// ConfigError est une exception : elle remonte jusqu'au main, qui
			// affiche "webserv: config error: line N: ..." et quitte avec 1.
			throw ConfigError(_line, "invalid character in configuration file");
	}
	return _tokens;
}

// Les 3 caractères qui structurent le fichier : ils forment un token à eux
// seuls, même collés à un mot ("8080;" donne "8080" puis ";").
bool Tokenizer::isSpecial(char c)
{
	return c == '{' || c == '}' || c == ';';
}

// Un mot est fait de caractères visibles (isgraph : ni espace, ni caractère
// de contrôle) qui ne sont ni spéciaux, ni le début d'un commentaire.
bool Tokenizer::isWordChar(char c)
{
	unsigned char uc = static_cast<unsigned char>(c);

	return std::isgraph(uc) && !isSpecial(c) && c != '#';
}

// Avance jusqu'à la fin de la ligne. On s'arrête SUR le '\n' sans le
// consommer : la boucle principale le verra et comptera la ligne.
void Tokenizer::skipComment()
{
	while (_pos < _content.size() && _content[_pos] != '\n')
		_pos++;
}

// Un caractère spécial devient un token d'un seul caractère.
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

// Lit le mot le plus long possible : tout jusqu'au premier caractère qui ne
// peut pas faire partie d'un mot (espace, ';', '{', '#'...).
void Tokenizer::readWord()
{
	size_t start = _pos;

	while (_pos < _content.size() && isWordChar(_content[_pos]))
		_pos++;
	_tokens.push_back(Token(Token::WORD, _content.substr(start, _pos - start), _line));
}
