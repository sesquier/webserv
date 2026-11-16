/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/10 15:08:41 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

// ============================================================================
// server.cpp : la partie réseau. C'est le fichier que le correcteur lit en
// premier (questions éliminatoires sur poll/epoll).
//
//   - ouvrir les sockets d'écoute (un par directive listen) ;
//   - LA boucle : un seul epoll_wait surveille tout (écoute, clients en
//     lecture, clients en écriture, pipes des CGI) ;
//   - handleEvent : accept, recv, puis lecture de la requête ;
//   - queueResponse / handleClientWrite : envoi de la réponse avec send ;
//   - closeClient : tout libérer pour un client.
//
// Règles du sujet respectées ici :
//   - aucune lecture ni écriture sur un socket sans qu'epoll l'ait signalé ;
//   - un seul recv ou send par événement ;
//   - le retour de recv/send est toujours testé (0 ET -1) ;
//   - jamais d'errno après une lecture ou une écriture.
// ============================================================================

#include "server.hpp"
#include "request.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <sstream>

// Crée un socket d'écoute prêt à accepter des clients. -1 si une étape échoue.
// Les 5 étapes classiques d'un serveur TCP :
//   socket()     crée le point de communication ;
//   setsockopt() SO_REUSEADDR : on peut relancer webserv tout de suite après
//                l'avoir arrêté (sinon le port reste bloqué ~1 min en TIME_WAIT).
//                Ça ne permet PAS à deux programmes d'écouter le même port ;
//   fcntl()      O_NONBLOCK : accept ne pourra jamais bloquer la boucle ;
//   bind()       attache le socket à l'adresse et au port (échoue si déjà pris) ;
//   listen()     le met en écoute ; 128 = taille de la file des connexions
//                en attente d'un accept.
static int openListeningSocket(const struct addrinfo* info)
{
	int fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

	if (fd == -1)
		return (-1);

	int opt = 1;

	// || s'arrête au premier appel qui échoue : on ferme le socket et on
	// renvoie -1, sans fuite de fd.
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1
		|| fcntl(fd, F_SETFL, O_NONBLOCK) == -1
		|| bind(fd, info->ai_addr, info->ai_addrlen) == -1
		|| listen(fd, 128) == -1)
	{
		close(fd);
		return (-1);
	}
	return (fd);
}

// "127.0.0.1:8080", pour les messages.
static std::string addressToString(const ListenAddress& address)
{
	std::ostringstream text;

	text << address.host << ":" << address.port;
	return (text.str());
}

// Une erreur détectée AVANT de savoir quelle location est visée (requête
// illisible, client trop lent) utilise les pages d'erreur de la location "/".
// Elle hérite des error_page du server : la page 404 personnalisée du server
// sert donc aussi pour ces erreurs.
static const LocationConfig* defaultLocation(const ServerConfig* server)
{
	return (&server->findLocation("/"));
}

// Le seul epoll du programme. epoll_create(1) : l'argument est ignoré depuis
// Linux 2.6.8, il doit seulement être > 0.
Server::Server(const std::vector<ServerConfig>& configs) : _epollFd(-1), _configs(configs)
{
	_epollFd = epoll_create(1);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create failed");
}

// Appelé quand run() se termine par une exception fatale (epoll_wait en
// erreur, port déjà pris...) : on tue et récupère les scripts en cours, puis
// on ferme les sockets d'écoute et epoll.
// Ctrl-C ne passe pas par ici : SIGINT termine le processus directement, et
// le noyau ferme tous ses fds. Les scripts meurent aussi : le terminal envoie
// SIGINT à tout le groupe de processus, dont ils font partie.
Server::~Server()
{
	while (!_cgiJobs.empty())
		abortCgiJob(_cgiJobs.begin()->first);
	for (std::map<int, const ServerConfig*>::iterator it = _listeners.begin(); it != _listeners.end(); ++it)
		close(it->first);
	if (_epollFd != -1)
		close(_epollFd);
}

// Un socket d'écoute par directive listen de chaque server.
void Server::setupListeningSockets()
{
	for (size_t i = 0; i < _configs.size(); i++)
	{
		for (size_t j = 0; j < _configs[i].listens.size(); j++)
			addListener(_configs[i].listens[j], _configs[i]);
	}
}

// Ouvre un socket d'écoute et l'inscrit dans epoll.
void Server::addListener(const ListenAddress& address, const ServerConfig& config)
{
	struct addrinfo hints;
	struct addrinfo* info = NULL;
	std::ostringstream port;

	// getaddrinfo transforme "127.0.0.1" + "8080" en adresse utilisable par
	// bind. AF_INET : IPv4 ; SOCK_STREAM : TCP ; AI_NUMERICHOST : l'hôte est
	// déjà une IP (le parser a converti "localhost"), pas de résolution DNS.
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	port << address.port;

	if (getaddrinfo(address.host.c_str(), port.str().c_str(), &hints, &info) != 0)
		throw std::runtime_error("invalid address " + addressToString(address));

	int fd = openListeningSocket(info);

	freeaddrinfo(info);		// getaddrinfo alloue : on libère tout de suite
	// Port déjà pris (un autre webserv, par exemple) : erreur fatale avec un
	// message clair. C'est le comportement attendu par la grille.
	if (fd == -1)
		throw std::runtime_error("cannot listen on " + addressToString(address));

	// Enregistré AVANT epoll_ctl : si epoll_ctl échoue, le destructeur
	// trouvera ce fd dans _listeners et le fermera.
	_listeners[fd] = &config;

	struct epoll_event event;

	// EPOLLIN sur un socket d'écoute = un client attend d'être accepté.
	std::memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &event) == -1)
		throw std::runtime_error("epoll_ctl failed");

	std::cout << "Listening on " << addressToString(address) << std::endl;
}

// ============================================================================
// LA boucle principale. Le correcteur doit la voir : un seul epoll_wait, qui
// surveille en même temps la lecture et l'écriture de tous les fds.
// ============================================================================
void Server::run()
{
	setupListeningSockets();

	// epoll_wait remplit ce tableau avec au plus 10 fds prêts par tour. S'il y
	// en a plus, les autres sont simplement rendus au tour suivant.
	struct epoll_event events[10];

	while (true)
	{
		// Le timeout (en ms) réveille la boucle même sans événement, pour
		// vérifier les délais. Pendant qu'un script tourne, on se réveille plus
		// souvent (50 ms) pour récupérer vite un script qui vient de finir.
		int count = epoll_wait(_epollFd, events, 10, _cgiJobs.empty() ? 1000 : 50);

		// Le SEUL errno du projet. Il suit epoll_wait, qui n'est ni une lecture
		// ni une écriture : la règle du sujet ne le vise pas.
		// EINTR : un signal (Ctrl-Z puis fg, par exemple) a interrompu
		// l'attente. Rien de grave : on recommence.
		if (count == -1)
		{
			int pollError = errno;

			if (pollError == EINTR)
				continue;

			throw std::runtime_error(
				std::string("epoll_wait failed: ") + std::strerror(pollError)
			);
		}

		// Chaque événement est traité dans un try : une erreur sur un client
		// (mémoire, epoll_ctl...) ferme ce client mais n'arrête jamais le serveur.
		for (int i = 0; i < count; i++)
		{
			try
			{
				handleEvent(events[i]);
			}
			catch (const std::exception& e)
			{
				std::cerr << "Event error: " << e.what() << std::endl;
			}
			catch (...)
			{
				std::cerr << "Unknown event error" << std::endl;
			}
		}

		std::time_t now = std::time(NULL);

		// Les scripts : ceux qui ont fini sont récupérés, ceux qui ne donnent
		// plus signe de vie depuis 10 s sont tués (504). Voir cgiEvents.cpp.
		checkCgiJobs(now);

		// Les clients silencieux depuis 30 s. Sans ce timeout, un client qui
		// ouvre une connexion et n'envoie rien occuperait un fd pour toujours.
		std::map<int, std::time_t>::iterator activityIt = _lastActivity.begin();

		while (activityIt != _lastActivity.end())
		{
			int clientFd = activityIt->first;
			std::time_t lastActivity = activityIt->second;

			// On avance l'itérateur AVANT tout : closeClient efface ce client
			// de _lastActivity, ce qui invaliderait l'itérateur courant.
			++activityIt;

			// Un client qui attend son script est sous le timeout du CGI.
			if (_cgiJobs.count(clientFd))
				continue;

			if (now - lastActivity >= 30)
			{
				std::map<int, bool>::iterator requestIt = _requestStarted.find(clientFd);
				std::map<int, ClientState>::iterator stateIt = _clientStates.find(clientFd);

				// Requête commencée mais pas finie : on répond 408 Request
				// Timeout. Connexion ouverte sans rien envoyer : on ferme.
				if (requestIt != _requestStarted.end()
					&& requestIt->second
					&& stateIt != _clientStates.end()
					&& stateIt->second == READING)
				{
					try
					{
						queueResponse(
							clientFd,
							buildError(408, defaultLocation(_clientServers[clientFd]))
						);
					}
					catch (...)
					{
						closeClient(clientFd);
					}
				}
				else
					closeClient(clientFd);
			}
		}
	}
}

// ============================================================================
// handleEvent : l'aiguillage. Un fd est prêt ; selon ce qu'il est (socket
// d'écoute, pipe de script, client) et l'état du client, on fait UNE action.
// C'est le "chemin de epoll jusqu'à l'I/O du client" que la grille demande.
// ============================================================================
void Server::handleEvent(const struct epoll_event& event)
{
	int fd = event.data.fd;
	// Le fd d'un client accepté pendant cet appel : si une exception arrive
	// avant qu'il soit enregistré, le catch du bas le ferme (pas de fuite).
	int acceptedClientFd = -1;

	std::map<int, const ServerConfig*>::iterator listener = _listeners.find(fd);

	try
	{
		// 1. Un socket d'écoute est prêt : un nouveau client attend.
		if (listener != _listeners.end())
		{
			struct sockaddr_in clientAddr;
			socklen_t clientLen = sizeof(clientAddr);

			int clientFd = accept(fd, reinterpret_cast<struct sockaddr*>(&clientAddr),
				&clientLen);

			// Le client est peut-être reparti entre-temps : on n'insiste pas,
			// sans regarder errno. On revient simplement à la boucle.
			if (clientFd == -1)
				return ;

			acceptedClientFd = clientFd;

			// Le socket du client en non bloquant, comme tous nos fds.
			if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1)
			{
				close(clientFd);
				acceptedClientFd = -1;
				return ;
			}

			// On surveille ce qu'il envoie (EPOLLIN) et s'il ferme sa
			// connexion (EPOLLRDHUP).
			struct epoll_event clientEvent;
			std::memset(&clientEvent, 0, sizeof(clientEvent));
			clientEvent.events = EPOLLIN | EPOLLRDHUP;
			clientEvent.data.fd = clientFd;

			if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
			{
				close(clientFd);
				acceptedClientFd = -1;
				return ;
			}

			// Le client existe maintenant dans toutes les maps.
			// _clientServers : on retient quel server (quel site) il a contacté,
			// d'après le socket d'écoute qui l'a reçu.
			_clients[clientFd] = "";
			_clientServers[clientFd] = listener->second;
			_clientStates[clientFd] = READING;
			_requestStarted[clientFd] = false;
			_lastActivity[clientFd] = std::time(NULL);

			acceptedClientFd = -1;

			std::cout << "Client connected: fd = " << clientFd << std::endl;

			return;
		}

		// 2. Le pipe d'un script, pas un client : voir cgiEvents.cpp.
		// Testé avant EPOLLERR : la fin d'un pipe arrive aussi en EPOLLHUP,
		// et c'est handleCgiEvent qui sait quoi en faire.
		if (_cgiPipes.count(fd))
		{
			handleCgiEvent(fd);
			return ;
		}

		// 3. Un client. Erreur sur son socket : on le ferme.
		if (event.events & EPOLLERR)
		{
			closeClient(fd);
			return ;
		}

		std::map<int, ClientState>::iterator stateIt = _clientStates.find(fd);

		// fd inconnu (ne devrait pas arriver) : on le ferme par prudence.
		if (stateIt == _clientStates.end())
		{
			closeClient(fd);
			return ;
		}

		// La connexion est coupée des deux côtés.
		if (event.events & EPOLLHUP)
		{
			closeClient(fd);
			return ;
		}

		// 3a. On lui envoie sa réponse : un send par EPOLLOUT.
		if (stateIt->second == WRITING)
		{
			if (event.events & EPOLLOUT)
				handleClientWrite(fd);
			return ;
		}

		// 3b. Son script tourne : on ne surveille que son départ
		// (EPOLLRDHUP). S'il part, on ferme, ce qui tue aussi son script.
		if (stateIt->second == CGI_RUNNING)
		{
			closeClient(fd);
			return ;
		}

		if (stateIt->second != READING)
			return ;

		// Il a fermé sa connexion et n'a plus rien à lire.
		if ((event.events & EPOLLRDHUP) && !(event.events & EPOLLIN))
		{
			closeClient(fd);
			return ;
		}

		if (!(event.events & EPOLLIN))
			return ;

		// 3c. Il a envoyé des données : UN SEUL recv, puis retour à la boucle
		// si la requête est incomplète. epoll (en mode level-triggered, celui
		// par défaut) nous rappellera tant qu'il reste des données.
		char buffer[4096];

		ssize_t bytesReceived = recv(fd, buffer, sizeof(buffer), 0);

		// > 0 : des octets à ajouter. 0 : le client a fermé. -1 : erreur.
		// Les deux derniers cas sont traités pareil : fermeture. On ne
		// regarde jamais errno pour savoir pourquoi.
		if (bytesReceived > 0)
		{
			_clients[fd].append(buffer, bytesReceived);
			_requestStarted[fd] = true;
			_lastActivity[fd] = std::time(NULL);
		}
		else
		{
			closeClient(fd);
			return ;
		}

		// On analyse tout ce qu'il a envoyé depuis le début. Le Request est
		// recréé à chaque fois : simple, et sans état à garder entre deux recv.
		Request request;
		Request::ParseStatus parseStatus;

		parseStatus = request.parseHeaders(_clients[fd]);

		// Pas encore de ligne vide : la suite arrivera au prochain EPOLLIN.
		if (parseStatus == Request::INCOMPLETE)
			return ;

		if (parseStatus == Request::INVALID)
		{
			queueResponse(fd, buildError(400, defaultLocation(_clientServers[fd])));
			return ;
		}

		if (parseStatus == Request::HEADERS_TOO_LARGE)
		{
			queueResponse(fd, buildError(431, defaultLocation(_clientServers[fd])));
			return ;
		}

		// Il faut la location AVANT de lire le body : c'est elle qui donne
		// la limite client_max_body_size. On retire "?query" et on décode les
		// %xx pour trouver la bonne location.
		std::string path = request.getTarget();
		std::string::size_type queryPos = path.find('?');

		if (queryPos != std::string::npos)
			path.erase(queryPos);

		std::string decodedPath;

		if (!decodePath(path, decodedPath))
		{
			queueResponse(fd, buildError(400, defaultLocation(_clientServers[fd])));
			return ;
		}

		path = decodedPath;

		const LocationConfig& location = _clientServers[fd]->findLocation(path);

		// Le body : 413 dès que Content-Length dépasse la limite, sans
		// attendre de l'avoir reçu ; INCOMPLETE tant qu'il n'est pas arrivé.
		parseStatus = request.parseBody(_clients[fd], location.maxBodySize);

		if (parseStatus == Request::INCOMPLETE)
			return ;

		if (parseStatus == Request::INVALID)
		{
			queueResponse(fd, buildError(400, &location));
			return ;
		}

		if (parseStatus == Request::BODY_TOO_LARGE)
		{
			queueResponse(fd, buildError(413, &location));
			return ;
		}

		std::cout << "Request parsed: " << request.getMethod() << " " << request.getTarget() << " "
			<< request.getVersion() << " (server root: " << _clientServers[fd]->root << ")"
			<< std::endl;

		// La requête est complète : handlers.cpp fabrique la réponse.
		// Pour un script, il remplit "cgi" au lieu de répondre tout de suite.
		CgiRequest cgi;
		std::string response = handleRequest(request, *_clientServers[fd], cgi);

		if (cgi.pending)
		{
			startCgiJob(fd, cgi);
			return ;
		}

		try
		{
			queueResponse(fd, response);
		}
		catch (const std::exception& e)
		{
			std::cerr << "Failed to prepare response: " << e.what() << std::endl;
			closeClient(fd);
		}
	}
	// Une exception (manque de mémoire, epoll_ctl...) : on ferme le client
	// concerné, puis on la relance pour que run() l'affiche. Jamais le
	// socket d'écoute : il doit continuer d'accepter les autres clients.
	catch (...)
	{
		if (acceptedClientFd != -1)
			closeClient(acceptedClientFd);
		else if (listener == _listeners.end())
			closeClient(fd);

		throw ;
	}
}

// Tout libérer pour un client, dans l'ordre : son script éventuel, epoll,
// le socket, puis toutes ses entrées dans les maps. Appeler closeClient sur
// un fd déjà fermé est sans danger : les erase ne trouvent rien.
void Server::closeClient(int clientFd)
{
	abortCgiJob(clientFd);
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
	close(clientFd);
	_clients.erase(clientFd);
	_clientServers.erase(clientFd);
	_responses.erase(clientFd);
	_bytesSent.erase(clientFd);
	_clientStates.erase(clientFd);
	_lastActivity.erase(clientFd);
	_requestStarted.erase(clientFd);
}

// Le client passe de "on lit" à "on écrit" : EPOLL_CTL_MOD remplace les
// événements surveillés par EPOLLOUT (= son socket peut recevoir des données).
void Server::enableWriteEvent(int clientFd)
{
	struct epoll_event event;
	std::memset(&event, 0, sizeof(event));

	event.events = EPOLLOUT;
	event.data.fd = clientFd;

	if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &event) == -1)
		throw std::runtime_error("Failed to enable EPOLLOUT");
}

// Appelée à chaque EPOLLOUT : envoie la suite de la réponse avec UN send.
// La réponse peut être plus grosse que ce que le socket accepte d'un coup :
// _bytesSent retient où on en est, le prochain EPOLLOUT enverra la suite.
void Server::handleClientWrite(int clientFd)
{
	std::map<int, std::string>::iterator responseIt = _responses.find(clientFd);

	std::map<int, std::string::size_type>::iterator sentIt = _bytesSent.find(clientFd);

	if (responseIt == _responses.end() || sentIt == _bytesSent.end())
	{
		closeClient(clientFd);
		return;
	}

	const std::string& response = responseIt->second;
	std::string::size_type offset = sentIt->second;

	if (offset >= response.size())
	{
		closeClient(clientFd);
		return;
	}

	std::string::size_type remaining = response.size() - offset;

	// MSG_NOSIGNAL : si le client est parti, send renvoie -1 au lieu
	// d'envoyer SIGPIPE (déjà ignoré dans main : double sécurité).
	ssize_t bytesSent = send(clientFd, response.c_str() + offset, remaining, MSG_NOSIGNAL);

	// > 0 : on avance ; tout est parti -> fermeture (Connection: close).
	// 0 ou -1 : le client ne peut plus recevoir -> fermeture.
	if (bytesSent > 0)
	{
		sentIt->second += static_cast<std::string::size_type>(bytesSent);
		// _lastActivity[clientFd] = std::time(NULL);

		if (sentIt->second == response.size())
			closeClient(clientFd);
	}
	else
	{
		closeClient(clientFd);
	}

}

// La réponse est prête : on la range et le client passe en écriture. Rien
// n'est envoyé ici : on attend qu'epoll dise que le socket est prêt.
void Server::queueResponse(int clientFd, const std::string& response)
{
	_responses[clientFd] = response;
	_bytesSent[clientFd] = 0;
	_clientStates[clientFd] = WRITING;
	_lastActivity[clientFd] = std::time(NULL);

	enableWriteEvent(clientFd);
}
