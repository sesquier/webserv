/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:39 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/09 18:40:48 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include <ctime>
#include "config/ServerConfig.hpp"
#include "cgi.hpp"

// Déclaration anticipée : server.hpp n'a besoin que du nom de la classe
// Request (il ne manipule que des références). Inclure request.hpp ici
// ralentirait la compilation de tous les fichiers qui incluent server.hpp.
class Request;

// ============================================================================
// Server : le cœur du programme. Un seul objet, une seule boucle epoll.
//
// Ses méthodes sont réparties dans 3 fichiers :
//   server.cpp     sockets, boucle epoll, lecture des requêtes, envoi des réponses
//   handlers.cpp   ce qu'on répond : GET, POST, DELETE, erreurs, CGI
//   cgiEvents.cpp  les scripts CGI vus depuis la boucle epoll
//
// L'état de chaque client est rangé dans des std::map dont la clé est le fd
// de son socket. Pas de classe Client : chaque map répond à une question
// ("qu'a-t-il envoyé ?", "où en est sa réponse ?"...), et closeClient les
// efface toutes.
// ============================================================================
class Server
{
	public:
		// explicit : pas de conversion implicite d'un vector en Server.
		explicit Server(const std::vector<ServerConfig>& configs);
		~Server();

		void run();

	private:
		int _epollFd;
		// Référence : la config appartient au main, le Server ne fait que la lire.
		const std::vector<ServerConfig>& _configs;

		std::map<int, const ServerConfig*> _listeners;		// socket d'écoute -> son server
		std::map<int, const ServerConfig*> _clientServers;	// socket client -> le server qu'il a contacté

		// La "machine à états" d'un client :
		//   READING -> (CGI_RUNNING) -> WRITING -> fermeture
		// L'état décide de ce qu'on surveille avec epoll et de ce qu'on fait
		// à chaque événement.
		enum ClientState
		{
			READING,		// on reçoit sa requête (EPOLLIN)
			CGI_RUNNING,	// requête complète, son script tourne
			WRITING			// on envoie sa réponse (EPOLLOUT)
		};

		std::map<int, std::string> _clients;			// ce que le client a envoyé jusqu'ici
		std::map<int, ClientState> _clientStates;
		std::map<int, bool> _requestStarted;			// a-t-il envoyé au moins un octet ? (408 ou simple fermeture)

		std::map<int, std::string> _responses;					// la réponse complète à envoyer
		std::map<int, std::string::size_type> _bytesSent;		// combien d'octets sont déjà partis
		std::map<int, std::time_t> _lastActivity;				// pour le timeout de 30 s

		// Un script en cours et ce que la boucle a échangé avec lui.
		struct CgiJob
		{
			CgiProcess				process;		// pid + 2 pipes ; un fd vaut -1 une fois fermé
			std::string				body;			// le body à écrire sur son stdin
			std::string::size_type	bodySent;		// combien d'octets du body sont partis
			std::string				output;			// ce qu'il a écrit sur son stdout
			const LocationConfig*	location;		// pour ses pages d'erreur (502, 504)
			std::time_t				lastActivity;	// pour le timeout de 10 s
		};

		std::map<int, CgiJob> _cgiJobs;		// socket client -> son script
		std::map<int, int> _cgiPipes;		// fd d'un pipe de script -> socket du client

		// Copie interdite : un Server possède des fds, deux copies les
		// fermeraient deux fois.
		Server(const Server& other);
		Server& operator=(const Server& rhs);

		// server.cpp : sockets, boucle epoll, lecture et envoi
		void setupListeningSockets();
		void addListener(const ListenAddress& address, const ServerConfig& config);
		void closeClient(int clientFD);
		void queueResponse(int clientFd, const std::string& response);
		void enableWriteEvent(int clientFd);
		void handleClientWrite(int clientfd);

		void handleEvent(const struct epoll_event& event);

		// cgiEvents.cpp : les scripts depuis la boucle epoll
		void startCgiJob(int clientFd, const CgiRequest& cgi);
		void watchCgiPipe(int pipeFd, unsigned int events, int clientFd);
		void closeCgiFd(int& pipeFd);
		void handleCgiEvent(int pipeFd);
		void writeCgiBody(int clientFd);
		void readCgiOutput(int clientFd);
		bool reapCgiJob(int clientFd, bool force);
		void abortCgiJob(int clientFd);
		void checkCgiJobs(std::time_t now);

		// handlers.cpp : construire la réponse d'une requête déjà lue.
		// Toutes const : elles ne modifient pas l'état du serveur, elles
		// renvoient la réponse sous forme de texte.
		std::string handleRequest(const Request& request, const ServerConfig& server, CgiRequest& cgi) const;
		std::string handleGet(const std::string& path, const LocationConfig& location) const;
		std::string handleDirectory(const std::string& path, const std::string& dirPath,
			const LocationConfig& location) const;
		std::string serveFile(const std::string& filePath, const LocationConfig& location) const;
		std::string handlePost(const Request& request, const std::string& path, const LocationConfig& location) const;
		std::string handleDelete(const std::string& path, const LocationConfig& location) const;
		std::string handleCgi(const Request& request, const std::string& path, const ServerConfig& server,
			const LocationConfig& location, CgiRequest& cgi) const;
		std::string cgiResponse(const std::string& output, const LocationConfig& location) const;
		std::string buildResponse(int statusCode, const std::string& reason, const std::string& body,
			const std::string& contentType = "text/plain", const std::string& extraHeaders = "") const;
		std::string buildError(int code, const LocationConfig* location,
			const std::string& extraHeaders = "") const;
};



#endif
