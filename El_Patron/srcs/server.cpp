/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/25 16:29:51 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "server.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

Server::Server() : _epollFd(-1), _listenFd(-1)
{
	_epollFd = epoll_create(1);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create failed");
}

Server::~Server()
{
	if (_listenFd != -1)
		close(_listenFd);
	if (_epollFd != -1)
		close(_epollFd);
}

void Server::setupListeningSocket()
{
	_listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_listenFd == -1)
		throw std::runtime_error("socket failed");
	
	int opt = 1;
	if (setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
		throw std::runtime_error("setsockopt failed");
	if (fcntl(_listenFd, F_SETFL, O_NONBLOCK) == -1)
		throw std::runtime_error("fcntl failed");

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(8080);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1)
		throw std::runtime_error("bind failed");
	if (listen(_listenFd, 128) == -1)
		throw std::runtime_error("listen failed");
}

void Server::run()
{
	setupListeningSocket();

	struct epoll_event event;
	std::memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = _listenFd;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _listenFd, &event) == -1)
		throw std::runtime_error("epoll_ctl failed");

	std::cout << "Listening on 127.0.0.1:8080" << std::endl;

	struct epoll_event events[10];

	while (true)
	{
		int count = epoll_wait(_epollFd, events, 10, -1);

		if (count == -1)
			throw std::runtime_error("epoll_wait failed");
		
		for (int i = 0; i < count; i++)
		{
			int fd = events[i].data.fd;

			if (fd == _listenFd)
			{
				struct sockaddr_in clientAddr;
				socklen_t clientLen = sizeof(clientAddr);

				int clientFd = accept(_listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
			
				if (clientFd == -1)
					continue;

				if (fcntl(clientFd, F_SETFL, O_NONBLOCK == -1))
				{
					close(clientFd);
					continue;
				}

				struct epoll_event clientEvent;
				std::memset(&clientEvent, 0, sizeof(clientEvent));
				clientEvent.events = EPOLLIN;
				clientEvent.data.fd = clientFd;

				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD,clientFd, &clientEvent) == -1)
				{
					close(clientFd);
					continue ;
				}

				_clients[clientFd] = "";

				std::cout << "Client connected: fd = " << clientFd << std::endl;
			}
			else
			{
				if (events[i].events & (EPOLLERR | EPOLLHUP))
				{
					closeClient(fd);
					continue;
				}
				if (events[i].events & EPOLLIN)
				{
					char buffer[4096];

					ssize_t bytesReceived = recv(fd, buffer, sizeof(buffer), 0);

					if (bytesReceived <= 0)
					{
						closeClient(fd);
						continue;
					}

					_clients[fd].append(buffer, bytesReceived);

					std::cout << "Received from client " << fd << ": " << std::endl;

					std::cout.write(buffer, bytesReceived) << std::endl;
				}
			}
		}
	}
}

void Server::closeClient(int clientFd)
{
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
	close(clientFd);
	_clients.erase(clientFd);
}