// ============================================================================
// cgiEvents.cpp : les scripts CGI depuis la boucle epoll.
//
// Le point clé à expliquer au correcteur : LE SERVEUR N'ATTEND JAMAIS UN
// SCRIPT. Les deux pipes du script sont inscrits dans epoll, exactement
// comme des sockets :
//   - pipe d'entrée (stdin du script) en EPOLLOUT : on écrit le body quand le
//     script peut le recevoir ;
//   - pipe de sortie (stdout du script) en EPOLLIN : on lit ce qu'il écrit
//     quand il y en a.
// Pendant ce temps, le client attend dans l'état CGI_RUNNING et la boucle
// continue de servir tous les autres. Un script en boucle infinie ne bloque
// donc rien : il est tué au bout de 10 s d'inactivité (504).
//
// Cycle de vie d'un script :
//   startCgiJob -> writeCgiBody* -> readCgiOutput* -> reapCgiJob -> réponse
//   (ou abortCgiJob si le client part, ou 504 dans checkCgiJobs)
// ============================================================================

#include "server.hpp"
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Un script qui ne lit ni n'écrit rien pendant ce temps est arrêté : 504.
// C'est de l'INACTIVITÉ : un script qui envoie une page énorme pendant 30 s
// sans s'arrêter d'écrire n'est pas tué.
static const std::time_t CGI_TIMEOUT = 10;

// Appelée quand la requête est complète : lance le script, ou répond 500.
void Server::startCgiJob(int clientFd, const CgiRequest& cgi)
{
	CgiProcess process;

	if (!startCgi(cgi.interpreter, cgi.scriptPath, cgi.env, process))
	{
		queueResponse(clientFd, buildError(500, cgi.location));
		return ;
	}

	// Enregistré EN PREMIER : si un epoll_ctl échoue plus bas, abortCgiJob
	// trouvera tout ce qu'il faut nettoyer (pipes et processus).
	CgiJob& job = _cgiJobs[clientFd];

	job.process = process;
	job.body = cgi.body;
	job.bodySent = 0;
	job.location = cgi.location;
	job.lastActivity = std::time(NULL);
	_clientStates[clientFd] = CGI_RUNNING;
	try
	{
		// Le client n'a plus rien à envoyer : epoll surveille seulement s'il
		// part (EPOLLRDHUP). S'il part, handleEvent ferme le client et
		// closeClient tue son script : aucun script ne tourne pour rien.
		struct epoll_event event;

		std::memset(&event, 0, sizeof(event));
		event.events = EPOLLRDHUP;
		event.data.fd = clientFd;
		if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &event) == -1)
			throw std::runtime_error("epoll_ctl failed on a CGI client");

		watchCgiPipe(job.process.outputFd, EPOLLIN, clientFd);
		// Pas de body (GET) : on ferme le stdin du script tout de suite, il
		// lit EOF. Sinon, on écrira le body quand epoll dira que c'est possible.
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

// Inscrit un pipe dans epoll, et retient à quel client il appartient : quand
// epoll signale ce pipe, handleEvent saura de quel client il s'agit.
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

// Ferme un bout de pipe : le retire d'epoll, le ferme, l'oublie, et met le fd
// à -1 (passé par référence) pour qu'on ne le ferme jamais deux fois.
// Fermer l'entrée donne EOF au script ; fermer la sortie arrête notre lecture.
void Server::closeCgiFd(int& pipeFd)
{
	if (pipeFd == -1)
		return ;
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
	close(pipeFd);
	_cgiPipes.erase(pipeFd);
	pipeFd = -1;
}

// Un événement sur un pipe : l'entrée peut prendre plus de body, ou la
// sortie a des données (ou est fermée : read renverra alors 0, ou -1).
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

// UN write du body par événement EPOLLOUT, comme pour les sockets.
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
	// Tout est parti : on ferme, et le script lit EOF (= body complet).
	// 0 ou -1 : le script a arrêté de lire (il a fini, ou planté). SIGPIPE est
	// ignoré (voir main) : write renvoie simplement -1. On ferme l'entrée et
	// c'est sa sortie qui décidera de la réponse. Pas d'errno.
	if (written <= 0 || job.bodySent == job.body.size())
		closeCgiFd(job.process.inputFd);
}

// UN read de la sortie par événement EPOLLIN.
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
	// 0 : le script a fermé sa sortie (EOF) : la sortie est complète.
	// -1 : erreur de lecture : on s'arrête aussi, sans chercher pourquoi.
	// On ferme aussi son entrée : il ne lira plus un body alors qu'il a fini.
	closeCgiFd(job.process.inputFd);
	closeCgiFd(job.process.outputFd);
	reapCgiJob(clientFd, false);
}

// La sortie est complète : on récupère le code de sortie du script, puis on
// répond. Un script ferme souvent sa sortie quelques microsecondes AVANT de
// se terminer : s'il tourne encore, checkCgiJobs réessaiera au prochain tour
// (ou le tuera si force est vrai). Renvoie vrai si une réponse est partie.
bool Server::reapCgiJob(int clientFd, bool force)
{
	CgiJob&	job = _cgiJobs[clientFd];
	int		status = 0;
	// WNOHANG : ne bloque JAMAIS. Renvoie 0 si le script tourne encore, son
	// pid s'il est terminé (et on lit son code de sortie dans status).
	// Lire ce code libère le processus : sans waitpid, il resterait zombie.
	pid_t	result = waitpid(job.process.pid, &status, WNOHANG);
	bool	succeeded;

	if (result == 0 && !force)
		return (false);
	if (result == 0)
	{
		// Il a fermé sa sortie mais ne se termine pas : on le tue, et sa
		// sortie est tout ce qu'on juge.
		kill(job.process.pid, SIGKILL);
		waitpid(job.process.pid, NULL, 0);
		succeeded = true;
	}
	else
		// Un script qui plante APRÈS avoir écrit ses headers ressemblerait à
		// une page valide mais vide : son code de sortie le révèle.
		// WIFEXITED : il s'est terminé normalement (pas tué par un signal) ;
		// WEXITSTATUS : son code de retour, 0 = succès.
		succeeded = (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	// On copie ce dont on a besoin AVANT d'effacer le job (job est une
	// référence vers l'élément de la map, qui va disparaître).
	const LocationConfig*	location = job.location;
	std::string				response = succeeded ? cgiResponse(job.output, *location)
										: buildError(502, location);

	_cgiJobs.erase(clientFd);
	queueResponse(clientFd, response);
	return (true);
}

// Arrête un script et l'oublie : le client est parti, ou le script est trop
// lent. waitpid juste après SIGKILL : le processus se termine aussitôt, il
// n'y a presque rien à attendre, et il ne restera pas zombie.
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

// Appelée après chaque tour de la boucle : les scripts qui attendent d'être
// récupérés, et ceux qui sont silencieux depuis trop longtemps.
void Server::checkCgiJobs(std::time_t now)
{
	std::map<int, CgiJob>::iterator it = _cgiJobs.begin();

	while (it != _cgiJobs.end())
	{
		int		clientFd = it->first;
		CgiJob&	job = it->second;
		bool	tooSlow = (now - job.lastActivity >= CGI_TIMEOUT);

		++it;	// avant que le job soit effacé plus bas (itérateur invalidé)
		try
		{
			// Sortie déjà finie : on attend juste que le processus se termine.
			if (job.process.outputFd == -1)
				reapCgiJob(clientFd, tooSlow);
			// Toujours en train de tourner, mais muet depuis 10 s : 504.
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
