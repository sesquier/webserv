/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:34:25 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/27 14:18:26 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#include "request.hpp"
#include <stdexcept>
#include <iostream>

Request::Request() : _method(), _target(), _version()
{

}

bool Request::parse(const std::string& raw)
{
    // Accept CRLF CRLF or LF LF as the end of headers.
    std::string::size_type headersEnd =
        raw.find("\r\n\r\n");

    if (headersEnd == std::string::npos)
        headersEnd = raw.find("\n\n");

    if (headersEnd == std::string::npos)
        return false;

    // Locate the end of the request line.
    std::string::size_type lineEnd = raw.find('\n');

    if (lineEnd == std::string::npos || lineEnd > headersEnd)
        throw std::runtime_error("Invalid HTTP request line");

    std::string line = raw.substr(0, lineEnd);

    // Remove a trailing carriage return if present.
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    std::string::size_type firstSpace = line.find(' ');
    if (firstSpace == std::string::npos)
        throw std::runtime_error("Invalid HTTP request line");

    std::string::size_type secondSpace =
        line.find(' ', firstSpace + 1);

    if (secondSpace == std::string::npos)
        throw std::runtime_error("Invalid HTTP request line");

    _method = line.substr(0, firstSpace);
    _target = line.substr(
        firstSpace + 1,
        secondSpace - firstSpace - 1
    );
    _version = line.substr(secondSpace + 1);

    if (_method.empty() || _target.empty() || _version.empty())
        throw std::runtime_error("Invalid HTTP request line");

    if (_version.find(' ') != std::string::npos)
        throw std::runtime_error("Invalid HTTP request line");

    return true;
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



