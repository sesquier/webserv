// Builds the HTTP response of a parsed request: routing through the location,
// then GET, POST and DELETE. The network loop lives in server.cpp.

#include "server.hpp"
#include "request.hpp"
#include "cgi.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <set>
#include <cctype>

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

// "/page?x=1" -> "/page": the query string is not part of the file path.
static std::string requestPath(const std::string& target)
{
	return (target.substr(0, target.find('?')));
}

static int hexValue(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

// "%20" -> ' ': a URL encodes every special character as '%' and two hex digits.
// Returns false on a malformed '%' or on "%00", which would cut the path short
// once it is given to a C function.
bool decodePath(const std::string& encoded, std::string& decoded)
{
	for (std::string::size_type i = 0; i < encoded.size(); i++)
	{
		if (encoded[i] != '%')
		{
			decoded += encoded[i];
			continue;
		}
		// Two hex digits must follow the '%'.
		if (i + 2 >= encoded.size())
			return (false);

		int high = hexValue(encoded[i + 1]);
		int low = hexValue(encoded[i + 2]);

		if (high < 0 || low < 0 || (high == 0 && low == 0))
			return (false);
		decoded += static_cast<char>(high * 16 + low);
		i += 2;
	}
	return (true);
}

// The reverse, for the links of the autoindex page: "a#b.txt" -> "a%23b.txt".
// Without it, '#' or '?' in a file name would cut the link.
static std::string encodePath(const std::string& path)
{
	const char*	digits = "0123456789ABCDEF";
	std::string	encoded;

	for (std::string::size_type i = 0; i < path.size(); i++)
	{
		unsigned char c = static_cast<unsigned char>(path[i]);

		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/')
			encoded += path[i];
		else
		{
			encoded += '%';
			encoded += digits[c / 16];
			encoded += digits[c % 16];
		}
	}
	return (encoded);
}

// "./www/site1/hello.py" -> ".py". Empty if the file name has no extension:
// a '.' in a directory name ("./www/v1.2/README") does not count.
static std::string fileExtension(const std::string& filePath)
{
	std::string::size_type dotPos = filePath.find_last_of('.');

	if (dotPos == std::string::npos || filePath.find('/', dotPos) != std::string::npos)
		return ("");
	return (filePath.substr(dotPos));
}

static std::string contentTypeFor(const std::string& filePath)
{
	std::string extension = fileExtension(filePath);

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

// The interpreter that runs this file, from the location's "cgi" directives
// (".py" -> "/usr/bin/python3"). Empty: the file is not a CGI script here.
static std::string cgiInterpreter(const std::string& filePath, const LocationConfig& location)
{
	std::map<std::string, std::string>::const_iterator it = location.cgi.find(fileExtension(filePath));

	if (it == location.cgi.end())
		return ("");
	return (it->second);
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

// A file name is shown inside the HTML page: "<", ">", "&" and quotes must not
// be read as HTML, or a file named "<script>..." would run in the browser.
static std::string escapeHtml(const std::string& text)
{
	std::string escaped;

	for (std::string::size_type i = 0; i < text.size(); i++)
	{
		if (text[i] == '<')
			escaped += "&lt;";
		else if (text[i] == '>')
			escaped += "&gt;";
		else if (text[i] == '&')
			escaped += "&amp;";
		else if (text[i] == '"')
			escaped += "&quot;";
		else
			escaped += text[i];
	}
	return (escaped);
}

// Names of the directory, sorted; a sub-directory ends with '/'.
// Returns false if the directory cannot be opened.
static bool readDirectory(const std::string& dirPath, std::set<std::string>& names)
{
	DIR* dir = opendir(dirPath.c_str());

	if (dir == NULL)
		return (false);

	struct dirent* entry;

	while ((entry = readdir(dir)) != NULL)
	{
		std::string	name = entry->d_name;
		struct stat	info;

		if (name == "." || name == "..")
			continue;
		if (stat((dirPath + name).c_str(), &info) == 0 && S_ISDIR(info.st_mode))
			name += "/";
		names.insert(name);
	}
	closedir(dir);
	return (true);
}

// The autoindex page: one link per entry of the directory.
static std::string directoryListing(const std::string& path, const std::set<std::string>& names)
{
	std::string title = "Index of " + escapeHtml(path);
	std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>"
		+ title + "</title>\n</head>\n<body>\n<h1>" + title + "</h1>\n<ul>\n";

	if (path != "/")
		html += "<li><a href=\"../\">../</a></li>\n";
	for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
		html += "<li><a href=\"" + encodePath(*it) + "\">" + escapeHtml(*it) + "</a></li>\n";
	html += "</ul>\n</body>\n</html>\n";
	return (html);
}

static std::string reasonPhrase(int code)
{
	switch (code)
	{
		case 200: return ("OK");
		case 201: return ("Created");
		case 302: return ("Found");
		case 400: return ("Bad Request");
		case 403: return ("Forbidden");
		case 404: return ("Not Found");
		case 405: return ("Method Not Allowed");
		case 408: return ("Request Timeout");
		case 409: return ("Conflict");
		case 411: return ("Length Required");
		case 413: return ("Content Too Large");
		case 431: return ("Request Header Fields Too Large");
		case 500: return ("Internal Server Error");
		case 501: return ("Not Implemented");
		case 502: return ("Bad Gateway");
		case 504: return ("Gateway Timeout");
	}
	return ("Error");
}

// Page sent when the configuration gives none for this code.
static std::string defaultErrorPage(int code, const std::string& reason)
{
	std::ostringstream page;

	page << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n"
		<< "<title>" << code << " " << reason << "</title>\n</head>\n<body>\n"
		<< "<h1>" << code << " " << reason << "</h1>\n<hr>\n<p>webserv</p>\n"
		<< "</body>\n</html>\n";
	return (page.str());
}

// Applies the rules shared by every method, then chooses the handler.
// The order matters: unknown method, redirection, then allowed methods.
std::string Server::handleRequest(const Request& request, const ServerConfig& server) const
{
	const std::string&	method = request.getMethod();
	std::string			path;

	if (method != "GET" && method != "POST" && method != "DELETE")
		return (buildError(501, NULL));
	// Decoded before any check: "%2e%2e" must reach resolvePath as "..".
	if (!decodePath(requestPath(request.getTarget()), path) || path.empty() || path[0] != '/')
		return (buildError(400, NULL));

	const LocationConfig& location = server.findLocation(path);

	if (location.redirectCode != 0)
		return (buildResponse(location.redirectCode, redirectReason(location.redirectCode), "",
			"text/plain", "Location: " + location.redirectUrl + "\r\n"));
	if (!location.isMethodAllowed(method))
		return (buildError(405, &location, "Allow: " + allowedMethods(location) + "\r\n"));
	// A script of a CGI location is run, never sent or overwritten.
	if (method == "GET" || method == "POST")
	{
		std::string scriptPath = resolvePath(location, path);
		std::string interpreter = cgiInterpreter(scriptPath, location);

		if (!interpreter.empty())
			return (handleCgi(request, scriptPath, interpreter, location));
	}
	if (method == "GET")
		return (handleGet(path, location));
	if (method == "POST")
		return (handlePost(request, path, location));
	return (handleDelete(path, location));
}

std::string Server::handleGet(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);

	if (filePath.empty())
		return (buildError(403, &location));

	struct stat info;

	if (stat(filePath.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
		return (handleDirectory(path, filePath, location));
	return (serveFile(filePath, location));
}

// In order: add the missing '/', serve the index file, list the content
// (autoindex on), or refuse (autoindex off).
std::string Server::handleDirectory(const std::string& path, const std::string& dirPath,
	const LocationConfig& location) const
{
	// Without the final '/', the browser would resolve the links of the page
	// from the parent directory: "/files" + "notes.txt" gives "/notes.txt".
	if (path[path.size() - 1] != '/')
		return (buildResponse(301, "Moved Permanently", "", "text/plain",
			"Location: " + path + "/\r\n"));

	std::string	indexPath = dirPath + location.index;
	struct stat	info;

	if (stat(indexPath.c_str(), &info) == 0 && S_ISREG(info.st_mode))
		return (serveFile(indexPath, location));
	if (!location.autoindex)
		return (buildError(403, &location));

	std::set<std::string> names;

	if (!readDirectory(dirPath, names))
		return (buildError(403, &location));
	return (buildResponse(200, "OK", directoryListing(path, names), "text/html"));
}

// stat before open: opening a FIFO would block the whole server.
std::string Server::serveFile(const std::string& filePath, const LocationConfig& location) const
{
	struct stat	info;
	std::string	content;

	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	if (access(filePath.c_str(), R_OK) == -1)
		return (buildError(403, &location));
	if (!readWholeFile(filePath, content))
		return (buildError(500, &location));
	return (buildResponse(200, "OK", content, contentTypeFor(filePath)));
}

// "POST /upload/photo.png" stores the body as <upload_store>/photo.png.
std::string Server::handlePost(const Request& request, const std::string& path,
	const LocationConfig& location) const
{
	if (location.uploadStore.empty())
		return (buildError(403, &location));
	if (!request.hasBodyFraming())
		return (buildError(411, &location));

	std::string filename = path.substr(location.path.size());

	if (!filename.empty() && filename[0] == '/')
		filename.erase(0, 1);
	if (!isValidUploadFilename(filename))
		return (buildError(400, &location));

	int status = saveUploadedFile(location.uploadStore + "/" + filename, request.getBody());

	if (status == 201)
		return (buildResponse(201, "Created", "201 Created\n"));
	if (status == 409)
		return (buildError(409, &location));
	return (buildError(500, &location));
}

std::string Server::handleDelete(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);
	struct stat info;

	if (filePath.empty())
		return (buildError(403, &location));
	if (stat(filePath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	if (unlink(filePath.c_str()) == -1)
		return (buildError(500, &location));
	return (buildResponse(204, "No Content", ""));
}

// Runs scriptPath with interpreter and sends back what the script writes.
// For now it only checks the script: launching it comes in the next steps.
std::string Server::handleCgi(const Request& request, const std::string& scriptPath,
	const std::string& interpreter, const LocationConfig& location) const
{
	struct stat info;

	if (stat(scriptPath.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
		return (buildError(404, &location));
	if (access(scriptPath.c_str(), R_OK) == -1)
		return (buildError(403, &location));
	(void)request;
	(void)interpreter;
	return (buildError(501, &location));
}

// The response from what the script wrote: its status, its headers, its body.
// 502 Bad Gateway when the output does not follow CGI: the server in front
// (webserv) got a bad answer from the program behind it.
std::string Server::cgiResponse(const std::string& output, const LocationConfig& location) const
{
	CgiOutput result;

	if (!parseCgiOutput(output, result))
		return (buildError(502, &location));
	if (result.reason.empty())
		result.reason = reasonPhrase(result.status);
	if (result.contentType.empty())
		result.contentType = "text/plain";
	return (buildResponse(result.status, result.reason, result.body, result.contentType, result.headers));
}

// The page of the location's error_page for this code if it can be read,
// otherwise a generated page. The status code stays the error code either way.
std::string Server::buildError(int code, const LocationConfig* location,
	const std::string& extraHeaders) const
{
	std::string reason = reasonPhrase(code);

	if (location != NULL)
	{
		std::map<int, std::string>::const_iterator page = location->errorPages.find(code);
		struct stat info;
		std::string content;

		// stat first, as in serveFile: an error page that is a FIFO must not block.
		if (page != location->errorPages.end()
			&& stat(page->second.c_str(), &info) == 0 && S_ISREG(info.st_mode)
			&& readWholeFile(page->second, content))
			return (buildResponse(code, reason, content, contentTypeFor(page->second), extraHeaders));
	}
	return (buildResponse(code, reason, defaultErrorPage(code, reason), "text/html", extraHeaders));
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
