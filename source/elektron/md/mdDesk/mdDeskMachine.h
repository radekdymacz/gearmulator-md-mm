#pragma once

#include "mdDeskAdapter.h"
#include "mdDeskDelivery.h"
#include "mdDeskModel.h"
#include "mdDeskRecord.h"
#include "mdDeskTelemetry.h"
#include "mdDeskWorkingKit.h"

#include "deskCore/deskAdapter.h"
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
	// The Machinedrum adapter (P6): mdDataLink, kit delivery, the knob recorder, the load queue,
	// pushes, sequences on facts and the machine state, behind deskCore's Machine protocol. The
	// core never sees SysEx, keys or RAM; this class never publishes documents.
	class MdMachine final : public deskCore::AdapterBase<MdModel, MdAdapter>
	{
	public:
		using Value = elektronData::json::Value;
		using Profile = mdDesk::Profile;
		using Port = DevicePort;

		MdMachine(Profile _profile, Port _port);

		// ---- deskCore::Machine ----
		deskCore::Outcome review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) override;
		deskCore::Outcome submit(const Change& _change, const Documents& _view) override;
		deskCore::Outcome command(const Value& _command, const Documents& _view) override;
		deskCore::Outcome askFor(const Value& _command, const Documents& _view) override;
		void onSysex(const Bytes& _message) override;
		void tick(double _nowMs, const Documents& _view) override;
		void pageReady() override;
		Value state(const Documents& _view) const override;
		deskCore::Capabilities capabilities() const override;
		deskCore::Lifecycle lifecycle() const override { return deskCore::lifecycleOf(facts()); }
		Context context() const override { return {m_session.state().kit}; }
		bool busy() const override;

		// ---- MdAdapter: Machinedrum facts from the device ----
		void setProbe(Probe _probe) override;
		TelemetryEvents onTelemetry(const Telemetry& _telemetry) override;
		void onWorkingKitMemory(const Bytes& _region, const Documents& _view) override;
		void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value, const Documents& _view) override;
		void onHostMute(uint8_t _track, bool _muted) override;
		void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) override;
		const Telemetry& telemetry() const override { return m_telemetry; }
		const mdDataLink::Session::State& linkState() const override { return m_session.state(); }
		bool replied() const override { return m_wire.replied; }
		double lastRoundTripMs() const override { return m_lastRoundTripMs; }

		const Profile& profile() const { return m_profile; }
		size_t loading() const { return m_loads.pending(); }
		// The model's machine commands this adapter runs (tests check them against the table).
		static std::vector<std::string> commandsHandled();

	private:
		using Handler = deskCore::Outcome (MdMachine::*)(const Value&, const Documents&);
		// The adapter's own op -> function map for the model's machine commands.
		static const std::map<std::string, Handler>& handlers();

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

		// What the panel-key features need (one fact each, read by capabilities() and the commands).
		bool panelKeys() const { return m_port.pressKey && !m_profile.wire; }
		bool canLiveRecord() const { return panelKeys() && m_telemetry.valid && m_port.turnKnob; }
		bool canChain() const { return panelKeys() && m_telemetry.valid; }
		deskCore::LifeFacts facts() const;
		void wireSession();
		void startOver();
		double now() const { return m_port.nowMs(); }
		bool keysOnTheirWay() const;
		bool pressKey(const std::string& _key);
		void releaseKeys();
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
		void takeWorkingKit(const Documents* _view);
		void judgeWorkingKit(const elektronData::MdKit& _stored);
		void setBaseChannel(const elektronData::MdGlobal& _g);
		void pumpLoads(double _now);
		void pumpPushes(double _now);
		void pumpRecording(double _now, const Documents& _view);
		void pumpSequence(double _now);
		void runSequence(std::vector<deskCore::SeqStep<Act>> _steps);

		// Machine commands (the table's handler column).
		deskCore::Outcome askSelect(const Value&, const Documents&);
		deskCore::Outcome askKitLoad(const Value&, const Documents&);
		deskCore::Outcome cmdLoad(const Value&, const Documents&);
		deskCore::Outcome cmdSelect(const Value&, const Documents&);
		deskCore::Outcome cmdSaveKit(const Value&, const Documents&);
		deskCore::Outcome cmdReloadKit(const Value&, const Documents&);
		deskCore::Outcome cmdKitLoad(const Value&, const Documents&);
		deskCore::Outcome cmdKitSaveAs(const Value&, const Documents&);
		deskCore::Outcome kitSwitchedBy(uint8_t _slot, bool _load);
		deskCore::Outcome cmdRecord(const Value&, const Documents&);
		deskCore::Outcome cmdRecTrig(const Value&, const Documents&);
		deskCore::Outcome cmdChain(const Value&, const Documents&);
		deskCore::Outcome cmdChainClear(const Value&, const Documents&);
		deskCore::Outcome cmdGlobalSlot(const Value&, const Documents&);
		deskCore::Outcome cmdSelectSong(const Value&, const Documents&);
		deskCore::Outcome cmdReloadSong(const Value&, const Documents&);
		deskCore::Outcome cmdSampleName(const Value&, const Documents&);
		deskCore::Outcome cmdPlay(const Value&, const Documents&);
		deskCore::Outcome cmdStop(const Value&, const Documents&);
		deskCore::Outcome cmdMute(const Value&, const Documents&);

		const Profile m_profile;
		Port m_port;
		mdDataLink::Session m_session;

		deskCore::LoadQueue<DocRef> m_loads;
		bool m_backgroundQueued = false;
		std::map<DocRef, Push> m_pushes;

		KitMemory m_memory;
		bool m_seedWorking = true;			// the working kit comes from the next dump of its slot (no memory)
		double m_kitStatusAskedMs = -1e9;

		Probe m_probe = Probe::Running;
		deskCore::WireFacts m_wire;
		bool m_telemetrySeen = false;
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
