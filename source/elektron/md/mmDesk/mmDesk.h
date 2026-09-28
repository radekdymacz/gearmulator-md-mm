#pragma once

#include "mmRecv.h"

#include "elektronData/json.h"
#include "elektronData/mmGlobal.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmSong.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mmDesk
{
	// The Monomachine Editor behind its page (MM OS 1.32B). The page sends whole
	// contract documents (mm-desk/pattern, kit, song, global) and small commands;
	// the desk validates, delivers and publishes the firmware's read-backs and the
	// machine state. Delivery:
	//   pattern, song, global, a stored kit: a dump on SYSEX RECV (RecvSession),
	//     then a dump request to confirm what the firmware holds;
	//   the working kit: CC per changed parameter and level, NRPN for the MIDI page,
	//     0x5B machine, 0x5C routing, 0x55 name; what has no live path is a kit dump
	//     to the current slot plus LOAD KIT (which also saves it there).
	// Single-threaded: call everything from one thread.
	class Desk
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;

		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			// A kit value as the plug-in's parameter: page 0-6 (DATA pages, CC),
			// 7 = level (index 0), 8 = mute (index 0, value 0/1).
			std::function<void(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value)> sendParam;
			// NRPN on the base channel: track 0-5, parameter (0x38-0x3f MIDI page, 0x40-0x45 multi env).
			std::function<void(uint8_t _track, uint8_t _param, uint8_t _value)> sendNrpn;
			// Press these keys one after another (10 ms held, 10 ms apart, machine time).
			std::function<bool(const std::vector<Key>&)> pressKeys;
			std::function<void(const Value&)> toPage;
			std::function<double()> nowMs;
		};

		enum class Engine
		{
			Missing,		// no ROM
			Unsupported,	// another firmware than MM OS 1.32B
			Loading,		// the device prepares or restores
			Booting,		// the firmware starts (MIDI not ready, or the start-up screen)
			Ready			// the main screen came: it takes input
		};

		explicit Desk(Port _port);

		void onPageMessage(const Value& _message);
		void onDeviceSysex(const Bytes& _message);
		void onTelemetry(const Telemetry& _t);
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		void onWorkingKit(const Bytes& _region);
		// A kit value changed outside the desk (host automation, the panel).
		void onHostParam(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value);
		void setEngine(Engine _engine);
		void tick();

		Engine engine() const { return m_engine; }
		bool isReady() const { return m_ready; }
		const RecvSession& recv() const { return m_recv; }
		const std::optional<elektronData::MmPattern>& pattern(uint8_t _slot) const { return m_patterns[_slot & 127]; }
		const std::optional<elektronData::MmKit>& kit(uint8_t _slot) const { return m_kits[_slot & 127]; }
		const std::optional<elektronData::MmKit>& workingKit() const { return m_working; }
		const std::optional<elektronData::MmSong>& song(uint8_t _slot) const { return m_songs[_slot % 24]; }
		const std::optional<elektronData::MmGlobal>& global(uint8_t _slot) const { return m_globals[_slot & 7]; }
		int currentPattern() const { return m_curPattern; }
		int currentKit() const { return m_curKit; }
		int currentSong() const { return m_curSong; }
		int currentGlobal() const { return m_curGlobal; }
		size_t loaded() const;
		double lastRoundTripMs() const { return m_lastRoundTripMs; }
		// The OS 1.32B machine table and enumerations as "mm-desk/catalogue".
		static Value catalogue();

	private:
		enum class Kind { Pattern, Kit, Song, Global };
		struct Ref
		{
			Kind kind;
			uint8_t slot;
			bool operator<(const Ref& _o) const { return kind != _o.kind ? kind < _o.kind : slot < _o.slot; }
			bool operator==(const Ref& _o) const { return kind == _o.kind && slot == _o.slot; }
		};
		struct Push
		{
			Bytes sent;
			double sentMs = 0;
			bool waitingRecv = false;	// queued on the RECV session
			bool waitingReadBack = false;
			std::optional<Bytes> next;	// a newer value while this one is in flight
		};

		void publish(const Value& _m) const;
		void publishDoc(const Ref& _r, bool _pending);
		void publishMachine();
		void result(const Value& _msg, bool _ok, const std::vector<std::string>& _errors, const std::string& _note);

		void handleSet(const Value& _msg);
		void deliverKitLive(const elektronData::MmKit& _from, const elektronData::MmKit& _to, std::vector<std::string>& _notes);
		void pushDump(const Ref& _r, Bytes _dump);
		void onDump(const Ref& _r, const Bytes& _sysex);
		void onStatus(uint8_t _param, uint8_t _value);
		void request(const Ref& _r, bool _urgent);
		void requestStatus();
		void pumpLoads(double _now);
		void pumpRecv(double _now);
		bool pressKeys(const std::vector<Key>& _keys);
		std::string kitState() const;

		Port m_port;
		RecvSession m_recv;
		std::array<std::optional<elektronData::MmPattern>, 128> m_patterns;
		std::array<std::optional<elektronData::MmKit>, 128> m_kits;
		std::array<std::optional<elektronData::MmSong>, 24> m_songs;
		std::array<std::optional<elektronData::MmGlobal>, 8> m_globals;
		std::optional<elektronData::MmKit> m_working;
		std::optional<Bytes> m_workingRegion;
		double m_liveEditMs = -1e9;
		std::map<Ref, Push> m_pushes;
		std::map<Ref, bool> m_pending;		// published as pending (optimistic)

		std::deque<Ref> m_loadQueue;
		std::optional<Ref> m_loading;
		double m_loadSentMs = 0;
		int m_loadRetries = 0;
		double m_lastRequestMs = -1e9;
		bool m_backgroundQueued = false;

		Telemetry m_tel;
		// The step byte moving: in the plug-in the RAM running flag (0x26b46e) can stay 0 while
		// the sequencer plays, so "playing" is the flag or the step advancing (onTelemetry).
		int m_rawStep = -1;
		int m_stepMoves = 0;
		double m_stepMovedMs = -1e9;
		Engine m_engine = Engine::Missing;
		bool m_ready = false;
		bool m_pageReady = false;
		bool m_machineDirty = true;
		int m_curPattern = -1, m_curKit = -1, m_curSong = -1, m_curGlobal = -1, m_songMode = -1;
		int m_queuedPattern = -1;
		int m_lastStep = -1;
		double m_lastStatusMs = -1e9;
		double m_lastTelemetryMs = -1e9;
		double m_lastRoundTripMs = -1;
		double m_keysBusyUntilMs = -1e9;
		std::string m_lastError;
	};
}
