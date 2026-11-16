#ifndef CONFIGERROR_HPP
#define CONFIGERROR_HPP

#include <stdexcept>
#include <string>

// ============================================================================
// ConfigError : l'exception lancée pour toute erreur de configuration.
//
// Elle hérite de std::runtime_error : what() renvoie le message, et un
// catch (const std::exception&) l'attrape aussi. Le main l'attrape à part
// pour afficher "webserv: config error: ..." puis quitter avec le code 1.
//
// Pourquoi une exception ? L'erreur peut être détectée très loin dans le
// parsing (dans une directive d'une location d'un server) : l'exception
// remonte directement jusqu'au main, sans faire passer un code d'erreur de
// fonction en fonction.
// ============================================================================
class ConfigError : public std::runtime_error
{
	public:
		// Deux constructeurs : une erreur sur tout le fichier (introuvable, aucun
		// server) n'a pas de ligne ; une erreur dans le contenu en a une.
		// explicit : empêche d'écrire "ConfigError e = message;" par accident.
		explicit ConfigError(const std::string& message);
		ConfigError(int line, const std::string& message);

	private:
		// Fabrique "line 12: message". static : n'utilise aucun membre, elle peut
		// donc être appelée dans la liste d'initialisation, avant que l'objet existe.
		static std::string withLine(int line, const std::string& message);
};

#endif
