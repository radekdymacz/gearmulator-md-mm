#pragma once

#include "mmDeskAdapter.h"
#include "mmDeskModel.h"
#include "mmRecv.h"

#include "deskCore/deskAdapter.h"
#include "deskCore/deskWorkingCopy.h"
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
	// Every byte that differs between _before and _after has _after's value in _image (raw kits).
	bool reflects(const elektronData::MmKit& _image, const elektronData::MmKit& _before, const elektronData::MmKit& _after);

	// The Monomachine adapter (P6) behind deskCore's Machine protocol. Delivery:
	//   pattern, song, global, a stored kit: a dump on SYSEX RECV (RecvSession drives the panel
	//     there on the emulator; over HW MIDI the user parks the machine on it), then a dump
	//     request to confirm what the firmware holds;
	//   the working kit: CC per changed parameter and level, NRPN for the MIDI page, 0x5B machine,
	//     0x5C routing, 0x55 name; what has no live path is a kit dump to the current slot plus
	//     LOAD KIT (which also saves it there). It is pending until memory shows it.
	// Without the panel (HW MIDI) only the person can open SYSEX RECV: the dumps wait (the machine
	// document's recv.waiting, "SEND n") until the page says the machine is on it ("hwSend").
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
		deskCore::Outcome askFor(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		Value state(const Documents& _view) const override;
		Value status() const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_curKit}; }
		bool busy() const override;

		// ---- MmAdapter: Monomachine facts from the device ----
		void setProbe(Probe _probe) override;
		bool onTelemetry(const Telemetry& _telemetry) override;
		void onWorkingKit(const Bytes& _region) override;
		void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) override;
		const Telemetry& telemetry() const override { return m_tel; }
		bool playing() const override { return m_playing; }
		int currentPattern() const { return m_curPattern; }
		int currentKit() const { return m_curKit; }
		int currentSong() const { return m_curSong; }
		int currentGlobal() const { return m_curGlobal; }

		const Profile& profile() const { return m_profile; }
		bool ready() const { return deskCore::takesInput(lifecycle()); }
		// The model's machine commands this adapter runs (tests check them against the table).
		static std::vector<std::string> commandsHandled();
		static std::vector<std::string> commandsAsking();

	private:
		using Handler = deskCore::Outcome (MmMachine::*)(const Value&, const Documents&);
		static const std::map<std::string, Handler>& handlers();
		static const std::map<std::string, Handler>& askers();
		deskCore::Outcome askLoadKit(const Value&, const Documents&);
		deskCore::Outcome askSaveKit(const Value&, const Documents&);
		deskCore::Outcome askSelect(const Value&, const Documents&);
		deskCore::Outcome askPlay(const Value&, const Documents&);

		enum class Act : uint8_t { Stop, Play, SelectPattern };
		struct Push
		{
			deskCore::PushSlot<Bytes> slot;
			bool onRecv = false;	// queued on the RECV session, not on the wire yet
		};

		deskCore::LifeFacts facts() const;
		void startOver();
		double now() const { return m_port.nowMs(); }
		void request(const Ref& _ref, bool _urgent);
		void requestStatus();
		void pushDump(const Ref& _ref, Bytes _dump);
		// The dump the slot lets go now: onto SYSEX RECV (or the person's, HW MIDI).
		void sendDump(const Ref& _ref, Push& _push, Bytes _dump);
		deskCore::PushPolicy pushPolicy(Kind _kind) const;
		void pumpPushes(double _now);
		// A message that must follow the dumps queued before it on SYSEX RECV (LOAD KIT after a kit dump).
		void afterDumps(Bytes _message);
		// The dumps wait for the person to open SYSEX RECV (no panel keys: HW MIDI).
		bool manualDumps() const { return !m_profile.panel; }
		void deliverKitLive(const elektronData::MmKit& _from, const elektronData::MmKit& _to, std::vector<std::string>& _notes);
		void onDump(const Ref& _ref, const Bytes& _sysex);
		void onStatus(uint8_t _param, uint8_t _value);
		void kitSwitched(int _from, int _to);
		void pumpLoads(double _now);
		void pumpRecv(double _now);
		void pumpSequence(double _now);
		void applyWorkingKit(double _now, const Documents& _view);
		bool pressKeys(const std::vector<Key>& _keys);
		deskCore::KitState kitState(const Documents& _view) const;
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
		deskCore::Outcome cmdFollowHost(const Value&, const Documents&);
		deskCore::Outcome cmdMuteMidi(const Value&, const Documents&);
		deskCore::Outcome cmdPoly(const Value&, const Documents&);
		deskCore::Outcome cmdRecord(const Value&, const Documents&);
		deskCore::Outcome cmdHwSend(const Value&, const Documents&);
		// MM-P8: pattern chaining as on the machine (hold BANK, press the TRIG keys; manual 1-46)
		deskCore::Outcome cmdChain(const Value&, const Documents&);
		deskCore::Outcome cmdChainClear(const Value&, const Documents&);
		deskCore::Outcome sendChain(const std::vector<int>& _patterns);
		void pumpChain();
		// BANK (+ BANK GROUP from the half the machine is in) and the TRIG keys of _patterns (one bank).
		bool pressBankTrigs(const std::vector<int>& _patterns);
		// Panel keys the desk pressed are still on their way (their own hold times), or the panel is
		// on SYSEX RECV's way: a chain waits (the latest wins).
		bool keysOnTheirWay() const;
		bool canChain() const { return machineState() && static_cast<bool>(m_port.pressBankTrigs); }
		bool chained() const { return m_tel.chainKnown && m_tel.chain.active && !m_tel.chain.patterns.empty(); }
		// The machine's own state is known: its RAM (mutes, recording) and its keys.
		bool machineState() const { return m_profile.telemetry && m_profile.panel && m_port.pressKeys; }

		const Profile m_profile;
		Port m_port;
		RecvSession m_recv;
		deskCore::WorkingCopy<elektronData::MmKit> m_working;	// where the kit that plays comes from
		uint32_t m_nextRecvTag = 1;
		std::map<uint32_t, Ref> m_recvRefs;		// a dump on the RECV session -> the push it is
		std::map<Ref, Push> m_pushes;
		// Without the panel: what waits for the person's SYSEX RECV, in order (a dump names its push).
		struct Waiting
		{
			Bytes bytes;
			std::optional<Ref> ref;
		};
		std::vector<Waiting> m_manual;
		bool m_reactivateGlobal = false;	// HW MIDI: the active global was written; SET ACTIVE GLOBAL before PLAY

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
		int m_activateGlobal = -1;	// the active global's slot to make active again (0x56) once RECV is left
		int m_poly = -1;			// the audio mode (SET STATUS 0x20): 0 mono, 1 POLY; -1 unknown
		int m_lastRecording = -1;	// the recording mode last seen (Telemetry::recording)
		double m_lastRecordReadMs = -1e9;
		int m_queuedPattern = -1;
		int m_lastStep = -1;
		bool m_lastPlaying = false;
		double m_lastStatusMs = -1e9;
		double m_lastTelemetryMs = -1e9;
		double m_lastRoundTripMs = -1;
		deskCore::Sequencer<Act> m_sequence;
		// MM-P8: the chain (empty: CLEAR) the page asked for while keys were on their way, sent after them
		std::optional<std::vector<int>> m_chainQueued;
		double m_keysUntilMs = -1e9;	// the desk's last panel keys are through by then
	};
}
