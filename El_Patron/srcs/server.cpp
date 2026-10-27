/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/27 16:05:16 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "server.hpp"
#include "request.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cerrno>

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
	int flags = fcntl(_listenFd, F_GETFL, 0);
	if (flags == -1 || fcntl(_listenFd, F_SETFL, O_NONBLOCK) == -1)
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
				int flags = fcntl(clientFd, F_GETFL, 0);
				if (flags == -1 || fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1)
				{
					close(clientFd);
					continue;
				}

				struct epoll_event clientEvent;
				std::memset(&clientEvent, 0, sizeof(clientEvent));
				clientEvent.events = EPOLLIN;
				clientEvent.data.fd = clientFd;

				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
				{
					close(clientFd);
					continue ;
				}

				_clients[clientFd] = "";
				_clientStates[clientFd] = READING;

				std::cout << "Client connected: fd = " << clientFd << std::endl;
			}
			else
			{
				if (events[i].events & EPOLLERR)
				{
					closeClient(fd);
					continue;
				}

				if (events[i].events & EPOLLHUP)
				{
					if (!(events[i].events & EPOLLIN))
					{
						closeClient(fd);					
						continue;
					}
				}

				std::map<int, ClientState>::iterator stateIt = _clientStates.find(fd);

				if (stateIt == _clientStates.end())
				{
					closeClient(fd);
					continue ;
				}

				if (stateIt->second == WRITING && (events[i].events & EPOLLOUT))
				{
					handleClientWrite(fd);
					continue ;
				}
				if (stateIt->second == READING && (events[i].events & EPOLLIN))
				{
					char buffer[4096];

					ssize_t bytesReceived = recv(fd, buffer, sizeof(buffer), 0);
					// std::cout << "DEBUG: recv returned " << bytesReceived << std::endl;

					if (bytesReceived > 0)
					{
						_clients[fd].append(buffer, bytesReceived);
					}
					else if (bytesReceived == 0)
					{
						closeClient(fd);
						continue ;
					}
					else
					{
						// if (errno == EAGAIN || errno == EWOULDBLOCK)
						// 	continue;
						// if (errno == EINTR)
						// 	continue ;
						closeClient(fd);
						continue ;
					}


					// //test temporaire
					// std::cout << "DEBUG: Bytes received: ";
					// for (ssize_t i = 0; i < bytesReceived; ++i)
					// {
					// 	std::cout << static_cast<int>(static_cast<unsigned char>(buffer[i])) << " ";
					// }
					// std::cout << std::endl;
					// // std::cout.write(buffer, bytesReceived) << std::endl;
					
					//fin du test temporaire

					Request request;
					Request::ParseStatus parseStatus;

					parseStatus = request.parse(_clients[fd]);

					if (parseStatus == Request::INCOMPLETE)
						continue ;
					if (parseStatus == Request::INVALID)
					{
						try
						{
							queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
						}
						catch (const std::exception& e)
						{
							std::cerr << "Failed to prepare response: " << e.what() << std::endl;
       						closeClient(fd);
						}
						continue ;
					}
					
					std::cout << "Request parsed: "
              				<< request.getMethod() << " "
              				<< request.getTarget() << " "
              				<< request.getVersion() << std::endl;

					std::string response;
					if (request.getMethod() == "GET")
					{
						response = buildResponse(200, "OK", "Hello World!\n");
					}
					else
					{
						response = buildResponse(501, "Not Implemented", "501 Not Implemented\n");
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

					// bool requestComplete = false;

					// try
					// {
    				// 	requestComplete = request.parse(_clients[fd]);
					// 	// std::cout << (requestComplete == true ? "true" : "false" )<< "\n";
					// }
					// catch (const std::exception& e)
					// {
					// 	std::cerr << "Invalid request: " << e.what() << std::endl;
    				// 	try
					// 	{
					// 		queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
					// 	}
					// 	catch (const std::exception& e)
					// 	{
					// 		std::cerr << "Failed to prepare response: "
	                //   				<< e.what() << std::endl;
       				// 		closeClient(fd);
					// 	}
    				// 	continue;
					// }

					// if (requestComplete)
					// {
    					// std::cout << "Request parsed: "
              			// 	<< request.getMethod() << " "
              			// 	<< request.getTarget() << " "
              			// 	<< request.getVersion() << std::endl;

						// std::string response;
						// if (request.getMethod() == "GET")
						// {
						// 	response = buildResponse(200, "OK", "Hello World!\n");
						// }
						// else
						// {
						// 	response = buildResponse(501, "Not Implemented", "501 Not Implemented\n");
						// }

						// try
						// {
						// 	queueResponse(fd, response);
						// }
						// catch (const std::exception& e)
						// {
						// 	std::cerr << "Failed to prepare response: " << e.what() << std::endl;
						// 	closeClient(fd);
						// }
					// }	
    					// _responses[fd] = buildResponse();
    					// _bytesSent[fd] = 0;
						// _clientStates[fd] = WRITING;

   						// try
   						// {
   						//     enableWriteEvent(fd);
   						// }
   						// catch (const std::exception& e)
   						// {
       					// 	std::cerr << "Failed to prepare response: "
	                  	// 			<< e.what() << std::endl;
       					// 	closeClient(fd);
    					// 	}
					// }
					// else{
					// 	std::cout << "Invalid request\n";
    				// 	// _responses[fd] = buildError();
					// 	_bytesSent[fd] = 0;
   					// 	try
   					// 	{
   					// 	    enableWriteEvent(fd);
   					// 	}
   					// 	catch (const std::exception& e)
   					// 	{
       				// 		std::cerr << "Failed to prepare response: "
	                //   				<< e.what() << std::endl;
       				// 		closeClient(fd);
    				// 		}
					// }					

					// try
					// {
					// 	if (request.parse(_clients[fd]))
					// 	{
					// 		std::cout << "HTTP request parsed:" << std::endl;
					// 		std::cout << "Method: " << request.getMethod() << std::endl;
					// 		std::cout << "Target: " << request.getTarget() << std::endl;
					// 		std::cout << "Version: " << request.getVersion() << std::endl;

					// 		closeClient(fd);
					// 	}
					// }
					// catch (const std::exception& e)
					// {
					// 	std::cerr << "Invalid request: " << e.what() << std::endl;
					// 	closeClient(fd);
					// }


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
	_responses.erase(clientFd);
	_bytesSent.erase(clientFd);
	_clientStates.erase(clientFd);
}

std::string Server::buildResponse(int statusCode, const std::string& reason, const std::string& body) const
{
	std::ostringstream lengthStream;
	lengthStream << body.size();

	std::string response;
	response += "HTTP/1.1 ";

	std::ostringstream statusStream;
	statusStream << statusCode;
	response += statusStream.str();
	// response += statusCode == 200 ? "200" :
	// 			statusCode == 400 ? "400" :
	// 			statusCode == 501 ? "501" :
	response += " ";
	response += reason;
	response += "\r\n";

	response += "Content-Type: text/plain\r\n";
	response += "Content-Length: ";
	response += lengthStream.str();
	response += "\r\n";
	response += "Connection: close\r\n";
	response += "\r\n";
	response += body;

	return (response);
}

// std::string Server::buildError() const
// {
// 	const std::string body = "Error";
// 	std::ostringstream lengthStream;
// 	lengthStream << body.size();

// 	std::string response;
// 	response += "HTTP/1.1 404 Not found\r\n";
// 	response += "Content-Type: text/plain\r\n";
// 	response += "Content-Length: ";
// 	response += lengthStream.str();
// 	response += "\r\n";
// 	response += "Connection: close \r\n";
// 	response += "\r\n";
// 	response += body;

// 	return (response);
// }

void Server::enableWriteEvent(int clientFd)
{
	struct epoll_event event;
	std::memset(&event, 0, sizeof(event));

	event.events = EPOLLOUT;
	event.data.fd = clientFd;

	if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &event) == -1)
		throw std::runtime_error("Failed to enable EPOLLOUT");
}

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

    ssize_t bytesSent = send(clientFd, response.c_str() + offset, remaining, MSG_NOSIGNAL);

    if (bytesSent > 0)
    {
        sentIt->second += static_cast<std::string::size_type>(bytesSent);

        if (sentIt->second == response.size())
            closeClient(clientFd);
    }
	// else if (bytesSent == -1)
	// {
	// 	// if (errno == EAGAIN || errno == EWOULDBLOCK)
	// 	// 	return ;
	// 	// if (errno == EINTR)
	// 	// 	return ;
	// 	closeClient(clientFd);
	// }
	else
	{
		closeClient(clientFd);
	}

}

void Server::queueResponse(int clientFd, const std::string& response)
{
	_responses[clientFd] = response;
	_bytesSent[clientFd] = 0;
	_clientStates[clientFd] = WRITING;

	enableWriteEvent(clientFd);
}
