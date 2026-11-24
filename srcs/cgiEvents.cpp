// The CGI part of the epoll loop: the script's pipes are watched like sockets.

#include "server.hpp"
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>

// A script that neither reads nor writes for this long is stopped: 504.
static const std::time_t CGI_TIMEOUT = 10;

// Called with the request complete: starts its script, or answers 500.
void Server::startCgiJob(int clientFd, const CgiRequest& cgi)
{
	CgiProcess process;

	if (!startCgi(cgi.interpreter, cgi.scriptPath, cgi.env, process))
	{
		queueResponse(clientFd, buildError(500, cgi.location));
		return ;
	}

	// Recorded first: if an epoll_ctl fails, abortCgiJob finds everything to clean.
	CgiJob& job = _cgiJobs[clientFd];

	job.process = process;
	job.body = cgi.body;
	job.bodySent = 0;
	job.location = cgi.location;
	job.lastActivity = std::time(NULL);
	_clientStates[clientFd] = CGI_RUNNING;
	try
	{
		// The client is only watched for leaving (EPOLLRDHUP).
		struct epoll_event event;

		std::memset(&event, 0, sizeof(event));
		event.events = EPOLLRDHUP;
		event.data.fd = clientFd;
		if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &event) == -1)
			throw std::runtime_error("epoll_ctl failed on a CGI client");

		watchCgiPipe(job.process.outputFd, EPOLLIN, clientFd);
		// No body: closing stdin at once gives the script EOF.
		if (job.body.empty())
			closeCgiFd(job.process.inputFd);
		else
			watchCgiPipe(job.process.inputFd, EPOLLOUT, clientFd);
	}
	catch (const std::exception&)
	{
		abortCgiJob(clientFd);
		queueResponse(clientFd, buildError(500, cgi.location));
	}
}

// Adds a pipe of the script to epoll.
void Server::watchCgiPipe(int pipeFd, unsigned int events, int clientFd)
{
	struct epoll_event event;

	std::memset(&event, 0, sizeof(event));
	event.events = events;
	event.data.fd = pipeFd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, pipeFd, &event) == -1)
		throw std::runtime_error("epoll_ctl failed on a CGI pipe");
	_cgiPipes[pipeFd] = clientFd;
}

// Closing the input gives the script EOF; closing the output ends our reading.
void Server::closeCgiFd(int& pipeFd)
{
	if (pipeFd == -1)
		return ;
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
	close(pipeFd);
	_cgiPipes.erase(pipeFd);
	pipeFd = -1;
}

// An event on a pipe: writes the body, or reads the output.
void Server::handleCgiEvent(int pipeFd)
{
	int clientFd = _cgiPipes[pipeFd];

	try
	{
		if (pipeFd == _cgiJobs[clientFd].process.inputFd)
			writeCgiBody(clientFd);
		else
			readCgiOutput(clientFd);
	}
	catch (const std::exception& e)
	{
		std::cerr << "CGI error: " << e.what() << std::endl;
		closeClient(clientFd);
	}
}

// One write of the body into the script's input.
void Server::writeCgiBody(int clientFd)
{
	CgiJob& job = _cgiJobs[clientFd];
	ssize_t written = write(job.process.inputFd, job.body.c_str() + job.bodySent,
		job.body.size() - job.bodySent);

	if (written > 0)
	{
		job.bodySent += static_cast<std::string::size_type>(written);
		job.lastActivity = std::time(NULL);
	}
	// All sent, or the script stopped reading: closing gives it EOF.
	if (written <= 0 || job.bodySent == job.body.size())
		closeCgiFd(job.process.inputFd);
}

// One read of the script's output.
void Server::readCgiOutput(int clientFd)
{
	CgiJob&	job = _cgiJobs[clientFd];
	char	buffer[4096];
	ssize_t	bytesRead = read(job.process.outputFd, buffer, sizeof(buffer));

	if (bytesRead > 0)
	{
		job.output.append(buffer, bytesRead);
		job.lastActivity = std::time(NULL);
		return ;
	}
	// EOF or error: the output is complete.
	closeCgiFd(job.process.inputFd);
	closeCgiFd(job.process.outputFd);
	reapCgiJob(clientFd, false);
}

// Collects the script's exit status and answers; false if it still runs (unless force).
bool Server::reapCgiJob(int clientFd, bool force)
{
	CgiJob&	job = _cgiJobs[clientFd];
	int		status = 0;
	pid_t	result = waitpid(job.process.pid, &status, WNOHANG);
	bool	succeeded;

	if (result == 0 && !force)
		return (false);
	if (result == 0)
	{
		// Closed its output but does not exit: its output is all we judge.
		kill(job.process.pid, SIGKILL);
		waitpid(job.process.pid, NULL, 0);
		succeeded = true;
	}
	else
		// The exit status reveals a crash after valid headers.
		succeeded = (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	const LocationConfig*	location = job.location;
	std::string				response = succeeded ? cgiResponse(job.output, *location)
										: buildError(502, location);

	_cgiJobs.erase(clientFd);
	queueResponse(clientFd, response);
	return (true);
}

// Kills the script and forgets the job.
void Server::abortCgiJob(int clientFd)
{
	std::map<int, CgiJob>::iterator it = _cgiJobs.find(clientFd);

	if (it == _cgiJobs.end())
		return ;
	closeCgiFd(it->second.process.inputFd);
	closeCgiFd(it->second.process.outputFd);
	kill(it->second.process.pid, SIGKILL);
	waitpid(it->second.process.pid, NULL, 0);
	_cgiJobs.erase(it);
}

// After each round of events: reaps finished scripts, stops silent ones (504).
void Server::checkCgiJobs(std::time_t now)
{
	std::map<int, CgiJob>::iterator it = _cgiJobs.begin();

	while (it != _cgiJobs.end())
	{
		int		clientFd = it->first;
		CgiJob&	job = it->second;
		bool	tooSlow = (now - job.lastActivity >= CGI_TIMEOUT);

		++it;	// before the job is erased below
		try
		{
			if (job.process.outputFd == -1)
				reapCgiJob(clientFd, tooSlow);
			else if (tooSlow)
			{
				const LocationConfig* location = job.location;

				abortCgiJob(clientFd);
				queueResponse(clientFd, buildError(504, location));
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "CGI error: " << e.what() << std::endl;
			closeClient(clientFd);
		}
	}
}
