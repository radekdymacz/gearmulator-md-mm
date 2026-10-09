#include "httpServer.h"

#include "httpGuard.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"
#include "networkLib/tcpServer.h"
#include "networkLib/tcpStream.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <system_error>

namespace mcpServer
{
	namespace
	{
		std::string trimWhitespace(const std::string& _s)
		{
			const auto first = _s.find_first_not_of(" \t");
			if (first == std::string::npos)
				return {};
			const auto last = _s.find_last_not_of(" \t");
			return _s.substr(first, last - first + 1);
		}

		// Content-Length is decimal digits only. Anything else (a sign, garbage, a number too big to hold) is
		// rejected without throwing and before any allocation is sized from it.
		bool parseContentLength(const std::string& _value, size_t& _length)
		{
			if (_value.empty())
				return false;

			size_t length = 0;
			for (const char c : _value)
			{
				if (c < '0' || c > '9')
					return false;
				const auto digit = static_cast<size_t>(c - '0');
				if (length > (std::numeric_limits<size_t>::max() - digit) / 10)
					return false;
				length = length * 10 + digit;
			}
			_length = length;
			return true;
		}
	}

	HttpServer::HttpServer(const int _port, RequestHandler _handler, const uint32_t _idleReadTimeoutMs)
		: m_port(_port)
		, m_idleReadTimeoutMs(_idleReadTimeoutMs)
		, m_handler(std::move(_handler))
	{
		m_tcpServer = std::make_unique<networkLib::TcpServer>([this](std::unique_ptr<networkLib::TcpStream> _stream)
		{
			onClientConnected(std::move(_stream));
		}, _port, networkLib::BindScope::Loopback);

		LOGNET(networkLib::LogLevel::Info, "MCP HTTP server started on 127.0.0.1:" << _port);
	}

	HttpServer::~HttpServer()
	{
		// No new clients after this: the accept thread has been joined
		m_tcpServer.reset();

		std::vector<std::unique_ptr<Client>> clients;
		{
			std::lock_guard lock(m_clientsMutex);
			clients.swap(m_clients);
		}

		// A client thread blocks reading its socket (an idle keep-alive connection) or writing to it. Joining
		// it as it is would wait for the peer; interrupting the socket returns both at once.
		for (const auto& client : clients)
		{
			std::lock_guard streamLock(client->streamMutex);
			client->stream->interrupt();
		}

		for (const auto& client : clients)
		{
			if (client->thread.joinable())
				client->thread.join();
		}
	}

	bool HttpServer::isRunning() const
	{
		return m_tcpServer != nullptr;
	}

	void HttpServer::onClientConnected(std::unique_ptr<networkLib::TcpStream> _stream)
	{
		LOGNET(networkLib::LogLevel::Info, "New TCP connection accepted");

		std::lock_guard lock(m_clientsMutex);

		reapFinishedClients();

		if (m_clients.size() >= g_maxClients)
		{
			// Closing here is safe: no other thread has seen this stream
			LOGNET(networkLib::LogLevel::Warning, "Refusing connection, " << m_clients.size() << " clients are connected already");
			_stream->close();
			return;
		}

		_stream->setReadTimeout(m_idleReadTimeoutMs);

		auto client = std::make_unique<Client>();
		client->stream = std::move(_stream);

		auto* c = client.get();
		try
		{
			c->thread = std::thread([this, c]()
			{
				handleClient(*c->stream);
				{
					// Closed now, not when the entry is reaped: the peer sees the end of the connection at once
					std::lock_guard streamLock(c->streamMutex);
					c->stream->close();
				}
				c->done = true;
			});
		}
		catch (const std::system_error& e)
		{
			// Out of threads: drop this client, keep serving the others (an exception here would end the accept thread)
			LOGNET(networkLib::LogLevel::Error, "Failed to start a client thread: " << e.what());
			return;
		}

		m_clients.push_back(std::move(client));
	}

	void HttpServer::reapFinishedClients()
	{
		// Caller holds m_clientsMutex. A finished thread is past its last use of the client, so join returns at once.
		for (auto it = m_clients.begin(); it != m_clients.end();)
		{
			auto& client = *it;
			if (!client->done)
			{
				++it;
				continue;
			}
			if (client->thread.joinable())
				client->thread.join();
			it = m_clients.erase(it);
		}
	}

	void HttpServer::handleClient(networkLib::TcpStream& _stream)
	{
		networkLib::Stream& stream = _stream;

		while (_stream.isValid())
		{
			try
			{
				HttpRequest request;
				if (!parseRequest(request, stream))
				{
					LOGNET(networkLib::LogLevel::Debug, "Client disconnected or sent a malformed request");
					break;
				}

				LOGNET(networkLib::LogLevel::Info, "HTTP " << request.method << " " << request.path
					<< " (Content-Length: " << request.body.size()
					<< ", Accept: " << request.getHeader("accept") << ")");

				// First, before any handler: a request a browser page could have made is refused
				if (!isRequestAllowed(request, m_port))
				{
					LOGNET(networkLib::LogLevel::Warning, "Refused request with Host '" << request.getHeader("host")
						<< "', Origin '" << request.getHeader("origin") << "'");
					HttpResponse forbidden;
					forbidden.statusCode = 403;
					forbidden.statusText = "Forbidden";
					forbidden.headers["Connection"] = "close";
					forbidden.headers["Content-Length"] = "0";
					sendResponse(forbidden, stream);
					break;
				}

				auto response = m_handler(request, stream);

				// For SSE, response headers are sent by the handler itself via the stream
				if (request.getHeader("accept") == "text/event-stream")
					continue;

				LOGNET(networkLib::LogLevel::Info, "HTTP Response: " << response.statusCode << " " << response.statusText
					<< " (body: " << response.body.size() << " bytes)");

				if (!sendResponse(response, stream))
				{
					LOGNET(networkLib::LogLevel::Warning, "Failed to send response");
					break;
				}
			}
			catch (const networkLib::NetException& e)
			{
				LOGNET(networkLib::LogLevel::Debug, "Client connection closed: " << e.what() << " (type: " << static_cast<int>(e.type()) << ")");
				break;
			}
			catch (const std::exception& e)
			{
				LOGNET(networkLib::LogLevel::Error, "Client handler error: " << e.what());
				break;
			}
		}

		LOGNET(networkLib::LogLevel::Debug, "Client handler thread exiting");
	}

	bool HttpServer::parseRequest(HttpRequest& _request, networkLib::Stream& _stream)
	{
		// Read request line
		std::string requestLine;
		if (!readLine(requestLine, _stream))
			return false;

		std::istringstream lineStream(requestLine);
		lineStream >> _request.method >> _request.path >> _request.httpVersion;

		if (_request.method.empty() || _request.path.empty())
			return false;

		// Read headers, up to the empty line that ends them
		std::string headerLine;
		size_t headerCount = 0;
		while (true)
		{
			if (!readLine(headerLine, _stream))
				return false;

			if (headerLine.empty())
				break;

			if (++headerCount > g_maxHeaderCount)
			{
				LOGNET(networkLib::LogLevel::Warning, "Request has more than " << g_maxHeaderCount << " headers");
				return false;
			}

			const auto colonPos = headerLine.find(':');
			if (colonPos == std::string::npos)
				continue;

			auto key = headerLine.substr(0, colonPos);
			const auto value = trimWhitespace(headerLine.substr(colonPos + 1));

			// Lowercase key for case-insensitive lookup
			std::transform(key.begin(), key.end(), key.begin(), [](const unsigned char _c)
			{
				return static_cast<char>(std::tolower(_c));	// a char of 0x80 and up is negative: undefined for tolower
			});
			_request.headers[key] = value;
		}

		// Read body if content-length present
		const auto contentLengthHeader = _request.headers.find("content-length");
		if (contentLengthHeader != _request.headers.end())
		{
			size_t contentLength = 0;
			if (!parseContentLength(contentLengthHeader->second, contentLength) || contentLength > g_maxBodySize)
			{
				LOGNET(networkLib::LogLevel::Warning, "Invalid or too large Content-Length '" << contentLengthHeader->second << "'");
				return false;
			}

			if (contentLength > 0)
			{
				_request.body.resize(contentLength);
				if (!_stream.read(_request.body.data(), static_cast<uint32_t>(contentLength)))
					return false;
			}
		}

		return true;
	}

	bool HttpServer::readLine(std::string& _line, networkLib::Stream& _stream)
	{
		_line.clear();
		char c;
		while (_stream.read(&c, 1))
		{
			if (c == '\r')
			{
				// Consume \n
				_stream.read(&c, 1);
				return true;
			}
			if (c == '\n')
				return true;
			// A line that never ends must not grow without bound
			if (_line.size() >= g_maxLineLength)
			{
				LOGNET(networkLib::LogLevel::Warning, "Request line longer than " << g_maxLineLength << " bytes");
				return false;
			}
			_line += c;
		}
		return false;
	}

	bool HttpServer::sendResponse(const HttpResponse& _response, networkLib::Stream& _stream)
	{
		const auto data = _response.serialize();
		try
		{
			if (!_stream.write(data.data(), static_cast<uint32_t>(data.size())))
				return false;
			return _stream.flush();
		}
		catch (...)
		{
			return false;
		}
	}
}
