#include "config/Config.hpp"
#include "config/ConfigFile.hpp"
#include "config/ConfigParser.hpp"
#include "config/Tokenizer.hpp"

std::vector<ServerConfig> loadConfig(const std::string& path)
{
	Tokenizer		tokenizer(readConfigFile(path));
	ConfigParser	parser(tokenizer.tokenize());

	return parser.parse();
}
