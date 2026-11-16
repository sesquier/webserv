// ============================================================================
// ConfigParser : 2e étape du parsing de la configuration.
//
// Technique : un parser "à descente récursive". Chaque règle de la grammaire
// devient une fonction, et une fonction appelle celles des règles qu'elle
// contient :
//   parse()          config    := server+
//   parseServer()    server    := "server" "{" (location | directive)* "}"
//   parseLocation()  location  := "location" CHEMIN "{" directive* "}"
//   parseDirective() directive := NOM ARG* ";"
//
// Ensuite, chaque directive est confiée à SA fonction de traitement, trouvée
// dans une table (std::map nom -> pointeur sur fonction membre). Ajouter une
// directive = écrire une fonction + une ligne dans initHandlers().
//
// Toute erreur lance une ConfigError avec le numéro de ligne.
// ============================================================================

#include "config/ConfigParser.hpp"
#include "config/ConfigError.hpp"
#include <cctype>
#include <climits>
#include <cstdlib>

ConfigParser::ConfigParser(const std::vector<Token>& tokens)
	: _tokens(tokens), _pos(0)
{
	initHandlers();
}

ConfigParser::~ConfigParser()
{
}

// ===== Point d'entrée =====

// Le fichier n'est qu'une suite de blocs "server". Tout autre mot au premier
// niveau est une erreur, et un fichier sans aucun server aussi.
std::vector<ServerConfig> ConfigParser::parse()
{
	std::vector<ServerConfig> servers;

	_pos = 0;
	_usedAddresses.clear();
	while (!isAtEnd())
	{
		if (!isWord("server"))
			throw ConfigError(current().line,
				"unexpected '" + current().value + "', expected 'server'");
		servers.push_back(parseServer());
	}
	if (servers.empty())
		throw ConfigError("no server block");
	return servers;
}

// ===== Se déplacer dans la liste de tokens =====
// Ces petites fonctions évitent de manipuler _pos partout, et donc les
// lectures hors du tableau.

bool ConfigParser::isAtEnd() const
{
	return _pos >= _tokens.size();
}

// Le token courant. À n'appeler qu'après avoir vérifié isAtEnd().
const Token& ConfigParser::current() const
{
	return _tokens[_pos];
}

void ConfigParser::advance()
{
	if (!isAtEnd())
		_pos++;
}

// Le token courant est-il ce mot précis ("server", "location"...) ?
bool ConfigParser::isWord(const std::string& value) const
{
	return !isAtEnd() && current().type == Token::WORD && current().value == value;
}

// "Il DOIT y avoir tel token ici" : on le consomme, sinon on lance une erreur
// qui dit ce qu'on attendait et ce qu'on a trouvé.
void ConfigParser::expect(Token::Type type, const std::string& what)
{
	if (isAtEnd())
		throw ConfigError(_tokens.back().line, "unexpected end of file, expected " + what);
	if (current().type != type)
		throw ConfigError(current().line,
			"expected " + what + ", found '" + current().value + "'");
	advance();
}

// Vrai sur '}'. Si le fichier se termine avant, le bloc n'a jamais été fermé :
// on donne la ligne où il a été OUVERT, c'est là qu'il faut aller corriger.
bool ConfigParser::isBlockEnd(int openLine) const
{
	if (isAtEnd())
		throw ConfigError(openLine, "unclosed block, missing '}'");
	return current().type == Token::CLOSE_BRACE;
}

// ===== Les règles de la grammaire =====

// server { ... } : on lit des locations et des directives jusqu'à '}'.
ServerConfig ConfigParser::parseServer()
{
	ServerConfig	server;
	int				openLine = current().line;

	_seenInServer.clear();		// pour détecter une directive écrite deux fois
	advance();					// le mot "server"
	expect(Token::OPEN_BRACE, "'{' after 'server'");
	while (!isBlockEnd(openLine))
	{
		rejectNestedBlock("server");
		if (isWord("location"))
			addLocation(server);
		else
			applyServerDirective(server, parseDirective());
	}
	// Vérifié à la fin du bloc : les directives peuvent venir dans n'importe
	// quel ordre, et les locations avant les directives du server.
	requireEssentials(server, openLine);
	finalizeServer(server);
	advance();					// le '}'
	return server;
}

// Deux locations au même chemin seraient ambiguës : refusé. Les chemins sont
// comparés après normalisation : "/files" et "/files/" sont la même location.
void ConfigParser::addLocation(ServerConfig& server)
{
	int				line = current().line;
	LocationConfig	location = parseLocation();

	for (size_t i = 0; i < server.locations.size(); i++)
	{
		if (server.locations[i].path == location.path)
			throw ConfigError(line, "duplicate location '" + location.path + "'");
	}
	server.locations.push_back(location);
}

// Un server sans port ou sans dossier racine ne peut rien servir.
void ConfigParser::requireEssentials(const ServerConfig& server, int openLine) const
{
	if (server.listens.empty())
		throw ConfigError(openLine, "server has no 'listen' directive");
	if (server.root.empty())
		throw ConfigError(openLine, "server has no 'root' directive");
}

// location /chemin { ... } : seulement des directives, pas de bloc imbriqué.
LocationConfig ConfigParser::parseLocation()
{
	LocationConfig	location;
	int				openLine = current().line;

	_seenInLocation.clear();
	advance();					// le mot "location"
	if (isAtEnd() || current().type != Token::WORD)
		throw ConfigError(openLine, "missing path after 'location'");
	if (current().value[0] != '/')
		throw ConfigError(openLine,
			"invalid location path '" + current().value + "', must start with '/'");
	location.path = normalizeLocationPath(current().value);
	advance();
	expect(Token::OPEN_BRACE, "'{' after location path");
	while (!isBlockEnd(openLine))
	{
		rejectNestedBlock("location");
		if (isWord("location"))
			throw ConfigError(current().line, "nested location is not allowed");
		applyLocationDirective(location, parseDirective());
	}
	applyLocationDefaults(location);
	advance();					// le '}'
	return location;
}

// Lit "nom arg1 arg2 ;". Tomber sur '{', '}' ou la fin du fichier avant le
// ';' veut dire que le ';' a été oublié : on donne la ligne de la directive.
ConfigParser::Directive ConfigParser::parseDirective()
{
	Directive directive;

	if (current().type != Token::WORD)
		throw ConfigError(current().line, "unexpected '" + current().value + "'");
	directive.name = current().value;
	directive.line = current().line;
	advance();
	while (isAtEnd() || current().type != Token::SEMICOLON)
	{
		if (isAtEnd() || current().type != Token::WORD)
			throw ConfigError(directive.line, "missing ';' after '" + directive.name + "'");
		directive.args.push_back(current().value);
		advance();
	}
	advance();					// le ';'
	return directive;
}

// Un "server" dans un server ou dans une location est refusé avec un message clair.
void ConfigParser::rejectNestedBlock(const std::string& context) const
{
	if (isWord("server"))
		throw ConfigError(current().line, "'server' block is not allowed inside a " + context);
}

// ===== Valeurs par défaut et héritage =====

// Appelée à la fin du bloc server (ses directives peuvent suivre ses
// locations). Les valeurs par défaut du server sont posées D'ABORD, pour que
// les locations héritent de valeurs complètes.
void ConfigParser::finalizeServer(ServerConfig& server)
{
	applyServerDefaults(server);
	ensureRootLocation(server);
	for (size_t i = 0; i < server.locations.size(); i++)
		inheritFromServer(server.locations[i], server);
}

// index et maxBodySize n'ont pas de défaut propre à une location : ils
// viennent de leur server. Défauts : index.html et 1 Mo (comme NGINX).
void ConfigParser::applyServerDefaults(ServerConfig& server)
{
	if (server.index.empty())
		server.index = "index.html";
	if (server.maxBodySize == BODY_SIZE_UNSET)
		server.maxBodySize = 1024 * 1024;
}

// Une location sans "methods" accepte seulement GET : le choix le plus sûr.
void ConfigParser::applyLocationDefaults(LocationConfig& location)
{
	if (location.methods.empty())
		location.methods.push_back("GET");
}

// Ajoute une location "/" si le fichier n'en a pas : toute URL trouve ainsi
// au moins une location, et findLocation ne peut jamais échouer.
void ConfigParser::ensureRootLocation(ServerConfig& server)
{
	for (size_t i = 0; i < server.locations.size(); i++)
	{
		if (server.locations[i].path == "/")
			return;
	}

	LocationConfig location;

	location.path = "/";
	applyLocationDefaults(location);
	server.locations.push_back(location);
}

// L'héritage server -> location : ce que la location ne précise pas est
// recopié depuis le server.
// hasOwnRoot reste false pour un root hérité : resolvePath en dépend.
// map::insert n'écrase pas une clé existante : les pages d'erreur propres à
// la location l'emportent sur celles du server.
void ConfigParser::inheritFromServer(LocationConfig& location, const ServerConfig& server)
{
	if (!location.hasOwnRoot)
		location.root = server.root;
	if (location.index.empty())
		location.index = server.index;
	if (location.maxBodySize == BODY_SIZE_UNSET)
		location.maxBodySize = server.maxBodySize;
	location.errorPages.insert(server.errorPages.begin(), server.errorPages.end());
}

// ===== Petits outils de validation =====

// "/files/" et "/files" sont le même préfixe ; "/" reste "/".
std::string ConfigParser::normalizeLocationPath(const std::string& path)
{
	std::string normalized = path;

	while (normalized.size() > 1 && normalized[normalized.size() - 1] == '/')
		normalized.erase(normalized.size() - 1);
	return normalized;
}

// Convertit un texte fait uniquement de chiffres.
// - le 1er caractère est vérifié à la main : strtol accepterait un signe ;
// - *end == '\0' : tout le texte a été lu ("80a" est refusé) ;
// - strtol renvoie LONG_MAX quand le nombre est trop grand : refusé aussi.
bool ConfigParser::parseNumber(const std::string& text, long& value)
{
	char* end;

	if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0])))
		return false;
	value = std::strtol(text.c_str(), &end, 10);
	return *end == '\0' && value != LONG_MAX;
}

// Une IPv4 : exactement 4 parties ; toutes sauf la dernière finissent par un
// point. (part < 3) != (dot trouvé) détecte un point manquant ou en trop.
bool ConfigParser::isValidIpv4(const std::string& ip)
{
	size_t start = 0;

	for (int part = 0; part < 4; part++)
	{
		size_t dot = ip.find('.', start);

		if ((part < 3) != (dot != std::string::npos))
			return false;
		if (!isValidIpv4Part(ip.substr(start, dot - start)))
			return false;
		start = dot + 1;
	}
	return true;
}

// Une partie : 0 à 255. Un zéro en tête est refusé : getaddrinfo pourrait lire
// "010" en octal, donc 8, et on écouterait sur une autre adresse que prévu.
bool ConfigParser::isValidIpv4Part(const std::string& part)
{
	long value;

	if (part.size() > 1 && part[0] == '0')
		return false;
	return parseNumber(part, value) && value <= 255;
}

// ===== Aiguillage des directives =====

// La table "nom de directive -> fonction qui la traite". Deux tables, car
// les directives autorisées ne sont pas les mêmes dans server et location.
// &ConfigParser::parseListen est un pointeur sur fonction MEMBRE : il faut un
// objet pour l'appeler (voir this->* plus bas).
void ConfigParser::initHandlers()
{
	_serverHandlers["listen"] = &ConfigParser::parseListen;
	_serverHandlers["root"] = &ConfigParser::parseServerRoot;
	_serverHandlers["index"] = &ConfigParser::parseServerIndex;
	_serverHandlers["client_max_body_size"] = &ConfigParser::parseServerBodySize;
	_serverHandlers["error_page"] = &ConfigParser::parseServerErrorPage;

	_locationHandlers["root"] = &ConfigParser::parseLocationRoot;
	_locationHandlers["index"] = &ConfigParser::parseLocationIndex;
	_locationHandlers["client_max_body_size"] = &ConfigParser::parseLocationBodySize;
	_locationHandlers["error_page"] = &ConfigParser::parseLocationErrorPage;
	_locationHandlers["methods"] = &ConfigParser::parseMethods;
	_locationHandlers["autoindex"] = &ConfigParser::parseAutoindex;
	_locationHandlers["return"] = &ConfigParser::parseReturn;
	_locationHandlers["upload_store"] = &ConfigParser::parseUploadStore;
	_locationHandlers["cgi"] = &ConfigParser::parseCgi;
}

// Trouve la fonction de la directive et l'appelle. Le message d'erreur
// distingue "directive inconnue" et "directive au mauvais endroit"
// (par exemple "methods" dans un server : elle n'existe que dans une location).
void ConfigParser::applyServerDirective(ServerConfig& server, const Directive& directive)
{
	std::map<std::string, ServerHandler>::const_iterator it = _serverHandlers.find(directive.name);

	if (it == _serverHandlers.end())
	{
		if (_locationHandlers.count(directive.name))
			throw ConfigError(directive.line,
				"'" + directive.name + "' is not allowed in a server block");
		throw ConfigError(directive.line, "unknown directive '" + directive.name + "'");
	}
	rejectDuplicate(_seenInServer, directive);
	// Appel par pointeur sur fonction membre : (objet->*pointeur)(arguments).
	(this->*(it->second))(server, directive);
}

void ConfigParser::applyLocationDirective(LocationConfig& location, const Directive& directive)
{
	std::map<std::string, LocationHandler>::const_iterator it = _locationHandlers.find(directive.name);

	if (it == _locationHandlers.end())
	{
		if (_serverHandlers.count(directive.name))
			throw ConfigError(directive.line,
				"'" + directive.name + "' is not allowed in a location block");
		throw ConfigError(directive.line, "unknown directive '" + directive.name + "'");
	}
	rejectDuplicate(_seenInLocation, directive);
	(this->*(it->second))(location, directive);
}

// Deux surcharges : un nombre exact d'arguments, ou un intervalle.
void ConfigParser::requireArgCount(const Directive& directive, size_t expected) const
{
	if (directive.args.size() != expected)
		throw ConfigError(directive.line, "wrong number of arguments for '" + directive.name + "'");
}

void ConfigParser::requireArgCount(const Directive& directive, size_t min, size_t max) const
{
	if (directive.args.size() < min || directive.args.size() > max)
		throw ConfigError(directive.line, "wrong number of arguments for '" + directive.name + "'");
}

// Une directive écrite deux fois dans le même bloc est refusée : laquelle
// garder serait arbitraire. std::set::insert renvoie une paire dont .second
// vaut false si le nom y était déjà.
// listen, error_page et cgi peuvent se répéter avec des valeurs différentes :
// leurs propres fonctions vérifient qu'une même valeur n'est pas répétée.
void ConfigParser::rejectDuplicate(std::set<std::string>& seen, const Directive& directive) const
{
	if (directive.name == "listen" || directive.name == "error_page" || directive.name == "cgi")
		return;
	if (!seen.insert(directive.name).second)
		throw ConfigError(directive.line, "duplicate '" + directive.name + "' directive");
}

// ===== Une fonction par directive =====
// Chacune vérifie son nombre d'arguments, valide les valeurs, puis remplit
// la structure.

void ConfigParser::parseServerRoot(ServerConfig& server, const Directive& directive)
{
	requireArgCount(directive, 1);
	server.root = directive.args[0];
}

// Un root de location remplace le préfixe (voir resolvePath) : on le note.
void ConfigParser::parseLocationRoot(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1);
	location.root = directive.args[0];
	location.hasOwnRoot = true;
}

void ConfigParser::parseServerIndex(ServerConfig& server, const Directive& directive)
{
	requireArgCount(directive, 1);
	server.index = directive.args[0];
}

void ConfigParser::parseLocationIndex(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1);
	location.index = directive.args[0];
}

// Seulement "on" ou "off", en minuscules, comme dans NGINX.
void ConfigParser::parseAutoindex(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1);
	if (directive.args[0] == "on")
		location.autoindex = true;
	else if (directive.args[0] == "off")
		location.autoindex = false;
	else
		throw ConfigError(directive.line, "invalid value '" + directive.args[0]
			+ "' for 'autoindex', expected 'on' or 'off'");
}

// "methods GET POST DELETE;" : 1 à 3 méthodes connues, sans doublon.
void ConfigParser::parseMethods(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1, 3);
	for (size_t i = 0; i < directive.args.size(); i++)
	{
		const std::string& method = directive.args[i];

		if (method != "GET" && method != "POST" && method != "DELETE")
			throw ConfigError(directive.line, "unknown method '" + method + "'");
		if (location.isMethodAllowed(method))
			throw ConfigError(directive.line, "method '" + method + "' listed twice");
		location.methods.push_back(method);
	}
}

// Le dossier où POST enregistre les fichiers. Sans elle, POST répond 403.
void ConfigParser::parseUploadStore(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1);
	location.uploadStore = directive.args[0];
}

// "return 301 /;" : seulement les codes de redirection qui ont un sens.
void ConfigParser::parseReturn(LocationConfig& location, const Directive& directive)
{
	long code;

	requireArgCount(directive, 2);
	if (!parseNumber(directive.args[0], code)
		|| (code != 301 && code != 302 && code != 303 && code != 307 && code != 308))
		throw ConfigError(directive.line, "invalid redirection code '" + directive.args[0] + "'");
	location.redirectCode = static_cast<int>(code);
	location.redirectUrl = directive.args[1];
}

// "cgi .py /usr/bin/python3;" : une extension -> son interpréteur. Plusieurs
// lignes cgi = plusieurs langages (le bonus "plusieurs CGI").
void ConfigParser::parseCgi(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 2);

	const std::string& extension = directive.args[0];

	if (extension.size() < 2 || extension[0] != '.')
		throw ConfigError(directive.line, "invalid cgi extension '" + extension + "'");
	if (location.cgi.count(extension))
		throw ConfigError(directive.line, "duplicate cgi extension '" + extension + "'");
	location.cgi[extension] = directive.args[1];
}

void ConfigParser::parseServerErrorPage(ServerConfig& server, const Directive& directive)
{
	addErrorPage(server.errorPages, directive);
}

void ConfigParser::parseLocationErrorPage(LocationConfig& location, const Directive& directive)
{
	addErrorPage(location.errorPages, directive);
}

// Mêmes règles au niveau server et location : seule la map de destination
// change, d'où cette fonction commune. Codes d'erreur seulement (400 à 599).
void ConfigParser::addErrorPage(std::map<int, std::string>& errorPages, const Directive& directive)
{
	long code;

	requireArgCount(directive, 2);
	if (!parseNumber(directive.args[0], code) || code < 400 || code > 599)
		throw ConfigError(directive.line, "invalid error code '" + directive.args[0] + "'");
	if (errorPages.count(static_cast<int>(code)))
		throw ConfigError(directive.line, "duplicate error page for code '" + directive.args[0] + "'");
	errorPages[static_cast<int>(code)] = directive.args[1];
}

void ConfigParser::parseServerBodySize(ServerConfig& server, const Directive& directive)
{
	server.maxBodySize = readBodySize(directive);
}

void ConfigParser::parseLocationBodySize(LocationConfig& location, const Directive& directive)
{
	location.maxBodySize = readBodySize(directive);
}

// "1024" est en octets, "10K" et "10M" sont multipliés par 1024 et 1024².
// Le dépassement est vérifié AVANT la multiplication (value > LONG_MAX / multiplier) :
// après, il serait trop tard, le résultat aurait déjà débordé.
size_t ConfigParser::readBodySize(const Directive& directive)
{
	requireArgCount(directive, 1);

	std::string	number = directive.args[0];
	char		unit = number[number.size() - 1];
	long		multiplier = 1;
	long		value;

	if (unit == 'K')
		multiplier = 1024;
	else if (unit == 'M')
		multiplier = 1024 * 1024;
	if (multiplier != 1)
		number.erase(number.size() - 1);
	if (!parseNumber(number, value) || value > LONG_MAX / multiplier)
		throw ConfigError(directive.line, "invalid size '" + directive.args[0] + "'");
	return static_cast<size_t>(value * multiplier);
}

// "listen 8080;" écoute sur toutes les interfaces (0.0.0.0).
// "listen 127.0.0.1:8080;" sur une seule. "localhost" devient 127.0.0.1 ici,
// pour que la comparaison de deux adresses sous forme de texte reste juste.
// Le port doit être entre 1 et 65535.
void ConfigParser::parseListen(ServerConfig& server, const Directive& directive)
{
	requireArgCount(directive, 1);

	const std::string&	text = directive.args[0];
	size_t				colon = text.find(':');
	std::string			portText = text;
	ListenAddress		address;
	long				port;

	address.host = "0.0.0.0";
	if (colon != std::string::npos)
	{
		address.host = text.substr(0, colon);
		portText = text.substr(colon + 1);
	}
	if (address.host == "localhost")
		address.host = "127.0.0.1";
	if (!isValidIpv4(address.host))
		throw ConfigError(directive.line, "invalid address '" + address.host + "' in 'listen'");
	if (!parseNumber(portText, port) || port < 1 || port > 65535)
		throw ConfigError(directive.line, "invalid port '" + portText + "' in 'listen'");
	address.port = static_cast<int>(port);
	// _usedAddresses contient tous les listen du FICHIER (tous servers
	// confondus) : deux sites sur la même adresse:port sont refusés ici,
	// avec un message, au lieu d'un bind() qui échouerait au démarrage.
	// Nous n'avons pas fait de virtual hosts (server_name) : c'est le choix
	// que la grille accepte ("this should result in an error").
	for (size_t i = 0; i < _usedAddresses.size(); i++)
	{
		if (addressesConflict(_usedAddresses[i], address))
			throw ConfigError(directive.line, "address '" + text + "' conflicts with an earlier 'listen'");
	}
	_usedAddresses.push_back(address);
	server.listens.push_back(address);
}

// Même port + même IP : conflit. 0.0.0.0 prend le port sur TOUTES les
// interfaces : il entre donc en conflit avec n'importe quelle IP sur ce port.
// 127.0.0.1:8080 et 127.0.0.2:8080 ne sont pas en conflit (deux sites).
bool ConfigParser::addressesConflict(const ListenAddress& a, const ListenAddress& b)
{
	if (a.port != b.port)
		return false;
	return a.host == b.host || a.host == "0.0.0.0" || b.host == "0.0.0.0";
}
