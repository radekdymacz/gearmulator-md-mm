#pragma once

#include "mdDeskDelivery.h"
#include "mdDeskEdit.h"
#include "mdDeskHistory.h"

#include "elektronData/json.h"
#include "mdDataLink/mdDataLink.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mdDesk
{
	// Sequencer telemetry published by the audio thread (MD OS 1.63 RAM bytes).
	struct Telemetry
	{
		int step = -1;			// 0-based, -1 unknown
		int pattern = -1;		// the pattern the sequencer plays, -1 unknown
		bool playing = false;
		bool valid = false;		// false: no telemetry for this firmware
	};

	// MD Desk behind the page: the page sends small commands, the desk edits the
	// documents (mdDeskEdit), delivers the change (pattern/song dumps through
	// mdDataLink, working-kit and global edits as live edits) and publishes the
	// firmware's read-back and the machine state as contract documents. Single
	// threaded: call everything from one thread.
	class Desk
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;

		// Everything the desk needs from its host. The plug-in and the firmware
		// smoke test implement it.
		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			// Live kit parameter: index 0-23, or 24 for the track level (CCs).
			std::function<void(uint8_t _track, uint8_t _index, uint8_t _value)> sendKitParam;
			std::function<void(uint8_t _track, bool _muted)> sendMute;
			// "play" or "stop" as a panel key press; false when not possible here.
			std::function<bool(const std::string& _key)> pressKey;
			std::function<void(const Value& _message)> toPage;
			std::function<double()> nowMs;
		};

		enum class Firmware
		{
			Missing,		// no ROM: the first-run screen
			Unsupported,	// another firmware than MD OS 1.63
			Present			// booting until the first status reply, then ready
		};

		explicit Desk(Port _port);

		void onPageMessage(const Value& _message);
		void onDeviceSysex(const Bytes& _message);
		// A kit parameter changed outside the desk (host automation, MIDI learn,
		// the panel editor). _index 24 = level.
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value);
		void onHostMute(uint8_t _track, bool _muted);
		void onTelemetry(const Telemetry& _telemetry);
		// The working-kit region read from the machine's memory (MD OS 1.63,
		// elektronData::mdWorkingKitFromMemory): kit number plus the kit that plays,
		// unsaved edits included. With it the Sound and Mix documents are firmware
		// truth: panel encoders, host automation and edits restored from a DAW
		// project all show without SAVE KIT. Send it when it changes.
		void onWorkingKitMemory(const Bytes& _region);
		void setFirmware(Firmware _firmware);
		// About 30 times a second: status polling, loading, timeouts, publishing.
		void tick();

		const Documents& documents() const { return m_docs; }
		const mdDataLink::Session& session() const { return m_session; }
		bool isReady() const { return m_ready; }
		bool isBusy() const;
		double lastRoundTripMs() const { return m_lastRoundTripMs; }

		// The OS 1.63 machine table as a "md-desk/machines" document.
		static Value machineCatalogue();

	private:
		struct Pending
		{
			double sentMs = 0;
		};

		void publish(const Value& _message) const;
		void publishDoc(const DocRef& _ref);
		void publishMachine();
		void result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note);
		void flush();

		void handleEdit(const Value& _message);
		void deliver(const Change& _change, std::vector<std::string>& _errors, std::string& _note);
		void deliverPattern(const elektronData::MdPattern& _p, std::vector<std::string>& _errors);
		void deliverSong(const elektronData::MdSong& _s, std::vector<std::string>& _errors);
		void handleUndo(bool _redo, const Value& _message);
		void handleSelect(const Value& _message);

		void onPattern(const elektronData::MdPattern& _p);
		void onKit(const elektronData::MdKit& _k);
		void onSong(const elektronData::MdSong& _s);
		void onGlobal(const elektronData::MdGlobal& _g);
		void onState(const mdDataLink::Session::State& _s);
		void applyWorkingKit();
		void judgeWorkingKit();

		void load(const DocRef& _ref, bool _urgent);
		void pumpLoads(double _now);
		void request(const DocRef& _ref);
		std::optional<uint8_t> currentKit() const;
		void schedule(double _delayMs, std::function<void()> _action);

		Port m_port;
		mdDataLink::Session m_session;
		Documents m_docs;
		History m_history;
		Clipboard m_clipboard;

		std::map<uint8_t, PushSlot<elektronData::MdPattern>> m_patternPush;
		std::map<uint8_t, PushSlot<elektronData::MdSong>> m_songPush;
		std::map<DocRef, double> m_pushSentMs;
		std::map<uint8_t, elektronData::MdKit> m_storedKits;	// last stored-slot dumps
		std::optional<Bytes> m_workingRegion;					// waiting to be applied
		std::optional<elektronData::MdKit> m_workingKit;		// last applied, from memory
		double m_kitStatusAskedMs = -1e9;

		std::deque<DocRef> m_loadQueue;
		std::set<DocRef> m_queued;
		std::optional<DocRef> m_loading;
		double m_loadSentMs = 0;
		int m_loadRetries = 0;
		double m_lastRequestMs = -1e9;
		bool m_backgroundQueued = false;

		std::set<DocRef> m_dirty;
		bool m_machineDirty = true;
		bool m_lastTx = false;
		bool m_pageReady = false;
		bool m_ready = false;
		Firmware m_firmware = Firmware::Present;
		double m_lastStatusMs = -1e9;
		double m_lastLiveEditMs = -1e9;
		double m_lastRoundTripMs = -1;
		std::optional<uint8_t> m_audibleQueue;
		double m_switchReportedMs = -1;
		std::optional<uint8_t> m_lastKit;
		std::optional<uint8_t> m_lastPattern;
		Telemetry m_telemetry;
		std::array<bool, 16> m_mutes{};
		std::vector<std::pair<double, std::function<void()>>> m_scheduled;
	};
}
