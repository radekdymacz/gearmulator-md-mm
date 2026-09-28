#pragma once

#include "mdDeskAdapter.h"
#include "mdDeskModel.h"
#include "mdDeskSetup.h"
#include "mdDeskTelemetry.h"

#include "deskCore/deskDesk.h"

#include "elektronData/json.h"
#include "mdDataLink/mdDataLink.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mdDesk
{
	// The Machinedrum Editor behind its page (P6): deskCore's Desk (core, one adapter from the
	// engine map, the table's router) plus what is the Machinedrum's own: the editor's setup (app
	// modulators, knob rows) and the device facts it forwards to its adapter. The plug-in's session
	// owns one, so it outlives the editor window; the firmware smoke tests drive one directly.
	class Desk final : public deskCore::Desk<MdModel, MdAdapter>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;
		using Probe = MdAdapter::Probe;
		using Adapter = MdAdapter;
		using Profile = mdDesk::Profile;
		using DevicePort = mdDesk::DevicePort;

		// The device edge (the engine's) plus the page and the project's setup store.
		struct Port
		{
			DevicePort device;
			std::function<void(const Value& _message)> toPage;
			// The editor's setup (md-desk/setup) changed: keep it with the project.
			std::function<void(const Value& _setup)> saveSetup;
			// After every ready of a page, once the desk has published its own documents.
			std::function<void()> ready;
		};

		// With the Machinedrum adapter both engines use (MdMachine), for this profile.
		explicit Desk(Port _port, const Profile& _profile = emulatorProfile());
		// With an engine's own adapter.
		Desk(std::unique_ptr<MdAdapter> _adapter, Port _port);

		// The adapter both engines of the engine map use today: mdDataLink over the device edge.
		static std::unique_ptr<MdAdapter> defaultAdapter(const Profile& _profile, const DevicePort& _device);

		// A kit parameter changed outside the desk (host automation, MIDI learn). _index 24 = level.
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value);
		void onHostMute(uint8_t _track, bool _muted);
		// Every tick, also without telemetry (valid = false).
		void onTelemetry(const Telemetry& _telemetry);
		// The working-kit region read from the machine's memory (elektronData::mdWorkingKitFromMemory).
		void onWorkingKitMemory(const Bytes& _region);
		void setProbe(Probe _probe);
		// The setup stored with the project (md-desk/setup); errors if it does not validate.
		std::vector<std::string> loadSetup(const Value& _setup);
		const DeskSetup& setup() const { return m_setup; }

		bool isBusy() const { return machine().busy(); }
		// The default adapter's protocol facts (MdMachine), for tests and diagnostics; empty, false
		// and -1 for another adapter. Not part of MdAdapter: an engine with its own protocol has none.
		const mdDataLink::Session::State& linkState() const;
		bool isReady() const;
		double lastRoundTripMs() const;

		// The Owner::Setup ops the desk has a function for (checked against the table).
		static std::vector<std::string> setupOps();

	private:
		static const std::map<std::string, void (Desk::*)(const Value&)>& setups();
		void onSetup(const Value& _message) override;
		void setModulators(const Value& _message);
		void setKnobs(const Value& _message);
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
