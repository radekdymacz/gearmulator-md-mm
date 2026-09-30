#pragma once

#include "juce_audio_processors/juce_audio_processors.h"
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

namespace synthLib
{
	class Device;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class DeskSession;

	// A machine without its ROM is not an error (no alert, no exception): the processor runs this silent
	// stand-in and the editor's page asks for the ROM (lifecycle "missing"; the page's start-up card takes
	// a chosen or dropped file, DeskSession::installRom, and the machine starts in its place). The
	// processor's createDevice calls the first (doc/modern-ux/UPSTREAM.md); the studio links' probes ask the second.
	synthLib::Device* makeNoRomDevice();
	bool isNoRomDevice(const synthLib::Device* _device);

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
		// No ROM, no project: the stand-in has no machine to give the project to, and saving it would
		// overwrite the saved one (with an empty state and stale automation). While the stand-in runs the
		// host keeps the project it was given, hands the same bytes back when the host asks to save, and
		// gives it to the machine that replaces the stand-in (takeHeldState; the processor's hooks).
		bool holdState(const void* _data, int _size);
		bool heldState(juce::MemoryBlock& _out) const;
		bool takeHeldState(std::vector<uint8_t>& _out);

		// Changes with every setSetup (the session's saves and project restores).
		uint32_t setupVersion() const { return m_setupVersion.load(std::memory_order_acquire); }

	private:
		AudioPluginAudioProcessor& m_processor;
		mutable std::mutex m_setupMutex;
		std::string m_setup;
		mutable std::mutex m_heldMutex;
		std::vector<uint8_t> m_held;
		std::atomic<uint32_t> m_setupVersion{0};
		std::unique_ptr<DeskSession> m_session;
		std::shared_ptr<void> m_editFlowDriver;	// a test build's edit-flow driver (mdEditFlowDriver.h); gone before the session
	};
}
