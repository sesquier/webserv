// The CGI part of the epoll loop. While its script runs, a client waits in
// state CGI_RUNNING; the two pipes of the script are watched by epoll like
// sockets: the loop writes the body when the script can take it, reads the
// output when there is some, and never waits for a script.

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
		// The client has nothing more to send: epoll only watches whether it
		// leaves (EPOLLRDHUP), which closes the client and stops its script.
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

// An event on a pipe: the input can take more body, or the output has data
// (or is closed: read then returns 0, or -1 on an error).
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
	// All sent: EOF tells the script the body is complete. -1: the script
	// stopped reading (SIGPIPE is ignored, see main); its output decides.
	if (written <= 0 || job.bodySent == job.body.size())
		closeCgiFd(job.process.inputFd);
}

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
	// 0: the script closed its output (EOF), the output is complete.
	// Its input is closed too: it cannot read a body it has finished answering.
	closeCgiFd(job.process.inputFd);
	closeCgiFd(job.process.outputFd);
	reapCgiJob(clientFd, false);
}

// The output is complete: collects the script's exit status, then answers.
// A script often closes its output a few microseconds before it exits: if
// it still runs, it is checked again by checkCgiJobs, or stopped if force.
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
		// A crash after the headers would look like a valid, empty page: the
		// exit status shows it.
		succeeded = (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	const LocationConfig*	location = job.location;
	std::string				response = succeeded ? cgiResponse(job.output, *location)
										: buildError(502, location);

	_cgiJobs.erase(clientFd);
	queueResponse(clientFd, response);
	return (true);
}

// Stops a script and forgets it: the client is gone, or the script is too slow.
// waitpid right after SIGKILL: the process ends at once, nothing to wait for.
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

// Called after each round of events: scripts waiting to be reaped, and
// scripts silent for too long.
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
