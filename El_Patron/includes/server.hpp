/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:39 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/26 18:35:15 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#ifndef SERVER_HPP
#define SERVER_HPP

#include <map>
#include <string>

class Server
{
	public:
		Server();
		~Server();

		void run();

	private:
		int _epollFd;
		int _listenFd;

		enum ClientState
		{
			READING,
			WRITING
		};

		std::map<int, std::string> _clients;
		std::map<int, ClientState> _clientStates;

		std::map<int, std::string> _responses;
		std::map<int, std::string::size_type> _bytesSent; // contient le nombre d'octets deja envoyes

		Server(const Server& other);
		Server& operator=(const Server& rhs);

		void setupListeningSocket();
		void closeClient(int clientFD);

		std::string buildResponse() const;
		std::string buildError() const;


		void enableWriteEvent(int clientFd);
		void handleClientWrite(int clientfd);
};



#endif