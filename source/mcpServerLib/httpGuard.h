#pragma once

#include "httpRequest.h"

#include <cctype>
#include <string>

namespace mcpServer
{
	// The MCP server listens on 127.0.0.1 only, but a web page in the user's browser can reach that address
	// too: by a cross-site request, which carries the page's Origin, or by DNS rebinding, where a name the page
	// controls resolves to 127.0.0.1 and travels in Host. A request is allowed only when Host names this server
	// by a loopback address and port and an Origin, if one was sent, is this server itself (no page is served
	// here, so no other origin has a reason to call). Clients outside a browser send no Origin.

	inline bool isLoopbackAuthority(const std::string& _authority, const int _port)
	{
		std::string authority = _authority;
		for (auto& c : authority)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		const auto port = ":" + std::to_string(_port);
		return authority == "127.0.0.1" + port || authority == "localhost" + port || authority == "[::1]" + port;
	}

	inline bool isRequestAllowed(const HttpRequest& _request, const int _port)
	{
		if (!isLoopbackAuthority(_request.getHeader("host"), _port))
			return false;

		const auto origin = _request.headers.find("origin");
		if (origin == _request.headers.end())
			return true;

		const std::string scheme = "http://";
		const auto& value = origin->second;
		if (value.size() <= scheme.size() || value.compare(0, scheme.size(), scheme) != 0)
			return false;
		return isLoopbackAuthority(value.substr(scheme.size()), _port);
	}
}
