/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:34:25 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/02 18:11:34 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "request.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstdio>

static const std::string::size_type MAX_BODY_SIZE = 1024 * 1024;
static const std::string::size_type MAX_HEADERS_SIZE = 16 * 1024;

static std::string toLowerCase(const std::string& value)
{
    std::string result = value;

    for (std::string::size_type i = 0; i < result.size(); i++)
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
            result[i] = result[i] - 'A' + 'a';
    }
    return (result);
}

Request::Request() : _method(), _target(), _version(), _hasContentLength(false)
{

}

Request::ParseStatus Request::parse(const std::string& raw)
{
    _method.clear();
    _target.clear();
    _version.clear();
    _body.clear();
    _hasContentLength = false;
    // Accept CRLF CRLF or LF LF as the end of headers.
    std::string::size_type headersEnd =
        raw.find("\r\n\r\n");
    std::string::size_type separatorSize = 4;

    if (headersEnd == std::string::npos)
    {
        headersEnd = raw.find("\n\n");
        separatorSize = 2;
    }

    if (headersEnd == std::string::npos)
    {
        if (raw.size() >= MAX_HEADERS_SIZE)
            return (HEADERS_TOO_LARGE);
        return (INCOMPLETE);
    }
    if (headersEnd > MAX_HEADERS_SIZE)
        return (HEADERS_TOO_LARGE);

    std::string headers = raw.substr(0, headersEnd);
    // Locate the end of the request line.
    std::string::size_type lineEnd = headers.find('\n');

    if (lineEnd == std::string::npos)
        return (INVALID);

    std::string line = headers.substr(0, lineEnd);

    // Remove a trailing carriage return if present.
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    std::string::size_type firstSpace = line.find(' ');
    if (firstSpace == std::string::npos)
        return (INVALID);

    std::string::size_type secondSpace =
        line.find(' ', firstSpace + 1);

    if (secondSpace == std::string::npos)
        return (INVALID);

    _method = line.substr(0, firstSpace);
    _target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    _version = line.substr(secondSpace + 1);

    if (_method.empty() || _target.empty() || _version.empty() || _version.find(' ') != std::string::npos)
        return (INVALID);

    if (_version != "HTTP/1.0" && _version != "HTTP/1.1")
        return (INVALID);
    
    std::string::size_type contentLength = 0;

    bool hasContentLength = false;

    std::string::size_type pos = lineEnd + 1;

    while (pos < headers.size())
    {
        std::string::size_type end = headers.find('\n', pos);

        if (end == std::string::npos)
            end = headers.size();

        std::string header = headers.substr(pos, end - pos);
        if (!header.empty() && header[header.size() - 1] == '\r')
            header.erase(header.size() - 1);

        std::string::size_type colon = header.find(':');

        if (colon == std::string::npos || colon == 0)
            return INVALID;

        std::string name = header.substr(0, colon);

        std::string value = header.substr(colon + 1);

        while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
            value.erase(0, 1);

        if (toLowerCase(name) == "content-length")
        {
            if (hasContentLength || value.empty())
                return INVALID;
            _hasContentLength = true;

            contentLength = 0;

            for (std::string::size_type i = 0; i < value.size(); ++i)
            {
                if (value[i] < '0' || value[i] > '9')
                    return INVALID;

                std::string::size_type digit = value[i] - '0';
                if (contentLength > (MAX_BODY_SIZE - digit) / 10)
                    return (BODY_TOO_LARGE);
                contentLength = contentLength * 10 + digit;
            }

            hasContentLength = true;
        }
        pos = end + 1;
    }

    std::string::size_type bodyStart = headersEnd + separatorSize;
    std::string::size_type availableBodySize = raw.size() - bodyStart;

    if (availableBodySize < contentLength)
        return INCOMPLETE;

    _body = raw.substr(bodyStart, contentLength);
    return COMPLETE;
}

const std::string& Request::getMethod() const
{
	return (_method);
}

const std::string& Request::getTarget() const
{
	return (_target);
}

const std::string& Request::getVersion() const
{
	return (_version);
}

const std::string& Request::getBody() const
{
	return (_body);
}

bool Request::hasContentLength() const
{
	return (_hasContentLength);
}


