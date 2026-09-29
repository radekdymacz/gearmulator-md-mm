#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "mdLib/mddeskdevice.h"

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class DeskSession;

	// The editors' part of the plug-in processor (doc/modern-ux/UPSTREAM.md): the editor's setup,
	// kept with the project as the "MDSK" chunk, and the editor's session (P6), which lives as long
	// as the plug-in instance. The processor owns one and calls it from its hooks: made and started
	// in its constructor, its chunks saved and read with the processor's, the start of a project
	// load, and gone first in its destructor. The plug-in's device is md::DeskDevice (mddeskdevice.h).
	class DeskHost
	{
	public:
		explicit DeskHost(AudioPluginAudioProcessor& _processor);
		~DeskHost();

		DeskHost(const DeskHost&) = delete;
		DeskHost& operator=(const DeskHost&) = delete;

		// The session (DeskSession::create); it reads the setup, so the host exists first.
		void startSession();
		DeskSession* session() const { return m_session.get(); }

		void saveChunks(baseLib::BinaryStream& _stream) const;
		void addChunkReaders(baseLib::ChunkReader& _reader);
		// A project without the editor's setup starts from the default setup.
		void beginProjectLoad() { setSetup({}); }

		// The Machinedrum Editor's own setup (md-desk/setup JSON: app modulators, knob-row CCs). The
		// host only stores the text; the session's mdDesk::Desk validates it. Any thread.
		std::string setup() const;
		void setSetup(std::string _json);
		// Changes with every setSetup (the session's saves and project restores).
		uint32_t setupVersion() const { return m_setupVersion.load(std::memory_order_acquire); }

	private:
		AudioPluginAudioProcessor& m_processor;
		mutable std::mutex m_setupMutex;
		std::string m_setup;
		std::atomic<uint32_t> m_setupVersion{0};
		std::unique_ptr<DeskSession> m_session;
		std::shared_ptr<void> m_editFlowDriver;	// a test build's edit-flow driver (mdEditFlowDriver.h); gone before the session
	};
}
