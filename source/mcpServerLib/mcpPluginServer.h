#pragma once

#include "mcpServerLib/mcpServer.h"

#include <atomic>
#include <memory>
#include <string>

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	class Controller;
	class Processor;
}

namespace mcpServer
{
	class McpPluginServer
	{
	public:
		explicit McpPluginServer(pluginLib::Processor& _processor, int _port = g_defaultPort);
		~McpPluginServer();

		bool start();
		void stop();
		bool isRunning() const;

		int getPort() const;

		McpServer& getServer() { return m_server; }

		// send_note's duration_ms, its schema's range enforced (500 ms when absent): the call holds its client's
		// thread, and a server shutdown, this long.
		static constexpr int g_noteDurationMinMs = 1;
		static constexpr int g_noteDurationMaxMs = 10000;
		static int noteDurationMs(const JsonValue& _params)
		{
			if(!_params.hasProperty("duration_ms"))
				return 500;
			const auto ms = _params.get("duration_ms").getDouble();	// a double: no int wraps past 2^31
			if(ms < g_noteDurationMinMs)
				return g_noteDurationMinMs;
			return ms > g_noteDurationMaxMs ? g_noteDurationMaxMs : static_cast<int>(ms);
		}

	private:
		void registerTools();

		// Tool implementations
		void registerParameterTools();
		void registerMidiTools();
		void registerStateTools();
		void registerDeviceInfoTools();

		static synthLib::MidiEventSource parseMidiSource(const JsonValue& _params);

		pluginLib::Processor& m_processor;
		McpServer m_server;
	};
}
