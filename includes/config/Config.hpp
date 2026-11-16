#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <iosfwd>
#include <string>
#include <vector>
#include "config/ConfigError.hpp"
#include "config/ServerConfig.hpp"

// ============================================================================
// Le seul en-tête dont le reste du programme a besoin pour la config.
// Le serveur ne connaît ni le Tokenizer ni le ConfigParser : il reçoit des
// ServerConfig déjà vérifiés et complétés. Le module de config et le serveur
// ont été écrits séparément (Simon d'un côté, Ayrton de l'autre) puis
// branchés par cette interface.
// ============================================================================

// Lit, découpe, analyse et valide le fichier. Lance ConfigError s'il est invalide.
std::vector<ServerConfig> loadConfig(const std::string& path);

// Affiche la configuration finale (après valeurs par défaut et héritage).
// Sert aux tests du parser (tests/config) : on compare cet affichage à un
// résultat attendu. <iosfwd> suffit ici : il déclare std::ostream sans
// inclure toute la bibliothèque des flux.
void dumpConfig(const std::vector<ServerConfig>& servers, std::ostream& out);

#endif
