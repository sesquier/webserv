/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:51:26 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/02 19:28:21 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "server.hpp"
#include "request.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <cerrno>
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
	if (flags == -1 || fcntl(_listenFd, F_SETFL, flags | O_NONBLOCK) == -1)
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
		int count = epoll_wait(_epollFd, events, 10, 1000);

		if (count == -1)
			throw std::runtime_error("epoll_wait failed");
		
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
				clientEvent.events = EPOLLIN | EPOLLRDHUP;
				clientEvent.data.fd = clientFd;

				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent) == -1)
				{
					close(clientFd);
					continue ;
				}

				_clients[clientFd] = "";
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
							std::cerr << "Failed to prepare error response: " << e.what() << std::endl;
       						closeClient(fd);
						}
						continue ;
					}
					if (parseStatus == Request::BODY_TOO_LARGE)
					{
						std::cerr << "DEBUG parseStatus = " << parseStatus << std::endl;
						std::cerr << "DEBUG request = [" << _clients[fd] << "]" << std::endl;
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
              				<< request.getVersion() << std::endl;

					std::string response;

					if (request.getMethod() == "GET")
					{	
						std::string target = request.getTarget();

						// Retirer la query string, par exemple ?page=2.
						std::string::size_type queryPos = target.find('?');

						if (queryPos != std::string::npos)
						target.erase(queryPos);

						// Le chemin d'une cible HTTP doit commencer par '/'.
						if (target.empty() || target[0] != '/')
						{
							response = buildResponse(400, "Bad Request", "400 Bad Request\n");
						}
						else
						{
							// Vérifier les segments du chemin pour empêcher
							// les tentatives de remontée dans les répertoires.
							bool forbidden = false;
							std::string::size_type pos = 0;

							while (pos < target.size())
							{
								std::string::size_type end = target.find('/', pos);

								if (end == std::string::npos)
									end = target.size();

								std::string segment = target.substr(pos, end - pos);

								if (segment == "..")
								{
									forbidden = true;
									break;
								}

								if (end == target.size())
									break;

								pos = end + 1;
							}

							if (forbidden)
							{
								response = buildResponse(403, "Forbidden", "403 Forbidden\n");
							}
							else
							{
								// Pour '/', servir la page d'accueil.
								if (target == "/")
									target = "/index.html";

								// Le répertoire public est temporairement fixé à ./www.
								std::string filePath = "./www" + target;

								int fileFd = open(filePath.c_str(), O_RDONLY);

								if (fileFd == -1)
								{
									response = buildResponse(404, "Not Found", "404 Not Found\n");
								}
								else
								{
									struct stat fileInfo;

									if (fstat(fileFd, &fileInfo) == -1 || !S_ISREG(fileInfo.st_mode))
									{
										close(fileFd);

										response = buildResponse(404, "Not Found", "404 Not Found\n");
									}
									else
									{
										std::string body;
										char buffer[4096];
										bool readError = false;

										while (true)
										{
											ssize_t bytesRead = read(fileFd, buffer, sizeof(buffer));

											if (bytesRead > 0)
											{
												body.append(buffer, bytesRead);
											}
											else if (bytesRead == 0)
											{
												// Fin du fichier.
												break;
											}
											else
											{
												// Pas de consultation d'errno après read().
												readError = true;
												break;
											}
										}

										close(fileFd);

										if (readError)
										{
											response = buildResponse(500, "Internal Server Error", "500 Internal Server Error\n");
										}
										else
										{
											// Déterminer le type de contenu grâce à l'extension.
											std::string contentType = "application/octet-stream";

											std::string::size_type dotPos =
											target.find_last_of('.');

											if (dotPos != std::string::npos)
											{
												std::string extension = target.substr(dotPos);

												if (extension == ".html" || extension == ".htm")
													contentType = "text/html";
												else if (extension == ".css")
													contentType = "text/css";
												else if (extension == ".js")
													contentType = "application/javascript";
												else if (extension == ".txt")
													contentType = "text/plain";
												else if (extension == ".json")
													contentType = "application/json";
												else if (extension == ".png")
													contentType = "image/png";
												else if (extension == ".jpg" || extension == ".jpeg")
													contentType = "image/jpeg";
												else if (extension == ".gif")
													contentType = "image/gif";
												else if (extension == ".svg")
													contentType = "image/svg+xml";
											}

											response = buildResponse(200, "OK", body, contentType);
										}
									}
								}
							}
						}
					}
					else if (request.getMethod() == "POST")
					{
    					std::string target = request.getTarget();

    					std::string::size_type queryPos = target.find('?');

    					if (queryPos != std::string::npos)
        					target.erase(queryPos);

    					const std::string prefix = "/upload/";

    					if (target.compare(0, prefix.size(), prefix) != 0)
    					{
        					response = buildResponse(404, "Not Found", "404 Not Found\n");
						}
    					else if (!request.hasContentLength())
    					{
        					response = buildResponse(411, "Length Required", "411 Length Required\n");
    					}
						else
    					{
        					std::string filename = target.substr(prefix.size());

        					if (!isValidUploadFilename(filename))
        					{
            					response = buildResponse(400, "Bad Request", "400 Bad Request\n");
        					}
        					else
        					{
            					std::string filePath = "./www/uploads/" + filename;

            					int status = saveUploadedFile(filePath, request.getBody());
		
								if (status == 201)
            					{
                					response = buildResponse(201, "Created", "201 Created\n");
            					}
            					else if (status == 409)
            					{
                					response = buildResponse(409, "Conflict", "409 Conflict\n");
            					}
            					else
            					{
                					response = buildResponse(500, "Internal Server Error", "500 Internal Server Error\n");
								}	
            				}
        				}
    				}


					// else if (request.getMethod() == "POST")
					// {
					// 	std::string target = request.getTarget();

					// 	//retirer query string
					// 	std::string::size_type queryPos = target.find('?');

					// 	if (queryPos != std::string::npos)
					// 		target.erase(queryPos);
						
					// 	//cette premiere version accepte iniquement /upload/nom_fichier.
					// 	const std::string prefix = "/upload/";

					// 	if (target.compare(0, prefix.size(), prefix) != 0)
					// 		response = buildResponse(404, "Not Found", "404 Not Found\n");
					// 	else if (!request.hasContentLength())
					// 		response = buildResponse(411, "Length Required", "411 Length Required\n");
					// 	else
					// 	{
					// 		std::string filename = target.substr(prefix.size());
							
					// 		if (!isValidUploadFilename(filename))
					// 			response = buildResponse(400, "Bad Request", "400 Bad Request\n");
					// 		else
					// 		{
					// 			std::string filePath = "./www/uploads/" + filename;

					// 			int fileFd = open(filePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);

					// 			if (fileFd == -1)
					// 				response = buildResponse(500, "Internal Server Error", "500 Internal Server Error\n");
					// 			else
					// 			{
					// 				const std::string& body = request.getBody();
					// 				std::string::size_type offset = 0;
					// 				bool writeError = false;

					// 				while (offset < body.size())
					// 				{
					// 					ssize_t bytesWritten = write(fileFd, body.data() + offset, body.size() - offset);

					// 					if (bytesWritten <= 0)
					// 					{
					// 						writeError = true;
					// 						break;
					// 					}

					// 					offset += static_cast<std::string::size_type>(bytesWritten);
					// 				}
					// 				close(fileFd);

					// 				if (writeError)
					// 					response = buildResponse(500, "Internal Server Error", "500 Internal Server Error\n");
					// 				else
					// 					response = buildResponse(201, "Created", "201 Created\n");

					// 			}
					// 		}	
					// 	}

					// }
					else if (request.getMethod() == "DELETE")
					{
						std::string target = request.getTarget();
						std::string::size_type queryPos = target.find('?');
						
						if (queryPos != std::string::npos)
							target.erase(queryPos);

						const std::string prefix = "/uploads/";

						if (target.compare(0, prefix.size(), prefix) != 0)
							response = buildResponse(404, "Not Found", "404 Not Found\n");
						else
						{
							std::string filename = target.substr(prefix.size());

							if (!isValidUploadFilename(filename))
								response = buildResponse(400, "Bad Request", "400 Bad Request\n");
							else
							{
								std::string filePath = "./www/uploads/" + filename;
								struct stat fileInfo;

								if (stat(filePath.c_str(), &fileInfo) || !S_ISREG(fileInfo.st_mode))
									response = buildResponse(404, "Not Found", "404 Not Found\n");
								else if (unlink(filePath.c_str()) == -1)
									response = buildResponse(500, "Internal Server Error", "500 Internal Server Error\n");
								else
									response = buildResponse(204, "No Content", "");



							}
						}
					}
					else
						response = buildResponse(501, "Not Implemented", "501 Not Implemented\n");

					// if (request.getMethod() == "GET")
					// {
					// 	response = buildResponse(200, "OK", "Hello World!\n");
					// }
					// else
					// {
					// 	response = buildResponse(501, "Not Implemented", "501 Not Implemented\n");
					// }

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
	_lastActivity.erase(clientFd);
}

std::string Server::buildResponse(int statusCode, const std::string& reason, const std::string& body, const std::string&  contentType) const
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

	event.events = EPOLLOUT
	;
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
	_lastActivity[clientFd] = std::time(NULL);

	enableWriteEvent(clientFd);
}
