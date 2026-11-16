#include "config/ServerConfig.hpp"

ListenAddress::ListenAddress()
	: port(0)
{
}

// Initialise seulement les types simples (sinon leur valeur serait aléatoire).
// Les vraies valeurs par défaut (index.html, 1M) sont posées après le parsing :
// on doit d'abord savoir si la directive était présente dans le fichier.
ServerConfig::ServerConfig()
	: maxBodySize(BODY_SIZE_UNSET)
{
}

// Routage : parmi les locations qui correspondent à l'URL, on garde celle dont
// le préfixe est le plus long, donc la plus précise. Pour "/upload/a.txt",
// "/upload" l'emporte sur "/". Même règle que NGINX pour les préfixes.
const LocationConfig& ServerConfig::findLocation(const std::string& path) const
{
	const LocationConfig* best = NULL;

	for (size_t i = 0; i < locations.size(); i++)
	{
		if (locations[i].matchesPath(path)
			&& (best == NULL || locations[i].path.size() > best->path.size()))
			best = &locations[i];
	}
	// best n'est jamais NULL : la location "/" correspond à tout.
	return *best;
}
