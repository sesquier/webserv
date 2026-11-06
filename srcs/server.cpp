/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/03 16:14:16 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "server.hpp"
#include "request.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cstdio>

static bool isValidUploadFilename(const std::string& filename)
{
    if (filename.empty() || filename == "." || filename == "..")
        return (false);
    
    for (std::string::size_type i = 0; i < filename.size(); i++)
    {
        char c = filename[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return (false);
    }
    return (true);
}

static int saveUploadedFile(const std::string& filePath, const std::string& body)
{
    int fileFd = open(
        filePath.c_str(),
        O_WRONLY | O_CREAT | O_EXCL,
        0644
    );

    if (fileFd == -1)
    {
        struct stat fileInfo;

        if (stat(filePath.c_str(), &fileInfo) == 0)
            return (409);

        return (500);
    }

    std::string::size_type offset = 0;
    bool writeError = false;

	while (offset < body.size())
    {
        ssize_t bytesWritten = write(
            fileFd,
            body.data() + offset,
            body.size() - offset
        );

        if (bytesWritten <= 0)
        {
            writeError = true;
            break;
        }

        offset += static_cast<std::string::size_type>(bytesWritten);
    }

    if (close(fileFd) == -1)
        writeError = true;

    if (writeError)
    {
        unlink(filePath.c_str());
        return (500);
    }

    return (201);
}

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

		// A signal interrupts epoll_wait (Ctrl-Z then fg): the server must keep running.
		if (count == -1)
			continue;

		for (int i = 0; i < count; i++)
		{
			int fd = events[i].data.fd;
			std::map<int, const ServerConfig*>::iterator listener = _listeners.find(fd);

			if (listener != _listeners.end())
			{
				// One accept per event: epoll reports the socket again if more clients wait.
				struct sockaddr_in clientAddr;
				socklen_t clientLen = sizeof(clientAddr);

				int clientFd = accept(fd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
		
				if (clientFd == -1)
					continue;
				if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1)
				{
					close(clientFd);
					continue;
				}

				struct epoll_event clientEvent;
				std::memset(&clientEvent, 0, sizeof(clientEvent));
				clientEvent.events = EPOLLIN | EPOLLRDHUP;
				clientEvent.data.fd = clientFd;

				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
				{
					close(clientFd);
					continue ;
				}

				_clients[clientFd] = "";
				_clientServers[clientFd] = listener->second;
				_clientStates[clientFd] = READING;
				_lastActivity[clientFd] = std::time(NULL);

				std::cout << "Client connected: fd = " << clientFd << std::endl;
			}
			else
			{
				if (events[i].events & EPOLLERR)
				{
					closeClient(fd);
					continue;
				}

				if (events[i].events & (EPOLLHUP | EPOLLRDHUP))
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
						_lastActivity[fd] = std::time(NULL);
					}
					else if (bytesReceived == 0)
					{
						closeClient(fd);
						continue ;
					}
					else
					{
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
							std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
       						closeClient(fd);
						}
						continue ;
					}
					if (parseStatus == Request::BODY_TOO_LARGE)
					{
						try
						{
							queueResponse(fd, buildResponse(413, "Content Too Large", "413 Content Too Large\n"));
						}
						catch (const std::exception& e)
						{
							std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
       						closeClient(fd);
						}
						continue ;
					}

					if (parseStatus == Request::HEADERS_TOO_LARGE)
					{
						try
						{
							queueResponse(fd, buildResponse(431, "Request Header Fields Too Large", "431 Request Header Fields Too Large\n"));
						}
						catch (const std::exception& e)
						{
							std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
       						closeClient(fd);
						}
						continue ;
					}
					
					std::cout << "Request parsed: "
              				<< request.getMethod() << " "
              				<< request.getTarget() << " "
              				<< request.getVersion()
              				<< " (server root: " << _clientServers[fd]->root << ")" << std::endl;

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
			}
		}

		// After the events: closing a client here cannot affect the events still to handle.
		std::time_t now = std::time(NULL);
		std::map<int, std::time_t>::iterator activityIt = _lastActivity.begin();
		while (activityIt != _lastActivity.end())
		{
			int clientFd = activityIt->first;
			std::time_t lastActivity = activityIt->second;

			if (now - lastActivity >= 30)
			{
				++activityIt;
				closeClient(clientFd);
			}
			else
			{
				++activityIt;
			}
		}
	}
}

// "/page?x=1" -> "/page": the query string is not part of the file path.
static std::string requestPath(const std::string& target)
{
	return (target.substr(0, target.find('?')));
}

static std::string contentTypeFor(const std::string& filePath)
{
	std::string::size_type dotPos = filePath.find_last_of('.');

	if (dotPos == std::string::npos || filePath.find('/', dotPos) != std::string::npos)
		return ("application/octet-stream");

	std::string extension = filePath.substr(dotPos);

	if (extension == ".html" || extension == ".htm")
		return ("text/html");
	if (extension == ".css")
		return ("text/css");
	if (extension == ".js")
		return ("application/javascript");
	if (extension == ".txt")
		return ("text/plain");
	if (extension == ".json")
		return ("application/json");
	if (extension == ".png")
		return ("image/png");
	if (extension == ".jpg" || extension == ".jpeg")
		return ("image/jpeg");
	if (extension == ".gif")
		return ("image/gif");
	if (extension == ".svg")
		return ("image/svg+xml");
	return ("application/octet-stream");
}

static std::string redirectReason(int code)
{
	if (code == 301)
		return ("Moved Permanently");
	if (code == 302)
		return ("Found");
	if (code == 303)
		return ("See Other");
	if (code == 307)
		return ("Temporary Redirect");
	return ("Permanent Redirect");
}

// Value of the Allow header sent with a 405: "GET, POST".
static std::string allowedMethods(const LocationConfig& location)
{
	std::string list;

	for (size_t i = 0; i < location.methods.size(); i++)
	{
		if (i > 0)
			list += ", ";
		list += location.methods[i];
	}
	return (list);
}

// Reads a whole regular file. Returns false if it cannot be read.
static bool readWholeFile(const std::string& filePath, std::string& content)
{
	int fd = open(filePath.c_str(), O_RDONLY);

	if (fd == -1)
		return (false);

	char	buffer[4096];
	ssize_t	bytesRead;

	while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
		content.append(buffer, bytesRead);
	close(fd);
	return (bytesRead == 0);
}

// Applies the rules shared by every method, then chooses the handler.
// The order matters: unknown method, redirection, then allowed methods.
std::string Server::handleRequest(const Request& request, const ServerConfig& server) const
{
	const std::string&	method = request.getMethod();
	std::string			path = requestPath(request.getTarget());

	if (method != "GET" && method != "POST" && method != "DELETE")
		return (buildResponse(501, "Not Implemented", "501 Not Implemented\n"));
	if (path.empty() || path[0] != '/')
		return (buildResponse(400, "Bad Request", "400 Bad Request\n"));

	const LocationConfig& location = server.findLocation(path);

	if (location.redirectCode != 0)
		return (buildResponse(location.redirectCode, redirectReason(location.redirectCode), "",
			"text/plain", "Location: " + location.redirectUrl + "\r\n"));
	if (!location.isMethodAllowed(method))
		return (buildResponse(405, "Method Not Allowed", "405 Method Not Allowed\n",
			"text/plain", "Allow: " + allowedMethods(location) + "\r\n"));
	if (method == "GET")
		return (handleGet(path, location));
	if (method == "POST")
		return (handlePost(request, path, location));
	return (handleDelete(path, location));
}

// A directory is served through its index file. Listing (autoindex) comes later.
std::string Server::handleGet(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);

	if (filePath.empty())
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));

	struct stat info;

	if (stat(filePath.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
	{
		if (filePath[filePath.size() - 1] != '/')
			filePath += "/";
		filePath += location.index;
	}
	return (serveFile(filePath));
}

// stat before open: opening a FIFO would block the whole server.
std::string Server::serveFile(const std::string& filePath) const
{
	struct stat	info;
	std::string	content;

	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildResponse(404, "Not Found", "404 Not Found\n"));
	if (access(filePath.c_str(), R_OK) == -1)
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));
	if (!readWholeFile(filePath, content))
		return (buildResponse(500, "Internal Server Error", "500 Internal Server Error\n"));
	return (buildResponse(200, "OK", content, contentTypeFor(filePath)));
}

// "POST /upload/photo.png" stores the body as <upload_store>/photo.png.
std::string Server::handlePost(const Request& request, const std::string& path,
	const LocationConfig& location) const
{
	if (location.uploadStore.empty())
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));
	if (!request.hasContentLength())
		return (buildResponse(411, "Length Required", "411 Length Required\n"));

	std::string filename = path.substr(location.path.size());

	if (!filename.empty() && filename[0] == '/')
		filename.erase(0, 1);
	if (!isValidUploadFilename(filename))
		return (buildResponse(400, "Bad Request", "400 Bad Request\n"));

	int status = saveUploadedFile(location.uploadStore + "/" + filename, request.getBody());

	if (status == 201)
		return (buildResponse(201, "Created", "201 Created\n"));
	if (status == 409)
		return (buildResponse(409, "Conflict", "409 Conflict\n"));
	return (buildResponse(500, "Internal Server Error", "500 Internal Server Error\n"));
}

std::string Server::handleDelete(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);
	struct stat info;

	if (filePath.empty())
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));
	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildResponse(404, "Not Found", "404 Not Found\n"));
	if (unlink(filePath.c_str()) == -1)
		return (buildResponse(500, "Internal Server Error", "500 Internal Server Error\n"));
	return (buildResponse(204, "No Content", ""));
}

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
}

std::string Server::buildResponse(int statusCode, const std::string& reason, const std::string& body,
	const std::string& contentType, const std::string& extraHeaders) const
{
	std::ostringstream lengthStream;
	lengthStream << body.size();

	std::ostringstream statusStream;
	statusStream << statusCode;
	
	std::string response;
	
	response += "HTTP/1.1 ";
	response += statusStream.str();
	response += " ";
	response += reason;
	response += "\r\n";

	response += "Content-Type: ";
	response += contentType;
	response += "\r\n";

	response += "Content-Length: ";
	response += lengthStream.str();
	response += "\r\n";
	
	response += extraHeaders;
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
		_lastActivity[clientFd] = std::time(NULL);

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
