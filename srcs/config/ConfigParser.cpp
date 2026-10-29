#include "config/ConfigParser.hpp"
#include "config/ConfigError.hpp"

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

// ===== Dispatch =====

void ConfigParser::initHandlers()
{
	_serverHandlers["listen"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["root"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["index"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["client_max_body_size"] = &ConfigParser::ignoreServerDirective;
	_serverHandlers["error_page"] = &ConfigParser::ignoreServerDirective;

	_locationHandlers["root"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["index"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["client_max_body_size"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["error_page"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["methods"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["autoindex"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["return"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["upload_store"] = &ConfigParser::ignoreLocationDirective;
	_locationHandlers["cgi"] = &ConfigParser::ignoreLocationDirective;
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
	(this->*(it->second))(location, directive);
}

// ===== Directive handlers =====

void ConfigParser::ignoreServerDirective(ServerConfig&, const Directive&)
{
}

void ConfigParser::ignoreLocationDirective(LocationConfig&, const Directive&)
{
}
