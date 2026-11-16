#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <string>

// ============================================================================
// Token : la plus petite unité du fichier de configuration.
//
// Le parsing de la config se fait en deux étapes, comme dans un compilateur :
//   1. le Tokenizer découpe le texte en Tokens (mots, '{', '}', ';') ;
//   2. le ConfigParser lit cette liste de Tokens et vérifie la grammaire.
// Séparer les deux simplifie chaque étape : le parser ne voit plus jamais
// d'espaces, de retours à la ligne ni de commentaires.
// ============================================================================
struct Token
{
	// Les 4 sortes de tokens possibles : un mot ("listen", "8080", "/upload"...)
	// ou un des trois caractères qui structurent le fichier.
	enum Type
	{
		WORD,
		OPEN_BRACE,
		CLOSE_BRACE,
		SEMICOLON
	};

	Type		type;
	std::string	value;
	// Numéro de ligne d'origine : chaque message d'erreur du parser l'affiche
	// ("config error: line 12: ..."), même si le parser ne voit plus le texte.
	int			line;

	Token(Type type, const std::string& value, int line);
};

#endif
