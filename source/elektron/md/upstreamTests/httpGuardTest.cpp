// isRequestAllowed (httpGuard.h): which Host and Origin values the MCP HTTP server accepts.

#include "httpGuard.h"

#include <iostream>
#include <string>

namespace
{
	constexpr int g_port = 13710;

	int g_failures = 0;

	mcpServer::HttpRequest request(const char* _host, const char* _origin = nullptr)
	{
		mcpServer::HttpRequest r;
		r.method = "POST";
		r.path = "/mcp";
		if(_host)
			r.headers["host"] = _host;
		if(_origin)
			r.headers["origin"] = _origin;
		return r;
	}

	void expect(const bool _allowed, const mcpServer::HttpRequest& _request, const std::string& _what)
	{
		const bool ok = mcpServer::isRequestAllowed(_request, g_port) == _allowed;
		std::cout << (ok ? "ok   " : "FAIL ") << (_allowed ? "allowed: " : "refused: ") << _what << '\n';
		if(!ok)
			++g_failures;
	}
}

int main()
{
	// Local clients outside a browser: a loopback Host with this port, no Origin
	expect(true, request("127.0.0.1:13710"), "Host 127.0.0.1:port");
	expect(true, request("localhost:13710"), "Host localhost:port");
	expect(true, request("LocalHost:13710"), "Host names are case-insensitive");
	expect(true, request("[::1]:13710"), "Host [::1]:port");

	// Host: DNS rebinding sends the attacker's name; anything else is not this server either
	expect(false, request(nullptr), "no Host");
	expect(false, request(""), "an empty Host");
	expect(false, request("evil.example:13710"), "a foreign name (DNS rebinding)");
	expect(false, request("127.0.0.1"), "a loopback Host without the port");
	expect(false, request("127.0.0.1:13711"), "another port");
	expect(false, request("127.0.0.1:13710.evil.example"), "a loopback prefix of a foreign name");
	expect(false, request("127.0.0.2:13710"), "another 127/8 address");
	expect(false, request("192.168.1.10:13710"), "a LAN address");

	// Origin: present only for requests from a browser page; only this server itself may call
	expect(true, request("127.0.0.1:13710", "http://127.0.0.1:13710"), "Origin of this server");
	expect(true, request("localhost:13710", "http://localhost:13710"), "Origin of this server by name");
	expect(false, request("127.0.0.1:13710", "http://evil.example"), "a foreign Origin (cross-site request)");
	expect(false, request("127.0.0.1:13710", "http://localhost:3000"), "another local page");
	expect(false, request("127.0.0.1:13710", "null"), "the opaque Origin of a file or sandboxed page");
	expect(false, request("127.0.0.1:13710", ""), "an empty Origin");
	expect(false, request("127.0.0.1:13710", "https://127.0.0.1:13710"), "another scheme");
	expect(false, request("127.0.0.1:13710", "http://127.0.0.1:13710/"), "an Origin with a path");
	expect(false, request("127.0.0.1:13710", "http://"), "a scheme without a host");

	if(g_failures)
	{
		std::cout << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "mcpHttpGuardTest: all checks passed\n";
	return 0;
}
