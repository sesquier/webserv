// ============================================================================
// cgi.cpp : les briques du CGI, sans epoll.
//
//   buildCgiEnv     la requête traduite en variables d'environnement ;
//   startCgi        pipe + fork + dup2 + chdir + execve ;
//   parseCgiOutput  la sortie du script découpée en réponse HTTP.
//
// Les variables suivent la RFC 3875, le standard CGI.
// ============================================================================

#include "cgi.hpp"
#include "request.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

CgiRequest::CgiRequest() : pending(false), location(NULL)
{
}

// Un nombre en texte (std::to_string n'existe pas en C++98).
static std::string numberToString(size_t number)
{
	std::ostringstream text;

	text << number;
	return (text.str());
}

// "/cgi-bin/hello.py?name=Bob" -> "name=Bob". Vide sans '?'.
static std::string queryString(const std::string& target)
{
	std::string::size_type mark = target.find('?');

	if (mark == std::string::npos)
		return ("");
	return (target.substr(mark + 1));
}

// "./www/site1/cgi-bin/hello.py" -> "hello.py".
static std::string fileName(const std::string& path)
{
	return (path.substr(path.find_last_of('/') + 1));
}

// Nom de header -> nom de variable : "user-agent" -> "HTTP_USER_AGENT".
// Renvoie "" pour un header qui ne doit PAS devenir une variable :
// - Content-Type et Content-Length ont déjà leur propre variable ;
// - "Proxy" : beaucoup de programmes lisent HTTP_PROXY comme leur réglage de
//   proxy ; un client pourrait rediriger les requêtes du script vers lui
//   (l'attaque "httpoxy") ;
// - un nom avec autre chose que lettres, chiffres et '-' : "X_Foo" donnerait
//   la même variable que "X-Foo", un client pourrait imiter un header.
static std::string headerVariable(const std::string& name)
{
	if (name == "content-type" || name == "content-length" || name == "proxy")
		return ("");

	std::string variable = "HTTP_";

	for (std::string::size_type i = 0; i < name.size(); i++)
	{
		unsigned char c = static_cast<unsigned char>(name[i]);

		if (c == '-')
			variable += '_';
		else if (std::isalnum(c))
			variable += static_cast<char>(std::toupper(c));
		else
			return ("");
	}
	return (variable);
}

// Le sujet : "the full request and arguments provided by the client must be
// available to the CGI". Tout passe donc par l'environnement, sauf le body.
std::vector<std::string> buildCgiEnv(const Request& request, const std::string& urlPath,
	const std::string& scriptPath, const ListenAddress& address)
{
	std::vector<std::string> env;

	// La requête : méthode, protocole, et ce qui suit le '?' dans l'URL.
	env.push_back("REQUEST_METHOD=" + request.getMethod());
	env.push_back("QUERY_STRING=" + queryString(request.getTarget()));
	env.push_back("SERVER_PROTOCOL=" + request.getVersion());

	// Le script : son chemin dans l'URL, et son fichier. Le script tourne
	// dans son propre dossier (chdir), donc son fichier est nommé depuis là :
	// "hello.py".
	env.push_back("SCRIPT_NAME=" + urlPath);
	env.push_back("SCRIPT_FILENAME=" + fileName(scriptPath));

	// Le body arrive sur stdin ; sa taille dit au script combien lire.
	// Pour un body chunked, c'est la taille du body reconstitué.
	if (request.getMethod() == "POST")
		env.push_back("CONTENT_LENGTH=" + numberToString(request.getBody().size()));

	// Les headers : le type du body, puis chaque header en HTTP_<NOM>
	// ("Cookie: id=42" -> HTTP_COOKIE=id=42).
	const std::map<std::string, std::string>&	headers = request.getHeaders();
	std::map<std::string, std::string>::const_iterator	it = headers.find("content-type");

	if (it != headers.end())
		env.push_back("CONTENT_TYPE=" + it->second);
	for (it = headers.begin(); it != headers.end(); ++it)
	{
		std::string variable = headerVariable(it->first);

		if (!variable.empty())
			env.push_back(variable + "=" + it->second);
	}

	// Le serveur qui lance le script.
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_SOFTWARE=webserv");
	env.push_back("SERVER_NAME=" + address.host);
	env.push_back("SERVER_PORT=" + numberToString(address.port));

	// php-cgi refuse de lancer un script sans elle (protection contre un
	// appel direct). Inutile pour Python, mais sans danger.
	env.push_back("REDIRECT_STATUS=200");
	return (env);
}

// "./www/site1/cgi-bin/hello.py" -> "./www/site1/cgi-bin".
static std::string directoryOf(const std::string& path)
{
	std::string::size_type slash = path.find_last_of('/');

	if (slash == std::string::npos)
		return (".");
	if (slash == 0)
		return ("/");
	return (path.substr(0, slash));
}

static void closePipe(int fds[2])
{
	close(fds[0]);
	close(fds[1]);
}

// ----------------------------------------------------------------------------
// Dans l'ENFANT, qui ne revient jamais d'ici : il branche les pipes sur stdin
// et stdout, va dans le dossier du script, puis DEVIENT l'interpréteur.
// Si une étape échoue, l'enfant quitte sans rien écrire : le serveur voit une
// sortie vide et un code de sortie 1, et répond 502.
// ----------------------------------------------------------------------------
static void runScript(int toScript[2], int fromScript[2], const std::string& interpreter,
	const std::string& scriptPath, const std::vector<std::string>& env)
{
	// dup2(a, b) : b devient une copie de a. Après ça, ce que le script lit
	// sur stdin (fd 0) vient du pipe toScript, et ce qu'il écrit sur stdout
	// (fd 1) part dans le pipe fromScript.
	if (dup2(toScript[0], STDIN_FILENO) == -1 || dup2(fromScript[1], STDOUT_FILENO) == -1)
		std::exit(1);
	// stdin et stdout SONT les pipes maintenant : les fds d'origine ne servent
	// plus. Un bout d'écriture laissé ouvert ici empêcherait le serveur de
	// voir EOF : un pipe n'est fini que quand TOUS ses bouts d'écriture sont
	// fermés.
	closePipe(toScript);
	closePipe(fromScript);
	// L'enfant a aussi hérité de TOUS les fds du serveur : sockets d'écoute,
	// epoll, autres clients. Laissés ouverts, un client fermé par le serveur
	// resterait connecté jusqu'à la fin du script. 1024 est la limite
	// habituelle de fds ouverts par processus.
	for (int fd = 3; fd < 1024; fd++)
		close(fd);
	// Le sujet : "the CGI should be run in the correct directory for relative
	// path file access". Le script peut ouvrir "data.txt" à côté de lui.
	if (chdir(directoryOf(scriptPath).c_str()) == -1)
		std::exit(1);

	// execve veut des tableaux de char* terminés par NULL :
	// argv = { "/usr/bin/python3", "hello.py", NULL }
	// envp = { "REQUEST_METHOD=GET", ..., NULL }
	// const_cast : execve n'est pas déclaré const, mais ne modifie rien.
	std::string			script = fileName(scriptPath);
	std::vector<char*>	argv;
	std::vector<char*>	envp;

	argv.push_back(const_cast<char*>(interpreter.c_str()));
	argv.push_back(const_cast<char*>(script.c_str()));
	argv.push_back(NULL);
	for (size_t i = 0; i < env.size(); i++)
		envp.push_back(const_cast<char*>(env[i].c_str()));
	envp.push_back(NULL);

	// execve REMPLACE le programme du processus : en cas de succès, la ligne
	// suivante n'est jamais exécutée.
	execve(interpreter.c_str(), &argv[0], &envp[0]);
	std::exit(1);	// atteint seulement si execve a échoué (interpréteur absent...)
}

// ----------------------------------------------------------------------------
// startCgi : crée les deux pipes et le processus du script.
//
//   serveur                         script
//   toScript[1]   --- body --->     toScript[0]   = son stdin
//   fromScript[0] <-- sortie ---    fromScript[1] = son stdout
//
// fork est utilisé UNIQUEMENT ici : le sujet l'interdit ailleurs que pour le
// CGI. Pourquoi fork ? execve remplace le processus qui l'appelle ; appelé
// directement, webserv deviendrait Python. On le fait donc dans un enfant.
// ----------------------------------------------------------------------------
bool startCgi(const std::string& interpreter, const std::string& scriptPath,
	const std::vector<std::string>& env, CgiProcess& process)
{
	int toScript[2];	// serveur -> script : le body, sur le stdin du script
	int fromScript[2];	// script -> serveur : la sortie, depuis son stdout

	// Chaque erreur ferme ce qui a déjà été ouvert : aucune fuite de fd.
	if (pipe(toScript) == -1)
		return (false);
	if (pipe(fromScript) == -1)
	{
		closePipe(toScript);
		return (false);
	}

	pid_t pid = fork();

	if (pid == -1)
	{
		closePipe(toScript);
		closePipe(fromScript);
		return (false);
	}
	// fork renvoie 0 dans l'enfant : il part dans runScript et n'en revient pas.
	if (pid == 0)
		runScript(toScript, fromScript, interpreter, scriptPath, env);

	// Le parent garde un bout de chaque pipe : il écrit le body et lit la
	// sortie. Les deux autres bouts appartiennent à l'enfant.
	close(toScript[0]);
	close(fromScript[1]);
	// Non bloquants : ces deux fds iront dans epoll, comme les sockets.
	if (fcntl(toScript[1], F_SETFL, O_NONBLOCK) == -1
		|| fcntl(fromScript[0], F_SETFL, O_NONBLOCK) == -1)
	{
		// Le script tourne déjà : on le tue et on le récupère (pas de zombie).
		close(toScript[1]);
		close(fromScript[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return (false);
	}
	process.pid = pid;
	process.inputFd = toScript[1];
	process.outputFd = fromScript[0];
	return (true);
}

// isdigit avec un char signé (accent UTF-8) est un comportement indéfini :
// on convertit d'abord en unsigned char.
static bool isDigit(char c)
{
	return (std::isdigit(static_cast<unsigned char>(c)) != 0);
}

// Les noms de headers ignorent la casse : "Content-Type" = "content-type".
static std::string toLower(const std::string& text)
{
	std::string lower = text;

	for (std::string::size_type i = 0; i < lower.size(); i++)
		lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
	return (lower);
}

// "Status: 404 Not Found" -> 404 et "Not Found". Trois chiffres, puis la
// phrase (facultative), comme sur la 1re ligne d'une réponse HTTP.
static bool parseStatus(const std::string& value, CgiOutput& result)
{
	if (value.size() < 3 || !isDigit(value[0]) || !isDigit(value[1])
		|| !isDigit(value[2]) || (value.size() > 3 && value[3] != ' '))
		return (false);

	int code = (value[0] - '0') * 100 + (value[1] - '0') * 10 + (value[2] - '0');

	if (code < 100 || code > 599)
		return (false);
	result.status = code;
	result.reason = (value.size() > 4) ? value.substr(4) : "";
	return (true);
}

// "Content-Length: 42" -> 42. Uniquement des chiffres ; refuse une longueur
// qui ferait déborder le calcul (18 chiffres maximum).
static bool parseLength(const std::string& value, std::string::size_type& length)
{
	if (value.empty() || value.size() > 18)
		return (false);
	length = 0;
	for (std::string::size_type i = 0; i < value.size(); i++)
	{
		if (!isDigit(value[i]))
			return (false);
		length = length * 10 + (value[i] - '0');
	}
	return (true);
}

// ----------------------------------------------------------------------------
// parseCgiOutput : la sortie du script ("headers, ligne vide, body")
// transformée en morceaux de réponse HTTP. Faux = sortie invalide -> 502.
// ----------------------------------------------------------------------------
bool parseCgiOutput(const std::string& output, CgiOutput& result)
{
	// Les headers finissent à la première ligne vide. Les scripts écrivent
	// souvent "\n" seul (le print de Python) : "\n\n" est accepté autant que
	// "\r\n\r\n". On garde celui qui arrive le plus tôt.
	std::string::size_type	headersEnd = output.find("\r\n\r\n");
	std::string::size_type	lfHeadersEnd = output.find("\n\n");
	std::string::size_type	separatorSize = 4;

	if (lfHeadersEnd < headersEnd)
	{
		headersEnd = lfHeadersEnd;
		separatorSize = 2;
	}
	// Pas de ligne vide : ce n'est pas une sortie CGI (script qui a planté
	// avant d'écrire, par exemple).
	if (headersEnd == std::string::npos)
		return (false);

	result.status = 200;
	result.reason = "";
	result.contentType = "";
	result.headers = "";
	result.body = output.substr(headersEnd + separatorSize);

	std::istringstream	lines(output.substr(0, headersEnd));
	std::string			line;
	bool				hasStatus = false;
	bool				hasLocation = false;

	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		std::string::size_type colon = line.find(':');

		if (colon == std::string::npos || colon == 0)
			return (false);

		std::string				name = line.substr(0, colon);
		std::string				value = line.substr(colon + 1);
		std::string::size_type	start = value.find_first_not_of(" \t");

		value = (start == std::string::npos) ? "" : value.substr(start);
		// "Status:" n'est pas un vrai header HTTP : c'est la façon CGI de
		// choisir le code de la réponse.
		if (toLower(name) == "status")
		{
			if (!parseStatus(value, result))
				return (false);
			hasStatus = true;
		}
		else if (toLower(name) == "content-type")
			result.contentType = value;
		else if (toLower(name) == "content-length")
		{
			// Sans lui, la fin de la sortie (EOF) marque la fin du body.
			// Avec lui, on coupe le body à cette taille. Plus grand que ce que
			// le script a écrit : sortie incohérente, refusée.
			std::string::size_type length;

			if (!parseLength(value, length) || length > result.body.size())
				return (false);
			result.body.erase(length);
		}
		else if (toLower(name) != "connection")	// webserv ferme lui-même chaque connexion
		{
			// Tous les autres headers sont recopiés tels quels dans la
			// réponse : Set-Cookie, Location, Cache-Control...
			if (toLower(name) == "location")
				hasLocation = true;
			result.headers += name + ": " + value + "\r\n";
		}
	}
	// Un script qui donne seulement "Location:" demande une redirection
	// (RFC 3875) : 302 Found.
	if (hasLocation && !hasStatus)
	{
		result.status = 302;
		result.reason = "Found";
	}
	// Un body doit dire ce qu'il est.
	if (result.contentType.empty() && !result.body.empty())
		return (false);
	return (true);
}
