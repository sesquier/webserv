#include "config/Config.hpp"
#include "config/ConfigFile.hpp"
#include "config/ConfigParser.hpp"
#include "config/Tokenizer.hpp"

// Le point d'entrée du module de config : les 3 étapes, dans l'ordre.
//   1. readConfigFile : le texte du fichier ;
//   2. Tokenizer      : le texte découpé en mots, '{', '}', ';' ;
//   3. ConfigParser   : la grammaire, les valeurs, les valeurs par défaut et
//                       l'héritage server -> location.
// Le résultat : un ServerConfig par bloc "server", prêt à l'emploi. La moindre
// erreur lance une ConfigError : le serveur ne démarre jamais avec une
// configuration à moitié valide.
std::vector<ServerConfig> loadConfig(const std::string& path)
{
	Tokenizer		tokenizer(readConfigFile(path));
	ConfigParser	parser(tokenizer.tokenize());

	return parser.parse();
}
