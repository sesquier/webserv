#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP

#include <string>
#include <vector>
#include "config/Token.hpp"

// Splits the raw text of the configuration file into tokens.
// It knows nothing about the grammar: structural errors
// (missing ';', unbalanced braces...) are reported by the parser.
class Tokenizer
{
	public:
		explicit Tokenizer(const std::string& content);
		~Tokenizer();

		std::vector<Token> tokenize();

	private:
		std::string			_content;
		size_t				_pos;
		int					_line;
		std::vector<Token>	_tokens;

		static bool	isSpecial(char c);
		static bool	isWordChar(char c);

		void	skipComment();
		void	readSpecial();
		void	readWord();

		Tokenizer(const Tokenizer& other);
		Tokenizer& operator=(const Tokenizer& rhs);
};

#endif
