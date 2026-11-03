#include "config/ServerConfig.hpp"

ListenAddress::ListenAddress()
	: port(0)
{
}

// Only initializes the plain types; real defaults are set after parsing.
ServerConfig::ServerConfig()
	: maxBodySize(BODY_SIZE_UNSET)
{
}
