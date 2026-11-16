#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// Valeur "pas encore réglé" de maxBodySize. 0 ne peut pas jouer ce rôle :
// "client_max_body_size 0;" est une vraie limite (aucun body accepté).
// On prend donc la plus grande valeur possible d'un size_t (-1 converti),
// que le parser ne produit jamais. Elle disparaît après l'héritage.
const size_t BODY_SIZE_UNSET = static_cast<size_t>(-1);

// ============================================================================
// Un bloc "location <préfixe> { ... }" : le comportement d'une route.
//
// Le parser recopie ici tout ce qui vient du server (root, index, taille max,
// pages d'erreur) quand la location ne le précise pas. Le serveur HTTP n'a
// donc qu'un seul objet à regarder pour traiter une requête.
// ============================================================================
struct LocationConfig
{
	std::string							path;			// "/upload", jamais de '/' final sauf "/"
	std::string							root;
	bool								hasOwnRoot;		// false si root vient du server
	std::string							index;
	size_t								maxBodySize;	// en octets
	std::map<int, std::string>			errorPages;		// 404 -> chemin sur le disque
	std::vector<std::string>			methods;		// "GET", "POST", "DELETE"
	bool								autoindex;
	int									redirectCode;	// 0 = pas de redirection
	std::string							redirectUrl;
	std::string							uploadStore;	// vide = upload désactivé
	std::map<std::string, std::string>	cgi;			// ".py" -> "/usr/bin/python3"

	LocationConfig();

	bool	isMethodAllowed(const std::string& method) const;
	bool	matchesPath(const std::string& requestPath) const;
};

// Transforme le chemin d'une URL en chemin sur le disque.
// Le chemin reçu est déjà sans "?query" et déjà décodé : "%2e%2e" doit être
// devenu ".." AVANT cette vérification, sinon il passerait le filtre.
// Renvoie une chaîne vide (= interdit, 403) si le chemin contient un segment
// "..", ne commence pas par '/' ou n'appartient pas à cette location.
std::string resolvePath(const LocationConfig& location, const std::string& path);

#endif
