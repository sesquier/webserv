#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include <map>
#include <set>
#include <string>
#include <vector>
#include "config/Token.hpp"
#include "config/ServerConfig.hpp"

// ============================================================================
// ConfigParser : transforme la liste de tokens en objets ServerConfig.
//
// Parser à descente récursive : une fonction par règle de la grammaire.
//   config    := server+
//   server    := "server" "{" (location | directive)* "}"
//   location  := "location" CHEMIN "{" directive* "}"
//   directive := NOM ARG* ";"
//
// Les détails sont expliqués dans srcs/config/ConfigParser.cpp.
// ============================================================================
class ConfigParser
{
	public:
		explicit ConfigParser(const std::vector<Token>& tokens);
		~ConfigParser();

		std::vector<ServerConfig> parse();

	private:
		// Une instruction "nom arg1 arg2 ;", lue mais pas encore interprétée.
		// On garde sa ligne pour les messages d'erreur.
		struct Directive
		{
			std::string					name;
			std::vector<std::string>	args;
			int							line;
		};

		// Type "pointeur sur une fonction membre de ConfigParser qui prend
		// (ServerConfig&, const Directive&)". Le typedef rend les maps lisibles.
		typedef void (ConfigParser::*ServerHandler)(ServerConfig&, const Directive&);
		typedef void (ConfigParser::*LocationHandler)(LocationConfig&, const Directive&);

		std::vector<Token>						_tokens;
		size_t									_pos;				// token courant
		std::map<std::string, ServerHandler>	_serverHandlers;	// "listen" -> parseListen...
		std::map<std::string, LocationHandler>	_locationHandlers;	// "methods" -> parseMethods...
		std::set<std::string>					_seenInServer;		// directives déjà vues dans ce server
		std::set<std::string>					_seenInLocation;	// directives déjà vues dans cette location
		std::vector<ListenAddress>				_usedAddresses;		// tous les 'listen' du fichier

		// Se déplacer dans les tokens
		bool			isAtEnd() const;
		const Token&	current() const;
		void			advance();
		bool			isWord(const std::string& value) const;
		void			expect(Token::Type type, const std::string& what);
		bool			isBlockEnd(int openLine) const;

		// Les règles de la grammaire
		ServerConfig	parseServer();
		LocationConfig	parseLocation();
		Directive		parseDirective();
		void			rejectNestedBlock(const std::string& context) const;
		void			addLocation(ServerConfig& server);
		void			requireEssentials(const ServerConfig& server, int openLine) const;

		// Aiguillage vers la fonction de chaque directive
		void	initHandlers();
		void	applyServerDirective(ServerConfig& server, const Directive& directive);
		void	applyLocationDirective(LocationConfig& location, const Directive& directive);
		void	requireArgCount(const Directive& directive, size_t expected) const;
		void	requireArgCount(const Directive& directive, size_t min, size_t max) const;
		void	rejectDuplicate(std::set<std::string>& seen, const Directive& directive) const;

		// Une fonction par directive
		void	parseServerRoot(ServerConfig& server, const Directive& directive);
		void	parseLocationRoot(LocationConfig& location, const Directive& directive);
		void	parseServerIndex(ServerConfig& server, const Directive& directive);
		void	parseLocationIndex(LocationConfig& location, const Directive& directive);
		void	parseAutoindex(LocationConfig& location, const Directive& directive);
		void	parseMethods(LocationConfig& location, const Directive& directive);
		void	parseUploadStore(LocationConfig& location, const Directive& directive);
		void	parseReturn(LocationConfig& location, const Directive& directive);
		void	parseCgi(LocationConfig& location, const Directive& directive);
		void	parseServerErrorPage(ServerConfig& server, const Directive& directive);
		void	parseLocationErrorPage(LocationConfig& location, const Directive& directive);
		void	addErrorPage(std::map<int, std::string>& errorPages, const Directive& directive);
		void	parseServerBodySize(ServerConfig& server, const Directive& directive);
		void	parseLocationBodySize(LocationConfig& location, const Directive& directive);
		size_t	readBodySize(const Directive& directive);
		void	parseListen(ServerConfig& server, const Directive& directive);

		// Valeurs par défaut, héritage, validation. static : ces fonctions
		// n'utilisent aucun membre du parser.
		static void			finalizeServer(ServerConfig& server);
		static void			applyServerDefaults(ServerConfig& server);
		static void			ensureRootLocation(ServerConfig& server);
		static void			inheritFromServer(LocationConfig& location, const ServerConfig& server);
		static void			applyLocationDefaults(LocationConfig& location);
		static std::string	normalizeLocationPath(const std::string& path);
		static bool			parseNumber(const std::string& text, long& value);
		static bool			isValidIpv4(const std::string& ip);
		static bool			isValidIpv4Part(const std::string& part);
		static bool			addressesConflict(const ListenAddress& a, const ListenAddress& b);

		// Copie interdite (forme canonique C++98).
		ConfigParser(const ConfigParser& other);
		ConfigParser& operator=(const ConfigParser& rhs);
};

#endif
