/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:34:25 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/10 15:19:03 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "request.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstdio>

static const std::string::size_type MAX_HEADERS_SIZE = 16 * 1024;

// Header names ignore case.
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

// Decodes a chunked body ("size\r\ndata\r\n" ... "0\r\n\r\n").
static Request::ParseStatus decodeChunkedBody(const std::string& raw, std::string::size_type bodyStart, std::string::size_type maxBodySize, std::string& decoded)
{
    decoded.clear();

    std::string::size_type pos = bodyStart;

    while (true)
    {
        std::string::size_type lineEnd = raw.find("\r\n", pos);

        if (lineEnd == std::string::npos)
            return (Request::INCOMPLETE);

        std::string sizeLine = raw.substr(pos, lineEnd - pos);
        std::string::size_type semicolon = sizeLine.find(';');

        std::string sizeText = sizeLine.substr(0, semicolon);

        if (sizeText.empty())
            return (Request::INVALID);

        std::string::size_type chunkSize = 0;

        for (std::string::size_type i = 0; i < sizeText.size(); ++i)
        {
            char c = sizeText[i];
            std::string::size_type digit;

            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return (Request::INVALID);

            if (chunkSize > ((std::string::size_type)-1 - digit) / 16)
                return (Request::INVALID);

            chunkSize = chunkSize * 16 + digit;
            }

        pos = lineEnd + 2;

        if (chunkSize == 0)
        {
            if (raw.compare(pos, 2, "\r\n") == 0)
                return (Request::COMPLETE);

            std::string::size_type trailerEnd = raw.find("\r\n\r\n", pos);

            if (trailerEnd == std::string::npos)
                return (Request::INCOMPLETE);

            std::string trailers = raw.substr(pos, trailerEnd - pos);
            std::string::size_type trailerPos = 0;

            while (trailerPos < trailers.size())
            {
                std::string::size_type end =
                trailers.find("\r\n", trailerPos);

                if (end == std::string::npos)
                    end = trailers.size();

                std::string trailer = trailers.substr(trailerPos, end - trailerPos);

                std::string::size_type colon = trailer.find(':');

                if (colon == std::string::npos || colon == 0)
                    return (Request::INVALID);

                std::string name = trailer.substr(0, colon);
                std::string lowerName = toLowerCase(name);

                for (std::string::size_type i = 0; i < name.size(); ++i)
                {
                    char c = name[i];

                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                        || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-'
                        || c == '.' || c == '^' || c == '_'
                        || c == '`' || c == '|' || c == '~'))
                    return (Request::INVALID);
                }

                if (lowerName == "content-length" || lowerName == "transfer-encoding" || lowerName == "host" || lowerName == "trailer")
                    return (Request::INVALID);

                for (std::string::size_type i = colon + 1; i < trailer.size(); ++i)
                {
                    unsigned char c = static_cast<unsigned char>(trailer[i]);

                    if ((c < 32 && c != '\t') || c == 127)
                        return (Request::INVALID);
                }

                trailerPos = end + 2;
            }

            return (Request::COMPLETE);
        }

        if (chunkSize > maxBodySize - decoded.size())
            return (Request::BODY_TOO_LARGE);

        if (chunkSize > raw.size() - pos)
            return (Request::INCOMPLETE);

        if (raw.size() - pos - chunkSize < 2)
            return (Request::INCOMPLETE);

        decoded.append(raw, pos, chunkSize);
        pos += chunkSize;

        if (raw.compare(pos, 2, "\r\n") != 0)
            return (Request::INVALID);

        pos += 2;
    }
}

Request::Request() : _method(), _target(), _version(), _hasContentLength(false), _isChunked(false), _contentLength(0), _bodyStart(0)
{

}

// Parses the request line and the headers, once the empty line has arrived.
Request::ParseStatus Request::parseHeaders(const std::string& raw)
{
    _method.clear();
    _target.clear();
    _version.clear();
    _body.clear();
    _headers.clear();

    _hasContentLength = false;
    _isChunked = false;
    _contentLength = 0;
    _bodyStart = 0;

    std::string::size_type headersEnd = raw.find("\r\n\r\n");
    std::string::size_type separatorSize = 4;

    std::string::size_type lfHeadersEnd = raw.find("\n\n");

    if (lfHeadersEnd < headersEnd)
    {
        headersEnd = lfHeadersEnd;
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
    std::string::size_type lineEnd = headers.find('\n');
    std::string line = headers.substr(0, lineEnd);

    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    std::string::size_type firstSpace = line.find(' ');

    if (firstSpace == std::string::npos)
        return (INVALID);

    std::string::size_type secondSpace = line.find(' ', firstSpace + 1);

    if (secondSpace == std::string::npos)
        return (INVALID);

    _method = line.substr(0, firstSpace);
    _target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    _version = line.substr(secondSpace + 1);

    if (_method.empty() || _target.empty() || _version.empty()
            || _version.find(' ') != std::string::npos)
        return (INVALID);

    for (std::string::size_type i = 0; i < _method.size(); ++i)
    {
        char c = _method[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*'
            || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~'))
            return (INVALID);
    }

    if (_version != "HTTP/1.0" && _version != "HTTP/1.1")
        return (INVALID);

    std::string::size_type pos = headers.size();

    if (lineEnd != std::string::npos)
        pos = lineEnd + 1;

    bool hasHost = false;

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
            return (INVALID);

        std::string name = header.substr(0, colon);
        std::string value = header.substr(colon + 1);

        if (toLowerCase(name) == "host")
        {
            if (hasHost || value.empty())
                return (INVALID);
            hasHost = true;
        }

        while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
            value.erase(0, 1);

        while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
            value.erase(value.size() - 1);

        // A header sent twice is one header with both values, comma-separated.
        std::string& stored = _headers[toLowerCase(name)];

        stored = stored.empty() ? value : stored + ", " + value;

        if (toLowerCase(name) == "content-length")
        {
            if (_hasContentLength || value.empty())
                return (INVALID);

            for (std::string::size_type i = 0; i < value.size(); ++i)
            {
                if (value[i] < '0' || value[i] > '9')
                    return (INVALID);

                std::string::size_type digit = value[i] - '0';

                if (_contentLength > ((std::string::size_type)-1 - digit) / 10)
                    return (INVALID);

                _contentLength = _contentLength * 10 + digit;
            }

            _hasContentLength = true;
        }
        else if (toLowerCase(name) == "transfer-encoding")
        {
            if (_isChunked || toLowerCase(value) != "chunked")
                return (INVALID);

            _isChunked = true;
        }

        pos = end + 1;
    }

    if (_version == "HTTP/1.1" && !hasHost)
        return (INVALID);
        
    if (_hasContentLength && _isChunked)
        return (INVALID);

    _bodyStart = headersEnd + separatorSize;

    return (COMPLETE);
}

// Reads the body announced by Content-Length or chunked encoding.
Request::ParseStatus Request::parseBody(const std::string& raw, std::string::size_type maxBodySize)
{
    _body.clear();

    if (_hasContentLength)
    {
        if (_contentLength > maxBodySize)
            return (BODY_TOO_LARGE);

        if (raw.size() - _bodyStart < _contentLength)
            return (INCOMPLETE);

        _body = raw.substr(_bodyStart, _contentLength);
        return (COMPLETE);
    }

    if (_isChunked)
    {
        return (decodeChunkedBody(raw, _bodyStart, maxBodySize, _body));
    }

    return (COMPLETE);
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

const std::map<std::string, std::string>& Request::getHeaders() const
{
    return (_headers);
}

bool Request::hasContentLength() const
{
	return (_hasContentLength);
}

// True if the body has a Content-Length or is chunked.
bool Request::hasBodyFraming() const
{
	return (_hasContentLength || _isChunked);
}

