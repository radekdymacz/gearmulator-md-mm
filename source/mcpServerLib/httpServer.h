#pragma once

#include "httpRequest.h"
#include "httpResponse.h"

#include "networkLib/networkThread.h"
#include "networkLib/stream.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace networkLib
{
	class TcpServer;
	class TcpStream;
}

namespace mcpServer
{
	// Listens on 127.0.0.1 only. Requests whose Host or Origin is not this server are refused (httpGuard.h).
	class HttpServer
	{
	public:
		// Bounds for what a client may send or hold: requests come from the network, so none of these is
		// left to the peer.
		static constexpr size_t g_maxClients = 32;
		static constexpr size_t g_maxLineLength = 8 * 1024;			// request line and each header line
		static constexpr size_t g_maxHeaderCount = 64;
		static constexpr size_t g_maxBodySize = 4 * 1024 * 1024;
		static constexpr uint32_t g_idleReadTimeoutMs = 60 * 1000;	// a client that sends nothing for this long is dropped

		using RequestHandler = std::function<HttpResponse(const HttpRequest&, networkLib::Stream&)>;

		// _idleReadTimeoutMs: g_idleReadTimeoutMs but in tests
		HttpServer(int _port, RequestHandler _handler, uint32_t _idleReadTimeoutMs = g_idleReadTimeoutMs);
		// Returns once every client thread has finished: blocked reads and writes are interrupted. A handler
		// that waits for something else (an SSE stream) must be woken by its owner first.
		~HttpServer();

		int getPort() const { return m_port; }
		bool isRunning() const;

	private:
		struct Client
		{
			std::unique_ptr<networkLib::TcpStream> stream;
			std::mutex streamMutex;	// the client thread's close() against the destructor's interrupt()
			std::thread thread;
			std::atomic<bool> done{false};
		};

		void onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream);
		void handleClient(networkLib::TcpStream& _stream);
		void reapFinishedClients();

		static bool parseRequest(HttpRequest& _request, networkLib::Stream& _stream);
		static bool readLine(std::string& _line, networkLib::Stream& _stream);
		static bool sendResponse(const HttpResponse& _response, networkLib::Stream& _stream);

		const int m_port;
		const uint32_t m_idleReadTimeoutMs;
		RequestHandler m_handler;
		std::unique_ptr<networkLib::TcpServer> m_tcpServer;

		std::mutex m_clientsMutex;
		std::vector<std::unique_ptr<Client>> m_clients;
	};
}
