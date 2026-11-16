#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP

#include <string>
#include <vector>
#include "config/Token.hpp"

// ============================================================================
// Tokenizer (ou "lexer") : 1re étape du parsing de la configuration.
//
// Il transforme le texte brut en liste de Tokens. Il ne connaît PAS la
// grammaire : un ';' oublié ou une accolade en trop ne sont pas son problème,
// c'est le ConfigParser qui les détecte. Lui ne refuse qu'un caractère
// impossible (un caractère de contrôle, par exemple).
//
// Exemple : "listen 8080;  # port"
//   -> WORD("listen") WORD("8080") SEMICOLON(";")   (le commentaire disparaît)
// ============================================================================
class Tokenizer
{
	public:
		// explicit : interdit la conversion implicite string -> Tokenizer.
		explicit Tokenizer(const std::string& content);
		~Tokenizer();

		std::vector<Token> tokenize();

	private:
		std::string			_content;	// tout le fichier
		size_t				_pos;		// position de lecture dans _content
		int					_line;		// ligne courante, recopiée dans chaque Token
		std::vector<Token>	_tokens;	// le résultat

		static bool	isSpecial(char c);
		static bool	isWordChar(char c);

		void	skipComment();
		void	readSpecial();
		void	readWord();

		// Copie interdite (déclarée privée, jamais définie) : règle classique du
		// C++98, qui n'a pas "= delete". Personne n'a besoin de copier un Tokenizer.
		Tokenizer(const Tokenizer& other);
		Tokenizer& operator=(const Tokenizer& rhs);
};

#endif
