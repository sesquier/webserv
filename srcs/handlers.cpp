// ============================================================================
// handlers.cpp : QUE répondre à une requête déjà lue en entier.
//
// La boucle réseau (server.cpp) s'occupe de recevoir et d'envoyer ; ce
// fichier ne fait aucune I/O réseau. Il prend une Request et renvoie la
// réponse HTTP complète sous forme de texte :
//
//   handleRequest  règles communes (méthode connue, redirection, méthode
//                  autorisée), puis aiguillage vers :
//   handleGet      fichier, dossier (index / autoindex / 403) ;
//   handlePost     upload d'un fichier ;
//   handleDelete   suppression d'un fichier ;
//   handleCgi      prépare le lancement d'un script (lancé par cgiEvents.cpp).
//
// Les fichiers du disque sont lus et écrits directement, sans epoll : la
// grille l'autorise ("Regular disk files are exempt"), et epoll refuse de
// toute façon les fichiers normaux.
// ============================================================================

#include "server.hpp"
#include "request.hpp"
#include "cgi.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <set>
#include <cctype>

// Le nom d'un fichier uploadé vient de l'URL : on n'accepte que lettres,
// chiffres, '.', '_' et '-'. Donc ni '/' (pas de sous-dossier, pas de
// "../../etc/passwd"), ni "." ou ".." seuls.
static bool isValidUploadFilename(const std::string& filename)
{
    if (filename.empty() || filename == "." || filename == "..")
        return (false);

    for (std::string::size_type i = 0; i < filename.size(); i++)
    {
        char c = filename[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return (false);
    }
    return (true);
}

// Écrit le body dans un nouveau fichier. Renvoie le code HTTP : 201, 409 ou 500.
static int saveUploadedFile(const std::string& filePath, const std::string& body)
{
    // O_CREAT | O_EXCL : créer le fichier, et ÉCHOUER s'il existe déjà.
    // On n'écrase jamais un fichier (celui d'un autre, ou une page du site).
    // 0644 : lisible par tous, modifiable par le propriétaire.
    int fileFd = open(
        filePath.c_str(),
        O_WRONLY | O_CREAT | O_EXCL,
        0644
    );

    // Pour savoir POURQUOI open a échoué, on aurait pu tester errno == EEXIST.
    // On fait un stat à la place : le fichier existe -> 409 Conflict, sinon
    // 500. Aucun errno après une opération de fichier.
    if (fileFd == -1)
    {
        struct stat fileInfo;

        if (stat(filePath.c_str(), &fileInfo) == 0)
            return (409);

        return (500);
    }

    std::string::size_type offset = 0;
    bool writeError = false;

    // Un write peut écrire moins que demandé : on boucle jusqu'à tout écrire.
    // C'est un fichier disque, pas un socket : boucler ici ne bloque pas.
	while (offset < body.size())
    {
        ssize_t bytesWritten = write(
            fileFd,
            body.data() + offset,
            body.size() - offset
        );

        if (bytesWritten <= 0)
        {
            writeError = true;
            break;
        }

        offset += static_cast<std::string::size_type>(bytesWritten);
    }

    if (close(fileFd) == -1)
        writeError = true;

    // Un fichier à moitié écrit est supprimé : pas de fichier corrompu.
    if (writeError)
    {
        unlink(filePath.c_str());
        return (500);
    }

    return (201);
}

// "/page?x=1" -> "/page" : la query string ne fait pas partie du chemin.
static std::string requestPath(const std::string& target)
{
	return (target.substr(0, target.find('?')));
}

// '0'-'9', 'a'-'f', 'A'-'F' -> 0 à 15 ; -1 pour un caractère non hexadécimal.
static int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

// Décodage d'URL : "%20" -> ' '. Une URL encode chaque caractère spécial en
// '%' suivi de deux chiffres hexadécimaux.
// Faux si un '%' est mal formé, ou pour "%00" : un octet nul couperait le
// chemin en deux une fois passé à une fonction C (open, stat...).
// Décoder AVANT de vérifier le chemin est essentiel : sinon "%2e%2e"
// passerait le filtre anti-".." puis deviendrait ".." plus tard.
bool decodePath(const std::string& encoded, std::string& decoded)
{
	for (std::string::size_type i = 0; i < encoded.size(); i++)
	{
		if (encoded[i] != '%')
		{
			decoded += encoded[i];
			continue;
		}
		// Deux chiffres hexadécimaux doivent suivre le '%'.
		if (i + 2 >= encoded.size())
			return (false);

		int high = hexValue(encoded[i + 1]);
		int low = hexValue(encoded[i + 2]);

		if (high < 0 || low < 0 || (high == 0 && low == 0))
			return (false);
		decoded += static_cast<char>(high * 16 + low);
		i += 2;
	}
	return (true);
}

// L'inverse, pour les liens de la page autoindex : "a#b.txt" -> "a%23b.txt".
// Sans lui, un '#' ou un '?' dans un nom de fichier couperait le lien.
static std::string encodePath(const std::string& path)
{
	const char*	digits = "0123456789ABCDEF";
	std::string	encoded;

	for (std::string::size_type i = 0; i < path.size(); i++)
	{
		unsigned char c = static_cast<unsigned char>(path[i]);

		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/')
			encoded += path[i];
		else
		{
			encoded += '%';
			encoded += digits[c / 16];
			encoded += digits[c % 16];
		}
	}
	return (encoded);
}

// "./www/site1/hello.py" -> ".py". Vide si le NOM du fichier n'a pas
// d'extension : un '.' dans un nom de dossier ("./www/v1.2/README") ne compte pas.
static std::string fileExtension(const std::string& filePath)
{
	std::string::size_type dotPos = filePath.find_last_of('.');

	if (dotPos == std::string::npos || filePath.find('/', dotPos) != std::string::npos)
		return ("");
	return (filePath.substr(dotPos));
}

// Le Content-Type selon l'extension. Sans lui, le navigateur n'appliquerait
// pas le CSS et n'afficherait pas les images. Par défaut :
// application/octet-stream ("données binaires", le navigateur propose de
// télécharger).
static std::string contentTypeFor(const std::string& filePath)
{
	std::string extension = fileExtension(filePath);

	if (extension == ".html" || extension == ".htm")
		return ("text/html");
	if (extension == ".css")
		return ("text/css");
	if (extension == ".js")
		return ("application/javascript");
	if (extension == ".txt")
		return ("text/plain");
	if (extension == ".json")
		return ("application/json");
	if (extension == ".png")
		return ("image/png");
	if (extension == ".jpg" || extension == ".jpeg")
		return ("image/jpeg");
	if (extension == ".gif")
		return ("image/gif");
	if (extension == ".svg")
		return ("image/svg+xml");
	return ("application/octet-stream");
}

// L'interpréteur qui exécute ce fichier, d'après les directives "cgi" de la
// location (".py" -> "/usr/bin/python3"). Vide : ce n'est pas un script ICI.
// C'est pourquoi un .py uploadé dans /upload n'est jamais exécuté : /upload
// n'a pas de directive cgi.
static std::string cgiInterpreter(const std::string& filePath, const LocationConfig& location)
{
	std::map<std::string, std::string>::const_iterator it = location.cgi.find(fileExtension(filePath));

	if (it == location.cgi.end())
		return ("");
	return (it->second);
}

// La phrase qui accompagne chaque code de redirection accepté par la config.
static std::string redirectReason(int code)
{
	if (code == 301)
		return ("Moved Permanently");
	if (code == 302)
		return ("Found");
	if (code == 303)
		return ("See Other");
	if (code == 307)
		return ("Temporary Redirect");
	return ("Permanent Redirect");
}

// La valeur du header Allow envoyé avec une 405 : "GET, POST".
// HTTP impose ce header avec une 405 : il dit au client ce qui est permis.
static std::string allowedMethods(const LocationConfig& location)
{
	std::string list;

	for (size_t i = 0; i < location.methods.size(); i++)
	{
		if (i > 0)
			list += ", ";
		list += location.methods[i];
	}
	return (list);
}

// Lit tout un fichier disque. Faux s'il ne peut pas être lu.
// read renvoie > 0 (des octets), 0 (fin du fichier) ou -1 (erreur) : on
// réussit seulement si la boucle s'arrête sur 0.
static bool readWholeFile(const std::string& filePath, std::string& content)
{
	int fd = open(filePath.c_str(), O_RDONLY);

	if (fd == -1)
		return (false);

	char	buffer[4096];
	ssize_t	bytesRead;

	while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
		content.append(buffer, bytesRead);
	close(fd);
	return (bytesRead == 0);
}

// Un nom de fichier est affiché dans une page HTML : '<', '>', '&' et les
// guillemets ne doivent pas être lus comme du HTML, sinon un fichier nommé
// "<script>..." s'exécuterait dans le navigateur (faille XSS).
static std::string escapeHtml(const std::string& text)
{
	std::string escaped;

	for (std::string::size_type i = 0; i < text.size(); i++)
	{
		if (text[i] == '<')
			escaped += "&lt;";
		else if (text[i] == '>')
			escaped += "&gt;";
		else if (text[i] == '&')
			escaped += "&amp;";
		else if (text[i] == '"')
			escaped += "&quot;";
		else
			escaped += text[i];
	}
	return (escaped);
}

// Les noms contenus dans un dossier, triés (std::set trie tout seul) ; un
// sous-dossier finit par '/'. opendir, readdir et closedir sont autorisés
// par le sujet. Faux si le dossier ne peut pas être ouvert.
static bool readDirectory(const std::string& dirPath, std::set<std::string>& names)
{
	DIR* dir = opendir(dirPath.c_str());

	if (dir == NULL)
		return (false);

	struct dirent* entry;

	while ((entry = readdir(dir)) != NULL)
	{
		std::string	name = entry->d_name;
		struct stat	info;

		if (name == "." || name == "..")
			continue;
		if (stat((dirPath + name).c_str(), &info) == 0 && S_ISDIR(info.st_mode))
			name += "/";
		names.insert(name);
	}
	closedir(dir);
	return (true);
}

// La page autoindex : un lien par élément du dossier, plus "../" pour remonter.
static std::string directoryListing(const std::string& path, const std::set<std::string>& names)
{
	std::string title = "Index of " + escapeHtml(path);
	std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>"
		+ title + "</title>\n</head>\n<body>\n<h1>" + title + "</h1>\n<ul>\n";

	if (path != "/")
		html += "<li><a href=\"../\">../</a></li>\n";
	for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
		html += "<li><a href=\"" + encodePath(*it) + "\">" + escapeHtml(*it) + "</a></li>\n";
	html += "</ul>\n</body>\n</html>\n";
	return (html);
}

// La phrase officielle de chaque code de statut (RFC 9110), pour la ligne
// "HTTP/1.1 404 Not Found". Les codes qui passent par buildError ou par un
// script sont ici ; 204 et 301 sont écrits directement par leur fonction.
static std::string reasonPhrase(int code)
{
	switch (code)
	{
		case 200: return ("OK");
		case 201: return ("Created");
		case 302: return ("Found");
		case 400: return ("Bad Request");
		case 403: return ("Forbidden");
		case 404: return ("Not Found");
		case 405: return ("Method Not Allowed");
		case 408: return ("Request Timeout");
		case 409: return ("Conflict");
		case 411: return ("Length Required");
		case 413: return ("Content Too Large");
		case 431: return ("Request Header Fields Too Large");
		case 500: return ("Internal Server Error");
		case 501: return ("Not Implemented");
		case 502: return ("Bad Gateway");
		case 504: return ("Gateway Timeout");
	}
	return ("Error");
}

// La page envoyée quand la config n'en donne aucune pour ce code : le sujet
// demande des pages d'erreur par défaut.
static std::string defaultErrorPage(int code, const std::string& reason)
{
	std::ostringstream page;

	page << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
		<< "<title>" << code << " " << reason << "</title>\n</head>\n<body>\n"
		<< "<h1>" << code << " " << reason << "</h1>\n<hr>\n<p>webserv</p>\n"
		<< "</body>\n</html>\n";
	return (page.str());
}

// ============================================================================
// handleRequest : les règles communes à toutes les méthodes, puis le choix du
// handler. L'ordre compte :
//   1. méthode inconnue          -> 501 (le serveur ne sait pas faire)
//   2. URL mal encodée           -> 400
//   3. location avec "return"    -> redirection, quelle que soit la méthode
//   4. méthode non autorisée     -> 405 + Allow (le serveur sait faire,
//                                   mais pas sur cette route)
//   5. script CGI (GET ou POST)  -> on prépare son lancement
//   6. GET, POST ou DELETE
// Pour un script, rien n'est renvoyé : "cgi" est rempli et la boucle réseau
// lance le script (cgiEvents.cpp).
// ============================================================================
std::string Server::handleRequest(const Request& request, const ServerConfig& server,
	CgiRequest& cgi) const
{
	const std::string&	method = request.getMethod();
	std::string			path;

	// HEAD, PUT, FOO... -> 501. Location NULL : page d'erreur par défaut.
	if (method != "GET" && method != "POST" && method != "DELETE")
		return (buildError(501, NULL));
	// Décodé avant toute vérification : "%2e%2e" doit arriver en ".." dans
	// resolvePath, qui le refusera.
	if (!decodePath(requestPath(request.getTarget()), path) || path.empty() || path[0] != '/')
		return (buildError(400, NULL));

	const LocationConfig& location = server.findLocation(path);

	if (location.redirectCode != 0)
		return (buildResponse(location.redirectCode, redirectReason(location.redirectCode), "",
			"text/plain", "Location: " + location.redirectUrl + "\r\n"));
	if (!location.isMethodAllowed(method))
		return (buildError(405, &location, "Allow: " + allowedMethods(location) + "\r\n"));
	// Un script d'une location CGI est exécuté, jamais envoyé tel quel ni
	// écrasé par un upload.
	if ((method == "GET" || method == "POST")
		&& !cgiInterpreter(resolvePath(location, path), location).empty())
		return (handleCgi(request, path, server, location, cgi));
	if (method == "GET")
		return (handleGet(path, location));
	if (method == "POST")
		return (handlePost(request, path, location));
	return (handleDelete(path, location));
}

// GET : un dossier ou un fichier.
std::string Server::handleGet(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);

	// Chemin vide = resolvePath a refusé (segment "..") -> 403.
	if (filePath.empty())
		return (buildError(403, &location));

	struct stat info;

	// stat donne le type de ce qui se trouve au chemin : dossier ou fichier.
	if (stat(filePath.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
		return (handleDirectory(path, filePath, location));
	return (serveFile(filePath, location));
}

// Un dossier, dans l'ordre : ajouter le '/' manquant, servir le fichier
// index, sinon lister le contenu (autoindex on), sinon refuser (403).
std::string Server::handleDirectory(const std::string& path, const std::string& dirPath,
	const LocationConfig& location) const
{
	// Sans le '/' final, le navigateur calculerait les liens de la page depuis
	// le dossier parent : "/files" + "notes.txt" donnerait "/notes.txt".
	// On redirige donc vers "/files/", comme NGINX.
	if (path[path.size() - 1] != '/')
		return (buildResponse(301, "Moved Permanently", "", "text/plain",
			"Location: " + path + "/\r\n"));

	std::string	indexPath = dirPath + location.index;
	struct stat	info;

	if (stat(indexPath.c_str(), &info) == 0 && S_ISREG(info.st_mode))
		return (serveFile(indexPath, location));
	if (!location.autoindex)
		return (buildError(403, &location));

	std::set<std::string> names;

	if (!readDirectory(dirPath, names))
		return (buildError(403, &location));
	return (buildResponse(200, "OK", directoryListing(path, names), "text/html"));
}

// Envoie un fichier. stat AVANT open : ouvrir un FIFO (tube nommé) en
// lecture bloquerait tout le serveur ; on n'ouvre donc que des fichiers
// normaux (S_ISREG). access(R_OK) : avons-nous le droit de le lire ?
std::string Server::serveFile(const std::string& filePath, const LocationConfig& location) const
{
	struct stat	info;
	std::string	content;

	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	if (access(filePath.c_str(), R_OK) == -1)
		return (buildError(403, &location));
	if (!readWholeFile(filePath, content))
		return (buildError(500, &location));
	return (buildResponse(200, "OK", content, contentTypeFor(filePath)));
}

// POST = upload. "POST /upload/photo.png" enregistre le body dans
// <upload_store>/photo.png. Le body est enregistré tel quel (pas de
// décodage multipart/form-data) : on envoie le fichier comme body,
// par exemple avec curl --data-binary @photo.png.
std::string Server::handlePost(const Request& request, const std::string& path,
	const LocationConfig& location) const
{
	// Pas d'upload_store dans cette location : POST n'y a pas de sens.
	if (location.uploadStore.empty())
		return (buildError(403, &location));
	// Ni Content-Length ni chunked : impossible de savoir où finit le body.
	if (!request.hasBodyFraming())
		return (buildError(411, &location));

	// Le nom du fichier = ce qui suit le préfixe de la location dans l'URL.
	std::string filename = path.substr(location.path.size());

	if (!filename.empty() && filename[0] == '/')
		filename.erase(0, 1);
	if (!isValidUploadFilename(filename))
		return (buildError(400, &location));

	int status = saveUploadedFile(location.uploadStore + "/" + filename, request.getBody());

	if (status == 201)
		return (buildResponse(201, "Created", "201 Created\n"));
	if (status == 409)
		return (buildError(409, &location));
	return (buildError(500, &location));
}

// DELETE : supprime un fichier normal. Jamais un dossier (404), jamais hors
// du site ("..": 403). La méthode doit être autorisée dans la location :
// handleRequest l'a déjà vérifié (sinon 405).
std::string Server::handleDelete(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);
	struct stat info;

	if (filePath.empty())
		return (buildError(403, &location));
	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	if (unlink(filePath.c_str()) == -1)
		return (buildError(500, &location));
	// 204 No Content : réussi, et rien à renvoyer.
	return (buildResponse(204, "No Content", ""));
}

// Vérifie le script, puis remplit "cgi" avec tout ce qu'il faut pour le
// lancer. Renvoie une réponse d'erreur, ou "" quand le script doit être lancé.
// Le lancement lui-même se fait dans la boucle réseau (startCgiJob), car il
// crée des pipes qu'epoll doit surveiller.
std::string Server::handleCgi(const Request& request, const std::string& path,
	const ServerConfig& server, const LocationConfig& location, CgiRequest& cgi) const
{
	std::string	scriptPath = resolvePath(location, path);
	struct stat	info;

	if (stat(scriptPath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	// L'interpréteur lit le script : il suffit qu'il soit lisible, il n'a pas
	// besoin d'être exécutable (chmod +x).
	if (access(scriptPath.c_str(), R_OK) == -1)
		return (buildError(403, &location));

	cgi.pending = true;
	cgi.interpreter = cgiInterpreter(scriptPath, location);
	cgi.scriptPath = scriptPath;
	// Le parser exige un "listen" dans chaque server : listens[0] existe.
	cgi.env = buildCgiEnv(request, path, scriptPath, server.listens[0]);
	cgi.body = request.getBody();
	cgi.location = &location;
	return ("");
}

// La réponse construite à partir de ce que le script a écrit : son statut,
// ses headers, son body. 502 Bad Gateway quand la sortie ne respecte pas le
// format CGI : le serveur "devant" (webserv) a reçu une mauvaise réponse du
// programme "derrière" (le script).
std::string Server::cgiResponse(const std::string& output, const LocationConfig& location) const
{
	CgiOutput result;

	if (!parseCgiOutput(output, result))
		return (buildError(502, &location));
	if (result.reason.empty())
		result.reason = reasonPhrase(result.status);
	if (result.contentType.empty())
		result.contentType = "text/plain";
	return (buildResponse(result.status, result.reason, result.body, result.contentType, result.headers));
}

// Une réponse d'erreur : la page error_page de la location pour ce code si
// elle existe et se lit, sinon une page générée. Le code reste le code
// d'erreur dans les deux cas. Une error_page qui pointe vers un fichier
// absent ne fait donc jamais planter : on retombe sur la page par défaut.
std::string Server::buildError(int code, const LocationConfig* location,
	const std::string& extraHeaders) const
{
	std::string reason = reasonPhrase(code);

	if (location != NULL)
	{
		std::map<int, std::string>::const_iterator page = location->errorPages.find(code);
		struct stat info;
		std::string content;

		// stat d'abord, comme dans serveFile : une page d'erreur qui serait
		// un FIFO ne doit pas bloquer le serveur.
		if (page != location->errorPages.end()
			&& stat(page->second.c_str(), &info) == 0 && S_ISREG(info.st_mode)
			&& readWholeFile(page->second, content))
			return (buildResponse(code, reason, content, contentTypeFor(page->second), extraHeaders));
	}
	return (buildResponse(code, reason, defaultErrorPage(code, reason), "text/html", extraHeaders));
}

// Assemble le texte de la réponse HTTP :
//     HTTP/1.1 200 OK\r\n
//     Content-Type: text/html\r\n
//     Content-Length: 1234\r\n          <- le client sait où finit le body
//     (headers en plus : Location, Allow, Set-Cookie...)
//     Connection: close\r\n             <- on ferme après chaque réponse
//     \r\n                              <- ligne vide : fin des headers
//     <body>
std::string Server::buildResponse(int statusCode, const std::string& reason, const std::string& body,
	const std::string& contentType, const std::string& extraHeaders) const
{
	std::ostringstream lengthStream;
	lengthStream << body.size();

	std::ostringstream statusStream;
	statusStream << statusCode;

	std::string response;

	response += "HTTP/1.1 ";
	response += statusStream.str();
	response += " ";
	response += reason;
	response += "\r\n";

	response += "Content-Type: ";
	response += contentType;
	response += "\r\n";

	response += "Content-Length: ";
	response += lengthStream.str();
	response += "\r\n";

	response += extraHeaders;
	response += "Connection: close\r\n";
	response += "\r\n";

	response += body;

	return (response);
}
