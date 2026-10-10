#pragma once

#include "mmDeskAdapter.h"
#include "deskCore/deskSongRow.h"
#include "mmDeskModel.h"
#include "mmRecv.h"
#include "mmDeskWatch.h"

#include "deskCore/deskAdapter.h"
#include "deskCore/deskChain.h"
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

	// B-051: which of the editor's channel messages the machine takes now. A track's CCs (its sound values, level,
	// mute) and notes need its own channel (elektronData::mmTrackChannel); NRPN needs the base channel. Without the
	// active global (not read yet) everything goes, as it always did.
	struct ChannelReach
	{
		std::array<bool, 6> track{true, true, true, true, true, true};
		bool nrpn = true;
		static ChannelReach none() { ChannelReach r; r.track.fill(false); r.nrpn = false; return r; }
		static ChannelReach of(const elektronData::MmGlobal* _global);
	};

	// The Monomachine adapter (P6) behind deskCore's Machine protocol. Its sources are split along the MD's seams:
	// mmDeskMachine.cpp (facts, capabilities, tick, the machine document), mmDeskDelivery.cpp, mmDeskLoad.cpp,
	// mmDeskCommands.cpp, mmDeskChain.cpp, mmDeskNotes.cpp, mmDeskRecord.cpp; small state values with pure steps
	// in mmDeskWatch.h. Delivery:
	//   pattern, song, global, a stored kit: a dump on SYSEX RECV (RecvSession drives the panel
	//     there on the emulator; over HW MIDI the user parks the machine on it), then a dump
	//     request to confirm what the firmware holds;
	//   the working kit: CC per changed parameter and level, NRPN for the MIDI page, 0x5B machine,
	//     0x5C routing, 0x55 name; what has no live path is a kit dump to the current slot plus
	//     LOAD KIT (which also saves it there). It is pending until memory shows it. A track the
	//     active global gives no MIDI channel (ChannelReach, B-051) has no live path for its values.
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
		Review review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) override;
		deskCore::Outcome submit(const Change& _change, const Intent& _intent, const Documents& _view) override;
		deskCore::Outcome command(const Value& _command, const Documents& _view) override;
		deskCore::Outcome askFor(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		Value state(const Documents& _view) const override;
		Value status() const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_curKit, m_curGlobal}; }
		bool busy() const override;
		std::string sendAsIs(const Bytes& _message, bool _dump) override;
		AsIs asIs() const override;

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
		// A push is parked while its dump is queued on the RECV session (or waits for the person's SYSEX
		// RECV, HW MIDI), not on the wire yet: a newer dump waits for it (PushPolicy::Mode::Held).
		using Pushes = deskCore::Pushes<Ref, Bytes>;

		deskCore::LifeFacts facts() const;
		void startOver();
		double now() const { return m_port.nowMs(); }
		// the session's clock where an engine may have none (a test's port)
		double clock() const { return m_port.nowMs ? m_port.nowMs() : 0; }
		// memory's mute of track t (0-5 synth, 6-11 MIDI), POLY and tempo (BPM x 24); nullopt: not known
		std::optional<bool> muteInMemory(const int _t) const { if(m_tel.mutes < 0) return std::nullopt; return ((m_tel.mutes >> _t) & 1) != 0; }
		std::optional<bool> polyInMemory() const { if(m_poly < 0) return std::nullopt; return m_poly == 1; }
		std::optional<int> tempoInMemory() const { if(m_tel.tempo < 720 || m_tel.tempo > 7200) return std::nullopt; return m_tel.tempo; }
		void request(const Ref& _ref, bool _urgent);
		void requestStatus();
		// True when the dump went on its way now; false when it waits its turn behind one still on its way.
		bool pushDump(const Ref& _ref, Bytes _dump);
		// LOAD KIT after the dump of a kit slot (a write into the kit that plays): at once when the dump went now,
		// else right after it when it goes (pumpPushes), so the machine never loads the dump before it.
		void loadKitAfter(const Ref& _ref, bool _sent);
		// The dump the slot lets go now: onto SYSEX RECV (or the person's, HW MIDI).
		void sendDump(const Ref& _ref, Bytes _dump);
		deskCore::PushPolicy pushPolicy(Kind _kind) const;
		void pumpPushes(double _now);
		// A message that must follow the dumps queued before it on SYSEX RECV (LOAD KIT after a kit dump).
		void afterDumps(Bytes _message);
		// The dumps wait for the person to open SYSEX RECV (no panel keys: HW MIDI).
		bool manualDumps() const { return !m_profile.panel; }
		void deliverKitLive(const elektronData::MmKit& _from, const elektronData::MmKit& _to, const ChannelReach& _reach,
			std::vector<std::string>& _notes);
		// The live messages (0x55, 0x5B, 0x5C, CC, NRPN) that turn the working kit _from into _to; nothing else. CC and
		// NRPN only where _reach says the machine takes them.
		void sendKitLive(const elektronData::MmKit& _from, const elektronData::MmKit& _to, const ChannelReach& _reach);
		// B-051: what the machine takes over MIDI channels now: the active global's, none for a while after its base
		// channel changed (the machine applies it once SYSEX RECV is left; the emulator's parameter layer learns it by its
		// own poll, every 5 s).
		ChannelReach reach(const Documents& _view) const;
		// Why track _t takes no CC, mute or note now; empty when it does.
		std::string noChannelReason(const Documents& _view, int _t) const;
		// B-027: a dump of the pattern that plays went into the stream; the kit reload it makes is
		// followed by the edits.
		void reloadFollows(const Bytes& _patternDump);
		void restoreWorkingKit(const Documents& _view);
		void onDump(const Ref& _ref, const Bytes& _sysex);
		void onStatus(uint8_t _param, uint8_t _value);
		void kitSwitched(int _from, int _to);
		void pumpLoads(double _now);
		void gaveUpLoad(const Ref& _r);
		bool current(const Ref& _r) const;
		void pumpRecv(double _now);
		void pumpTransport(double _now);
		void pumpSequence(double _now);
		// RECORD: the current pattern read back while the machine records (true: the mode changed).
		bool readWhileRecording(double _now);
		// The transport's telemetry message (the playhead, playing, the recording mode).
		void publishTransport(double _now, bool _recordChanged);
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
		deskCore::Outcome cmdSeqMode(const Value&, const Documents&);
		deskCore::Outcome cmdFollowHost(const Value&, const Documents&);
		deskCore::Outcome cmdGlobalSlot(const Value&, const Documents&);
		deskCore::Outcome cmdMuteMidi(const Value&, const Documents&);
		deskCore::Outcome cmdPoly(const Value&, const Documents&);
		deskCore::Outcome cmdRecord(const Value&, const Documents&);
		deskCore::Outcome cmdHwSend(const Value&, const Documents&);
		// MM-P8: pattern chaining as on the machine (hold BANK, press the TRIG keys; manual 1-46)
		deskCore::Outcome cmdChain(const Value&, const Documents&);
		deskCore::Outcome cmdChainClear(const Value&, const Documents&);
		// The page's keyboard: the note intent (deskCore/deskNotes.h), a MIDI note on the track's channel.
		deskCore::Outcome cmdNoteOn(const Value&, const Documents&);
		deskCore::Outcome cmdNoteOff(const Value&, const Documents&);
		deskCore::Outcome sendChain(const std::vector<int>& _patterns);
		// CLEAR: BANK + the TRIG key of the pattern that plays (false: the panel did not take them).
		bool clearChain();
		void pumpChain();
		// 0.3.5: an edited current song is heard without LOAD SONG by hand (pumpSongReload)
		void pumpSongReload(double _now);
		bool m_songReloadNeeded = false;
		double m_songEditedMs = -1;
		static constexpr double g_songReloadQuietMs = 300;
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
		deskCore::Stream m_stream;		// B-014: the one way to the machine (after m_port: it holds its SysEx)
		RecvSession m_recv;
		deskCore::WorkingCopy<elektronData::MmKit> m_working;	// where the kit that plays comes from
		// B-027: pattern dumps whose kit reload the working kit's edits must follow (reloadFollows), from when the dump
		// went into the stream until the machine applied it; then the kit whose edits go again (-1: none). Meanwhile
		// memory shows the kit before the reload, then the stored slot: no image is taken (the restore sets what
		// memory must show).
		int m_reloadsPending = 0;
		double m_reloadQueuedMs = 0;
		int m_restoreKit = -1;
		static constexpr double g_reloadHoldMs = 10000;	// a reload not restored by then holds nothing any more
		bool reloadHolds() const { return m_restoreKit >= 0
			|| (m_reloadsPending > 0 && clock() - m_reloadQueuedMs < g_reloadHoldMs); }
		uint32_t m_nextRecvTag = 1;
		std::map<uint32_t, Ref> m_recvRefs;		// a dump on the RECV session -> the push it is
		Pushes m_pushes;
		std::set<Ref> m_loadAfter;				// kit slots to LOAD KIT once their waiting dump goes
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
		deskCore::SongRowHeard m_songRow;	// 0.3.5: the row heard (the RAM byte runs ahead of the pass)
		// "playing" is the RAM flag or the step byte advancing (watchStep, onTelemetry)
		bool m_playing = false;
		StepWatch m_steps;
		Probe m_probe = Probe::Missing;
		deskCore::WireFacts m_wire;
		int m_curPattern = -1, m_curKit = -1, m_curSong = -1, m_curGlobal = -1, m_songMode = -1;
		int m_activateGlobal = -1;	// the active global's slot to make active again (0x56) once RECV is left
		int m_baseChannel = -1;		// the active global's base channel as last read (-1: not yet)
		double m_channelsSettleUntilMs = -1e9;	// B-051: after a base channel change, no channel messages until then
		static constexpr double g_channelsSettleMs = 6000;
		int m_poly = -1;			// the audio mode (SET STATUS 0x20): 0 mono, 1 POLY; -1 unknown
		// DESIGN-UNIFY.md 4.4: what the editor set and memory has not shown yet: the twelve mutes (bit t of
		// Telemetry::mutes), POLY and the tempo (BPM x 24). The machine document says these until then.
		std::array<deskCore::FieldExpectation<bool>, 12> m_expectMute{};
		deskCore::FieldExpectation<bool> m_expectPoly;
		deskCore::FieldExpectation<int> m_expectTempo;
		RecordReads m_recordReads;		// RECORD's read-backs (watchRecord)
		int m_queuedPattern = -1;
		TelemetryOut m_telemetryOut;	// the transport message last published (telemetryDue)
		double m_lastStatusMs = -1e9;
		double m_lastRoundTripMs = -1;
		deskCore::Sequencer<Act> m_sequence;
		// MM-P8: the chain (or CLEAR) the page asked for while keys were on their way, sent after them
		deskCore::Latest<deskCore::ChainRequest> m_chain;
		double m_keysUntilMs = -1e9;	// the desk's last panel keys are through by then
		// 0.3.4: PLAY or STOP asked while the panel was busy (a dump on its way to SYSEX RECV, which the stream paces
		// at cable speed while playing): pressed once it is free (pumpTransport), the newest wins, given up after a while
		std::optional<Key> m_pendingTransport;
		double m_pendingTransportMs = 0;
		// The keyboard's sounding notes (pressNote / releaseNotes)
		SoundingNotes m_notes;
	};
}
