#pragma once

#include "mdDeskMachine.h"
#include "mdDeskMod.h"
#include "mdDeskModel.h"
#include "mdDeskSetup.h"
#include "mdDeskTelemetry.h"

#include "deskCore/deskCore.h"

#include "elektronData/json.h"
#include "mdDataLink/mdDataLink.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mdDesk
{
	// The Machinedrum Editor behind its page (P6): the engine-neutral core (documents as
	// observed + pending, pure edits, undo, publishing), one Machinedrum adapter chosen
	// from the engine map (the emulated OS 1.63, or HW MIDI), and the editor's own setup
	// (app modulators, knob rows). The command table routes every page message by owner.
	// Single threaded: call everything from one thread. The plug-in's processor owns one,
	// so it outlives the editor window; the firmware smoke tests drive one directly.
	class Desk
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;
		using Firmware = MdMachine::Firmware;

		// Everything the desk needs from its host: the device edge (MdMachine::Port) plus the
		// page, the project's setup store and the clock.
		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			std::function<void(uint8_t _track, uint8_t _index, uint8_t _value)> sendKitParam;
			std::function<void(uint8_t _track, bool _muted)> sendMute;
			std::function<bool(const std::string& _key)> pressKey;
			std::function<bool(uint8_t _encoder, int _steps)> turnKnob;
			std::function<void(const Value& _message)> toPage;
			// The editor's setup (md-desk/setup) changed: keep it with the project.
			std::function<void(const Value& _setup)> saveSetup;
			std::function<double()> nowMs;
		};

		explicit Desk(Port _port);

		// A page message, routed by the command table. False for a host command (the plug-in's:
		// MIDI learn, menus, the engine map), which the caller handles.
		bool onPageMessage(const Value& _message);
		void onDeviceSysex(const Bytes& _message);
		// A kit parameter changed outside the desk (host automation, MIDI learn). _index 24 = level.
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value);
		void onHostMute(uint8_t _track, bool _muted);
		// Every tick, also without telemetry (valid = false).
		void onTelemetry(const Telemetry& _telemetry);
		// The working-kit region read from the machine's memory (elektronData::mdWorkingKitFromMemory).
		void onWorkingKitMemory(const Bytes& _region);
		void setFirmware(Firmware _firmware);
		// The engine map (profiles()): a new adapter, the documents start over. _port replaces the
		// device edge (HW MIDI's is the wire); unset keeps the current one.
		bool setEngine(const std::string& _id, const std::optional<Port>& _port = std::nullopt);
		void setHardwareLink(bool _hardware) { setEngine(_hardware ? "hw" : "emu"); }
		bool isHardwareLink() const { return m_machine->profile().wire; }
		const std::string& engine() const { return m_machine->profile().id; }
		// The setup stored with the project (md-desk/setup); errors if it does not validate.
		std::vector<std::string> loadSetup(const Value& _setup);
		const DeskSetup& setup() const { return m_setup; }
		// About 30 times a second: status polling, loading, timeouts, publishing.
		void tick();
		// The page went away (the editor window closed): nothing is published until the next ready.
		void detachPage();

		const Documents& documents() const { return m_core.view(); }
		const deskCore::Core<MdModel>& core() const { return m_core; }
		const MdMachine& machine() const { return *m_machine; }
		const mdDataLink::Session& session() const { return m_machine->session(); }
		deskCore::Lifecycle lifecycle() const { return m_machine->lifecycle(); }
		// The firmware answered (a status reply).
		bool isReady() const { return m_machine->replied(); }
		bool isInputReady() const { return deskCore::takesInput(lifecycle()); }
		bool isBusy() const { return m_machine->busy(); }
		double lastRoundTripMs() const { return m_machine->lastRoundTripMs(); }

		// The OS 1.63 machine table as a "md-desk/machines" document.
		static Value machineCatalogue();

	private:
		MdMachine::Port devicePort() const;
		void onReady();
		void flush();
		bool gate(const deskCore::Command& _spec, const Value& _message);
		void onSetup(const Value& _message);
		void publishSetup();
		void publishModulators();
		void saveSetup() const;
		void runModulators();

		Port m_port;
		deskCore::Core<MdModel> m_core;
		std::unique_ptr<MdMachine> m_machine;
		DeskSetup m_setup;
		ModEngine m_mods;
		bool m_pageReady = false;
	};
}
