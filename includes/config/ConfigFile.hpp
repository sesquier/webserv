#ifndef CONFIGFILE_HPP
#define CONFIGFILE_HPP

#include <string>

// Lit tout le fichier de configuration et renvoie son contenu.
// Lance ConfigError si le chemin n'existe pas, n'est pas un fichier normal
// (un dossier par exemple) ou ne peut pas être lu.
std::string readConfigFile(const std::string& path);

#endif
