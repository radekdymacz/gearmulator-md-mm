#pragma once

#include "mdDeskMachine.h"
#include "mdDeskMod.h"
#include "mdDeskModel.h"
#include "mdDeskSetup.h"
#include "mdDeskTelemetry.h"

#include "deskCore/deskDesk.h"

#include "elektronData/json.h"
#include "mdDataLink/mdDataLink.h"

#include <functional>
#include <string>
#include <vector>

namespace mdDesk
{
	// The Machinedrum Editor behind its page (P6): deskCore's Desk (core, one adapter from the
	// engine map, the table's router) plus what is the Machinedrum's own: the editor's setup (app
	// modulators, knob rows) and the device facts it forwards to its adapter. The plug-in's session
	// owns one, so it outlives the editor window; the firmware smoke tests drive one directly.
	class Desk final : public deskCore::Desk<MdModel, MdMachine>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;
		using Probe = MdMachine::Probe;

		// The device edge (MdMachine::Port) plus the page, the project's setup store and the clock.
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

			MdMachine::Port device() const { return {sendSysex, sendKitParam, sendMute, pressKey, turnKnob, nowMs}; }
		};

		explicit Desk(Port _port, const Profile& _profile = emulatorProfile());

		// A kit parameter changed outside the desk (host automation, MIDI learn). _index 24 = level.
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value);
		void onHostMute(uint8_t _track, bool _muted);
		// Every tick, also without telemetry (valid = false).
		void onTelemetry(const Telemetry& _telemetry);
		// The working-kit region read from the machine's memory (elektronData::mdWorkingKitFromMemory).
		void onWorkingKitMemory(const Bytes& _region);
		void setProbe(Probe _probe);
		// An engine of the engine map: its profile and its device edge.
		void setEngine(const Profile& _profile, const MdMachine::Port& _device);
		// The setup stored with the project (md-desk/setup); errors if it does not validate.
		std::vector<std::string> loadSetup(const Value& _setup);
		const DeskSetup& setup() const { return m_setup; }

		const mdDataLink::Session& session() const { return machine().session(); }
		bool isHardwareLink() const { return machine().profile().wire; }
		bool isReady() const { return machine().replied(); }
		bool isBusy() const { return machine().busy(); }
		double lastRoundTripMs() const { return machine().lastRoundTripMs(); }

	private:
		void onSetup(const Value& _message) override;
		void onReadyExtra() override;
		void publishSetup();
		void publishModulators();
		void saveSetup() const;
		void runModulators();

		std::function<void(const Value&)> m_saveSetup;
		DeskSetup m_setup;
		ModEngine m_mods;
	};
}
