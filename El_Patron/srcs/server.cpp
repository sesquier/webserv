/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/09 18:59:16 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "server.hpp"
#include "request.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>

// Returns the listening socket, or -1 if any step fails.
static int openListeningSocket(const struct addrinfo* info)
{
	int fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

	if (fd == -1)
		return (-1);

	int opt = 1;

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

static std::string addressToString(const ListenAddress& address)
{
	std::ostringstream text;

	text << address.host << ":" << address.port;
	return (text.str());
}

Server::Server(const std::vector<ServerConfig>& configs) : _epollFd(-1), _configs(configs)
{
	_epollFd = epoll_create(1);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create failed");
}

Server::~Server()
{
	for (std::map<int, const ServerConfig*>::iterator it = _listeners.begin(); it != _listeners.end(); ++it)
		close(it->first);
	if (_epollFd != -1)
		close(_epollFd);
}

// One listening socket per 'listen' directive of every server.
void Server::setupListeningSockets()
{
	for (size_t i = 0; i < _configs.size(); i++)
	{
		for (size_t j = 0; j < _configs[i].listens.size(); j++)
			addListener(_configs[i].listens[j], _configs[i]);
	}
}

void Server::addListener(const ListenAddress& address, const ServerConfig& config)
{
	struct addrinfo hints;
	struct addrinfo* info = NULL;
	std::ostringstream port;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;
	port << address.port;

	if (getaddrinfo(address.host.c_str(), port.str().c_str(), &hints, &info) != 0)
		throw std::runtime_error("invalid address " + addressToString(address));

	int fd = openListeningSocket(info);

	freeaddrinfo(info);
	if (fd == -1)
		throw std::runtime_error("cannot listen on " + addressToString(address));

	// Recorded first: the destructor then closes it even if epoll_ctl fails.
	_listeners[fd] = &config;

	struct epoll_event event;

	std::memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &event) == -1)
		throw std::runtime_error("epoll_ctl failed");

	std::cout << "Listening on " << addressToString(address) << std::endl;
}

void Server::run()
{
    setupListeningSockets();

    struct epoll_event events[10];

    while (true)
    {
        int count = epoll_wait(_epollFd, events, 10, 1000);

        if (count == -1)
        {
            int pollError = errno;

            if (pollError == EINTR)
                continue;

            throw std::runtime_error(
                std::string("epoll_wait failed: ") + std::strerror(pollError)
            );
        }

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
        std::map<int, std::time_t>::iterator activityIt = _lastActivity.begin();

        while (activityIt != _lastActivity.end())
        {
            int clientFd = activityIt->first;
            std::time_t lastActivity = activityIt->second;

            ++activityIt;

            if (now - lastActivity >= 30)
            {
                std::map<int, bool>::iterator requestIt = _requestStarted.find(clientFd);
                std::map<int, ClientState>::iterator stateIt = _clientStates.find(clientFd);

                if (requestIt != _requestStarted.end()
                    && requestIt->second
                    && stateIt != _clientStates.end()
                    && stateIt->second == READING)
                {
                    try
                    {
                        queueResponse(
                            clientFd,
                            buildResponse(408, "Request Timeout", "408 Request Timeout\n")
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

        // std::time_t now = std::time(NULL);
        // std::map<int, std::time_t>::iterator activityIt = _lastActivity.begin();

        // while (activityIt != _lastActivity.end())
        // {
        //     int clientFd = activityIt->first;
        //     std::time_t lastActivity = activityIt->second;

        //     ++activityIt;

        //     if (now - lastActivity >= 30)
		// 	{
   		// 		std::map<int, bool>::iterator requestIt = _requestStarted.find(clientFd);

    	// 		if (requestIt != _requestStarted.end() && requestIt->second && _clientStates[clientFd] == READING)
    	// 		{
        // 			queueResponse(clientFd, buildResponse(408, "Request Timeout", "408 Request Timeout\n"));
    	// 		}
    	// 		else
        // 			closeClient(clientFd);
		// 	}

        // }
    }
}

void Server::handleEvent(const struct epoll_event& event)
{
	int fd = event.data.fd;
	int acceptedClientFd = -1;

	std::map<int, const ServerConfig*>::iterator listener = _listeners.find(fd);

	try
	{
		if (listener != _listeners.end())
		{
			struct sockaddr_in clientAddr;
			socklen_t clientLen = sizeof(clientAddr);

			int clientFd = accept(fd, reinterpret_cast<struct sockaddr*>(&clientAddr),
				&clientLen);

			if (clientFd == -1)
				return ;

			acceptedClientFd = clientFd;

			if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1)
			{
				close(clientFd);
				acceptedClientFd = -1;
				return ;
			}

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

			_clients[clientFd] = "";
			_clientServers[clientFd] = listener->second;
			_clientStates[clientFd] = READING;
			_requestStarted[clientFd] = false;
			_lastActivity[clientFd] = std::time(NULL);

			acceptedClientFd = -1;

			std::cout << "Client connected: fd = " << clientFd << std::endl;

			return;
		}

		if (event.events & EPOLLERR)
        {
            closeClient(fd);
            return ;
        }

        std::map<int, ClientState>::iterator stateIt = _clientStates.find(fd);

        if (stateIt == _clientStates.end())
        {
            closeClient(fd);
            return ;
        }

        if (event.events & EPOLLHUP)
        {
            closeClient(fd);
            return ;
        }

        if (stateIt->second == WRITING)
        {
            if (event.events & EPOLLOUT)
                handleClientWrite(fd);
            return ;
        }

        if (stateIt->second != READING)
            return ;

        if ((event.events & EPOLLRDHUP) && !(event.events & EPOLLIN))
        {
            closeClient(fd);
            return ;
        }

        if (!(event.events & EPOLLIN))
            return ;
			
		// if (event.events & EPOLLERR)
		// {
		// 	closeClient(fd);
		// 	return ;
		// }

		// if (event.events & (EPOLLHUP | EPOLLRDHUP))
		// {
		// 	if (!(event.events & EPOLLIN))
		// 	{
		// 		closeClient(fd);
		// 		return ;
		// 	}
		// }

		// std::map<int, ClientState>::iterator stateIt = _clientStates.find(fd);

		// if (stateIt == _clientStates.end())
		// {
		// 	closeClient(fd);
		// 	return ;
		// }

		// if (stateIt->second == WRITING && (event.events & EPOLLOUT))
		// {
		// 	handleClientWrite(fd);
		// 	return ;
		// }

		// if (stateIt->second != READING || !(event.events & EPOLLIN))
		// 	return ;

		char buffer[4096];

		ssize_t bytesReceived = recv(fd, buffer, sizeof(buffer), 0);

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

		Request request;
		Request::ParseStatus parseStatus;

		parseStatus = request.parseHeaders(_clients[fd]);

		if (parseStatus == Request::INCOMPLETE)
			return ;

		if (parseStatus == Request::INVALID)
		{
			queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
			return ;
		}

		if (parseStatus == Request::HEADERS_TOO_LARGE)
		{
			queueResponse(fd, buildResponse(431, "Request Header Fields Too Large", "431 Request Header Fields Too Large\n"));
			return ;
		}

		std::string path = request.getTarget();
		std::string::size_type queryPos = path.find('?');

		if (queryPos != std::string::npos)
			path.erase(queryPos);

		std::string decodedPath;

		if (!decodePath(path, decodedPath))
		{
			queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
			return ;
		}

		path = decodedPath;

		const LocationConfig& location = _clientServers[fd]->findLocation(path);

		parseStatus = request.parseBody(_clients[fd], location.maxBodySize);

		if (parseStatus == Request::INCOMPLETE)
			return ;

		if (parseStatus == Request::INVALID)
		{
			queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
			return ;
		}

		if (parseStatus == Request::BODY_TOO_LARGE)
		{
			queueResponse(fd, buildResponse(413, "Content Too Large", "413 Content Too Large\n"));
			return ;
		}

		std::cout << "Request parsed: " << request.getMethod() << " " << request.getTarget() << " "
			<< request.getVersion() << " (server root: " << _clientServers[fd]->root << ")"
			<< std::endl;

		std::string response = handleRequest(request, *_clientServers[fd]);

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
	catch (...)
	{
		if (acceptedClientFd != -1)
			closeClient(acceptedClientFd);
		else if (listener == _listeners.end())
			closeClient(fd);

		throw ;
	}
}

// void Server::run()
// {
// 	setupListeningSockets();

// 	struct epoll_event events[10];

// 	while (true)
// 	{
// 		int count = epoll_wait(_epollFd, events, 10, 1000);

// 		// A signal interrupts epoll_wait (Ctrl-Z then fg): the server must keep running.
// 		if (count == -1)
// 			continue;

// 		for (int i = 0; i < count; i++)
// 		{
// 			int fd = events[i].data.fd;
// 			std::map<int, const ServerConfig*>::iterator listener = _listeners.find(fd);

// 			if (listener != _listeners.end())
// 			{
// 				// One accept per event: epoll reports the socket again if more clients wait.
// 				struct sockaddr_in clientAddr;
// 				socklen_t clientLen = sizeof(clientAddr);

// 				int clientFd = accept(fd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
		
// 				if (clientFd == -1)
// 					continue;
// 				if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1)
// 				{
// 					close(clientFd);
// 					continue;
// 				}

// 				struct epoll_event clientEvent;
// 				std::memset(&clientEvent, 0, sizeof(clientEvent));
// 				clientEvent.events = EPOLLIN | EPOLLRDHUP;
// 				clientEvent.data.fd = clientFd;

// 				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
// 				{
// 					close(clientFd);
// 					continue ;
// 				}

// 				_clients[clientFd] = "";
// 				_clientServers[clientFd] = listener->second;
// 				_clientStates[clientFd] = READING;
// 				_lastActivity[clientFd] = std::time(NULL);

// 				std::cout << "Client connected: fd = " << clientFd << std::endl;
// 			}
// 			else
// 			{
// 				if (events[i].events & EPOLLERR)
// 				{
// 					closeClient(fd);
// 					continue;
// 				}

// 				if (events[i].events & (EPOLLHUP | EPOLLRDHUP))
// 				{
// 					if (!(events[i].events & EPOLLIN))
// 					{
// 						closeClient(fd);					
// 						continue;
// 					}
// 				}

// 				std::map<int, ClientState>::iterator stateIt = _clientStates.find(fd);

// 				if (stateIt == _clientStates.end())
// 				{
// 					closeClient(fd);
// 					continue ;
// 				}

// 				if (stateIt->second == WRITING && (events[i].events & EPOLLOUT))
// 				{
// 					handleClientWrite(fd);
// 					continue ;
// 				}
// 				if (stateIt->second == READING && (events[i].events & EPOLLIN))
// 				{
// 					char buffer[4096];

// 					ssize_t bytesReceived = recv(fd, buffer, sizeof(buffer), 0);
// 					// std::cout << "DEBUG: recv returned " << bytesReceived << std::endl;

// 					if (bytesReceived > 0)
// 					{
// 						_clients[fd].append(buffer, bytesReceived);
// 						_lastActivity[fd] = std::time(NULL);
// 					}
// 					else if (bytesReceived == 0)
// 					{
// 						closeClient(fd);
// 						continue ;
// 					}
// 					else
// 					{
// 						closeClient(fd);
// 						continue ;
// 					}

// 					Request request;
// 					Request::ParseStatus parseStatus;

// 					parseStatus = request.parseHeaders(_clients[fd]);

// 					if (parseStatus == Request::INCOMPLETE)
// 						continue;

// 					if (parseStatus == Request::INVALID)
// 					{
// 						queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
// 							continue;
// 					}

// 					if (parseStatus == Request::HEADERS_TOO_LARGE)
// 					{
// 						queueResponse(fd, buildResponse(431, "Request Header Fields Too Large", "431 Request Header Fields Too Large\n"));
// 							continue;
// 					}

// 					std::string path = request.getTarget();
// 					std::string::size_type queryPos = path.find('?');

// 					if (queryPos != std::string::npos)
// 						path.erase(queryPos);

// 					std::string decodedPath;
					
// 					if (!decodePath(path, decodedPath))
// 					{
// 						queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
// 						continue ;
// 					}
// 					path = decodedPath;	
// 					const LocationConfig& location = _clientServers[fd]->findLocation(path);

// 					parseStatus = request.parseBody(
// 					_clients[fd], location.maxBodySize);

// 					if (parseStatus == Request::INCOMPLETE)
// 						continue;

// 					if (parseStatus == Request::INVALID)
// 					{
// 						queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
// 							continue;
// 					}

// 					if (parseStatus == Request::BODY_TOO_LARGE)
// 					{
// 						queueResponse(fd, buildResponse(413, "Content Too Large", "413 Content Too Large\n"));
// 							continue;
// 					}

// 					std::cout << "Request parsed: " << request.getMethod() << " " << request.getTarget() << " "
// 					<< request.getVersion() << " (server root: " << _clientServers[fd]->root << ")" << std::endl;

// 					std::string response = handleRequest(request, *_clientServers[fd]);

// 					// Request request;
// 					// Request::ParseStatus parseStatus;

// 					// parseStatus = request.parse(_clients[fd]);

// 					// if (parseStatus == Request::INCOMPLETE)
// 					// 	continue ;
// 					// if (parseStatus == Request::INVALID)
// 					// {
// 					// 	try
// 					// 	{
// 					// 		queueResponse(fd, buildResponse(400, "Bad Request", "400 Bad Request\n"));
// 					// 	}
// 					// 	catch (const std::exception& e)
// 					// 	{
// 					// 		std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
//        				// 		closeClient(fd);
// 					// 	}
// 					// 	continue ;
// 					// }
// 					// if (parseStatus == Request::BODY_TOO_LARGE)
// 					// {
// 					// 	try
// 					// 	{
// 					// 		queueResponse(fd, buildResponse(413, "Content Too Large", "413 Content Too Large\n"));
// 					// 	}
// 					// 	catch (const std::exception& e)
// 					// 	{
// 					// 		std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
//        				// 		closeClient(fd);
// 					// 	}
// 					// 	continue ;
// 					// }

// 					// if (parseStatus == Request::HEADERS_TOO_LARGE)
// 					// {
// 					// 	try
// 					// 	{
// 					// 		queueResponse(fd, buildResponse(431, "Request Header Fields Too Large", "431 Request Header Fields Too Large\n"));
// 					// 	}
// 					// 	catch (const std::exception& e)
// 					// 	{
// 					// 		std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
//        				// 		closeClient(fd);
// 					// 	}
// 					// 	continue ;
// 					// }
					
// 					// std::cout << "Request parsed: "
//               		// 		<< request.getMethod() << " "
//               		// 		<< request.getTarget() << " "
//               		// 		<< request.getVersion()
//               		// 		<< " (server root: " << _clientServers[fd]->root << ")" << std::endl;

// 					// std::string response = handleRequest(request, *_clientServers[fd]);

// 					try
// 					{
// 						queueResponse(fd, response);
// 					}
// 					catch (const std::exception& e)
// 					{
// 						std::cerr << "Failed to prepare response: " << e.what() << std::endl;
// 						closeClient(fd);
// 					}		
// 				}
// 			}
// 		}

// 		// After the events: closing a client here cannot affect the events still to handle.
// 		std::time_t now = std::time(NULL);
// 		std::map<int, std::time_t>::iterator activityIt = _lastActivity.begin();
// 		while (activityIt != _lastActivity.end())
// 		{
// 			int clientFd = activityIt->first;
// 			std::time_t lastActivity = activityIt->second;

// 			if (now - lastActivity >= 30)
// 			{
// 				++activityIt;
// 				closeClient(clientFd);
// 			}
// 			else
// 			{
// 				++activityIt;
// 			}
// 		}
// 	}
// }

void Server::closeClient(int clientFd)
{
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
		// _lastActivity[clientFd] = std::time(NULL);

        if (sentIt->second == response.size())
            closeClient(clientFd);
    }
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
	_lastActivity[clientFd] = std::time(NULL);

	enableWriteEvent(clientFd);
}
