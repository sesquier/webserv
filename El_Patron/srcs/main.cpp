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

#include "server.hpp"
#include "config/Config.hpp"
#include <iostream>
#include <exception>
#include <csignal>

int main(int argc, char** argv)
{
	if (argc > 2)
	{
		std::cerr << "usage: " << argv[0] << " [configuration file]" << std::endl;
		return (1);
	}
	// Writing to a pipe whose script has exited would send SIGPIPE, which
	// kills the server: ignored, write then just returns -1.
	signal(SIGPIPE, SIG_IGN);
	try
	{
		// Declared first: the Server keeps pointers to these objects until the end.
		const std::vector<ServerConfig> configs = loadConfig(argc == 2 ? argv[1] : "config/default.conf");
		Server server(configs);

		server.run();
	}
	catch (const ConfigError& e)
	{
		std::cerr << "webserv: config error: " << e.what() << std::endl;
		return (1);
	}
	catch (const std::exception& e)
	{
		std::cerr << "webserv: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}
