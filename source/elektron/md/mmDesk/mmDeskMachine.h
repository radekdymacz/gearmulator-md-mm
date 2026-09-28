#pragma once

#include "mmDeskAdapter.h"
#include "mmDeskModel.h"
#include "mmRecv.h"

#include "deskCore/deskAdapter.h"
#include "deskCore/deskCore.h"
#include "deskCore/deskLoadQueue.h"
#include "deskCore/deskPush.h"
#include "deskCore/deskSequence.h"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mmDesk
{
	// What the adapter knows of the kit that plays from the machine's memory (P6), one value: an
	// image not taken yet and the live edits sent and not yet seen. The kit itself is the core's
	// (Kind::WorkingKit): observed from memory, pending while edits are on their way.
	struct KitMemory
	{
		std::optional<std::vector<uint8_t>> region;
		std::optional<std::vector<uint8_t>> shown;	// the raw image last published
		deskCore::Expectation<elektronData::MmKit> expect;
	};

	// Every byte that differs between _before and _after has _after's value in _image (raw kits).
	bool reflects(const elektronData::MmKit& _image, const elektronData::MmKit& _before, const elektronData::MmKit& _after);

	// The Monomachine adapter (P6) behind deskCore's Machine protocol. Delivery:
	//   pattern, song, global, a stored kit: a dump on SYSEX RECV (RecvSession drives the panel
	//     there on the emulator; over HW MIDI the user parks the machine on it), then a dump
	//     request to confirm what the firmware holds;
	//   the working kit: CC per changed parameter and level, NRPN for the MIDI page, 0x5B machine,
	//     0x5C routing, 0x55 name; what has no live path is a kit dump to the current slot plus
	//     LOAD KIT (which also saves it there). It is pending until memory shows it.
	class MmMachine final : public deskCore::AdapterBase<MmModel, MmAdapter>
	{
	public:
		using Value = elektronData::json::Value;
		using Profile = mmDesk::Profile;
		using Port = DevicePort;

		MmMachine(Profile _profile, Port _port);

		// ---- deskCore::Machine ----
		deskCore::Outcome review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) override;
		deskCore::Outcome submit(const Change& _change, const Documents& _view) override;
		deskCore::Outcome command(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		Value state(const Documents& _view) const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_curKit}; }
		bool busy() const override;

		// ---- MmAdapter: Monomachine facts from the device ----
		void setProbe(Probe _probe) override;
		void onTelemetry(const Telemetry& _telemetry) override;
		void onWorkingKit(const Bytes& _region) override;
		void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) override;
		const Telemetry& telemetry() const override { return m_tel; }
		bool playing() const override { return m_playing; }
		const RecvSession& recv() const override { return m_recv; }
		int currentPattern() const override { return m_curPattern; }
		int currentKit() const override { return m_curKit; }
		int currentSong() const override { return m_curSong; }
		int currentGlobal() const override { return m_curGlobal; }
		size_t loaded() const override { return knownCount(); }
		double lastRoundTripMs() const override { return m_lastRoundTripMs; }

		const Profile& profile() const { return m_profile; }
		bool ready() const { return deskCore::takesInput(lifecycle()); }
		// The model's machine commands this adapter runs (tests check them against the table).
		static std::vector<std::string> commandsHandled();

	private:
		using Handler = deskCore::Outcome (MmMachine::*)(const Value&, const Documents&);
		static const std::map<std::string, Handler>& handlers();

		enum class Act : uint8_t { Stop, Play, SelectPattern };
		struct Push
		{
			deskCore::PushSlot<Bytes> slot;
			double sentMs = 0;
			bool onRecv = false;	// queued on the RECV session, not on the wire yet
		};

		deskCore::LifeFacts facts() const;
		void startOver();
		double now() const { return m_port.nowMs(); }
		void request(const Ref& _ref, bool _urgent);
		void requestStatus();
		void pushDump(const Ref& _ref, Bytes _dump);
		void deliverKitLive(const elektronData::MmKit& _from, const elektronData::MmKit& _to, std::vector<std::string>& _notes);
		void onDump(const Ref& _ref, const Bytes& _sysex);
		void onStatus(uint8_t _param, uint8_t _value);
		void kitSwitched(int _from, int _to);
		void pumpLoads(double _now);
		void pumpRecv(double _now);
		void pumpSequence(double _now);
		void applyWorkingKit(double _now);
		bool pressKeys(const std::vector<Key>& _keys);
		std::string kitState(const Documents& _view) const;
		void setBaseChannel(const elektronData::MmGlobal& _g);

		deskCore::Outcome cmdLoad(const Value&, const Documents&);
		deskCore::Outcome cmdSelect(const Value&, const Documents&);
		deskCore::Outcome cmdLoadKit(const Value&, const Documents&);
		deskCore::Outcome cmdSaveKit(const Value&, const Documents&);
		deskCore::Outcome kitAction(int _kit, bool _save);
		deskCore::Outcome cmdLoadSong(const Value&, const Documents&);
		deskCore::Outcome cmdSaveSong(const Value&, const Documents&);
		deskCore::Outcome cmdTempo(const Value&, const Documents&);
		deskCore::Outcome cmdPlay(const Value&, const Documents&);
		deskCore::Outcome cmdStop(const Value&, const Documents&);
		deskCore::Outcome cmdMute(const Value&, const Documents&);

		const Profile m_profile;
		Port m_port;
		RecvSession m_recv;
		KitMemory m_memory;
		bool m_seedWorking = true;		// the working kit comes from the next dump of its slot
		std::map<Ref, Push> m_pushes;

		deskCore::LoadQueue<Ref> m_loads;
		bool m_backgroundQueued = false;

		Telemetry m_tel;
		// The step byte moving: in the plug-in the RAM running flag (0x26b46e) can stay 0 while the
		// sequencer plays, so "playing" is the flag or the step advancing (onTelemetry).
		bool m_playing = false;
		int m_rawStep = -1;
		int m_stepMoves = 0;
		double m_stepMovedMs = -1e9;
		Probe m_probe = Probe::Missing;
		deskCore::WireFacts m_wire;
		int m_curPattern = -1, m_curKit = -1, m_curSong = -1, m_curGlobal = -1, m_songMode = -1;
		int m_queuedPattern = -1;
		int m_lastStep = -1;
		bool m_lastPlaying = false;
		double m_lastStatusMs = -1e9;
		double m_lastTelemetryMs = -1e9;
		double m_lastRoundTripMs = -1;
		deskCore::Sequencer<Act> m_sequence;
	};
}
