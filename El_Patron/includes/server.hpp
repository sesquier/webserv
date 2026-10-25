/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:39 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/23 15:16:08 by ale-coss         ###   ########.fr       */
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

		std::map<int, std::string> _clients;
		Server(const Server& other);
		Server& operator=(const Server& rhs);

		void setupListeningSocket();
		void closeClient(int clientFD);
};



#endif