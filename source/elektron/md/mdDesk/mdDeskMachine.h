#pragma once

#include "mdDeskDelivery.h"
#include "mdDeskModel.h"
#include "mdDeskRecord.h"
#include "mdDeskTelemetry.h"
#include "mdDeskWorkingKit.h"

#include "deskCore/deskCore.h"
#include "deskCore/deskLoadQueue.h"
#include "deskCore/deskPacer.h"
#include "deskCore/deskSequence.h"

#include "mdDataLink/mdDataLink.h"

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mdDesk
{
	// What an engine's wire allows, as data (P6). The emulated MD OS 1.63 and a real Machinedrum
	// over HW MIDI speak the same protocol (mdDataLink); the profile is how they differ. The engine
	// map (the plug-in's session) holds one engine record per engine, with its profile.
	struct Profile
	{
		std::string id;				// the engine map's key
		std::string label;			// the LCD's engine label when ready ("EMU OS 1.63")
		bool wire = false;			// at DIN speed: timeouts follow it, the lifecycle follows replies
		bool kitsFirst = false;		// background loads: the 64 small kits first (a pattern is 1.7 s over DIN)
		bool memory = true;			// the device publishes the working kit and the LCD
	};

	const Profile& emulatorProfile();	// "emu"
	const Profile& wireProfile();		// "hw"

	// The Machinedrum adapter (P6): mdDataLink, kit delivery, the knob recorder, the load queue,
	// pushes, sequences on facts and the machine state, behind deskCore's Machine protocol. The
	// core never sees SysEx, keys or RAM; this class never publishes documents.
	class MdMachine final : public deskCore::Machine<MdModel>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Value = elektronData::json::Value;
		using Profile = mdDesk::Profile;
		using Probe = deskCore::LifeFacts::Probe;

		// The device edge: where bytes, CCs and keys go.
		struct Port
		{
			std::function<void(const Bytes&)> sendSysex;
			// Live kit parameter: index 0-23, or 24 for the track level (CCs).
			std::function<void(uint8_t _track, uint8_t _index, uint8_t _value)> sendKitParam;
			std::function<void(uint8_t _track, bool _muted)> sendMute;
			// A panel key press and release; false when not possible here. Keys: "play", "stop",
			// "record", "recordPlay", "page", "trig1".."trig16". Unset: no panel.
			std::function<bool(const std::string& _key)> pressKey;
			// DATA ENTRY knob 0-7 turned by _steps. Unset: no panel.
			std::function<bool(uint8_t _encoder, int _steps)> turnKnob;
			std::function<double()> nowMs;
		};

		MdMachine(Profile _profile, Port _port);

		// ---- deskCore::Machine ----
		deskCore::Outcome review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) override;
		deskCore::Outcome submit(const Change& _change, const Documents& _view) override;
		deskCore::Outcome command(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		void pageReady() override;
		std::vector<Ev> drain() override;
		Value state(const Documents& _view) const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_session.state().kit}; }
		bool busy() const override;

		// ---- Machinedrum facts from the device ----
		// What the device says about the firmware (the emulator; a wire has no probe).
		void setProbe(Probe _probe);
		// Every tick, also without telemetry (valid = false).
		TelemetryEvents onTelemetry(const Telemetry& _telemetry);
		// The working-kit region read from memory (elektronData::mdWorkingKitFromMemory).
		void onWorkingKitMemory(const Bytes& _region);
		// A kit parameter changed outside the editor (host automation, MIDI learn, a modulator).
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value, const Documents& _view);
		void onHostMute(uint8_t _track, bool _muted);
		// App modulation: a CC through the parameter layer, like host automation.
		void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view);

		const Profile& profile() const { return m_profile; }
		const mdDataLink::Session& session() const { return m_session; }
		const Telemetry& telemetry() const { return m_telemetry; }
		double lastRoundTripMs() const { return m_lastRoundTripMs; }
		size_t loading() const { return m_loads.pending(); }
		bool replied() const { return m_replied; }

	private:
		friend struct MdModel;	// the command table's handler column

		// What a sequence step does (deskCore::Sequencer, facts instead of fixed delays).
		enum class Act : uint8_t { Stop, Play, RecordPlay, LoadSong, SelectPattern };
		struct Push
		{
			PushSlot<Document> slot;
			double sentMs = 0;
		};
		struct RecLock
		{
			uint8_t track = 0, param = 0, step = 0;
			double atMs = 0;
		};
		// Panel keys on their way (a fact from the device's telemetry, P6): pressed at atMs, seen
		// pending by the device, and over when the device says none are left.
		struct Keys
		{
			std::vector<std::string> waiting;	// held back while a dump request is in flight
			bool sent = false;
			bool seenPending = false;
			double sentMs = 0;
		};

		deskCore::LifeFacts facts() const;
		void wireSession();
		void startOver();
		double now() const { return m_port.nowMs(); }
		bool keysOnTheirWay() const;
		bool pressKey(const std::string& _key);
		void releaseKeys();
		void observe(const Document& _doc, deskCore::Source _source);
		void forget(const DocRef& _ref);
		void load(const DocRef& _ref, bool _urgent);
		void request(const DocRef& _ref);
		std::optional<uint8_t> currentKit() const { return m_session.state().kit; }
		deskCore::Outcome pushDump(const Document& _doc);

		void onPattern(const elektronData::MdPattern& _p);
		void onKit(const elektronData::MdKit& _k);
		void onSong(const elektronData::MdSong& _s);
		void onGlobal(const elektronData::MdGlobal& _g);
		void onDumpReadBack(const Document& _doc);
		void onState(const mdDataLink::Session::State& _s);
		void takeWorkingKit();
		void judgeWorkingKit();
		void pumpLoads(double _now);
		void pumpPushes(double _now);
		void pumpRecording(double _now, const Documents& _view);
		void pumpSequence(double _now);
		void runSequence(std::vector<deskCore::SeqStep<Act>> _steps);

		// Machine commands (the table's handler column).
		deskCore::Outcome cmdLoad(const Value&, const Documents&);
		deskCore::Outcome cmdSelect(const Value&, const Documents&);
		deskCore::Outcome cmdSaveKit(const Value&, const Documents&);
		deskCore::Outcome cmdReloadKit(const Value&, const Documents&);
		deskCore::Outcome cmdKitSlot(const Value&, const Documents&);
		deskCore::Outcome cmdRecord(const Value&, const Documents&);
		deskCore::Outcome cmdRecTrig(const Value&, const Documents&);
		deskCore::Outcome cmdChain(const Value&, const Documents&);
		deskCore::Outcome cmdChainClear(const Value&, const Documents&);
		deskCore::Outcome cmdGlobalSlot(const Value&, const Documents&);
		deskCore::Outcome cmdSelectSong(const Value&, const Documents&);
		deskCore::Outcome cmdReloadSong(const Value&, const Documents&);
		deskCore::Outcome cmdSampleName(const Value&, const Documents&);
		deskCore::Outcome cmdTransport(const Value&, const Documents&);
		deskCore::Outcome cmdMute(const Value&, const Documents&);

		const Profile m_profile;
		Port m_port;
		mdDataLink::Session m_session;
		std::vector<Ev> m_events;
		std::set<DocRef> m_known;			// documents observed (a background load skips them)

		deskCore::LoadQueue<DocRef> m_loads;
		bool m_backgroundQueued = false;
		std::map<DocRef, Push> m_pushes;

		std::map<uint8_t, elektronData::MdKit> m_storedKits;	// last stored-slot dumps
		WorkingKit m_working;
		double m_kitStatusAskedMs = -1e9;

		Probe m_probe = Probe::Running;
		bool m_replied = false;
		uint32_t m_statusReplies = 0;
		bool m_telemetrySeen = false;
		double m_lastReplyMs = -1e9;
		double m_wireSinceMs = 0;
		double m_lastStatusMs = -1e9;
		double m_lastRoundTripMs = -1;
		Keys m_keys;
		std::optional<uint8_t> m_audibleQueue;
		double m_switchReportedMs = -1;
		std::optional<uint8_t> m_lastKit;
		std::optional<uint8_t> m_lastPattern;
		Telemetry m_telemetry;
		std::array<bool, 16> m_mutes{};
		KnobRecorder m_knobs;
		std::optional<RecLock> m_recLock;
		double m_recordPollMs = -1e9;
		deskCore::Sequencer<Act> m_sequence;
	};
}
