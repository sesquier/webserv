/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:27:20 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/27 16:02:09 by ale-coss         ###   ########.fr       */
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
			INVALID
		};
		
		Request();

		ParseStatus parse(const std::string& raw);
		
		const std::string& getMethod() const;
		const std::string& getTarget() const;
		const std::string& getVersion() const;
		const std::string& getBody() const;


	private:
		std::string _method;
		std::string _target;
		std::string _version;
		std::string _body;
};


#endif