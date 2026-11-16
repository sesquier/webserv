#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include "config/LocationConfig.hpp"

// ============================================================================
// Les structures produites par le parser : ce que le serveur sait de la config.
// Ce sont de simples "struct" de données : le parser les remplit, le serveur
// les lit sans jamais les modifier (il ne garde que des const&).
// ============================================================================

// Une directive "listen" : "listen 127.0.0.1:8080;" ou "listen 8080;".
struct ListenAddress
{
	std::string	host;	// "0.0.0.0" (toutes les interfaces) si aucune IP n'est donnée
	int			port;

	ListenAddress();
};

// Un bloc "server { ... }" : un site.
struct ServerConfig
{
	std::vector<ListenAddress>	listens;		// un server peut écouter sur plusieurs ports
	std::string					root;
	std::string					index;
	size_t						maxBodySize;	// en octets
	std::map<int, std::string>	errorPages;		// 404 -> chemin de la page sur le disque
	std::vector<LocationConfig>	locations;

	ServerConfig();

	// Choisit la location d'une URL : celle dont le préfixe est le plus long.
	// Ne peut pas échouer : le parser ajoute toujours une location "/", qui
	// correspond à toutes les URL.
	const LocationConfig&	findLocation(const std::string& path) const;
};

#endif
