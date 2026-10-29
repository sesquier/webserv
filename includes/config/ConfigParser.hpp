#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include <map>
#include <string>
#include <vector>
#include "config/Token.hpp"
#include "config/ServerConfig.hpp"

// Recursive descent parser: turns the token list into ServerConfig objects.
// Grammar:
//   config    := server+
//   server    := "server" "{" (location | directive)* "}"
//   location  := "location" PATH "{" directive* "}"
//   directive := NAME ARG* ";"
class ConfigParser
{
	public:
		explicit ConfigParser(const std::vector<Token>& tokens);
		~ConfigParser();

		std::vector<ServerConfig> parse();

	private:
		// One "name arg1 arg2 ;" statement, before interpretation.
		struct Directive
		{
			std::string					name;
			std::vector<std::string>	args;
			int							line;
		};

		// Pointers to member functions: one handler per directive name.
		typedef void (ConfigParser::*ServerHandler)(ServerConfig&, const Directive&);
		typedef void (ConfigParser::*LocationHandler)(LocationConfig&, const Directive&);

		std::vector<Token>						_tokens;
		size_t									_pos;
		std::map<std::string, ServerHandler>	_serverHandlers;
		std::map<std::string, LocationHandler>	_locationHandlers;

		// Token navigation
		bool			isAtEnd() const;
		const Token&	current() const;
		void			advance();
		bool			isWord(const std::string& value) const;
		void			expect(Token::Type type, const std::string& what);
		bool			isBlockEnd(int openLine) const;

		// Grammar rules
		ServerConfig	parseServer();
		LocationConfig	parseLocation();
		Directive		parseDirective();
		void			rejectNestedBlock(const std::string& context) const;

		// Dispatch to the directive handlers
		void	initHandlers();
		void	applyServerDirective(ServerConfig& server, const Directive& directive);
		void	applyLocationDirective(LocationConfig& location, const Directive& directive);

		// Directive handlers (placeholders until each directive is implemented)
		void	ignoreServerDirective(ServerConfig&, const Directive&);
		void	ignoreLocationDirective(LocationConfig&, const Directive&);

		static std::string	normalizeLocationPath(const std::string& path);

		ConfigParser(const ConfigParser& other);
		ConfigParser& operator=(const ConfigParser& rhs);
};

#endif
