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

// ===== Entry point =====

std::vector<ServerConfig> ConfigParser::parse()
{
	std::vector<ServerConfig> servers;

	_pos = 0;
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

// ===== Token navigation =====

bool ConfigParser::isAtEnd() const
{
	return _pos >= _tokens.size();
}

// Callers must check isAtEnd() first.
const Token& ConfigParser::current() const
{
	return _tokens[_pos];
}

void ConfigParser::advance()
{
	if (!isAtEnd())
		_pos++;
}

bool ConfigParser::isWord(const std::string& value) const
{
	return !isAtEnd() && current().type == Token::WORD && current().value == value;
}

// Consumes the current token if it has the expected type, throws otherwise.
void ConfigParser::expect(Token::Type type, const std::string& what)
{
	if (isAtEnd())
		throw ConfigError(_tokens.back().line, "unexpected end of file, expected " + what);
	if (current().type != type)
		throw ConfigError(current().line,
			"expected " + what + ", found '" + current().value + "'");
	advance();
}

// True on '}'. Reaching the end of the file means the block opened
// at openLine was never closed: report that line, it is where the fix goes.
bool ConfigParser::isBlockEnd(int openLine) const
{
	if (isAtEnd())
		throw ConfigError(openLine, "unclosed block, missing '}'");
	return current().type == Token::CLOSE_BRACE;
}

// ===== Grammar rules =====

ServerConfig ConfigParser::parseServer()
{
	ServerConfig	server;
	int				openLine = current().line;

	_seenInServer.clear();
	advance();
	expect(Token::OPEN_BRACE, "'{' after 'server'");
	while (!isBlockEnd(openLine))
	{
		rejectNestedBlock("server");
		if (isWord("location"))
			server.locations.push_back(parseLocation());
		else
			applyServerDirective(server, parseDirective());
	}
	advance();
	return server;
}

LocationConfig ConfigParser::parseLocation()
{
	LocationConfig	location;
	int				openLine = current().line;

	_seenInLocation.clear();
	advance();
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
	advance();
	return location;
}

// Reads "name arg1 arg2 ;". Hitting '{', '}' or the end of the file
// before ';' means the ';' was forgotten.
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
	advance();
	return directive;
}

void ConfigParser::rejectNestedBlock(const std::string& context) const
{
	if (isWord("server"))
		throw ConfigError(current().line, "'server' block is not allowed inside a " + context);
}

// "/files/" and "/files" are the same prefix; "/" stays "/".
std::string ConfigParser::normalizeLocationPath(const std::string& path)
{
	std::string normalized = path;

	while (normalized.size() > 1 && normalized[normalized.size() - 1] == '/')
		normalized.erase(normalized.size() - 1);
	return normalized;
}

// Converts a text made only of digits. The first character is checked because
// strtol would accept a sign. It returns LONG_MAX when the number is too big.
bool ConfigParser::parseNumber(const std::string& text, long& value)
{
	char* end;

	if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0])))
		return false;
	value = std::strtol(text.c_str(), &end, 10);
	return *end == '\0' && value != LONG_MAX;
}

// ===== Dispatch =====

void ConfigParser::initHandlers()
{
	_serverHandlers["listen"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["root"] = &ConfigParser::parseServerRoot;
	_serverHandlers["index"] = &ConfigParser::parseServerIndex;
	_serverHandlers["client_max_body_size"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["error_page"] = &ConfigParser::parseServerErrorPage;

	_locationHandlers["root"] = &ConfigParser::parseLocationRoot;
	_locationHandlers["index"] = &ConfigParser::parseLocationIndex;
	_locationHandlers["client_max_body_size"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["error_page"] = &ConfigParser::parseLocationErrorPage;
	_locationHandlers["methods"] = &ConfigParser::parseMethods;
	_locationHandlers["autoindex"] = &ConfigParser::parseAutoindex;
	_locationHandlers["return"] = &ConfigParser::parseReturn;
	_locationHandlers["upload_store"] = &ConfigParser::parseUploadStore;
	_locationHandlers["cgi"] = &ConfigParser::parseCgi;
}

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

// listen, error_page and cgi may repeat with different keys:
// their own handlers check for a repeated key.
void ConfigParser::rejectDuplicate(std::set<std::string>& seen, const Directive& directive) const
{
	if (directive.name == "listen" || directive.name == "error_page" || directive.name == "cgi")
		return;
	if (!seen.insert(directive.name).second)
		throw ConfigError(directive.line, "duplicate '" + directive.name + "' directive");
}

// ===== Directive handlers =====

void ConfigParser::parseServerRoot(ServerConfig& server, const Directive& directive)
{
	requireArgCount(directive, 1);
	server.root = directive.args[0];
}

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

void ConfigParser::parseUploadStore(LocationConfig& location, const Directive& directive)
{
	requireArgCount(directive, 1);
	location.uploadStore = directive.args[0];
}

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

// Same rules at both levels: only the destination map changes.
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

void ConfigParser::ignoreServerDirective(ServerConfig&, const Directive&)
{
}

void ConfigParser::ignoreLocationDirective(LocationConfig&, const Directive&)
{
}
