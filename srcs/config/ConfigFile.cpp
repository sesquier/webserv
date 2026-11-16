#include "config/ConfigFile.hpp"
#include "config/ConfigError.hpp"
#include <sys/stat.h>
#include <fstream>
#include <sstream>

// La config est un fichier sur le disque : on la lit d'un bloc, avant de
// lancer le serveur. Le sujet n'impose pas epoll pour les fichiers disque.
std::string readConfigFile(const std::string& path)
{
	struct stat info;

	// stat d'abord : sous Linux, ifstream "ouvre" un dossier sans erreur, puis
	// la lecture échoue de façon peu claire. On vérifie donc qu'il s'agit d'un
	// fichier normal (S_ISREG) pour donner un message précis.
	if (stat(path.c_str(), &info) != 0)
		throw ConfigError("cannot access '" + path + "'");
	if (!S_ISREG(info.st_mode))
		throw ConfigError("'" + path + "' is not a regular file");

	// En C++98, ifstream::open prend un const char*, pas une std::string.
	std::ifstream file;
	file.open(path.c_str());
	if (!file.is_open())
		throw ConfigError("cannot open '" + path + "'");

	// rdbuf() : copie tout le contenu du fichier dans le flux en une instruction.
	std::ostringstream content;
	content << file.rdbuf();
	if (file.bad())
		throw ConfigError("cannot read '" + path + "'");
	// Pas de close() : le destructeur de ifstream ferme le fichier (RAII).
	return content.str();
}
