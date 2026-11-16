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

// ============================================================================
// request.cpp : comprendre ce que le client a envoyé.
//
// Une requête HTTP est du texte :
//     POST /upload/a.txt HTTP/1.1\r\n        <- ligne de requête
//     Host: localhost\r\n                    <- headers
//     Content-Length: 5\r\n
//     \r\n                                   <- ligne vide : fin des headers
//     hello                                  <- body (facultatif)
//
// Comme TCP livre les données par morceaux, chaque fonction peut répondre
// INCOMPLETE ("revenez quand vous aurez la suite"). La boucle rappelle le
// parsing après chaque recv, sur tout ce qui a été reçu depuis le début.
//
// Deux temps, séparés exprès :
//   parseHeaders : la ligne de requête et les headers ;
//   parseBody    : le body, avec la limite de taille de la LOCATION, qu'on
//                  ne connaît qu'après avoir lu l'URL dans les headers.
// ============================================================================

#include "request.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstdio>

// Au-delà de 16 Ko de headers : 431 Request Header Fields Too Large.
// Sans limite, un client pourrait envoyer des headers sans fin et remplir
// la mémoire du serveur.
static const std::string::size_type MAX_HEADERS_SIZE = 16 * 1024;

// Les noms de headers ne tiennent pas compte de la casse : "Content-Length"
// et "content-length" sont le même header. On compare donc en minuscules.
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

// ----------------------------------------------------------------------------
// Le format chunked (Transfer-Encoding: chunked) : le body arrive en morceaux,
// chacun précédé de sa taille en HEXADÉCIMAL :
//     5\r\nhello\r\n      <- un morceau de 5 octets
//     6\r\n world\r\n     <- un morceau de 6 octets
//     0\r\n\r\n           <- taille 0 : fin du body
// Le client l'utilise quand il ne connaît pas la taille totale à l'avance.
// Cette fonction reconstitue le body complet dans "decoded" ("hello world").
// ----------------------------------------------------------------------------
static Request::ParseStatus decodeChunkedBody(const std::string& raw, std::string::size_type bodyStart, std::string::size_type maxBodySize, std::string& decoded)
{
    decoded.clear();

    std::string::size_type pos = bodyStart;

    while (true)
    {
        // La ligne de taille n'est pas encore arrivée en entier.
        std::string::size_type lineEnd = raw.find("\r\n", pos);

        if (lineEnd == std::string::npos)
            return (Request::INCOMPLETE);

        // "5;extension=x" : ce qui suit un ';' est une extension, ignorée.
        std::string sizeLine = raw.substr(pos, lineEnd - pos);
        std::string::size_type semicolon = sizeLine.find(';');

        std::string sizeText = sizeLine.substr(0, semicolon);

        if (sizeText.empty())
            return (Request::INVALID);

        // Conversion hexadécimale à la main, chiffre par chiffre.
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

            // Vérifié AVANT de multiplier : une taille énorme ferait déborder
            // chunkSize et deviendrait petite. (size_type)-1 = valeur maximale.
            if (chunkSize > ((std::string::size_type)-1 - digit) / 16)
                return (Request::INVALID);

            chunkSize = chunkSize * 16 + digit;
            }

        pos = lineEnd + 2;

        // Morceau de taille 0 : c'est la fin. Il peut être suivi de
        // "trailers" (des headers placés après le body), puis d'une ligne vide.
        if (chunkSize == 0)
        {
            // Cas normal : pas de trailer, juste la ligne vide.
            if (raw.compare(pos, 2, "\r\n") == 0)
                return (Request::COMPLETE);

            std::string::size_type trailerEnd = raw.find("\r\n\r\n", pos);

            if (trailerEnd == std::string::npos)
                return (Request::INCOMPLETE);

            // Chaque trailer est vérifié comme un header : "nom: valeur",
            // nom fait de caractères autorisés, valeur sans caractère de contrôle.
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

                // Ces headers-là ne peuvent pas arriver après le body : ils
                // changeraient la façon de lire une requête déjà lue (RFC 9112).
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

        // La limite client_max_body_size s'applique au body RECONSTITUÉ.
        // Écrit comme une soustraction pour ne jamais déborder.
        if (chunkSize > maxBodySize - decoded.size())
            return (Request::BODY_TOO_LARGE);

        // Le morceau, ou le "\r\n" qui le suit, n'est pas encore arrivé.
        if (chunkSize > raw.size() - pos)
            return (Request::INCOMPLETE);

        if (raw.size() - pos - chunkSize < 2)
            return (Request::INCOMPLETE);

        decoded.append(raw, pos, chunkSize);
        pos += chunkSize;

        // Chaque morceau doit se terminer par "\r\n".
        if (raw.compare(pos, 2, "\r\n") != 0)
            return (Request::INVALID);

        pos += 2;
    }
}

Request::Request() : _method(), _target(), _version(), _hasContentLength(false), _isChunked(false), _contentLength(0), _bodyStart(0)
{

}

// ----------------------------------------------------------------------------
// parseHeaders : la ligne de requête et les headers.
// Renvoie INCOMPLETE, COMPLETE, INVALID (-> 400) ou HEADERS_TOO_LARGE (-> 431).
// ----------------------------------------------------------------------------
Request::ParseStatus Request::parseHeaders(const std::string& raw)
{
    // Remise à zéro : l'objet peut être réutilisé.
    _method.clear();
    _target.clear();
    _version.clear();
    _body.clear();
    _headers.clear();

    _hasContentLength = false;
    _isChunked = false;
    _contentLength = 0;
    _bodyStart = 0;

    // La fin des headers : une ligne vide. La norme dit "\r\n\r\n", mais on
    // tolère aussi "\n\n" (certains outils, comme nc, n'envoient que des \n).
    // On garde le séparateur trouvé le plus tôt.
    std::string::size_type headersEnd = raw.find("\r\n\r\n");
    std::string::size_type separatorSize = 4;

    std::string::size_type lfHeadersEnd = raw.find("\n\n");

    if (lfHeadersEnd < headersEnd)
    {
        headersEnd = lfHeadersEnd;
        separatorSize = 2;
    }

    // Pas encore de ligne vide : on attend la suite, sauf si on a déjà reçu
    // plus que la limite.
    if (headersEnd == std::string::npos)
    {
        if (raw.size() >= MAX_HEADERS_SIZE)
            return (HEADERS_TOO_LARGE);

        return (INCOMPLETE);
    }

    if (headersEnd > MAX_HEADERS_SIZE)
        return (HEADERS_TOO_LARGE);

    // La ligne de requête : "MÉTHODE CIBLE VERSION", séparés par un espace.
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

    // Exactement 3 morceaux, aucun vide.
    if (_method.empty() || _target.empty() || _version.empty()
            || _version.find(' ') != std::string::npos)
        return (INVALID);

    // Une méthode est un "token" HTTP : lettres, chiffres et quelques
    // symboles. Une méthode inconnue mais bien formée (FOO) passe ici et
    // recevra 501 plus tard ; une méthode mal formée est une requête invalide.
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

    // Les headers, une ligne à la fois : "Nom: valeur".
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

        // Un seul Host, et non vide.
        if (toLowerCase(name) == "host")
        {
            if (hasHost || value.empty())
                return (INVALID);
            hasHost = true;
        }

        // On retire les espaces et tabulations autour de la valeur.
        while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
            value.erase(0, 1);

        while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
            value.erase(value.size() - 1);

        // Tous les headers sont gardés (nom en minuscules) : le CGI doit
        // recevoir toute la requête (Cookie -> HTTP_COOKIE...). Un header
        // répété devient un seul header aux valeurs séparées par ", ",
        // comme le prévoit HTTP.
        std::string& stored = _headers[toLowerCase(name)];

        stored = stored.empty() ? value : stored + ", " + value;

        // Content-Length : uniquement des chiffres, une seule fois, sans
        // débordement. Deux Content-Length différents permettraient à deux
        // serveurs de lire la requête différemment ("request smuggling").
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
        // Le seul encodage de transfert géré : chunked.
        else if (toLowerCase(name) == "transfer-encoding")
        {
            if (_isChunked || toLowerCase(value) != "chunked")
                return (INVALID);

            _isChunked = true;
        }

        pos = end + 1;
    }

    // HTTP/1.1 exige un Host. HTTP/1.0 non.
    if (_version == "HTTP/1.1" && !hasHost)
        return (INVALID);

    // Les deux à la fois : ambigu, refusé (même raison que plus haut).
    if (_hasContentLength && _isChunked)
        return (INVALID);

    // Le body commence juste après la ligne vide.
    _bodyStart = headersEnd + separatorSize;

    return (COMPLETE);
}

// ----------------------------------------------------------------------------
// parseBody : le body, avec la limite de la location.
// Avec Content-Length, la limite est vérifiée AVANT d'avoir reçu le body :
// on répond 413 tout de suite, sans lire des mégaoctets pour rien.
// Sans Content-Length ni chunked : pas de body (GET, DELETE).
// ----------------------------------------------------------------------------
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

// Accesseurs : renvoient des références const, sans copie, en lecture seule.
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

// Le client a-t-il dit comment trouver la fin de son body ? Un POST sans
// Content-Length ni chunked reçoit 411 Length Required.
bool Request::hasBodyFraming() const
{
	return (_hasContentLength || _isChunked);
}
