/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:27:20 by ale-coss          #+#    #+#             */
/*   Updated: 2026/11/07 18:56:05 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>

class Request
{
	public:
		enum ParseStatus
		{
			INCOMPLETE,
			COMPLETE,
			INVALID,
			BODY_TOO_LARGE,
			HEADERS_TOO_LARGE
		};
		
		Request();

		ParseStatus parse(const std::string& raw, std::string::size_type maxBodySize);
		
		ParseStatus parseHeaders(const std::string& raw);
		ParseStatus parseBody(const std::string& raw, std::string::size_type maxBodySize);


		const std::string& getMethod() const;
		const std::string& getTarget() const;
		const std::string& getVersion() const;
		const std::string& getBody() const;

		bool hasContentLength() const;
		bool hasBodyFraming() const;


	private:
		std::string _method;
		std::string _target;
		std::string _version;
		std::string _body;

		bool _hasContentLength;
		bool _isChunked;
		std::string::size_type _contentLength;
		std::string::size_type _bodyStart;

};

bool decodePath(const std::string& encoded, std::string& decoded);

#endif