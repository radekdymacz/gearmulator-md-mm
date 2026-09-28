#pragma once

#include "mmDeskMachine.h"
#include "mmDeskModel.h"
#include "mmRecv.h"

#include "deskCore/deskCore.h"

#include "elektronData/json.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mmDesk
{
	// The Monomachine Editor behind its page (MM OS 1.32B, P6): the same engine-neutral core
	// as the Machinedrum's (documents as observed + pending, pure edits - here whole documents
	// - and undo), one Monomachine adapter from the engine map (the emulator, or HW MIDI), and
	// the command table routing every page message by owner. Single-threaded.
	class Desk
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;

		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			std::function<void(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value)> sendParam;
			std::function<void(uint8_t _track, uint8_t _param, uint8_t _value)> sendNrpn;
			std::function<bool(const std::vector<Key>&)> pressKeys;
			std::function<void(const Value&)> toPage;
			std::function<double()> nowMs;
		};

		// What the device says (the plug-in's probe).
		enum class Engine
		{
			Missing,		// no ROM
			Unsupported,	// another firmware than MM OS 1.32B
			Loading,		// the device prepares or restores
			Booting,		// the firmware starts (MIDI not ready, or the start-up screen)
			Ready			// the main screen came: it takes input
		};

		explicit Desk(Port _port);

		// False for a host command (the plug-in's), which the caller handles.
		bool onPageMessage(const Value& _message);
		void onDeviceSysex(const Bytes& _message);
		void onTelemetry(const Telemetry& _t);
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		void onWorkingKit(const Bytes& _region);
		// A kit value changed outside the desk (host automation): the working kit from memory
		// follows by itself.
		void onHostParam(uint8_t, uint8_t, uint8_t, uint8_t) {}
		void setEngine(Engine _engine);
		// The engine map: a new adapter; the documents start over. _port replaces the device edge.
		bool setEngineId(const std::string& _id, const std::optional<Port>& _port = std::nullopt);
		const std::string& engineId() const { return m_machine->profile().id; }
		void tick();
		void detachPage();

		Engine engine() const;
		bool isReady() const { return m_machine->ready(); }
		const deskCore::Core<MmModel>& core() const { return m_core; }
		const MmMachine& machine() const { return *m_machine; }
		const RecvSession& recv() const { return m_machine->recv(); }
		std::optional<elektronData::MmPattern> pattern(uint8_t _slot) const;
		// The stored slot (what a dump holds).
		const std::optional<elektronData::MmKit>& kit(uint8_t _slot) const { return m_machine->storedKit(_slot); }
		const std::optional<elektronData::MmKit>& workingKit() const { return m_machine->workingKit(); }
		std::optional<elektronData::MmSong> song(uint8_t _slot) const;
		std::optional<elektronData::MmGlobal> global(uint8_t _slot) const;
		int currentPattern() const { return m_machine->currentPattern(); }
		int currentKit() const { return m_machine->currentKit(); }
		int currentSong() const { return m_machine->currentSong(); }
		int currentGlobal() const { return m_machine->currentGlobal(); }
		size_t loaded() const { return m_machine->loaded(); }
		double lastRoundTripMs() const { return m_machine->lastRoundTripMs(); }
		// The OS 1.32B machine table and enumerations as "mm-desk/catalogue".
		static Value catalogue();

	private:
		MmMachine::Port devicePort() const;
		void onReady();
		void flush();

		Port m_port;
		deskCore::Core<MmModel> m_core;
		std::unique_ptr<MmMachine> m_machine;
		Engine m_engine = Engine::Missing;
		bool m_pageReady = false;
	};
}
