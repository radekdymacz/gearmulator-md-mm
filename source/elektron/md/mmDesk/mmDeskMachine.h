#pragma once

#include "mmDeskModel.h"
#include "mmRecv.h"

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
	// The engine map (P6): the emulated MM OS 1.32B, or a real Monomachine over HW MIDI. Both
	// speak the same protocol; the profile says what the wire allows.
	struct Profile
	{
		std::string id;			// the engine map's key
		std::string label;		// the LCD's engine label when ready ("EMU OS 1.32B")
		std::string about;		// what the engine is, for the label's tooltip
		bool wire = false;		// at DIN speed: no panel to drive SYSEX RECV, no telemetry, replies make the lifecycle
	};

	const Profile& emulatorProfile();	// "emu"
	const Profile& wireProfile();		// "hw"

	// The kit that plays, as one value (P6): the working kit (memory, or what the editor sent), a
	// memory image not taken yet, and the live edits sent and not yet seen in memory (raw kit bytes
	// from and to). An image is taken once it shows every byte the editor changed, or once the edit
	// is older than g_expectTimeoutMs (the machine then holds something else: it wins).
	struct WorkingKit
	{
		static constexpr double g_expectTimeoutMs = 1000;

		std::optional<elektronData::MmKit> kit;
		std::optional<std::vector<uint8_t>> region;
		std::vector<uint8_t> expectFrom, expectTo;
		double expectedAtMs = 0;

		bool expecting(double _nowMs) const { return !expectTo.empty() && _nowMs - expectedAtMs < g_expectTimeoutMs; }
		// Every byte that differs between expectFrom and expectTo has expectTo's value in _raw.
		bool reflected(const std::vector<uint8_t>& _raw) const;
	};

	// The Monomachine adapter (P6) behind deskCore's Machine protocol. Delivery:
	//   pattern, song, global, a stored kit: a dump on SYSEX RECV (RecvSession drives the panel
	//     there on the emulator; over HW MIDI the user parks the machine on it), then a dump
	//     request to confirm what the firmware holds;
	//   the working kit: CC per changed parameter and level, NRPN for the MIDI page, 0x5B machine,
	//     0x5C routing, 0x55 name; what has no live path is a kit dump to the current slot plus
	//     LOAD KIT (which also saves it there).
	class MmMachine final : public deskCore::Machine<MmModel>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;

		using Profile = mmDesk::Profile;
		using Probe = deskCore::LifeFacts::Probe;

		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			// A kit value as the plug-in's parameter: page 0-6 (DATA pages, CC), 7 = level (index 0),
			// 8 = mute (index 0, value 0/1).
			std::function<void(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value)> sendParam;
			// NRPN on the base channel: track 0-5, parameter (0x38-0x3f MIDI page, 0x40-0x45 multi env).
			std::function<void(uint8_t _track, uint8_t _param, uint8_t _value)> sendNrpn;
			// Press these keys one after another (10 ms held, 10 ms apart, machine time). Over HW
			// MIDI only Play and Stop exist (MIDI Start / Stop).
			std::function<bool(const std::vector<Key>&)> pressKeys;
			std::function<double()> nowMs;
		};

		MmMachine(Profile _profile, Port _port);

		// ---- deskCore::Machine ----
		deskCore::Outcome review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) override;
		deskCore::Outcome submit(const Change& _change, const Documents& _view) override;
		deskCore::Outcome command(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		std::vector<Ev> drain() override;
		Value state(const Documents& _view) const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_curKit}; }
		bool busy() const override;

		// ---- Monomachine facts from the device ----
		// What the device says about the firmware (the emulator; a wire has no probe).
		void setProbe(Probe _probe);
		void onTelemetry(const Telemetry& _telemetry);
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		void onWorkingKit(const Bytes& _region);

		const Profile& profile() const { return m_profile; }
		const RecvSession& recv() const { return m_recv; }
		const std::optional<elektronData::MmKit>& storedKit(uint8_t _slot) const { return m_kits[_slot & 127]; }
		const std::optional<elektronData::MmKit>& workingKit() const { return m_working.kit; }
		int currentPattern() const { return m_curPattern; }
		int currentKit() const { return m_curKit; }
		int currentSong() const { return m_curSong; }
		int currentGlobal() const { return m_curGlobal; }
		size_t loaded() const { return m_known.size(); }
		double lastRoundTripMs() const { return m_lastRoundTripMs; }
		bool ready() const { return deskCore::takesInput(lifecycle()); }

	private:
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
		void observe(const Document& _doc, deskCore::Source _source);
		void forget(const Ref& _ref);
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
		std::string kitState() const;

		deskCore::Outcome cmdLoad(const Value&, const Documents&);
		deskCore::Outcome cmdSelect(const Value&, const Documents&);
		deskCore::Outcome cmdKit(const Value&, const Documents&);
		deskCore::Outcome cmdSong(const Value&, const Documents&);
		deskCore::Outcome cmdTempo(const Value&, const Documents&);
		deskCore::Outcome cmdTransport(const Value&, const Documents&);
		deskCore::Outcome cmdMute(const Value&, const Documents&);
		friend struct MmModel;	// the command table's handler column

		const Profile m_profile;
		Port m_port;
		RecvSession m_recv;
		std::vector<Ev> m_events;
		std::set<Ref> m_known;
		std::array<std::optional<elektronData::MmKit>, 128> m_kits;	// stored slots
		WorkingKit m_working;
		std::map<Ref, Push> m_pushes;

		deskCore::LoadQueue<Ref> m_loads;
		bool m_backgroundQueued = false;

		Telemetry m_tel;
		// The step byte moving: in the plug-in the RAM running flag (0x26b46e) can stay 0 while the
		// sequencer plays, so "playing" is the flag or the step advancing (onTelemetry).
		int m_rawStep = -1;
		int m_stepMoves = 0;
		double m_stepMovedMs = -1e9;
		Probe m_probe = Probe::Missing;
		bool m_replied = false;
		uint32_t m_statusReplies = 0;
		double m_lastReplyMs = -1e9;
		double m_wireSinceMs = 0;
		int m_curPattern = -1, m_curKit = -1, m_curSong = -1, m_curGlobal = -1, m_songMode = -1;
		int m_queuedPattern = -1;
		int m_lastStep = -1;
		bool m_lastPlaying = false;
		double m_lastStatusMs = -1e9;
		double m_lastTelemetryMs = -1e9;
		double m_lastRoundTripMs = -1;
		std::string m_lastError;
		deskCore::Sequencer<Act> m_sequence;
	};
}
