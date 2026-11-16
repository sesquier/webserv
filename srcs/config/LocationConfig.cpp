#include "config/LocationConfig.hpp"
#include <algorithm>

// Initialise seulement les types simples ; les vraies valeurs par défaut sont
// posées après le parsing (voir ConfigParser::applyLocationDefaults).
LocationConfig::LocationConfig()
	: hasOwnRoot(false), maxBodySize(BODY_SIZE_UNSET), autoindex(false), redirectCode(0)
{
}

// La méthode est-elle dans la liste de la directive "methods" ?
// Sinon, le serveur répond 405 avec un header Allow.
bool LocationConfig::isMethodAllowed(const std::string& method) const
{
	return std::find(methods.begin(), methods.end(), method) != methods.end();
}

// Compare des segments entiers : "/files" correspond à "/files" et à
// "/files/a", mais pas à "/filesxyz" (qui serait un autre dossier).
// "/" correspond à toutes les URL : c'est la location de secours.
bool LocationConfig::matchesPath(const std::string& requestPath) const
{
	if (path == "/")
		return true;
	if (requestPath.compare(0, path.size(), path) != 0)
		return false;
	return requestPath.size() == path.size() || requestPath[path.size()] == '/';
}

// Protection contre le "path traversal" (remonter hors du site avec "..").
// On cherche un segment ".." ENTIER, entouré de '/' ou des bords du chemin :
// "a..b" ou "..." sont des noms de fichiers ordinaires, ils ne remontent pas.
static bool hasParentSegment(const std::string& path)
{
	size_t pos = path.find("..");

	while (pos != std::string::npos)
	{
		bool startsSegment = (pos == 0 || path[pos - 1] == '/');
		bool endsSegment = (pos + 2 == path.size() || path[pos + 2] == '/');

		if (startsSegment && endsSegment)
			return true;
		pos = path.find("..", pos + 1);
	}
	return false;
}

// Deux cas, selon que la location a son propre "root" ou non :
//  - son propre root : il REMPLACE le préfixe de la location (comme "alias"
//    dans NGINX). location /files { root ./www/site1/files; }
//    "/files/notes.txt" -> "./www/site1/files" + "/notes.txt"
//  - root hérité du server : on ajoute tout le chemin de l'URL.
//    "/contact.html" -> "./www/site1" + "/contact.html"
std::string resolvePath(const LocationConfig& location, const std::string& path)
{
	if (path.empty() || path[0] != '/' || hasParentSegment(path) || !location.matchesPath(path))
		return "";
	if (location.hasOwnRoot && location.path != "/")
		return location.root + path.substr(location.path.size());
	return location.root + path;
}
