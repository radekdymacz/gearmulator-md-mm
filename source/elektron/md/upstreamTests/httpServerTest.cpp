// The MCP HTTP server (httpServer.cpp) over real sockets: it listens on loopback only, refuses requests a browser
// page could make, bounds what a client may send or hold, and shuts down without waiting for its clients.

#include "httpServer.h"

#include "networkLib/logging.h"

#include "ptypes/pinet.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#	include <mach/mach.h>
#endif
#ifndef _WIN32
#	include <arpa/inet.h>
#	include <cerrno>
#	include <fcntl.h>
#	include <ifaddrs.h>
#	include <net/if.h>
#	include <netinet/in.h>
#	include <poll.h>
#	include <sys/socket.h>
#	include <unistd.h>
#endif

namespace
{
	using Clock = std::chrono::steady_clock;
	using Milliseconds = std::chrono::milliseconds;
	using ClientStream = std::unique_ptr<ptypes::ipstream>;

	int g_failures = 0;
	int g_nextPort = 47100;

	void check(const bool _ok, const std::string& _what)
	{
		std::cout << (_ok ? "ok   " : "FAIL ") << _what << '\n';
		if(!_ok)
			++g_failures;
	}

	// Ends the test when a step does not finish in time, instead of hanging until ctest gives up
	class Watchdog
	{
	public:
		Watchdog(const Milliseconds _limit, std::string _what) : m_what(std::move(_what))
		{
			m_thread = std::thread([this, _limit]
			{
				std::unique_lock lock(m_mutex);
				if(m_cv.wait_for(lock, _limit, [this] { return m_done; }))
					return;
				std::cout << "FAIL " << m_what << ": did not finish within " << _limit.count() << " ms" << std::endl;
				std::_Exit(1);
			});
		}

		~Watchdog()
		{
			{
				std::lock_guard lock(m_mutex);
				m_done = true;
			}
			m_cv.notify_all();
			m_thread.join();
		}

	private:
		std::string m_what;
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_done = false;
		std::thread m_thread;
	};

	struct Server
	{
		std::unique_ptr<mcpServer::HttpServer> http;
		int port = 0;
		std::atomic<int> handled{0};
	};

	// A server on a free port whose handler answers every request with 200 and an empty JSON object
	std::unique_ptr<Server> startServer()
	{
		auto server = std::make_unique<Server>();
		auto* s = server.get();
		for(; g_nextPort < 48100 && !server->http; ++g_nextPort)
		{
			try
			{
				server->http = std::make_unique<mcpServer::HttpServer>(g_nextPort, [s](const mcpServer::HttpRequest&, networkLib::Stream&)
				{
					++s->handled;
					mcpServer::HttpResponse response;
					response.setJsonBody("{}");
					return response;
				});
				server->port = g_nextPort;
			}
			catch(const std::exception&)
			{
			}
		}
		return server->http ? std::move(server) : nullptr;
	}

	ClientStream connect(const ptypes::ipaddress& _ip, const int _port)
	{
		auto stream = std::make_unique<ptypes::ipstream>(_ip, _port);
		try
		{
			stream->open();
			return stream;
		}
		catch(ptypes::exception* e)
		{
			delete e;
			return nullptr;
		}
	}

	ClientStream connectLoopback(const int _port)
	{
		return connect(ptypes::ipaddress(127, 0, 0, 1), _port);
	}

	bool send(ptypes::ipstream& _s, const std::string& _data)
	{
		try
		{
			_s.write(_data.data(), static_cast<int>(_data.size()));
			_s.flush();
			return true;
		}
		catch(ptypes::exception* e)
		{
			delete e;
			return false;
		}
	}

	std::string requestText(const int _port, const std::string& _extraHeaders = {}, const std::string& _host = {})
	{
		const auto host = _host.empty() ? "127.0.0.1:" + std::to_string(_port) : _host;
		return "POST /mcp HTTP/1.1\r\nHost: " + host + "\r\n" + _extraHeaders + "Content-Length: 2\r\n\r\n{}";
	}

	// Collects what arrives until _done says the data is complete, the server closes the connection or the
	// time is up. Returns true when the connection was closed by the server.
	bool receive(ptypes::ipstream& _s, const Milliseconds _timeout, std::string& _data, const std::function<bool(const std::string&)>& _done)
	{
		const auto deadline = Clock::now() + _timeout;
		try
		{
			while(!_done(_data))
			{
				// A timeout of 0 still polls once
				const auto remaining = std::max<long long>(0, std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()).count());
				if(!_s.waitfor(static_cast<int>(remaining)))
					return false;
				if(_s.get_eof())
					return true;
				std::string chunk(static_cast<size_t>(_s.get_dataavail()), '\0');
				_s.read(chunk.data(), static_cast<int>(chunk.size()));
				_data += chunk;
			}
			return false;
		}
		catch(ptypes::exception* e)
		{
			// Reset by the server: closed as well
			delete e;
			return true;
		}
	}

	bool waitClosed(ptypes::ipstream& _s, const Milliseconds _timeout)
	{
		std::string ignored;
		return receive(_s, _timeout, ignored, [](const std::string&) { return false; });
	}

	// One complete response (headers and a Content-Length body), or "" when none arrives in time
	std::string readResponse(ptypes::ipstream& _s, const Milliseconds _timeout)
	{
		std::string data;
		const auto complete = [](const std::string& _d)
		{
			const auto headerEnd = _d.find("\r\n\r\n");
			if(headerEnd == std::string::npos)
				return false;
			size_t length = 0;
			const auto pos = _d.find("Content-Length: ");
			if(pos != std::string::npos && pos < headerEnd)
				length = std::strtoul(_d.c_str() + pos + 16, nullptr, 10);
			return _d.size() >= headerEnd + 4 + length;
		};
		receive(_s, _timeout, data, complete);
		return complete(data) ? data : std::string();
	}

	bool isStatus(const std::string& _response, const int _status)
	{
		return _response.rfind("HTTP/1.1 " + std::to_string(_status) + ' ', 0) == 0;
	}

	// Live threads of this process, -1 where this test cannot count them
	int countThreads()
	{
#if defined(__APPLE__)
		thread_act_array_t threads = nullptr;
		mach_msg_type_number_t count = 0;
		if(task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
			return -1;
		for(mach_msg_type_number_t i = 0; i < count; ++i)
			mach_port_deallocate(mach_task_self(), threads[i]);
		vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_act_t));
		return static_cast<int>(count);
#elif defined(__linux__)
		std::ifstream status("/proc/self/status");
		std::string line;
		while(std::getline(status, line))
		{
			if(line.rfind("Threads:", 0) == 0)
				return std::atoi(line.c_str() + 8);
		}
		return -1;
#else
		return -1;
#endif
	}

#ifndef _WIN32
	// This machine's IPv4 addresses other than loopback, in network byte order
	std::vector<in_addr_t> nonLoopbackAddresses()
	{
		std::vector<in_addr_t> result;
		ifaddrs* list = nullptr;
		if(getifaddrs(&list) != 0)
			return result;
		for(const auto* a = list; a; a = a->ifa_next)
		{
			if(!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
				continue;
			if((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_RUNNING))
				continue;
			const auto address = reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr;
			if((ntohl(address) >> 24) != 127)
				result.push_back(address);
		}
		freeifaddrs(list);
		return result;
	}

	sockaddr_in socketAddress(const in_addr_t _address, const int _port)
	{
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons(static_cast<uint16_t>(_port));
		a.sin_addr.s_addr = _address;
		return a;
	}

	// Whether another socket can take _address:_port. A server listening on every interface holds it, so the
	// bind fails; a server on 127.0.0.1 alone leaves it free. A local check: no packet has to pass a firewall.
	bool canBind(const in_addr_t _address, const int _port)
	{
		const int s = ::socket(AF_INET, SOCK_STREAM, 0);
		if(s < 0)
			return false;
		const auto a = socketAddress(_address, _port);
		const bool bound = ::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
		::close(s);
		return bound;
	}

	// Whether a client connects to _address:_port within _timeout. Refused and timed out (a firewall that
	// drops) both count as not reached.
	bool reaches(const in_addr_t _address, const int _port, const Milliseconds _timeout)
	{
		const int s = ::socket(AF_INET, SOCK_STREAM, 0);
		if(s < 0)
			return false;
		::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
		const auto a = socketAddress(_address, _port);
		bool connected = ::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
		if(!connected && errno == EINPROGRESS)
		{
			pollfd p{s, POLLOUT, 0};
			if(::poll(&p, 1, static_cast<int>(_timeout.count())) == 1)
			{
				int error = 0;
				socklen_t length = sizeof(error);
				connected = ::getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
			}
		}
		::close(s);
		return connected;
	}
#endif

	void testLoopbackOnly()
	{
		const Watchdog watchdog(Milliseconds(20000), "loopback bind");
		auto server = startServer();
		if(!server)
		{
			check(false, "bind: a free port");
			return;
		}

		auto local = connectLoopback(server->port);
		check(local != nullptr, "bind: a client on 127.0.0.1 connects");

#ifdef _WIN32
		std::cout << "skip bind: the interface list is read on macOS and Linux only\n";
#else
		const auto addresses = nonLoopbackAddresses();
		if(addresses.empty())
		{
			std::cout << "skip bind: no non-loopback IPv4 interface to try\n";
			return;
		}
		for(const auto address : addresses)
		{
			char name[INET_ADDRSTRLEN] = {};
			::inet_ntop(AF_INET, &address, name, sizeof(name));
			check(canBind(address, server->port), std::string("bind: the server does not hold ") + name + ':' + std::to_string(server->port));
			check(!reaches(address, server->port, Milliseconds(1000)), std::string("bind: a client on ") + name + " does not reach it");
		}
#endif
	}

	void testHostAndOrigin()
	{
		const Watchdog watchdog(Milliseconds(20000), "Host and Origin");
		auto server = startServer();
		if(!server)
		{
			check(false, "guard: a free port");
			return;
		}

		{
			auto c = connectLoopback(server->port);
			const auto response = c && send(*c, requestText(server->port)) ? readResponse(*c, Milliseconds(2000)) : std::string();
			check(isStatus(response, 200) && server->handled == 1, "guard: a loopback Host is served");
		}
		{
			auto c = connectLoopback(server->port);
			const auto response = c && send(*c, requestText(server->port, {}, "evil.example:" + std::to_string(server->port))) ? readResponse(*c, Milliseconds(2000)) : std::string();
			check(isStatus(response, 403) && server->handled == 1, "guard: a foreign Host (DNS rebinding) gets 403, the handler is not called");
			check(c && waitClosed(*c, Milliseconds(2000)), "guard: and the connection is closed");
		}
		{
			auto c = connectLoopback(server->port);
			const auto response = c && send(*c, requestText(server->port, "Origin: http://evil.example\r\n")) ? readResponse(*c, Milliseconds(2000)) : std::string();
			check(isStatus(response, 403) && server->handled == 1, "guard: a foreign Origin (cross-site request) gets 403, the handler is not called");
		}
	}

	void testShutdownWithIdleClient()
	{
		auto server = startServer();
		if(!server)
		{
			check(false, "shutdown: a free port");
			return;
		}

		// A keep-alive client that got its answer and now sends nothing: its server thread waits in read()
		auto c = connectLoopback(server->port);
		const auto response = c && send(*c, requestText(server->port)) ? readResponse(*c, Milliseconds(2000)) : std::string();
		check(isStatus(response, 200), "shutdown: the idle client was served once");

		const Watchdog watchdog(Milliseconds(5000), "shutdown with an idle keep-alive client");
		const auto start = Clock::now();
		server->http.reset();
		const auto elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start).count();
		check(elapsed < 2000, "shutdown: the server stops within 2 s with an idle client connected (" + std::to_string(elapsed) + " ms)");
		check(c && waitClosed(*c, Milliseconds(2000)), "shutdown: and the client sees its connection closed");
	}

	void testClientLimitAndReaping()
	{
		const Watchdog watchdog(Milliseconds(60000), "client limit and reaping");
		const auto threadsBefore = countThreads();

		auto server = startServer();
		if(!server)
		{
			check(false, "limit: a free port");
			return;
		}

		// More idle clients than the server serves at once: the extra ones are closed at once
		constexpr size_t extra = 8;
		std::vector<ClientStream> clients;
		for(size_t i = 0; i < mcpServer::HttpServer::g_maxClients + extra; ++i)
		{
			if(auto c = connectLoopback(server->port))
				clients.push_back(std::move(c));
		}
		check(clients.size() == mcpServer::HttpServer::g_maxClients + extra, "limit: every client connects at the TCP level");

		// Poll every client until the expected number is closed, then once more to see that no further one is
		std::vector<bool> isClosed(clients.size(), false);
		size_t closed = 0;
		const auto pollClosed = [&]
		{
			for(size_t i = 0; i < clients.size(); ++i)
			{
				if(!isClosed[i] && waitClosed(*clients[i], Milliseconds(0)))
				{
					isClosed[i] = true;
					++closed;
				}
			}
		};
		const auto deadline = Clock::now() + Milliseconds(3000);
		while(closed < extra && Clock::now() < deadline)
		{
			pollClosed();
			std::this_thread::sleep_for(Milliseconds(20));
		}
		std::this_thread::sleep_for(Milliseconds(200));
		pollClosed();
		check(closed == extra, "limit: exactly the clients over g_maxClients are closed (" + std::to_string(closed) + ", expected " + std::to_string(extra) + ")");

		const auto threadsBusy = countThreads();
		if(threadsBefore >= 0 && threadsBusy >= 0)
		{
			// One accept thread and a thread per served client
			const auto added = threadsBusy - threadsBefore;
			check(added <= static_cast<int>(mcpServer::HttpServer::g_maxClients) + 1, "limit: threads stay bounded (" + std::to_string(added) + " added)");
		}
		else
		{
			std::cout << "skip limit: cannot count threads here\n";
		}

		clients.clear();

		// Many short connections in a row: finished clients are reaped, so they never use up the limit
		size_t served = 0;
		for(int i = 0; i < 200; ++i)
		{
			auto c = connectLoopback(server->port);
			if(c && send(*c, requestText(server->port)) && isStatus(readResponse(*c, Milliseconds(2000)), 200))
				++served;
		}
		check(served == 200, "reaping: 200 connect/request/close cycles are all served (" + std::to_string(served) + ")");

		std::this_thread::sleep_for(Milliseconds(200));
		{
			auto c = connectLoopback(server->port);
			const auto response = c && send(*c, requestText(server->port)) ? readResponse(*c, Milliseconds(2000)) : std::string();
			check(isStatus(response, 200), "reaping: a client after them is still served");
		}

		const auto threadsAfter = countThreads();
		if(threadsBefore >= 0 && threadsAfter >= 0)
			check(threadsAfter - threadsBefore <= 4, "reaping: the threads of finished clients are gone (" + std::to_string(threadsAfter - threadsBefore) + " left)");
	}

	void testOversizedInput()
	{
		const Watchdog watchdog(Milliseconds(30000), "oversized input");
		auto server = startServer();
		if(!server)
		{
			check(false, "input: a free port");
			return;
		}

		{
			// 8 MiB of request line without a line end
			auto c = connectLoopback(server->port);
			bool writeFailed = !c || !send(*c, "GET /");
			const std::string chunk(64 * 1024, 'a');
			for(size_t sent = 0; !writeFailed && sent < 8 * 1024 * 1024; sent += chunk.size())
				writeFailed = !send(*c, chunk);
			check(c && (writeFailed || waitClosed(*c, Milliseconds(2000))), "input: a line of 8 MiB without a line end is dropped");
		}
		{
			const auto start = Clock::now();
			auto c = connectLoopback(server->port);
			const bool sent = c && send(*c, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(server->port) + "\r\nContent-Length: 2000000000\r\n\r\n");
			const bool closed = sent && waitClosed(*c, Milliseconds(1000));
			const auto elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start).count();
			check(closed, "input: Content-Length 2000000000 is refused at once (" + std::to_string(elapsed) + " ms)");
		}
		for(const char* value : {"abc", "-1", "12abc", "99999999999999999999999999"})
		{
			auto c = connectLoopback(server->port);
			const bool sent = c && send(*c, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(server->port) + "\r\nContent-Length: " + value + "\r\n\r\n{}");
			check(sent && waitClosed(*c, Milliseconds(1000)), std::string("input: Content-Length '") + value + "' is refused");
		}
		{
			std::string headers;
			for(size_t i = 0; i <= mcpServer::HttpServer::g_maxHeaderCount; ++i)
				headers += "X-Header-" + std::to_string(i) + ": x\r\n";
			auto c = connectLoopback(server->port);
			const bool sent = c && send(*c, requestText(server->port, headers));
			check(sent && waitClosed(*c, Milliseconds(1000)), "input: more than g_maxHeaderCount headers are refused");
		}
		check(server->handled == 0, "input: no oversized request reached the handler");

		{
			auto c = connectLoopback(server->port);
			const auto response = c && send(*c, requestText(server->port)) ? readResponse(*c, Milliseconds(2000)) : std::string();
			check(isStatus(response, 200) && server->handled == 1, "input: the server still serves a well-formed request");
		}
	}
}

int main(const int _argc, char* _argv[])
{
	networkLib::setLogFunc([](const networkLib::LogLevel _level, const char*, int, const std::string& _message)
	{
		if(_level >= networkLib::LogLevel::Error)
			std::cout << "     log: " << _message << '\n';
	});

	// Optional arguments: the cases to run (bind, guard, shutdown, limit, input); all of them by default
	const std::vector<std::string> only(_argv + 1, _argv + _argc);
	const auto run = [&only](const std::string& _name, void (*_case)())
	{
		if(only.empty() || std::find(only.begin(), only.end(), _name) != only.end())
			_case();
	};

	run("bind", testLoopbackOnly);
	run("guard", testHostAndOrigin);
	run("shutdown", testShutdownWithIdleClient);
	run("limit", testClientLimitAndReaping);
	run("input", testOversizedInput);

	if(g_failures)
	{
		std::cout << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "mcpHttpServerTest: all checks passed\n";
	return 0;
}
