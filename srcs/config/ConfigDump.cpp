#include "config/Config.hpp"
#include <ostream>

static void dumpErrorPages(const std::map<int, std::string>& errorPages,
	const std::string& indent, std::ostream& out)
{
	for (std::map<int, std::string>::const_iterator it = errorPages.begin();
		it != errorPages.end(); ++it)
		out << indent << "error_page: " << it->first << " -> " << it->second << "\n";
}

static void dumpMethods(const LocationConfig& location, std::ostream& out)
{
	out << "        methods:";
	for (size_t i = 0; i < location.methods.size(); i++)
		out << " " << location.methods[i];
	out << "\n";
}

// Optional features are shown only when they are enabled.
static void dumpOptions(const LocationConfig& location, std::ostream& out)
{
	if (location.redirectCode != 0)
		out << "        return: " << location.redirectCode << " " << location.redirectUrl << "\n";
	if (!location.uploadStore.empty())
		out << "        upload_store: " << location.uploadStore << "\n";
	for (std::map<std::string, std::string>::const_iterator it = location.cgi.begin();
		it != location.cgi.end(); ++it)
		out << "        cgi: " << it->first << " -> " << it->second << "\n";
}

static void dumpLocation(const LocationConfig& location, std::ostream& out)
{
	out << "    location " << location.path << "\n"
		<< "        root: " << location.root
		<< (location.hasOwnRoot ? " (own)" : " (inherited)") << "\n"
		<< "        index: " << location.index << "\n"
		<< "        client_max_body_size: " << location.maxBodySize << "\n";
	dumpMethods(location, out);
	out << "        autoindex: " << (location.autoindex ? "on" : "off") << "\n";
	dumpOptions(location, out);
	dumpErrorPages(location.errorPages, "        ", out);
}

static void dumpServer(const ServerConfig& server, std::ostream& out)
{
	for (size_t i = 0; i < server.listens.size(); i++)
		out << "    listen: " << server.listens[i].host << ":" << server.listens[i].port << "\n";
	out << "    root: " << server.root << "\n"
		<< "    index: " << server.index << "\n"
		<< "    client_max_body_size: " << server.maxBodySize << "\n";
	dumpErrorPages(server.errorPages, "    ", out);
	for (size_t i = 0; i < server.locations.size(); i++)
		dumpLocation(server.locations[i], out);
}

void dumpConfig(const std::vector<ServerConfig>& servers, std::ostream& out)
{
	for (size_t i = 0; i < servers.size(); i++)
	{
		out << "server #" << i + 1 << "\n";
		dumpServer(servers[i], out);
	}
}
