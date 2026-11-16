#ifndef CGI_HPP
#define CGI_HPP

#include <string>
#include <vector>
#include <sys/types.h>
#include "config/ServerConfig.hpp"

class Request;

// ============================================================================
// CGI (Common Gateway Interface, RFC 3875) : faire fabriquer une page par un
// programme externe (un script Python, Perl, Bash...).
//
// Le contrat entre le serveur et le script :
//   - la requête arrive dans des VARIABLES D'ENVIRONNEMENT
//     (REQUEST_METHOD, QUERY_STRING, CONTENT_LENGTH, HTTP_COOKIE...) ;
//   - le body arrive sur l'ENTRÉE STANDARD du script (stdin) ;
//   - le script écrit sur sa SORTIE STANDARD (stdout) : des headers, une
//     ligne vide, puis la page.
//
// Ce fichier déclare les briques "pures" (sans epoll) : préparer
// l'environnement, lancer le processus, lire sa sortie. Leur branchement sur
// la boucle epoll est dans cgiEvents.cpp.
// ============================================================================

// L'environnement du script, sous forme de chaînes "NOM=valeur" : tout ce que
// le script doit savoir de la requête, sauf le body, qui passe par stdin.
std::vector<std::string>	buildCgiEnv(const Request& request, const std::string& urlPath,
								const std::string& scriptPath, const ListenAddress& address);

// Une requête à laquelle un script doit répondre. handleRequest la remplit
// au lieu de construire une réponse, et la boucle réseau lance le script à
// partir d'elle. Elle fait le lien entre handlers.cpp (qui ne fait aucune
// I/O) et la boucle epoll.
struct CgiRequest
{
	bool						pending;		// true : pas encore de réponse, lancer le script
	std::string					interpreter;	// "/usr/bin/python3"
	std::string					scriptPath;		// "./www/site1/cgi-bin/hello.py"
	std::vector<std::string>	env;
	std::string					body;			// à écrire sur le stdin du script
	const LocationConfig*		location;		// pour les pages d'erreur (502, 504)

	CgiRequest();
};

// Un script en cours, vu du serveur : son pid et les bouts de pipes que le
// serveur garde.
struct CgiProcess
{
	pid_t	pid;
	int		inputFd;	// on écrit le body ici : c'est le stdin du script
	int		outputFd;	// on lit le stdout du script ici, jusqu'à EOF
};

// Lance "interpréteur script" dans le dossier du script, avec env comme
// environnement. Les deux fds rendus sont non bloquants, pour epoll.
// Faux si les pipes ou le processus ne peuvent pas être créés (-> 500).
bool						startCgi(const std::string& interpreter, const std::string& scriptPath,
								const std::vector<std::string>& env, CgiProcess& process);

// Ce que le script a écrit, découpé en morceaux d'une réponse HTTP.
struct CgiOutput
{
	int			status;			// 200, ou ce que dit le header "Status:" du script
	std::string	reason;			// "Not Found" ; vide si le script n'en donne pas
	std::string	contentType;
	std::string	headers;		// ses autres headers (Set-Cookie...), chacun finit par "\r\n"
	std::string	body;
};

// Lit la sortie d'un script : headers, ligne vide, body.
// Faux si elle ne respecte pas le format CGI (-> 502 Bad Gateway).
bool						parseCgiOutput(const std::string& output, CgiOutput& result);

#endif
