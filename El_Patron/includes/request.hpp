/******************************************************************************/
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   request.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ale-coss <ale-coss@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/25 17:27:20 by ale-coss          #+#    #+#             */
/*   Updated: 2026/10/25 17:32:10 by ale-coss         ###   ########.fr       */
/*                                                                            */
/******************************************************************************/

#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>

class Request
{
	public:
		Request();

		bool parse(const std::string& raw);
		
		const std::string& getMethod() const;
		const std::string& getTarget() const;
		const std::string& getVersion() const;

	private:
		std::string _method;
		std::string _target;
		std::string _version;
};


#endif