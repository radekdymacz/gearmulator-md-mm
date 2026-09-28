#pragma once

#include "mmDeskMachine.h"
#include "mmDeskModel.h"
#include "mmRecv.h"

#include "deskCore/deskDesk.h"

#include "elektronData/json.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mmDesk
{
	// The Monomachine Editor behind its page (MM OS 1.32B, P6): deskCore's Desk (the same core,
	// router and adapter seam as the Machinedrum's; here the edits are whole documents) plus the
	// device facts it forwards to its adapter. Single-threaded.
	class Desk final : public deskCore::Desk<MmModel, MmMachine>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;
		using Probe = MmMachine::Probe;

		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			std::function<void(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value)> sendParam;
			std::function<void(uint8_t _track, uint8_t _param, uint8_t _value)> sendNrpn;
			std::function<bool(const std::vector<Key>&)> pressKeys;
			std::function<void(const Value&)> toPage;
			std::function<double()> nowMs;

			MmMachine::Port device() const { return {sendSysex, sendParam, sendNrpn, pressKeys, nowMs}; }
		};

		explicit Desk(Port _port, const Profile& _profile = emulatorProfile());

		void onTelemetry(const Telemetry& _t) { machine().onTelemetry(_t); }
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		void onWorkingKit(const Bytes& _region) { machine().onWorkingKit(_region); }
		void setProbe(Probe _probe) { machine().setProbe(_probe); }
		// An engine of the engine map: its profile and its device edge.
		void setEngine(const Profile& _profile, const MmMachine::Port& _device)
		{
			deskCore::Desk<MmModel, MmMachine>::setEngine(_profile, _device);
		}

		bool isReady() const { return machine().ready(); }
		const RecvSession& recv() const { return machine().recv(); }
		std::optional<elektronData::MmPattern> pattern(uint8_t _slot) const;
		// The stored slot (what a dump holds).
		const std::optional<elektronData::MmKit>& kit(uint8_t _slot) const { return machine().storedKit(_slot); }
		const std::optional<elektronData::MmKit>& workingKit() const { return machine().workingKit(); }
		std::optional<elektronData::MmSong> song(uint8_t _slot) const;
		std::optional<elektronData::MmGlobal> global(uint8_t _slot) const;
		int currentPattern() const { return machine().currentPattern(); }
		int currentKit() const { return machine().currentKit(); }
		int currentSong() const { return machine().currentSong(); }
		int currentGlobal() const { return machine().currentGlobal(); }
		size_t loaded() const { return machine().loaded(); }
		double lastRoundTripMs() const { return machine().lastRoundTripMs(); }
	};
}
