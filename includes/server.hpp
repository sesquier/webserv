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

class Request;

class Server
{
	public:
		explicit Server(const std::vector<ServerConfig>& configs);
		~Server();

		void run();

	private:
		int _epollFd;
		const std::vector<ServerConfig>& _configs;

		std::map<int, const ServerConfig*> _listeners;		// listening socket -> its server
		std::map<int, const ServerConfig*> _clientServers;	// client socket -> the server it reached

		enum ClientState
		{
			READING,
			CGI_RUNNING,	// the request is complete, its script is running
			WRITING
		};

		std::map<int, std::string> _clients;
		std::map<int, ClientState> _clientStates;
		std::map<int, bool> _requestStarted;

		std::map<int, std::string> _responses;
		std::map<int, std::string::size_type> _bytesSent; // bytes of the response already sent
		std::map<int, std::time_t> _lastActivity;

		// A running script and what the loop has exchanged with it so far.
		struct CgiJob
		{
			CgiProcess				process;		// an fd is -1 once closed
			std::string				body;
			std::string::size_type	bodySent;
			std::string				output;
			const LocationConfig*	location;
			std::time_t				lastActivity;
		};

		std::map<int, CgiJob> _cgiJobs;		// client socket -> its running script
		std::map<int, int> _cgiPipes;		// pipe fd of a script -> its client socket

		Server(const Server& other);
		Server& operator=(const Server& rhs);

		// server.cpp: sockets, epoll loop, reading and sending
		void setupListeningSockets();
		void addListener(const ListenAddress& address, const ServerConfig& config);
		void closeClient(int clientFD);
		void queueResponse(int clientFd, const std::string& response);
		void enableWriteEvent(int clientFd);
		void handleClientWrite(int clientfd);

		void handleEvent(const struct epoll_event& event);

		// cgiEvents.cpp: running scripts from the epoll loop
		void startCgiJob(int clientFd, const CgiRequest& cgi);
		void watchCgiPipe(int pipeFd, unsigned int events, int clientFd);
		void closeCgiFd(int& pipeFd);
		void handleCgiEvent(int pipeFd);
		void writeCgiBody(int clientFd);
		void readCgiOutput(int clientFd);
		bool reapCgiJob(int clientFd, bool force);
		void abortCgiJob(int clientFd);
		void checkCgiJobs(std::time_t now);

		// handlers.cpp: building the response of a parsed request
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