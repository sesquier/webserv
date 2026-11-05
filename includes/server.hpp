/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:39 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/29 15:48:34 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>
#include <vector>
#include <ctime>
#include "config/ServerConfig.hpp"

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
			WRITING
		};

		std::map<int, std::string> _clients;
		std::map<int, ClientState> _clientStates;

		std::map<int, std::string> _responses;
		std::map<int, std::string::size_type> _bytesSent; // contient le nombre d'octets deja envoyes
		std::map<int, std::time_t> _lastActivity;

		Server(const Server& other);
		Server& operator=(const Server& rhs);

		void setupListeningSockets();
		void addListener(const ListenAddress& address, const ServerConfig& config);
		void closeClient(int clientFD);

		std::string buildResponse(int statusCode, const std::string& reason, const std::string& body, const std::string&  contentType = "text/plain") const;
		// std::string buildError() const;
		void queueResponse(int clientFd, const std::string& response);

		void enableWriteEvent(int clientFd);
		void handleClientWrite(int clientfd);
};



#endif