/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/22 16:50:43 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/22 17:18:30 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

// ============================================================================
// Point d'entrée du programme : ./webserv [fichier de configuration]
//
// Trois étapes : charger la config, créer le serveur, lancer la boucle.
// Toutes les erreurs remontent ici sous forme d'exceptions : le programme
// affiche un message clair et quitte avec le code 1, sans jamais planter.
// ============================================================================

#include "server.hpp"
#include "config/Config.hpp"
#include <iostream>
#include <exception>
#include <csignal>

int main(int argc, char** argv)
{
	// Le sujet : le programme prend un fichier de config en argument, ou
	// utilise un chemin par défaut. Plus d'un argument est une erreur d'usage.
	if (argc > 2)
	{
		std::cerr << "usage: " << argv[0] << " [configuration file]" << std::endl;
		return (1);
	}
	// SIGPIPE est envoyé quand on écrit dans un pipe ou un socket dont l'autre
	// bout est fermé (un script CGI terminé, un client parti). Par défaut, ce
	// signal TUE le programme. Ignoré, l'écriture renvoie simplement -1 et on
	// ferme proprement. signal() fait partie des fonctions autorisées.
	signal(SIGPIPE, SIG_IGN);
	try
	{
		// configs est déclaré AVANT server : le Server garde des références et
		// des pointeurs vers ces objets jusqu'à la fin. Les variables locales
		// sont détruites dans l'ordre inverse : server d'abord, configs ensuite.
		const std::vector<ServerConfig> configs = loadConfig(argc == 2 ? argv[1] : "config/default.conf");
		Server server(configs);

		// run() ne rend jamais la main normalement : c'est la boucle epoll.
		// Ctrl-C (SIGINT) termine le programme.
		server.run();
	}
	// Erreur dans le fichier de config : message avec la ligne fautive.
	catch (const ConfigError& e)
	{
		std::cerr << "webserv: config error: " << e.what() << std::endl;
		return (1);
	}
	// Toute autre erreur fatale : port déjà pris (bind), epoll_create...
	catch (const std::exception& e)
	{
		std::cerr << "webserv: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
