// mcpServer::McpServer (mcpServer.cpp) over a loopback socket (codex review 2026-10, items 11 and 12): stop() returns
// promptly with an SSE client connected (the SSE wait is woken, not slept out), a slow tool does not hold up another
// client's tools/list (handlers run outside the tools mutex), and send_note's duration is clamped to its schema's
// range (McpPluginServer::noteDurationMs). Needs juce_core (the JSON values), not the plug-in.

#include "loopbackClient.h"

#include "mcpServerLib/mcpPluginServer.h"
#include "mcpServerLib/mcpServer.h"

#include "networkLib/logging.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace loopbackClient;

	int g_failures = 0;
	constexpr int g_firstPort = 48300;	// McpServer::start tries the next ports when one is taken

	void check(const bool _ok, const std::string& _what)
	{
		std::cout << (_ok ? "ok   " : "FAIL ") << _what << '\n';
		if(!_ok)
			++g_failures;
	}

	long long millisecondsSince(const Clock::time_point _start)
	{
		return std::chrono::duration_cast<Milliseconds>(Clock::now() - _start).count();
	}

	void testStopWakesSse()
	{
		mcpServer::McpServer server(g_firstPort);
		if(!server.start())
		{
			check(false, "sse: the server starts");
			return;
		}
		const auto port = server.getPort();

		auto c = connectLoopback(port);
		std::string events;
		const bool sent = c && send(*c, "GET /sse HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port)
			+ "\r\nAccept: text/event-stream\r\n\r\n");
		if(sent)
			receive(*c, Milliseconds(2000), events, [](const std::string& _d) { return _d.find("data: /message") != std::string::npos; });
		check(events.find("event: endpoint") != std::string::npos, "sse: the stream is open (its endpoint event came)");

		// The handler now waits up to 15 s for its next keep-alive: stop() must wake it, not wait it out. 2 s, not
		// less: the accept thread (networkLib::TcpServer) looks for its stop once a second.
		const Watchdog watchdog(Milliseconds(10000), "stop with an SSE client connected");
		const auto start = Clock::now();
		server.stop();
		const auto elapsed = millisecondsSince(start);
		check(elapsed < 2000, "sse: stop() returns within 2 s with an SSE client connected (" + std::to_string(elapsed) + " ms)");
		check(c && waitClosed(*c, Milliseconds(2000)), "sse: and the client sees its stream end");
	}

	void testSlowToolDoesNotBlockToolsList()
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool started = false;
		bool release = false;

		mcpServer::McpServer server(g_firstPort);
		mcpServer::ToolDef slow;
		slow.name = "slow";
		slow.description = "Waits until the test lets it finish";
		slow.handler = [&](const mcpServer::JsonValue&)
		{
			std::unique_lock lock(mutex);
			started = true;
			condition.notify_all();
			condition.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
			return mcpServer::JsonValue::object();
		};
		server.registerTool(std::move(slow));
		if(!server.start())
		{
			check(false, "tools: the server starts");
			return;
		}
		const auto port = server.getPort();
		const Watchdog watchdog(Milliseconds(15000), "a tools/list while another client's tool runs");

		std::string slowResponse;
		std::thread slowClient([&]
		{
			auto c = connectLoopback(port);
			if(c && send(*c, postText(port, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"slow","arguments":{}}})")))
				slowResponse = readResponse(*c, Milliseconds(8000));
		});
		{
			std::unique_lock lock(mutex);
			condition.wait_for(lock, std::chrono::seconds(3), [&] { return started; });
		}
		check(started, "tools: the slow tool runs");

		// Another client, while the slow tool still runs: its tools/list must not wait for it
		const auto start = Clock::now();
		auto c = connectLoopback(port);
		const auto response = c && send(*c, postText(port, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"))
			? readResponse(*c, Milliseconds(2000)) : std::string();
		const auto elapsed = millisecondsSince(start);
		check(isStatus(response, 200) && response.find("\"slow\"") != std::string::npos && elapsed < 1000,
			"tools: tools/list is answered while another client's tool runs (" + std::to_string(elapsed) + " ms)");

		{
			std::lock_guard lock(mutex);
			release = true;
		}
		condition.notify_all();
		slowClient.join();
		check(isStatus(slowResponse, 200) && slowResponse.find("\"result\"") != std::string::npos,
			"tools: the slow tool's call is answered once it finishes");
		server.stop();
	}

	void testNoteDurationClamp()
	{
		using Plugin = mcpServer::McpPluginServer;
		const auto duration = [](const char* _json)
		{
			return Plugin::noteDurationMs(mcpServer::JsonValue::parse(_json));
		};
		check(duration("{}") == 500, "send_note: 500 ms when no duration is given");
		check(duration(R"({"duration_ms":250})") == 250, "send_note: a duration in range is kept");
		check(duration(R"({"duration_ms":2147483647})") == Plugin::g_noteDurationMaxMs
			&& duration(R"({"duration_ms":1e300})") == Plugin::g_noteDurationMaxMs,
			"send_note: a duration over 10 s is 10 s (it holds a client thread and the shutdown)");
		check(duration(R"({"duration_ms":0})") == Plugin::g_noteDurationMinMs
			&& duration(R"({"duration_ms":-5})") == Plugin::g_noteDurationMinMs
			&& duration(R"({"duration_ms":"x"})") == Plugin::g_noteDurationMinMs,
			"send_note: zero, negative or not a number is 1 ms");
	}
}

int main(const int _argc, char* _argv[])
{
	networkLib::setLogFunc([](const networkLib::LogLevel _level, const char*, int, const std::string& _message)
	{
		if(_level >= networkLib::LogLevel::Error)
			std::cout << "     log: " << _message << '\n';
	});

	const std::vector<std::string> only(_argv + 1, _argv + _argc);
	const auto run = [&only](const std::string& _name, void (*_case)())
	{
		if(only.empty() || std::find(only.begin(), only.end(), _name) != only.end())
			_case();
	};
	run("sse", testStopWakesSse);
	run("tools", testSlowToolDoesNotBlockToolsList);
	run("note", testNoteDurationClamp);

	if(g_failures)
	{
		std::cout << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "mcpServerTest: all checks passed\n";
	return 0;
}
