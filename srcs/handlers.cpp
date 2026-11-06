// Builds the HTTP response of a parsed request: routing through the location,
// then GET, POST and DELETE. The network loop lives in server.cpp.

#include "server.hpp"
#include "request.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <set>

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
		html += "<li><a href=\"" + escapeHtml(*it) + "\">" + escapeHtml(*it) + "</a></li>\n";
	html += "</ul>\n</body>\n</html>\n";
	return (html);
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

std::string Server::handleGet(const std::string& path, const LocationConfig& location) const
{
	std::string filePath = resolvePath(location, path);

	if (filePath.empty())
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));

	struct stat info;

	if (stat(filePath.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
		return (handleDirectory(path, filePath, location));
	return (serveFile(filePath));
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
		return (serveFile(indexPath));
	if (!location.autoindex)
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));

	std::set<std::string> names;

	if (!readDirectory(dirPath, names))
		return (buildResponse(403, "Forbidden", "403 Forbidden\n"));
	return (buildResponse(200, "OK", directoryListing(path, names), "text/html"));
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
