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
#include <map>

// ============================================================================
// Request : une requête HTTP découpée en morceaux utilisables
// (méthode, cible, version, headers, body). Le texte brut reste dans le
// tampon du client (Server::_clients) : Request ne fait que l'analyser.
// ============================================================================
class Request
{
	public:
		// Le résultat d'une analyse. INCOMPLETE n'est pas une erreur : la
		// suite de la requête n'est simplement pas encore arrivée.
		enum ParseStatus
		{
			INCOMPLETE,			// attendre le prochain recv
			COMPLETE,			// on peut traiter la requête
			INVALID,			// -> 400 Bad Request
			BODY_TOO_LARGE,		// -> 413 Content Too Large
			HEADERS_TOO_LARGE	// -> 431 Request Header Fields Too Large
		};

		Request();

		// Ancienne interface "tout en un", déclarée mais jamais définie ni
		// appelée : le serveur utilise parseHeaders puis parseBody.
		ParseStatus parse(const std::string& raw, std::string::size_type maxBodySize);

		// En deux temps : la limite de body dépend de la location, connue
		// seulement après avoir lu l'URL dans les headers.
		ParseStatus parseHeaders(const std::string& raw);
		ParseStatus parseBody(const std::string& raw, std::string::size_type maxBodySize);


		const std::string& getMethod() const;
		const std::string& getTarget() const;	// l'URL avec sa query : "/a.py?x=1"
		const std::string& getVersion() const;
		const std::string& getBody() const;		// déjà dé-chunké
		// Tous les headers, nom en minuscules : "content-type" -> "text/html".
		const std::map<std::string, std::string>& getHeaders() const;

		bool hasContentLength() const;
		bool hasBodyFraming() const;			// Content-Length ou chunked


	private:
		std::string _method;
		std::string _target;
		std::string _version;
		std::string _body;
		std::map<std::string, std::string> _headers;

		bool _hasContentLength;
		bool _isChunked;
		std::string::size_type _contentLength;
		std::string::size_type _bodyStart;		// position du body dans le texte brut

};

// Décode les %xx d'une URL ("%20" -> ' '). Faux si l'encodage est invalide.
// Défini dans handlers.cpp, utilisé aussi par server.cpp.
bool decodePath(const std::string& encoded, std::string& decoded);

#endif
