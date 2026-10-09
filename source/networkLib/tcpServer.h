#pragma once

#include <memory>

#include "tcpConnection.h"

namespace ptypes
{
	class ipstmserver;
}

namespace networkLib
{
	// The interfaces a server listens on
	enum class BindScope
	{
		All,		// every interface: the DSP bridge serves clients on other machines
		Loopback	// 127.0.0.1 only: a local control port such as the MCP server
	};

	class TcpServer : TcpConnection
	{
	public:
		// Throws on bind failure so callers (e.g. McpServer::start's port-retry
		// loop) can detect a port collision and try the next port.
		TcpServer(OnConnectedFunc _onConnected, int _tcpPort, BindScope _scope = BindScope::All);
		~TcpServer() override;
	protected:
		void threadFunc() override;

	private:
		const int m_port;
		std::unique_ptr<ptypes::ipstmserver> m_listener;
	};
}
