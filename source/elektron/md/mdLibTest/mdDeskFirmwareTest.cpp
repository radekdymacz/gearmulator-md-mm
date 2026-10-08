// P2 MD Desk smoke test against MD OS 1.63 firmware (manual: needs a
// user-supplied ROM). The page is played by this program: it sends the same
// JSON commands the MD Desk page sends, through mdDesk::Desk, and checks what
// the firmware stored - patterns by dump read-back, working-kit edits by SAVE
// KIT + kit dump (the only way to read the working kit).
//
//   mdDeskFirmwareTest <ROM>          the smoke test (edits, read-backs, timing)
//   mdDeskFirmwareTest <ROM> probe    also: telemetry RAM, group removal
//   mdDeskFirmwareTest <ROM> playload PLAY while the desk loads in the background
//   mdDeskFirmwareTest <ROM> samples  P9: WAV files into UW ROM slots (SDS), the waveforms read back
//
// Exits 77 (skip) without arguments.

#include "contractCheck.h"
#include "mdFirmwareSession.h"

#include "mdDesk/mdDesk.h"
#include "mdDesk/mdDeskKeys.h"
#include "mdDesk/mdDeskLibrary.h"
#include "mdDesk/mdDeskWirePort.h"
#include "deskCore/deskPacer.h"
#include "deskWire/mdWire.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdSong.h"
#include "elektronData/mdWorkingKit.h"
#include "elektronData/syxImport.h"

#include "elektronData/mdSamples.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mddeskdevice.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdsequencerstate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <set>
#include <functional>
#include <map>
#include <memory>

using namespace mdFirmwareSession;
namespace ed = elektronData;
using ed::json::Value;

namespace
{
	int g_failures = 0;
	contractCheck::Checker g_contract(MDDESK_SCHEMA);

	void check(const bool _condition, const std::string& _what)
	{
		std::printf("  %s %s\n", _condition ? "ok  " : "FAIL", _what.c_str());
		if(!_condition)
			++g_failures;
	}

	double ms(const uint64_t _frames) { return _frames * 1000.0 / g_rate; }

	Value parse(const std::string& _json)
	{
		auto v = ed::json::parse(_json);
		require(v.has_value(), "bad JSON: " + _json);
		return *v;
	}

	// The page, the plug-in and the machine in one loop. Desk output is queued and
	// sent between steps, as the plug-in's MIDI path does: the desk never re-enters.
	class Rig
	{
	public:
		explicit Rig(const Bytes& _rom, const std::string& _romName, const Bytes& _patchRam = {}, const bool _waitSplash = true)
			: m_machine(_rom, _romName, _patchRam, _waitSplash)
		{
			mdDesk::Desk::Port port;
			port.device.sendSysex = [this](const Bytes& _b) { m_patternDumps += _b.size() > 6 && _b[6] == 0x67; m_out.push_back(_b); };
			// Kit values and mutes as the wire's CCs on the base channel (deskWire, the plug-in's
			// encoders), the base channel as the adapter gives it.
			port.device.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				// As the plug-in's parameter (pluginLike): a value it already holds changes nothing and sends nothing.
				if(pluginLike)
				{
					auto& last = m_paramHeld[_t * 25 + _i];
					if(last == _v)
						return;
					last = _v;
				}
				if(const auto cc = deskWire::md::kitParam(m_channel, _t, _i, _v))
					m_out.push_back(*cc);
			};
			// The engine's direct CC, past the plug-in's parameter (mdSessionMd.cpp): always sent.
			port.device.sendHeldParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				if(const auto cc = deskWire::md::kitParam(m_channel, _t, _i, _v))
					m_out.push_back(*cc);
			};
			port.device.pressKey = [this](const std::string& _key)
			{
				const auto states = md::panelKeySequence(md::MachineModel::Machinedrum, _key);
				if(_key == "hold:function" || _key == "release:function")
					m_functionHeld = _key == "hold:function";
				for(const auto& st : states)
					m_keys.push_back({st, 40});
				return !states.empty();
			};
			port.device.turnKnob = [this](const uint8_t _e, const int _steps)
			{
				const auto command = md::panelEncoderCommand(md::MachineModel::Machinedrum, static_cast<md::PanelEncoder>(_e));
				// One packet a step. Control All (FUNCTION held): one a 128-frame block after the keys on their
				// way, as md::DeskDevice sends them in the plug-in (many at one instant, the firmware loses steps
				// then). The knob recorder's turns (live recording) at once, as this rig always sent them.
				for(int i = 0; command && i < std::abs(_steps); ++i)
				{
					const md::PanelPacket step{*command, static_cast<uint8_t>(_steps > 0 ? 0x01 : 0xff)};
					if(!m_functionHeld && m_keys.empty())
						m_machine.hardware().trySendPanelEvent(step.row, step.mask);
					else
						m_keys.push_back({step, 128.0 * 1000 / g_rate});
				}
				return command.has_value();
			};
			port.device.sendMute = [this](const uint8_t _t, const bool _on)
			{
				if(const auto cc = deskWire::md::mute(m_channel, _t, _on))
					m_out.push_back(*cc);
			};
			port.device.sendNote = [this](const uint8_t _ch, const uint8_t _n, const uint8_t _v)
			{
				if(const auto b = deskWire::md::note(_ch, _n, _v))
					m_out.push_back(*b);
			};
			port.device.baseChannel = [this](const uint8_t _ch) { m_channel = _ch; };
			port.toPage = [this](const Value& _m) { g_contract(_m); onPage(_m); };
			port.device.nowMs = [this] { return ms(m_machine.now()); };
			// P9: the audition as md::DeskDevice has it; the test is its audio thread (auditionMix).
			port.device.audition = [this](const ed::AuditionClip& _c) { return m_audition.play(_c); };
			port.device.auditionStatus = [this] { return m_audition.status(); };
			m_desk = std::make_unique<mdDesk::Desk>(port);
			m_machine.onSysex = [this](const Bytes& _b) { m_in.push_back(_b); };
		}

		Machine& machine() { return m_machine; }
		mdDesk::Desk& desk() { return *m_desk; }

		void page(const std::string& _json)
		{
			m_lastResult.reset();
			g_contract.command(parse(_json));
			m_desk->onPageMessage(parse(_json));
		}

		// A command the user confirms, as the page does: an ask it raises is answered by sending the
		// ask's command again with force.
		void pageConfirmed(const std::string& _json)
		{
			m_lastAsk.reset();
			page(_json);
			if(!m_lastAsk)
				return;
			Value c = *m_lastAsk->find("command");
			std::printf("  ask %s (confirmed)\n", m_lastAsk->find("ask")->asString().c_str());
			c.put("force", true);
			page(ed::json::write(c));
		}

		// Advance emulated time, delivering desk output and machine replies.
		void run(const double _ms)
		{
			const auto end = m_machine.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(m_machine.now() < end)
				stepOnce();
		}

		bool runUntil(const std::function<bool()>& _done, const double _timeoutMs)
		{
			const auto end = m_machine.now() + static_cast<uint64_t>(_timeoutMs * g_rate / 1000);
			while(m_machine.now() < end)
			{
				if(_done())
					return true;
				stepOnce();
			}
			return _done();
		}

		const std::optional<Value>& lastResult() const { return m_lastResult; }
		const std::optional<Value>& lastAsk() const { return m_lastAsk; }
		void clearAsk() { m_lastAsk.reset(); }
		const std::optional<Value>& lastError() const { return m_lastError; }
		bool pageTx() const { return m_tx; }
		std::optional<Value> pageDoc(const std::string& _kind, const int _slot) const
		{
			const auto it = m_docs.find(_kind + ":" + std::to_string(_slot));
			return it == m_docs.end() ? std::nullopt : std::optional<Value>(it->second);
		}
		// How many documents of a kind the page was sent.
		int pageDocCount(const std::string& _kind) const
		{
			const auto it = m_docCounts.find(_kind);
			return it == m_docCounts.end() ? 0 : it->second;
		}
		// Where the last document of a kind came from ("memory", "dump", ...).
		std::string pageDocSource(const std::string& _kind) const
		{
			const auto it = m_sources.find(_kind);
			return it == m_sources.end() ? std::string() : it->second;
		}
		const std::optional<Value>& machineDoc() const { return m_machineDoc; }

		// The page's own telemetry, as published ({"type":"telemetry", step, pattern, playing,
		// recording, valid}) - not the rig's own read of the device (telemetry() below). The
		// transport checks (PLAY/STOP/REC) want what the page sees, the same as machineDoc().
		struct PageTelemetry
		{
			bool valid = false;
			int step = -1;
			int pattern = -1;
			bool playing = false;
			bool recording = false;
		};
		PageTelemetry pageTelemetry() const
		{
			PageTelemetry t;
			if(!m_publishedTelemetry)
				return t;
			const auto& v = *m_publishedTelemetry;
			if(const auto* p = v.find("valid")) t.valid = p->asBool();
			if(const auto* p = v.find("step")) t.step = static_cast<int>(p->asNumber());
			if(const auto* p = v.find("pattern")) t.pattern = static_cast<int>(p->asNumber());
			if(const auto* p = v.find("playing")) t.playing = p->asBool();
			if(const auto* p = v.find("recording")) t.recording = p->asBool();
			return t;
		}

		// A direct read, outside the desk (the test's own oracle).
		std::optional<ed::MdKit> saveAndReadKit(const uint8_t _slot)
		{
			flushOut();
			m_machine.send(ed::mdSaveKit(_slot));
			return ed::decodeMdKit(m_machine.request(ed::mdKitRequest(_slot), ed::g_mdKitDump));
		}

		// The pattern dumps the desk sent (the pacing: one per pattern change at most).
		size_t patternDumps() const { return m_patternDumps; }

		std::optional<ed::MdPattern> readPattern(const uint8_t _slot)
		{
			flushOut();
			return ed::decodeMdPattern(m_machine.request(ed::mdPatternRequest(_slot), ed::g_mdPatternDump));
		}

	private:
		uint8_t m_channel = 0;	// the machine's base channel (the adapter's fact)
	public:
		// the plug-in's delivery (keepedits): sendKitParam drops a value equal to the last one, and what is queued goes
		// to the machine in one go (one audio block)
		bool pluginLike = false;
		// The plug-in's parameters hold the kit as it was loaded; what the machine changes by itself (a Control All
		// gesture on its panel, a machine change's initial values) they do not learn.
		void seedPluginParams(const ed::MdKit& _kit)
		{
			for(size_t t = 0; t < 16; ++t)
			{
				for(size_t i = 0; i < 24; ++i)
					m_paramHeld[t * 25 + i] = _kit.params[t][i];
				m_paramHeld[t * 25 + 24] = _kit.levels[t];
			}
		}
		// What the desk sent the machine (B-010: the traffic of an edit), bytes and messages.
		size_t bytesToMachine = 0;
		size_t messagesToMachine = 0;
		size_t patternRequests = 0;
		std::map<int, size_t> messageKinds;	// SysEx: the command byte; else the status nibble
		std::map<int, uint64_t> ingestFrames;	// the longest a message of that kind took into the firmware
		// The desk's messages go to the device and the rig goes on at once, as in the plug-in (the
		// desk keeps ticking while a dump is on its way in).
		bool postOnly = false;
	private:
		void toMachine(const Bytes& _b)
		{
			bytesToMachine += _b.size();
			++messagesToMachine;
			patternRequests += _b.size() > 6 && _b[0] == 0xf0 && _b[6] == 0x68;
			const int kind = _b.size() > 6 && _b[0] == 0xf0 ? _b[6] : (_b.empty() ? 0 : (_b[0] & 0xf0));
			++messageKinds[kind];
			if(postOnly)
			{
				m_machine.post(_b);
				return;
			}
			const auto frames = m_machine.send(_b);
			ingestFrames[kind] = std::max(ingestFrames[kind], frames);
		}
		std::array<int, 16 * 25> m_paramHeld = [] { std::array<int, 16 * 25> a{}; a.fill(-1); return a; }();
		size_t m_patternDumps = 0;

		void flushOut()
		{
			while(!m_out.empty())
			{
				const auto b = m_out.front();
				m_out.pop_front();
				toMachine(b);
			}
			deliverIn();
		}

		void deliverIn()
		{
			while(!m_in.empty())
			{
				const auto b = m_in.front();
				m_in.pop_front();
				m_desk->onDeviceSysex(b);
			}
		}

		void stepOnce()
		{
			// pluginLike: what is queued goes in one go, as the plug-in hands the machine a block's MIDI events
			if(pluginLike && !m_out.empty())
			{
				while(!m_out.empty())
				{
					toMachine(m_out.front());
					m_out.pop_front();
				}
			}
			else if(!m_out.empty())
			{
				const auto b = m_out.front();
				m_out.pop_front();
				toMachine(b);
			}
			else if(!m_keys.empty())
			{
				const auto [k, holdMs] = m_keys.front();
				m_keys.pop_front();
				m_machine.hardware().trySendPanelEvent(k.row, k.mask);
				m_machine.run(holdMs);
			}
			else
				m_machine.step();
			m_leds.update(m_machine.read8(md::SequencerState::g_stepAddress), m_machine.read8(md::SequencerState::g_stoppedAddress),
				m_machine.read8(md::SequencerState::g_recordLedAddress), m_machine.now() - m_ledsAt);
			m_ledsAt = m_machine.now();
			deliverIn();
			const auto now = m_machine.now();
			if(now - m_lastTick < g_rate / 30)
				return;
			m_lastTick = now;
			const int step = m_machine.read8(g_playheadAddress);
			if(step != m_lastStep)
			{
				m_lastStep = step;
				m_stepChangedAt = now;
			}
			mdDesk::Telemetry t;
			t.valid = true;
			t.step = step;
			t.pattern = m_machine.read8(0x28d205);
			t.playing = m_leds.playing();
			t.recording = m_leds.recording();
			t.gridEdit = m_leds.gridEdit();
			t.knobPage = m_machine.read8(md::SequencerState::g_knobPageAddress);
			t.panelPending = static_cast<int>(m_keys.size());	// the rig's key queue, as md::Device reports its own
			// P4, as md::Device publishes them.
			m_boot.update(m_machine.read8(md::BootAnimation::g_mainScreenAddress), now - m_bootAt);
			m_bootAt = now;
			t.bootAnimation = m_boot.state();
			t.mutes = (m_machine.read8(md::ChainAndMutes::g_muteAddress) << 8) | m_machine.read8(md::ChainAndMutes::g_muteAddress + 1);
			{
				const auto long32 = [&](const uint32_t _a)
				{
					return (uint32_t(m_machine.read8(_a)) << 24) | (uint32_t(m_machine.read8(_a + 1)) << 16)
						| (uint32_t(m_machine.read8(_a + 2)) << 8) | m_machine.read8(_a + 3);
				};
				const auto a = md::ChainAndMutes::g_chainAddress;
				const auto active = long32(a), next = long32(a + 4), length = long32(a + 8);
				t.chainKnown = active <= 1 && length <= 16 && next <= 16;
				if(t.chainKnown)
				{
					t.chain.active = active == 1;
					t.chain.next = static_cast<int>(next);
					for(uint32_t i = 0; i < length; ++i)
						t.chain.patterns.push_back(static_cast<uint8_t>(long32(a + 12 + 4 * i) & 0x7f));
				}
			}
			{
				const auto panel = m_machine.hardware().getFrontPanelSnapshot();
				using L = md::FrontPanel::ModeLed;
				if(panel.wasLedBankWritten(md::FrontPanel::LedBank::Mode))
					t.bankGroup = panel.getModeLed(L::BankGroupEH) ? 1 : panel.getModeLed(L::BankGroupAD) ? 0 : -1;
			}
			m_lastTelemetry = t;
			m_desk->onTelemetry(t);
			// The working-kit region, as md::Device publishes it: when it changed.
			Bytes region(ed::g_mdWorkingKitRegionSize);
			for(size_t i = 0; i < region.size(); ++i)
				region[i] = m_machine.read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
			if(region != m_lastRegion)
			{
				m_lastRegion = region;
				m_desk->onWorkingKitMemory(region);
			}
			if(m_sampleWanted)
			{
				const auto memory = md::sampleMemoryOf(m_machine.hardware());
				const auto signature = ed::mdSampleSignature(memory);
				auto& uc = m_machine.hardware().getUC();
				const bool quiet = !uc.flashDirty() || uc.flashIdleCycles() > 12'000'000;
				if(signature == m_sampleSeen && quiet && (signature != m_samplePublished || !m_samplesDoc))
				{
					m_samplePublished = signature;
					m_desk->onSampleBank(ed::readMdSampleBank(memory));
				}
				m_sampleSeen = signature;
			}
			m_desk->tick();
		}

	public:
		// B-014: what the page is sent, as JSON text (bytes the bridge carries and the page parses)
		size_t pageBytes = 0, pagePatternDocBytes = 0, pagePatternDocs = 0;
	private:
		void onPage(const Value& _m)
		{
			if(std::getenv("PLOCK_PAGEBYTES"))
			{
				const auto n = elektronData::json::write(_m).size();
				pageBytes += n;
				if(const auto* k = _m.find("kind"); k && k->isString() && k->asString() == "pattern")
				{
					pagePatternDocBytes += n;
					++pagePatternDocs;
				}
			}
			const auto* type = _m.find("type");
			if(!type)
				return;
			const auto& t = type->asString();
			if(t == "result")
				m_lastResult = _m;
			else if(t == "ask")
				m_lastAsk = _m;
			else if(t == "error")
			{
				m_lastError = _m;
				std::printf("  page error: %s\n", _m.find("message")->asString().c_str());
			}
			else if(t == "doc")
			{
				const auto& doc = *_m.find("doc");
				m_docs[_m.find("kind")->asString() + ":" + std::to_string(int(doc.find("slot")->asNumber()))] = doc;
				++m_docCounts[_m.find("kind")->asString()];
				if(const auto* src = _m.find("source"); src && src->isString())
					m_sources[_m.find("kind")->asString()] = src->asString();
			}
			else if(t == "machine")
			{
				m_machineDoc = *_m.find("doc");
				m_tx = m_machineDoc->find("desk")->find("tx")->asBool();
			}
			else if(t == "telemetry")
				m_publishedTelemetry = _m;
			else if(t == "samples")
				m_samplesDoc = *_m.find("doc");
			else if(t == "sampleWave")
				m_sampleWave = _m;
			else if(t == "audition")
				m_auditionMsg = _m;
			else if(t == "sampleLoad")
			{
				m_sampleLoad = _m;
				if(_m.find("state")->asString() != "sending")
					std::printf("  sampleLoad %s: %s\n", _m.find("state")->asString().c_str(), _m.find("text")->asString().c_str());
			}
		}

		Machine m_machine;
		std::unique_ptr<mdDesk::Desk> m_desk;
		std::deque<Bytes> m_out;
		std::deque<Bytes> m_in;
		std::deque<std::pair<md::PanelPacket, double>> m_keys;
		bool m_functionHeld = false;	// hold:function pressed and not released (Control All)	// panel states and how long each is held (ms)
		uint64_t m_lastTick = 0;
		int m_lastStep = -1;
		uint64_t m_stepChangedAt = 0;
		std::optional<Value> m_lastResult;
		std::optional<Value> m_lastError;
		std::map<std::string, Value> m_docs;
		std::map<std::string, int> m_docCounts;
		std::map<std::string, std::string> m_sources;
		std::optional<Value> m_lastAsk;
		std::optional<Value> m_machineDoc;
		bool m_tx = false;
		std::optional<Value> m_publishedTelemetry;
		std::optional<Value> m_samplesDoc;
		std::optional<Value> m_sampleLoad;
		std::optional<Value> m_sampleWave, m_auditionMsg;
		ed::AuditionMixer m_audition;
		uint64_t m_sampleSeen = 0, m_samplePublished = 0;
		bool m_sampleWanted = false;
		Bytes m_lastRegion;
		md::SequencerState m_leds;
		uint64_t m_ledsAt = 0;
		md::BootAnimation m_boot;
		uint64_t m_bootAt = 0;
		mdDesk::Telemetry m_lastTelemetry;

	public:
		const mdDesk::Telemetry& telemetry() const { return m_lastTelemetry; }
		// P9: the UW sample bank, as md::DeskDevice publishes it (when the memory changed and has been quiet).
		void watchSamples() { m_sampleWanted = true; }
		const std::optional<Value>& samplesDoc() const { return m_samplesDoc; }
		const std::optional<Value>& sampleLoad() const { return m_sampleLoad; }
		const std::optional<Value>& sampleWave() const { return m_sampleWave; }
		const std::optional<Value>& auditionMessage() const { return m_auditionMsg; }
		// The audition's audio, as the device's audio thread mixes it into the main output.
		void auditionMix(std::vector<float>& _out, const double _rate) { m_audition.mix(_out.data(), nullptr, _out.size(), _rate); }
		std::string machineString(std::initializer_list<const char*> _path) const
		{
			if(!m_machineDoc)
				return {};
			const Value* v = &*m_machineDoc;
			for(const auto* k : _path)
				if(!(v = v->find(k)))
					return {};
			return v->isString() ? v->asString() : std::string();
		}
	};

	bool resultOk(const Rig& _rig)
	{
		const auto& r = _rig.lastResult();
		if(!r)
			return false;
		const bool ok = r->find("ok")->asBool();
		if(!ok)
			for(const auto& e : r->find("errors")->asArray())
				std::printf("  refused: %s\n", e.asString().c_str());
		return ok;
	}

	int intAt(const Value& _v, std::initializer_list<const char*> _path)
	{
		const Value* v = &_v;
		for(const auto* k : _path)
		{
			v = v->find(k);
			if(!v)
				return -1;
		}
		return v->isNumber() ? static_cast<int>(v->asNumber()) : -1;
	}

	void smoke(Rig& _rig)
	{
		auto& m = _rig.machine();
		auto& desk = _rig.desk();
		std::puts("== MD Desk smoke test: page commands -> firmware -> read-back");
		_rig.page(R"({"op":"ready"})");
		const bool ready = _rig.runUntil([&]
		{
			const auto& s = desk.linkState();
			return desk.isReady() && s.pattern && s.kit && desk.documents().patterns.count(*s.pattern)
				&& desk.documents().kits.count(*s.kit) && desk.documents().global;
		}, 3000);
		check(ready, "ready: current pattern, its kit and the global settings loaded");
		if(!ready)
			return;
		const auto pattern = *desk.linkState().pattern;
		const auto kit = *desk.linkState().kit;
		std::printf("  current pattern %s, kit %u\n", ed::mdPatternName(pattern).c_str(), kit + 1);

		// 1. A trig, through the page command, read back from the firmware.
		const auto before = desk.documents().patterns.at(pattern);
		const bool had = ed::hasTrig(before, 0, 5);
		const auto t0 = m.now();
		_rig.page("{\"op\":\"trig\",\"p\":" + std::to_string(pattern) + ",\"t\":0,\"s\":5,\"id\":1}");
		check(resultOk(_rig), "trig command accepted");
		const bool confirmed = _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 1000);
		const auto confirmMs = ms(m.now() - t0);
		check(confirmed, "TX LED off: the firmware read-back confirmed the edit");
		std::printf("  command -> confirmed read-back: %.1f ms emulated (desk round trip %.1f ms)\n", confirmMs,
			desk.lastRoundTripMs());
		const auto oracle = _rig.readPattern(pattern);
		check(oracle && ed::hasTrig(*oracle, 0, 5) != had, "the firmware holds the toggled trig");
		const auto shown = _rig.pageDoc("pattern", pattern);
		bool pageHas = false;
		if(shown)
			for(const auto& s : shown->find("tracks")->asArray()[0].find("trigs")->asArray())
				pageHas |= static_cast<int>(s.asNumber()) == 5;
		check(pageHas != had, "the page's pattern document shows it");

		// 2. A lock on that trig (turn the trig on first if it was toggled off).
		if(had)
		{
			_rig.page("{\"op\":\"trig\",\"p\":" + std::to_string(pattern) + ",\"t\":0,\"s\":5,\"on\":true}");
			_rig.runUntil([&] { return !desk.isBusy(); }, 4000);
		}
		// (B-014: a dump waits for the one before it to have had its MIDI cable time, up to 1.73 s)
		_rig.page("{\"op\":\"lock\",\"p\":" + std::to_string(pattern) + ",\"t\":0,\"i\":12,\"s\":5,\"v\":33}");
		check(resultOk(_rig), "lock command accepted");
		_rig.runUntil([&] { return !desk.isBusy(); }, 4000);
		const auto locked = _rig.readPattern(pattern);
		check(locked && ed::lockValue(*locked, 0, 12, 5) == uint8_t{33}, "the firmware holds the lock");

		// 3. A working-kit value via the CC path; SAVE KIT is the only way to read it.
		const auto kitBefore = desk.documents().working->kit;
		const uint8_t newDist = static_cast<uint8_t>((kitBefore.params[0][16] + 17) & 0x7f);
		_rig.page("{\"op\":\"param\",\"k\":" + std::to_string(kit) + ",\"t\":0,\"i\":16,\"v\":"
			+ std::to_string(newDist) + "}");
		check(resultOk(_rig), "kit param command accepted");
		_rig.run(50);
		check(_rig.machineDoc() && _rig.machineDoc()->find("kit")->find("working")->asString() == "edited",
			"the page shows the kit as edited (not saved on the machine)");
		auto saved = _rig.saveAndReadKit(kit);
		check(saved && saved->params[0][16] == newDist, "after SAVE KIT the stored kit holds the CC edit");

		// 4. Machine assignment (0x5b) and the synthesis values sent after it.
		const auto efm = *ed::mdMachineModel("EFM-SD");
		_rig.page("{\"op\":\"machine\",\"k\":" + std::to_string(kit) + ",\"t\":1,\"model\":" + std::to_string(efm)
			+ ",\"keepFx\":true}");
		check(resultOk(_rig), "machine command accepted");
		_rig.run(80);
		saved = _rig.saveAndReadKit(kit);
		const auto& want = desk.documents().working->kit;
		check(saved && saved->models[1] == efm, "track 2 plays EFM-SD");
		check(saved && saved->params[1] == want.params[1], "all 24 values of track 2 match the desk's kit");

		// 5. LFO, master effect, mute group, kit name: the SysEx live edits.
		_rig.page("{\"op\":\"lfo\",\"k\":" + std::to_string(kit) + ",\"t\":2,\"field\":\"shape1\",\"v\":4}");
		_rig.page("{\"op\":\"lfo\",\"k\":" + std::to_string(kit) + ",\"t\":2,\"field\":\"param\",\"v\":7}");
		_rig.page("{\"op\":\"masterFx\",\"k\":" + std::to_string(kit) + ",\"fx\":\"rhythmEcho\",\"i\":2,\"v\":55}");
		_rig.page("{\"op\":\"masterFx\",\"k\":" + std::to_string(kit) + ",\"fx\":\"gateBox\",\"i\":6,\"v\":44}");
		_rig.page("{\"op\":\"group\",\"k\":" + std::to_string(kit) + ",\"t\":3,\"kind\":\"mute\",\"target\":4}");
		_rig.page("{\"op\":\"group\",\"k\":" + std::to_string(kit) + ",\"t\":5,\"kind\":\"trig\",\"target\":6}");
		_rig.page("{\"op\":\"kitName\",\"k\":" + std::to_string(kit) + ",\"name\":\"DESK SMOKE\"}");
		_rig.run(80);
		saved = _rig.saveAndReadKit(kit);
		check(saved && saved->lfos[2].shape1 == 4 && saved->lfos[2].param == 7, "LFO shape and target (0x62)");
		check(saved && saved->masterFx[ed::MdKit::RhythmEcho][2] == 55, "rhythm echo parameter (0x5d)");
		check(saved && saved->masterFx[ed::MdKit::GateBox][6] == 44, "gate box parameter (0x5e)");
		check(saved && saved->muteGroups[3] == 4, "mute group (0x66)");
		check(saved && saved->trigGroups[5] == 6, "trig group (0x65)");
		check(saved && std::string(reinterpret_cast<const char*>(saved->name.data()), 10) == "DESK SMOKE",
			"kit name (0x55)");

		// Group removal: target 0x7f.
		_rig.page("{\"op\":\"group\",\"k\":" + std::to_string(kit) + ",\"t\":3,\"kind\":\"mute\",\"target\":null}");
		_rig.page("{\"op\":\"group\",\"k\":" + std::to_string(kit) + ",\"t\":5,\"kind\":\"trig\",\"target\":null}");
		_rig.run(50);
		saved = _rig.saveAndReadKit(kit);
		std::printf("  group removal read back: mute[3] = 0x%02x, trig[5] = 0x%02x\n", saved ? saved->muteGroups[3] : 0,
			saved ? saved->trigGroups[5] : 0);
		check(saved && saved->muteGroups[3] == ed::MdKit::g_noGroup && saved->trigGroups[5] == ed::MdKit::g_noGroup,
			"removing a group stores none (0xff)");

		// 6. Undo of the DIST edit goes back through the CC path.
		_rig.page(R"({"op":"undo"})");
		_rig.page(R"({"op":"undo"})");
		_rig.run(50);
		const auto undoneTo = desk.documents().working->kit;
		saved = _rig.saveAndReadKit(kit);
		check(saved && saved->trigGroups[5] == undoneTo.trigGroups[5] && saved->muteGroups[3] == undoneTo.muteGroups[3],
			"undo restores the groups on the machine");

		// 7. Global routing (0x5c) and tempo (0x61), read back from the global dump.
		const auto gslot = desk.documents().global->position;
		_rig.page(R"({"op":"route","t":7,"out":"C"})");
		_rig.page(R"({"op":"tempo","bpm":131})");
		_rig.run(200);
		const auto g = ed::decodeMdGlobal(m.request(ed::mdGlobalRequest(gslot), ed::g_mdGlobalDump));
		check(g && g->routing[7] == 2 && g->tempo == 131 * 24, "routing and tempo live edits land in the global");

		// 8. A song row, pushed and read back.
		const auto song = desk.linkState().song.value_or(0);
		_rig.runUntil([&] { return desk.documents().songs.count(song) > 0; }, 1000);
		_rig.page("{\"op\":\"rowInsert\",\"s\":" + std::to_string(song) + ",\"i\":0,\"row\":{\"kind\":\"pattern\","
			"\"pattern\":" + std::to_string(pattern) + ",\"repeats\":1,\"start\":0,\"end\":16,\"tempo\":null,"
			"\"mutes\":[3]}}");
		check(resultOk(_rig), "song row insert accepted");
		const auto s0 = m.now();
		_rig.runUntil([&] { return !desk.isBusy(); }, 1500);
		std::printf("  song push -> read-back %.1f ms emulated\n", ms(m.now() - s0));
		const auto sgBack = ed::decodeMdSong(m.request(ed::mdSongRequest(song), ed::g_mdSongDump));
		check(sgBack && sgBack->rows.size() >= 2 && sgBack->rows[0].pattern == pattern && sgBack->rows[0].mutes == 8,
			"the firmware holds the new song row");
		check(desk.linkState().songReloadNeeded, "the current song is marked reload-needed");

		// 9. Pattern select while playing: queued until the sequencer switches.
		auto next = desk.documents().patterns.at(pattern);
		next.position = static_cast<uint8_t>((pattern + 1) & 127);
		next.kit = kit;
		next.scale = 0;
		next.length = 16;
		m.send(ed::encodeMdPattern(next));
		_rig.run(100);
		_rig.page(R"({"op":"play"})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; },
			2000);
		_rig.run(300);
		const auto q0 = m.now();
		_rig.page("{\"op\":\"select\",\"p\":" + std::to_string(next.position) + "}");
		_rig.run(20);
		const auto queued = _rig.machineDoc() ? intAt(*_rig.machineDoc(), {"desk", "queued"}) : -1;
		check(queued == next.position, "the page shows the pattern as queued");
		double statusAt = -1, ramAt = -1, clearedAt = -1;
		_rig.runUntil([&]
		{
			if(statusAt < 0 && desk.linkState().pattern == next.position)
				statusAt = ms(m.now() - q0);
			if(ramAt < 0 && m.read8(0x28d205) == next.position)
				ramAt = ms(m.now() - q0);
			if(clearedAt < 0 && _rig.machineDoc() && intAt(*_rig.machineDoc(), {"desk", "queued"}) < 0)
				clearedAt = ms(m.now() - q0);
			return statusAt >= 0 && ramAt >= 0 && clearedAt >= 0;
		}, 8000);
		std::printf("  queued -> status reports switch %.0f ms, RAM 0x28d205 %.0f ms, page clears queue %.0f ms\n",
			statusAt, ramAt, clearedAt);
		check(clearedAt >= 0, "the queue clears when the new pattern becomes current");
		check(clearedAt > statusAt, "the queue clears at the playhead wrap, after the early status switch");
		_rig.page(R"({"op":"stop"})");
		_rig.run(300);
		check(!_rig.pageTelemetry().playing,
			"the page shows the machine stopped");
	}

	// How does the firmware store "no group"? Candidates for the target byte.
	void probeGroups(Rig& _rig)
	{
		std::puts("== probe: removing a mute/trig group");
		auto& m = _rig.machine();
		const auto kit = *_rig.desk().linkState().kit;
		for(const uint8_t candidate : {uint8_t{0x7f}, uint8_t{0x10}, uint8_t{0x40}, uint8_t{0x0f}})
		{
			m.send(ed::mdSetMuteGroup(3, 4));
			m.send(ed::mdSetTrigGroup(5, 6));
			m.send(ed::mdSetMuteGroup(3, candidate));
			m.send(ed::mdSetTrigGroup(5, candidate));
			const auto k = _rig.saveAndReadKit(kit);
			std::printf("  target 0x%02x -> mute[3] 0x%02x, trig[5] 0x%02x\n", candidate, k ? k->muteGroups[3] : 0,
				k ? k->trigGroups[5] : 0);
		}
	}

	// Which RAM bytes tell "playing"? Stopped vs playing snapshots.
	void probeTelemetry(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== probe: RAM byte for 'playing'");
		std::vector<Bytes> stopped, playing;
		for(int i = 0; i < 3; ++i)
		{
			stopped.push_back(m.snapshotRam());
			_rig.run(137);
		}
		m.panel(md::PanelControl::Play);
		_rig.run(400);
		for(int i = 0; i < 3; ++i)
		{
			playing.push_back(m.snapshotRam());
			_rig.run(173);
		}
		m.panel(md::PanelControl::Stop);
		_rig.run(400);
		const auto after = m.snapshotRam();
		int shown = 0;
		for(size_t a = 0; a < after.size() && shown < 24; ++a)
		{
			const auto s = stopped[0][a];
			const auto p = playing[0][a];
			if(s == p || s > 1 || p > 1)
				continue;
			bool stable = after[a] == s;
			for(const auto& x : stopped)
				stable &= x[a] == s;
			for(const auto& x : playing)
				stable &= x[a] == p;
			if(!stable)
				continue;
			std::printf("  0x%06zx: stopped %u, playing %u\n", 0x200000 + a, s, p);
			++shown;
		}
	}
}

	// P3: the working kit from memory. A panel encoder edit the desk never sent, and
	// a DAW project restored into a new machine, both show without SAVE KIT.
	Bytes workingKitTruth(Rig& _rig)
	{
		auto& m = _rig.machine();
		auto& desk = _rig.desk();
		std::puts("== P3 working kit from memory (panel edit, DAW restore)");
		const auto kit = *desk.linkState().kit;
		const auto before = desk.documents().working->kit.params[0][2];
		m.send(ed::mdSetStatus(ed::MdStatus::Track, 0));
		m.run(40);
		// DATA ENTRY C on the synthesis page of track 1: five steps, like a finger.
		const int dir = before > 100 ? -1 : 1;
		const auto t0 = m.now();
		for(int i = 0; i < 5; ++i)
			m.hardware().trySendPanelEvent(0x32, dir > 0 ? 0x01 : 0xff);
		const auto want = static_cast<uint8_t>(before + 5 * dir);
		const bool seen = _rig.runUntil([&]
		{
			const auto doc = _rig.pageDoc("workingKit", kit);
			return doc && doc->find("tracks")->asArray()[0].find("synth")->asArray()[2].asNumber() == want;
		}, 1000);
		check(seen, "panel encoder edit shows in the page's kit document");
		std::printf("  panel encoder -> page kit document: %.1f ms emulated (value %u -> %u)\n", ms(m.now() - t0), before, want);
		const auto& md = _rig.machineDoc();
		check(md && md->find("kit")->find("working")->asString() == "edited", "the kit is 'edited' without SAVE KIT");
		const auto wk = _rig.pageDoc("workingKit", kit);
		check(wk && _rig.pageDocSource("workingKit") == "memory", "kit source: memory");
		return m.hardware().copyPatchRam();
	}

	void restoredKitTruth(const Bytes& _rom, const std::string& _romName, const Bytes& _patchRam, const uint8_t _kit,
		const uint8_t _value)
	{
		std::puts("== P3 DAW project restore: unsaved kit edit in a new machine");
		Rig rig(_rom, _romName, _patchRam);
		rig.page(R"({"op":"ready"})");
		const bool shown = rig.runUntil([&]
		{
			const auto doc = rig.pageDoc("workingKit", _kit);
			return doc && doc->find("tracks")->asArray()[0].find("synth")->asArray()[2].asNumber() == _value
				&& rig.machineDoc() && rig.machineDoc()->find("kit")->find("working")->asString() == "edited";
		}, 5000);
		check(shown, "the restored machine's page shows the unsaved edit, marked edited");
	}

	// P3: REC as on the machine. Live recording starts (hold RECORD, press PLAY), a
	// track played from the page and a knob moved from the page are recorded by the
	// firmware (trig; lock on the next note), grid edits wait, REC again leaves
	// recording and keeps playing.
	void liveRecording(Rig& _rig)
	{
		auto& m = _rig.machine();
		auto& desk = _rig.desk();
		std::puts("== P3 live recording (REC)");
		const auto pattern = *desk.linkState().pattern;
		const auto p = std::to_string(pattern);
		_rig.page(R"({"op":"stop","id":40})");
		_rig.run(300);
		const auto total = std::to_string(ed::visibleSteps(desk.documents().patterns.at(pattern)));
		for(const int t : {14, 15})
		{
			_rig.page("{\"op\":\"clearSteps\",\"p\":" + p + ",\"t\":" + std::to_string(t) + ",\"from\":0,\"to\":" + total
				+ ",\"id\":41}");
			resultOk(_rig);
			_rig.runUntil([&] { return !desk.isBusy(); }, 2000);
		}
		_rig.runUntil([&] { return !desk.isBusy(); }, 2000);
		{
			const auto cleared = _rig.readPattern(pattern);
			int left = 0;
			for(size_t s = 0; cleared && s < ed::visibleSteps(*cleared); ++s)
				left += ed::hasTrig(*cleared, 14, s) + ed::hasTrig(*cleared, 15, s);
			check(cleared && left == 0, "tracks 15 and 16 cleared before recording");
		}
		// P6: the lock goes to the track's next programmed trig whose step has not started when the
		// turn lands (P4, mdP4ProbeFirmwareTest lockwindow); a note played live in the same moment is
		// a firmware race (3 of 9), which made this check hang on a few ms of phase. So track 15 gets
		// its note at step 10 before recording, and the knob is turned at step 7.
		_rig.page("{\"op\":\"trig\",\"p\":" + p + R"(,"t":14,"s":9,"on":true,"id":40})");
		check(resultOk(_rig), "track 15's note at step 10");
		_rig.runUntil([&] { return !desk.isBusy(); }, 2000);
		const auto recording = [&] { return _rig.pageTelemetry().recording; };
		const auto t0 = m.now();
		_rig.page(R"({"op":"record","id":42})");
		check(resultOk(_rig), "REC accepted");
		check(_rig.runUntil(recording, 1000), "live recording starts (hold RECORD, press PLAY)");
		std::printf("  REC -> recording reported: %.1f ms emulated\n", ms(m.now() - t0));
		const auto stepIs = [&](const int _s) { return [&m, _s] { return m.playhead() == _s; }; };
		_rig.runUntil(stepIs(4), 3000);
		_rig.page(R"({"op":"recTrig","t":15,"id":44})");
		check(resultOk(_rig), "track 16 played from the page");
		// A knob move on track 15 FLTF (effects page, param 12), then its note.
		_rig.runUntil(stepIs(6), 3000);
		const auto k0 = m.now();
		_rig.page("{\"op\":\"param\",\"k\":" + std::to_string(*desk.linkState().kit) + R"(,"t":14,"i":12,"v":99,"id":43})");
		check(resultOk(_rig), "knob move accepted while recording");
		const bool turned = _rig.runUntil([&] { return m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 14 * 24 + 12) == 99; }, 1500);
		check(turned, "the knob move reaches the machine as DATA ENTRY turns");
		std::printf("  knob move -> value in the machine: %.1f ms emulated (select track, page key, turn)\n", ms(m.now() - k0));
		_rig.run(50);
		_rig.page("{\"op\":\"trig\",\"p\":" + p + R"(,"t":0,"s":3,"id":46})");
		check(_rig.lastResult() && !_rig.lastResult()->find("ok")->asBool(), "grid edits wait while recording");
		_rig.runUntil(stepIs(12), 3000);
		_rig.page(R"({"op":"record","id":47})");
		check(_rig.runUntil([&] { return !recording(); }, 1000), "REC again leaves recording");
		check(_rig.pageTelemetry().playing, "and the pattern keeps playing");
		const auto trigsOf = [&](const Value& _doc, const int _t)
		{
			std::vector<int> s;
			for(const auto& v : _doc.find("tracks")->asArray()[size_t(_t)].find("trigs")->asArray())
				s.push_back(int(v.asNumber()));
			return s;
		};
		const bool shown = _rig.runUntil([&]
		{
			const auto d = _rig.pageDoc("pattern", pattern);
			return d && !trigsOf(*d, 15).empty() && !trigsOf(*d, 14).empty();
		}, 2000);
		check(shown, "the recorded trigs reach the page's pattern document");
		const auto d = _rig.pageDoc("pattern", pattern);
		if(d)
		{
			std::printf("  track 16 recorded at step(s):");
			for(const int s : trigsOf(*d, 15))
				std::printf(" %d", s + 1);
			std::printf("; track 15:");
			for(const int s : trigsOf(*d, 14))
				std::printf(" %d", s + 1);
			std::printf("\n");
			bool lock = false;
			for(const auto& l : d->find("locks")->asArray())
				if(l.find("track")->asNumber() == 14 && l.find("param")->asNumber() == 12)
					for(const auto& sv : l.find("steps")->asArray())
					{
						std::printf("  lock track 15 FLTF step %d = %d\n", int(sv.asArray()[0].asNumber()) + 1,
							int(sv.asArray()[1].asNumber()));
						lock |= sv.asArray()[1].asNumber() == 99;
					}
			check(lock, "the knob move was recorded as a lock (99) on track 15's note");
		}
		_rig.page(R"({"op":"stop","id":48})");
		_rig.run(300);
	}

	// P10, the page's keyboard as the note intent (noteOn / noteOff): a key is the track's MAP EDITOR note
	// (the machine trigs it, at the note's velocity); on a ROM machine the pitch is a PTCH the machine holds
	// while the key is down, the working copy's held layer: memory images read meanwhile report no kit
	// edit, and letting go puts the document's PTCH back.
	void keyboard(Rig& _rig)
	{
		auto& m = _rig.machine();
		auto& desk = _rig.desk();
		std::puts("== P10 keyboard: a key plays the selected track, a held PTCH is not a kit edit and is put back");
		_rig.page(R"({"op":"stop","id":90})");
		_rig.run(600);
		const int t = 0;
		_rig.page(R"({"op":"mute","t":0,"on":false,"id":91})");
		_rig.run(100);
		const auto ptchAt = ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + t * 24 + mdDesk::keys::g_ptchIndex;
		const auto peakOf = [&](const std::string& _press, const double _ms)
		{
			const auto from = m.left().size();
			_rig.page(_press);
			check(resultOk(_rig), "noteOn accepted: " + _press);
			_rig.run(_ms);
			float peak = 0;
			for(size_t i = from; i < m.left().size(); ++i)
				peak = std::max({peak, std::abs(m.left()[i]), std::abs(m.right()[i])});
			return peak;
		};
		float quiet = 0;
		{
			const auto from = m.left().size();
			_rig.run(200);
			for(size_t i = from; i < m.left().size(); ++i)
				quiet = std::max(quiet, std::abs(m.left()[i]));
		}
		const auto loud = peakOf(R"({"op":"noteOn","t":0,"vel":127,"pitch":0,"id":92})", 250);
		_rig.page(R"({"op":"noteOff","t":0,"pitch":0,"id":93})");
		_rig.run(600);
		const auto soft = peakOf(R"({"op":"noteOn","t":0,"vel":30,"pitch":0,"id":94})", 250);
		_rig.page(R"({"op":"noteOff","t":0,"pitch":0,"id":95})");
		_rig.run(600);
		std::printf("  track 1 by its note: stopped %.4f, velocity 127 peak %.4f, velocity 30 peak %.4f\n", quiet, loud, soft);
		check(loud > 0.01f && loud > quiet * 4, "a key plays the track (its MAP EDITOR note) while stopped");
		check(soft < loud * 0.8f, "the note's velocity is heard (30 softer than 127)");

		// Track 1 becomes ROM-01 (a sample machine: pitched by PTCH).
		const auto kit = *desk.linkState().kit;
		const auto model = desk.documents().working->kit.models[t];
		_rig.page("{\"op\":\"machine\",\"k\":" + std::to_string(kit) + ",\"t\":0,\"model\":" + std::to_string(*ed::mdMachineModel("ROM-01"))
			+ ",\"keepFx\":true,\"id\":96}");
		check(resultOk(_rig), "track 1 is ROM-01");
		_rig.runUntil([&] { return !desk.coreState().state({mdDesk::DocKind::WorkingKit, 0})->pending; }, 3000);
		_rig.run(300);
		const auto working = [&] { return _rig.machineDoc() ? _rig.machineDoc()->find("kit")->find("working")->asString() : std::string("?"); };
		const auto kitState = working();
		const auto before = desk.documents().working->kit.params[t][mdDesk::keys::g_ptchIndex];
		const int pitch = before > 90 ? -12 : 12;
		const auto held = mdDesk::keys::heldPtch(before, pitch);
		const auto imagesBefore = _rig.pageDocCount("workingKit");
		_rig.page("{\"op\":\"noteOn\",\"t\":0,\"vel\":100,\"pitch\":" + std::to_string(pitch) + ",\"id\":97}");
		check(resultOk(_rig), "a pitched key accepted");
		check(_rig.runUntil([&] { return m.read8(ptchAt) == held; }, 1000), "the machine holds the key's PTCH while it is down");
		_rig.run(400);	// memory images are read meanwhile (the region changed)
		const auto* w = desk.documents().working ? &desk.documents().working->kit : nullptr;
		check(w && w->params[t][mdDesk::keys::g_ptchIndex] == before && !desk.coreState().state({mdDesk::DocKind::WorkingKit, 0})->pending,
			"held: the memory image read during the hold leaves the kit document's PTCH as it was");
		check(working() == kitState, "held: the kit's edited/clean state is unchanged (the key is no kit edit): " + working());
		std::printf("  workingKit documents published during the hold: %d\n", _rig.pageDocCount("workingKit") - imagesBefore);
		_rig.page("{\"op\":\"noteOff\",\"t\":0,\"pitch\":" + std::to_string(pitch) + ",\"id\":98}");
		check(_rig.runUntil([&] { return m.read8(ptchAt) == before; }, 1000), "let go: the machine's PTCH is the document's again");
		_rig.run(400);
		w = desk.documents().working ? &desk.documents().working->kit : nullptr;
		check(w && w->params[t][mdDesk::keys::g_ptchIndex] == before && working() == kitState, "after: the kit document and its state as they were");
		std::printf("  PTCH %d, held %d, after %d\n", before, held, m.read8(ptchAt));
		_rig.page("{\"op\":\"machine\",\"k\":" + std::to_string(kit) + ",\"t\":0,\"model\":" + std::to_string(model) + ",\"keepFx\":true,\"id\":99}");
		_rig.run(300);
	}

	// P3 control: an app LFO moves a kit parameter on the machine's steps.
	void appModulators(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P3 control: app LFO -> track 2 DIST, on the machine's steps");
		_rig.page(R"({"op":"modSet","id":70,"doc":{"schema":"md-desk/modulators","version":1,
			"sources":[{"id":"lfo1","label":"LFO A","kind":"lfo","shape":0,"rate":"1/2","depth":100}],
			"links":[{"source":"lfo1","track":1,"param":16,"min":10,"max":110,"curve":"lin"}]}})");
		check(resultOk(_rig), "modulator setup accepted");
		std::set<int> seen;
		_rig.page(R"({"op":"play","id":71})");
		_rig.run(400);
		const auto t0 = m.now();
		while(ms(m.now() - t0) < 2000)
		{
			_rig.run(20);
			seen.insert(m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 1 * 24 + 16));
		}
		_rig.page(R"({"op":"stop","id":72})");
		_rig.run(300);
		std::printf("  track 2 DIST took %zu values in 2 s (%d..%d)\n", seen.size(), *seen.begin(), *seen.rbegin());
		check(seen.size() >= 4 && *seen.begin() >= 10 && *seen.rbegin() <= 110, "the machine's DIST follows the LFO within min..max");
		_rig.page(R"({"op":"modSet","id":73,"doc":{"schema":"md-desk/modulators","version":1,"sources":[],"links":[]}})");
	}

	// P3 sampler: Rename sends 0x73. The firmware has no name request, so the only
	// evidence is its own copy of the last name it took (RAM 0x29f300).
	void sampleName(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P3 sampler: Rename (0x73)");
		_rig.page(R"({"op":"sampleName","slot":5,"name":"KIK","id":60})");
		check(resultOk(_rig), "sample name accepted");
		_rig.run(100);
		const bool taken = m.read8(0x29f300) == 'K' && m.read8(0x29f301) == 'I' && m.read8(0x29f302) == 'K'
			&& m.read8(0x29f303) == ' ';
		check(taken, "the firmware took the name (its last-name buffer, RAM 0x29f300)");
		_rig.page(R"({"op":"sampleName","slot":5,"name":"TOOLONG","id":61})");
		check(_rig.lastResult() && !_rig.lastResult()->find("ok")->asBool(), "a name longer than 4 is refused");
	}

	// P4: the start-up animation holds input; the engine label says so until keys work.
	void bootHold(const Bytes& _rom, const std::string& _romName)
	{
		std::puts("== P4 boot: input held until the start-up animation is over");
		Rig rig(_rom, _romName, {}, false);
		auto& m = rig.machine();
		const auto t0 = m.now();
		rig.page(R"({"op":"ready"})");
		rig.runUntil([&] { return rig.desk().isReady(); }, 3000);
		const auto answered = ms(m.now() - t0);
		check(rig.machineString({"lifecycle"}) == "animating",
			"the firmware answers, the engine still says BOOTING OS (animation)");
		rig.page(R"({"op":"play","id":900})");
		check(rig.lastResult() && !rig.lastResult()->find("ok")->asBool(), "PLAY during the animation is held back, with the reason");
		const bool ready = rig.runUntil([&] { return rig.desk().isInputReady(); }, 30000);
		const auto readyMs = ms(m.now() - t0);
		std::printf("  status reply %.0f ms, input ready %.0f ms after the firmware took MIDI\n", answered, readyMs);
		check(ready && rig.machineString({"lifecycle"}) == "ready", "the engine says ready when the animation is over");
		rig.page(R"({"op":"play","id":901})");
		const bool plays = rig.runUntil([&] { return rig.pageTelemetry().playing; }, 2000);
		check(plays, "the first PLAY after ready plays (no key swallowed)");
		rig.page(R"({"op":"stop","id":902})");
		rig.run(300);
	}

	// P4: the mutes are the machine's (RAM 0x28b34a), whoever sets them.
	void mutesTruth(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P4 mutes: from the machine's memory");
		const auto muted = [&](const int _t)
		{
			const auto* list = _rig.machineDoc() ? _rig.machineDoc()->find("desk")->find("mutes") : nullptr;
			if(!list)
				return false;
			for(const auto& v : list->asArray())
				if(static_cast<int>(v.asNumber()) == _t)
					return true;
			return false;
		};
		_rig.page(R"({"op":"mute","t":2,"on":true,"id":910})");
		const bool landed = _rig.runUntil([&] { return muted(2) && _rig.telemetry().mutes == 0x0004; }, 1000);
		std::printf("  machine mutes %04x, page %s\n", _rig.telemetry().mutes, muted(2) ? "muted 3" : "not muted 3");
		check(landed, "a page mute lands in the machine's mute set");
		check(_rig.machineString({"desk", "mutesSource"}) == "memory", "the page's mutes are read from memory");
		// The panel's MUTE window: FUNCTION + A/E, TRIG 6.
		m.hardware().trySendPanelEvent(0x24, 0x02); _rig.run(60);
		m.hardware().trySendPanelEvent(0x23, 0x01); _rig.run(60);
		m.hardware().trySendPanelEvent(0x23, 0x00); _rig.run(60);
		m.hardware().trySendPanelEvent(0x24, 0x00); _rig.run(200);
		m.hardware().trySendPanelEvent(0x20, 0x20); _rig.run(60);
		m.hardware().trySendPanelEvent(0x20, 0x00); _rig.run(200);
		m.panel(md::PanelControl::Exit);
		check(_rig.runUntil([&] { return muted(5) && muted(2); }, 1000), "a mute made in the panel's MUTE window shows in the page");
		_rig.page(R"({"op":"mute","t":2,"on":false,"id":911})");
		_rig.page(R"({"op":"mute","t":5,"on":false,"id":912})");
		check(_rig.runUntil([&] { return _rig.telemetry().mutes == 0; }, 1000), "unmuted again");
	}

	std::vector<int> chainOf(const Rig& _rig)
	{
		std::vector<int> v;
		const auto& t = _rig.telemetry();
		if(t.chainKnown && t.chain.active)
			for(const auto p : t.chain.patterns)
				v.push_back(p);
		return v;
	}

	// The pattern the machine reports at each playhead wrap.
	std::vector<int> wrapPatterns(Rig& _rig, const int _n)
	{
		std::vector<int> seen;
		int last = _rig.telemetry().step;
		for(int guard = 0; guard < 4000 && static_cast<int>(seen.size()) < _n; ++guard)
		{
			_rig.run(10);
			const int s = _rig.telemetry().step;
			if(s >= 0 && last >= 0 && s < last)
			{
				_rig.run(40);
				seen.push_back(_rig.telemetry().pattern);
			}
			last = s;
		}
		return seen;
	}

	void chaining(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P4 pattern chaining");
		// Short patterns so the wraps come quickly: A02..A05 16 steps, one trig each.
		for(uint8_t s = 1; s <= 4; ++s)
		{
			auto p = _rig.readPattern(s);
			require(p.has_value(), "pattern");
			p->length = 16;
			m.send(ed::encodeMdPattern(*p));
		}
		m.send(ed::mdLoadPattern(0));
		_rig.run(200);
		_rig.page(R"({"op":"play","id":920})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		const auto t0 = m.now();
		_rig.page(R"({"op":"chain","patterns":[3,1,4],"id":921})");
		check(resultOk(_rig), "chain A04 A02 A05 accepted");
		const bool made = _rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{3, 1, 4}; }, 3000);
		check(made, "the firmware holds the chain (internal SRAM), as the page sees it");
		std::printf("  chain command -> firmware chain %.0f ms\n", ms(m.now() - t0));
		const auto order = wrapPatterns(_rig, 5);
		std::printf("  pattern at each wrap:");
		for(const int p : order) std::printf(" %s", p >= 0 ? ed::mdPatternName(static_cast<uint8_t>(p)).c_str() : "?");
		std::printf("\n");
		check(order.size() == 5 && order[0] == 3 && order[1] == 1 && order[2] == 4 && order[3] == 3, "the machine plays A04 A02 A05 and loops");
		// A grid edit of a chained pattern (a pattern dump): does the chain survive?
		_rig.page(R"({"op":"trig","p":1,"t":2,"s":6,"id":922})");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 1000);
		_rig.run(100);
		check(chainOf(_rig) == std::vector<int>{3, 1, 4}, "a pattern dump into a chained pattern keeps the chain");
		// LOAD PATTERN would clear it: the desk asks first.
		_rig.page(R"({"op":"select","p":7,"id":923})");
		check(_rig.telemetry().chain.active, "select while chained asks first (breakChain) and sends nothing");
		_rig.page(R"({"op":"select","p":7,"force":true,"id":924})");
		check(_rig.runUntil([&] { return !_rig.telemetry().chain.active; }, 1000), "select with force clears the chain, as on the machine");
		// A chain in bank E: the BANK GROUP key first.
		for(uint8_t s = 64; s <= 65; ++s)
		{
			auto p = _rig.readPattern(s);
			p->length = 16;
			m.send(ed::encodeMdPattern(*p));
		}
		_rig.page(R"({"op":"chain","patterns":[65,64],"id":925})");
		check(resultOk(_rig), "chain E02 E01 accepted");
		check(_rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{65, 64}; }, 3000), "bank E chain: BANK GROUP pressed, chain held");
		const auto e = wrapPatterns(_rig, 3);
		check(e.size() == 3 && e[0] == 65 && e[1] == 64 && e[2] == 65, "the machine plays E02 E01 and loops");
		_rig.page(R"({"op":"chainClear","id":926})");
		check(_rig.runUntil([&] { return !_rig.telemetry().chain.active; }, 1000), "CLEAR ends the chain");
		_rig.page(R"({"op":"chain","patterns":[1,3],"id":927})");
		check(resultOk(_rig) && _rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{1, 3}; }, 3000), "chain A02 A04");
		// The page sends the chain again at every pad: quick re-sends, one in bank E from A-D (BANK GROUP
		// first). Only the latest is pressed once the keys before are worked off, so the group is right.
		{
			const auto t1 = m.now();
			_rig.page(R"({"op":"chain","patterns":[65,64],"id":980})");
			_rig.run(20);
			_rig.page(R"({"op":"chain","patterns":[65,64,66],"id":981})");
			_rig.page(R"({"op":"chain","patterns":[65,64,66,67],"id":982})");
			check(resultOk(_rig), "re-sent chains accepted while the keys before are on their way");
			const bool latest = _rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{65, 64, 66, 67}; }, 4000);
			std::printf("  three chain sends in a row -> firmware chain %s after %.0f ms\n", latest ? "E02 E01 E03 E04" : "other", ms(m.now() - t1));
			check(latest, "the firmware holds the latest chain, in bank E");
			_rig.run(300);
			check(chainOf(_rig) == std::vector<int>{65, 64, 66, 67}, "and keeps it (no older key run lands after it)");
			_rig.page(R"({"op":"chain","patterns":[65,66],"id":983})");
			_rig.page(R"({"op":"chainClear","id":984})");
			_rig.run(1500);
			check(!_rig.telemetry().chain.active, "CLEAR right after a chain: it waits for the chain's keys, then the chain ends");
			_rig.page(R"({"op":"chain","patterns":[1,3],"id":985})");
			check(_rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{1, 3}; }, 3000), "chain A02 A04 again");
		}
		_rig.page(R"({"op":"stop","id":928})");
		_rig.runUntil([&] { return !_rig.pageTelemetry().playing; }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"play","id":929})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		_rig.run(100);
		const auto after = wrapPatterns(_rig, 3);
		std::printf("  STOP, PLAY with a chain: plays %d, then", _rig.pageTelemetry().pattern);
		for(const int p : after) std::printf(" %d", p);
		std::printf("; chain %s\n", _rig.telemetry().chain.active ? "active" : "gone");
		// Stopped: does the gesture chain?
		_rig.page(R"({"op":"stop","id":930})");
		_rig.runUntil([&] { return !_rig.pageTelemetry().playing; }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"chain","patterns":[2,4],"id":931})");
		_rig.run(800);
		std::printf("  chain while stopped: firmware chain %s, current %d\n", chainOf(_rig) == std::vector<int>{2, 4} ? "A03 A05" : "not made",
			_rig.pageTelemetry().pattern);
		_rig.page(R"({"op":"play","id":932})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		const auto st = wrapPatterns(_rig, 3);
		std::printf("  then PLAY:");
		for(const int p : st) std::printf(" %d", p);
		std::printf("\n");
		_rig.page(R"({"op":"chainClear","id":933})");
		_rig.page(R"({"op":"stop","id":934})");
		_rig.run(500);
		m.send(ed::mdLoadPattern(0));
		_rig.run(300);
		_rig.page(R"({"op":"chain","patterns":[1,17],"id":935})");
		check(_rig.lastResult() && !_rig.lastResult()->find("ok")->asBool(), "a chain across banks is refused (the machine's rule)");
		// SONG mode: the machine plays its song and ignores a pattern chain. Asking for a chain
		// switches whatever plays (song or pattern) to the chain.
		{
			auto p7 = _rig.readPattern(7);
			require(p7.has_value(), "pattern");
			p7->length = 16;
			m.send(ed::encodeMdPattern(*p7));
			ed::MdSong s;
			s.position = 5;
			s.rows.clear();
			ed::MdSongRow r;
			r.pattern = 7;
			r.end = 16;
			r.repeats = 63;
			s.rows.push_back(r);
			s.rows.push_back(ed::MdSongRow{});
			m.send(ed::encodeMdSong(s));
			m.send(ed::mdLoadSong(5));
			m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 1));
			_rig.run(300);
			_rig.page(R"({"op":"play","id":936})");
			_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
			const auto song = wrapPatterns(_rig, 2);
			check(song.size() == 2 && song[1] == 7, "song mode: the machine plays the song's A08");
			_rig.page(R"({"op":"chain","patterns":[1,3],"id":937})");
			check(resultOk(_rig), "chain A02 A04 in song mode accepted");
			_rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{1, 3}; }, 3000);
			const auto played = wrapPatterns(_rig, 4);
			std::printf("  song mode, then chain A02 A04: plays");
			for(const int p : played) std::printf(" %d", p);
			std::printf("\n");
			check(played.size() == 4 && std::count(played.begin(), played.end(), 1) >= 1 && std::count(played.begin(), played.end(), 3) >= 1
				&& played.back() != 7, "a chain asked for in song mode plays the chain, not the song");
			m.send(ed::mdStatusRequest(ed::MdStatus::SequencerMode));
			_rig.run(200);
			const auto sm = _rig.desk().linkState().songMode;
			std::printf("  sequencer mode after the chain: %s\n", sm ? (*sm ? "song" : "pattern") : "unknown");
			check(sm == false, "and the machine is in pattern mode (the desk knows it)");
			_rig.page(R"({"op":"stop","id":950})");
			_rig.runUntil([&] { return !_rig.pageTelemetry().playing; }, 2000);
			_rig.run(300);
			_rig.page(R"({"op":"play","id":951})");
			_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
			const auto again = wrapPatterns(_rig, 3);
			std::printf("  STOP, PLAY:");
			for(const int p : again) std::printf(" %d", p);
			std::printf("\n");
			check(again.size() == 3 && std::find(again.begin(), again.end(), 7) == again.end(), "STOP, PLAY plays the chain again, not the song");
			_rig.page(R"({"op":"chainClear","id":938})");
			_rig.page(R"({"op":"stop","id":939})");
			_rig.run(500);
			m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 0));
			m.send(ed::mdLoadPattern(0));
			_rig.run(300);
		}
		// Stopped on A01, which is not in the chain: the chain is what plays next.
		{
			_rig.page(R"({"op":"chain","patterns":[2,4],"id":940})");
			check(resultOk(_rig), "chain A03 A05 while stopped on A01 accepted");
			_rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{2, 4}; }, 3000);
			_rig.run(300);
			std::printf("  stopped on A01, chain A03 A05: current %d\n", _rig.pageTelemetry().pattern);
			_rig.page(R"({"op":"play","id":941})");
			_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
			_rig.run(100);
			const int first = _rig.pageTelemetry().pattern;
			const auto then = wrapPatterns(_rig, 3);
			std::printf("  then PLAY: %d, then", first);
			for(const int p : then) std::printf(" %d", p);
			std::printf("\n");
			check(first == 2 && then.size() == 3 && then[0] == 4 && then[1] == 2, "stopped: PLAY starts the chain at A03, not the selected A01");
			_rig.page(R"({"op":"chainClear","id":942})");
			_rig.page(R"({"op":"stop","id":943})");
			_rig.run(500);
		}
		// Playing A01 with A08 picked (queued for the pattern end): the chain replaces the pick.
		{
			m.send(ed::mdLoadPattern(0));
			_rig.run(300);
			_rig.page(R"({"op":"play","id":944})");
			_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
			_rig.run(200);
			_rig.page(R"({"op":"select","p":7,"id":945})");
			_rig.run(100);
			_rig.page(R"({"op":"chain","patterns":[2,4],"id":946})");
			check(resultOk(_rig), "chain A03 A05 with A08 queued accepted");
			check(_rig.runUntil([&] { return chainOf(_rig) == std::vector<int>{2, 4}; }, 3000), "the firmware holds the chain");
			const auto then = wrapPatterns(_rig, 4);
			std::printf("  then plays:");
			for(const int p : then) std::printf(" %d", p);
			std::printf("\n");
			check(then.size() == 4 && then[0] == 2 && then[1] == 4 && then[2] == 2, "the chain replaces the queued A08: A03 A05 play");
			check(!_rig.desk().linkState().queuedPattern, "and the desk drops the queued A08 (the page shows the chain, not NEXT A08)");
			_rig.page(R"({"op":"chainClear","id":947})");
			_rig.page(R"({"op":"stop","id":948})");
			_rig.run(500);
		}
	}

	// P4: while live recording, a value moved in the page locks the trig the desk names.
	void recLockTruth(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P4 live recording: the lock lands where the desk says");
		const auto slot = *_rig.desk().linkState().pattern;
		auto p = *_rig.readPattern(slot);
		p.length = 16;
		for(uint8_t t = 0; t < 16; ++t)
			for(uint8_t s = 0; s < 64; ++s)
				if(ed::hasTrig(p, t, s))
					p = ed::withTrig(p, t, s, false);
		p = ed::withTrig(p, 14, 8, true);
		p = ed::withTrig(p, 14, 12, true);
		p.lockMasks.fill(0);
		m.send(ed::encodeMdPattern(p));
		_rig.page("{\"op\":\"load\",\"kind\":\"pattern\",\"slot\":" + std::to_string(slot) + "}");
		_rig.runUntil([&] { return ed::hasTrig(_rig.desk().documents().patterns.at(slot), 14, 12); }, 2000);
		_rig.run(300);
		const auto kit = *_rig.desk().linkState().kit;
		const int before = _rig.desk().documents().working->kit.params[14][0];
		_rig.page(R"({"op":"record","id":940})");
		_rig.runUntil([&] { return _rig.pageTelemetry().recording; }, 3000);
		_rig.runUntil([&] { return _rig.pageTelemetry().step == 2; }, 5000);
		const int want = before > 60 ? before - 20 : before + 20;
		_rig.page("{\"op\":\"param\",\"k\":" + std::to_string(kit) + ",\"t\":14,\"i\":0,\"v\":" + std::to_string(want) + ",\"id\":941}");
		int predicted = -1;
		_rig.runUntil([&]
		{
			const auto* d = _rig.machineDoc() ? _rig.machineDoc()->find("desk") : nullptr;
			const auto* l = d ? d->find("recLock") : nullptr;
			if(l && l->isObject())
				predicted = static_cast<int>(l->find("step")->asNumber());
			return predicted >= 0;
		}, 2000);
		_rig.runUntil([&] { return _rig.pageTelemetry().step == 14; }, 5000);
		_rig.page(R"({"op":"record","id":942})");
		_rig.run(200);
		_rig.page(R"({"op":"stop","id":943})");
		_rig.runUntil([&] { return !_rig.pageTelemetry().playing; }, 3000);
		_rig.run(300);
		const auto after = *_rig.readPattern(slot);
		std::printf("  desk said step %d; locks on track 15 param 0:", predicted + 1);
		int landed = -1;
		for(uint8_t s = 0; s < 16; ++s)
			if(const auto v = ed::lockValue(after, 14, 0, s)) { std::printf(" step %u = %u", s + 1, *v); if(landed < 0) landed = s; }
		std::printf("\n");
		check(predicted >= 0 && landed == predicted, "the firmware locked the trig the desk named");
	}

	std::string kitNameOf(const ed::MdKit& _k)
	{
		std::string n;
		for(const auto c : _k.name)
		{
			if(!c)
				break;
			n += static_cast<char>(c);
		}
		return n;
	}

	// P4: the kit library and pattern chooser on firmware, checked by the machine's own dumps.
	void library(Rig& _rig)
	{
		auto& m = _rig.machine();
		auto& desk = _rig.desk();
		std::puts("== P4 kit library and pattern chooser");
		const bool all = _rig.runUntil([&] { return desk.documents().kits.size() == 64 && desk.documents().patterns.size() == 128 && !desk.isBusy(); }, 30000);
		check(all, "all 64 kits and 128 patterns loaded in the background");
		// Slots never written hold battery-RAM bytes for a name (7f..): no name, so the library shows them empty.
		{
			int unwritten = 0, garbled = 0;
			for(const auto& [slot, k] : desk.documents().kits)
			{
				const bool raw = k.name[0] != 0 && mdDesk::kitNameText(k).empty();
				garbled += raw;
				unwritten += raw && mdDesk::isEmptyKit(k);
			}
			std::printf("  kit slots with non-text name bytes: %d, of them every track GND-EMPTY (unwritten): %d\n", garbled, unwritten);
			check(desk.documents().kits.count(63) && mdDesk::isEmptyKit(desk.documents().kits.at(63)), "K64, never written, is an empty slot (its name bytes are no name)");
		}
		const auto kitStatus = [&] { return ed::parseMdStatusResponse(m.request(ed::mdStatusRequest(ed::MdStatus::Kit), 0x72))->value; };
		const auto readKit = [&](const uint8_t _k) { return *ed::decodeMdKit(m.request(ed::mdKitRequest(_k), ed::g_mdKitDump)); };
		const auto cur = *desk.linkState().kit;
		const auto working = desk.documents().working->kit;
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy(); }, 2000); _rig.run(200); };
		_rig.pageConfirmed("{\"op\":\"kitCopy\",\"k\":" + std::to_string(cur) + ",\"id\":950}");
		_rig.pageConfirmed(R"({"op":"kitPaste","k":40,"id":951})");
		check(resultOk(_rig), "paste into K41 accepted");
		settle();
		const auto k40 = readKit(40);
		check(k40.params == working.params && k40.models == working.models, "K41 holds the copied kit (machine dump)");
		_rig.pageConfirmed(R"({"op":"kitRename","k":40,"name":"lib test","id":952})");
		settle();
		check(kitNameOf(readKit(40)) == "LIB TEST", "rename of a slot that does not play: dump with the new name");
		_rig.pageConfirmed(R"({"op":"kitClear","k":40,"id":953})");
		settle();
		const auto cleared = readKit(40);
		check(std::all_of(cleared.models.begin(), cleared.models.end(), [](const uint32_t _m) { return _m == 0; }) && kitNameOf(cleared).empty(),
			"clear: every track GND-EMPTY, no name");
		_rig.pageConfirmed(R"({"op":"undo","id":954})");
		settle();
		check(kitNameOf(readKit(40)) == "LIB TEST", "undo brings the renamed kit back");
		_rig.pageConfirmed(R"({"op":"kitCopyTo","from":40,"to":41,"id":955})");
		settle();
		check(kitNameOf(readKit(41)) == "LIB TEST", "drag-copy K41 -> K42");
		_rig.pageConfirmed(R"({"op":"kitSaveAs","k":42,"id":956})");
		settle();
		check(kitStatus() == 42, "Save as K43: it is the current kit");
		const auto pat = *desk.linkState().pattern;
		check(_rig.readPattern(pat)->kit == 42, "and the current pattern links to it (EXTENDED)");
		_rig.pageConfirmed("{\"op\":\"kitLoad\",\"k\":" + std::to_string(cur) + ",\"force\":true,\"id\":957}");
		settle();
		check(kitStatus() == cur, "LOAD KIT back to the first kit");
		// A click in the library over unsaved edits: the ask offers Save and load (saveKit, then kitLoad with force).
		{
			auto stored = readKit(cur);
			const int want = stored.params[2][1] == 33 ? 34 : 33;
			_rig.page("{\"op\":\"param\",\"k\":" + std::to_string(cur) + ",\"t\":2,\"i\":1,\"v\":" + std::to_string(want) + ",\"id\":958}");
			settle();
			_rig.clearAsk();
			_rig.page(R"({"op":"kitLoad","k":43,"id":959})");
			const auto& a = _rig.lastAsk();
			const auto* alts = a ? a->find("alternatives") : nullptr;
			check(a && a->find("ask")->asString() == "loadKit" && alts && alts->asArray().size() == 1 && kitStatus() == cur,
				"LOAD KIT over unsaved edits asks first (Save and load offered), nothing sent");
			if(alts && alts->asArray().size() == 1)
			{
				const auto t0 = m.now();
				for(const auto& c : alts->asArray()[0].find("first")->asArray())
					_rig.page(ed::json::write(c));
				_rig.page(R"({"op":"kitLoad","k":43,"force":true,"id":960})");
				settle();
				std::printf("  save and load: %.0f ms\n", ms(m.now() - t0));
				check(kitStatus() == 43 && readKit(cur).params[2][1] == want, "Save and load: the edit is in the stored slot, K44 is the current kit");
			}
			// K44 was never written: the firmware names its working kit NEW KIT, which is not an edit.
			const auto working = [&] { return _rig.machineDoc() ? _rig.machineDoc()->find("kit")->find("working")->asString() : std::string("?"); };
			const bool saved = _rig.runUntil([&] { return working() == "clean"; }, 2000);
			std::printf("  K44 (never written) loaded: kit %s\n", working().c_str());
			check(saved, "an unwritten slot just loaded is clean, not edited (the firmware's NEW KIT name)");
			_rig.clearAsk();
			_rig.page("{\"op\":\"kitLoad\",\"k\":" + std::to_string(cur) + ",\"id\":961}");
			const bool asked = _rig.lastAsk().has_value();
			if(asked)
				_rig.page("{\"op\":\"kitLoad\",\"k\":" + std::to_string(cur) + ",\"force\":true,\"id\":962}");
			settle();
			check(kitStatus() == cur && !asked, "with no edits a click on another slot loads it at once, no ask");
		}
		// Paste into the kit that plays: a dump plus LOAD KIT, heard at once.
		_rig.pageConfirmed("{\"op\":\"kitCopy\",\"k\":40,\"id\":970}");
		_rig.pageConfirmed("{\"op\":\"kitPaste\",\"k\":" + std::to_string(cur) + ",\"force\":true,\"id\":971}");
		settle();
		const auto image = ed::mdWorkingKitFromMemory([&]
		{
			Bytes r(ed::g_mdWorkingKitRegionSize);
			for(size_t i = 0; i < r.size(); ++i)
				r[i] = m.read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
			return r;
		}());
		check(image && kitNameOf(*image) == "LIB TEST" && image->models == readKit(40).models, "paste into the kit that plays: heard at once (working kit in memory)");
		_rig.pageConfirmed("{\"op\":\"kitName\",\"k\":" + std::to_string(cur) + ",\"name\":\"LIVE NAME\",\"id\":972}");
		settle();
		_rig.run(300);
		check(kitNameOf(desk.documents().working->kit) == "LIVE NAME", "rename of the kit that plays: live (0x55), the working kit shows it");
		// Patterns.
		_rig.pageConfirmed("{\"op\":\"patCopy\",\"p\":" + std::to_string(pat) + ",\"id\":958}");
		_rig.pageConfirmed(R"({"op":"patPaste","p":100,"id":959})");
		settle();
		const auto src = *_rig.readPattern(pat), p100 = *_rig.readPattern(100);
		check(p100.trigs == src.trigs && p100.lockMasks == src.lockMasks && p100.kit == src.kit, "pattern paste into G05: notes, locks, kit link");
		_rig.pageConfirmed(R"({"op":"patClear","p":100,"id":960})");
		settle();
		const auto c100 = *_rig.readPattern(100);
		check(std::all_of(c100.trigs.begin(), c100.trigs.end(), [](const uint64_t _t) { return _t == 0; }) && c100.length == src.length,
			"pattern clear: no trigs, length kept");
		// Switch now while playing.
		_rig.pageConfirmed(R"({"op":"play","id":961})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		_rig.run(500);
		const auto target = static_cast<uint8_t>((pat + 3) % 128);
		const auto t0 = m.now();
		_rig.pageConfirmed("{\"op\":\"select\",\"p\":" + std::to_string(target) + ",\"now\":true,\"force\":true,\"id\":962}");
		const bool now = _rig.runUntil([&] { return _rig.pageTelemetry().playing && _rig.pageTelemetry().pattern == target; }, 3000);
		std::printf("  switch now: playing %s after %.0f ms\n", ed::mdPatternName(target).c_str(), ms(m.now() - t0));
		check(now, "Now while playing: STOP, LOAD PATTERN, PLAY plays the new pattern");
		_rig.pageConfirmed(R"({"op":"stop","id":963})");
		_rig.run(300);
		m.send(ed::mdLoadPattern(pat));
		_rig.run(300);
	}

	// P4 HW MIDI against the emulated MD as the MIDI peer ("a real Machinedrum"): the desk has
	// no telemetry, no memory, no panel keys. The device port is the plug-in's wire engine's
	// (deskWire: the MidiWire paced at DIN speed, its CCs on the base channel the adapter gives,
	// PLAY/STOP as realtime); only the cable between it and the machine is the test's.
	class HwRig
	{
	public:
		HwRig(const Bytes& _rom, const std::string& _romName) : m_machine(_rom, _romName)
		{
			mdDesk::Desk::Port port;
			port.device = mdDesk::wirePort(m_wire, m_channel, [this] { return ms(m_machine.now()); });
			port.toPage = [this](const Value& _m)
			{
				g_contract(_m);
				const auto* t = _m.find("type");
				if(t && t->asString() == "machine")
					m_machineDoc = *_m.find("doc");
				if(t && t->asString() == "result")
					m_result = _m;
				if(t && t->asString() == "sampleLoad")
					m_sampleLoad = _m;
			};
			m_desk = std::make_unique<mdDesk::Desk>(port, mdDesk::wireProfile());
			m_machine.onSysex = [this](const Bytes& _b) { if(m_connected) m_toDesk.send(ms(m_machine.now()), _b); };
		}

		void page(const std::string& _json) { m_result.reset(); g_contract.command(parse(_json)); m_desk->onPageMessage(parse(_json)); }
		void run(const double _ms)
		{
			const auto end = m_machine.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(m_machine.now() < end)
				step();
		}
		bool runUntil(const std::function<bool()>& _done, const double _timeoutMs)
		{
			const auto end = m_machine.now() + static_cast<uint64_t>(_timeoutMs * g_rate / 1000);
			while(m_machine.now() < end)
			{
				if(_done())
					return true;
				step();
			}
			return _done();
		}
		// The lifecycle the machine document shows: hwConnecting, ready, hwLost.
		std::string lifecycle() const
		{
			const auto* l = m_machineDoc ? m_machineDoc->find("lifecycle") : nullptr;
			return l && l->isString() ? l->asString() : std::string();
		}
		mdDesk::Desk& desk() { return *m_desk; }
		Machine& machine() { return m_machine; }
		const std::optional<Value>& lastResult() const { return m_result; }
		const std::optional<Value>& sampleLoad() const { return m_sampleLoad; }
		void setConnected(const bool _c) { m_connected = _c; }
		size_t bytesOut() const { return m_bytesOut; }

	private:
		// A message reaches the other side when its last byte has: the wire time after the
		// wire was free for it.
		struct Wire
		{
			double freeAt = 0;
			std::deque<std::pair<double, Bytes>> flight;
			void send(const double _now, Bytes _b)
			{
				freeAt = std::max(freeAt, _now) + deskCore::DinPacer::wireMs(_b.size());
				flight.emplace_back(freeAt, std::move(_b));
			}
		};

		// The cable's far end: what has arrived at the desk's side by now.
		std::vector<Bytes> arrived()
		{
			std::vector<Bytes> out;
			while(!m_toDesk.flight.empty() && m_toDesk.flight.front().first <= ms(m_machine.now()))
			{
				out.push_back(std::move(m_toDesk.flight.front().second));
				m_toDesk.flight.pop_front();
			}
			return out;
		}

		// The session's step for the wire engine: the machine's time, then the wire (out at DIN
		// speed, in whole), then the desk's tick. No telemetry: the wire engine feeds none.
		void step()
		{
			const double now = ms(m_machine.now());
			while(!m_toMachine.flight.empty() && m_toMachine.flight.front().first <= now)
			{
				auto b = std::move(m_toMachine.flight.front().second);
				m_toMachine.flight.pop_front();
				m_bytesOut += b.size();
				if(m_connected)
					m_machine.send(b);
			}
			m_machine.step();
			m_wire.pump(ms(m_machine.now()), [this](const Bytes& _b) { m_desk->onDeviceSysex(_b); });
			if(m_machine.now() - m_lastTick >= g_rate / 30)
			{
				m_lastTick = m_machine.now();
				m_desk->tick();
			}
		}

		Machine m_machine;
		std::unique_ptr<mdDesk::Desk> m_desk;
		deskWire::MidiWire m_wire{[this](const Bytes& _b) { m_toMachine.send(ms(m_machine.now()), _b); }, [this] { return arrived(); }};
		uint8_t m_channel = 0;	// the machine's base channel (the adapter's fact)
		Wire m_toMachine, m_toDesk;
		std::optional<Value> m_machineDoc, m_result, m_sampleLoad;
		uint64_t m_lastTick = 0;
		bool m_connected = true;
		size_t m_bytesOut = 0;
	};

	void hardwareMidi(const Bytes& _rom, const std::string& _romName)
	{
		std::puts("== P4 HW MIDI: the editor drives a Machinedrum over MIDI at DIN speed (the emulator as the peer)");
		HwRig hw(_rom, _romName);
		auto& m = hw.machine();
		hw.page(R"({"op":"ready"})");
		check(hw.lifecycle() == "hwConnecting", "HW CONNECT until the machine answers");
		const auto t0 = m.now();
		const bool up = hw.runUntil([&] { return hw.lifecycle() == "ready" && hw.desk().linkState().pattern && hw.desk().linkState().kit
			&& hw.desk().documents().patterns.count(*hw.desk().linkState().pattern) && hw.desk().documents().kits.count(*hw.desk().linkState().kit); }, 20000);
		std::printf("  status, current pattern and kit over DIN: %.0f ms\n", ms(m.now() - t0));
		check(up, "HW MIDI: status, the current pattern and its kit read");
		if(!up)
			return;
		const auto pat = *hw.desk().linkState().pattern;
		const auto kit = *hw.desk().linkState().kit;
		// A grid edit: a pattern dump out, a read-back in, 1.7 s each way.
		const bool had = ed::hasTrig(hw.desk().documents().patterns.at(pat), 2, 7);
		auto t1 = m.now();
		hw.page("{\"op\":\"trig\",\"p\":" + std::to_string(pat) + ",\"t\":2,\"s\":7,\"id\":980}");
		const bool confirmed = hw.runUntil([&] { return !hw.desk().isBusy(); }, 10000);
		const double pushMs = ms(m.now() - t1);
		std::printf("  trig edit -> confirmed read-back over DIN: %.0f ms (desk round trip %.0f ms)\n", pushMs, hw.desk().lastRoundTripMs());
		check(confirmed && ed::hasTrig(hw.desk().documents().patterns.at(pat), 2, 7) != had, "a pattern edit reaches the machine and is read back, no timeout");
		check(pushMs > 3000, "the timing is the wire's (two 5410-byte dumps at 3125 bytes/s)");
		// A kit value: a CC, in the machine's working kit.
		const uint8_t v = static_cast<uint8_t>((hw.desk().documents().working->kit.params[0][16] + 17) & 0x7f);
		hw.page("{\"op\":\"param\",\"k\":" + std::to_string(kit) + ",\"t\":0,\"i\":16,\"v\":" + std::to_string(v) + ",\"id\":981}");
		hw.run(300);
		check(m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 16) == v, "a kit value goes out as a CC and the machine plays it");
		// The kit library over MIDI.
		const auto kitsAt = m.now();
		const bool kits = hw.runUntil([&] { return hw.desk().documents().kits.size() == 64; }, 60000);
		std::printf("  all 64 kits over DIN (background): %s after %.0f ms more\n", kits ? "read" : "NOT read", ms(m.now() - kitsAt));
		hw.page("{\"op\":\"kitCopy\",\"k\":" + std::to_string(kit) + ",\"id\":982}");
		hw.page(R"({"op":"kitPaste","k":50,"force":true,"id":983})");
		hw.runUntil([&] { return !hw.desk().isBusy(); }, 6000);
		hw.run(2500);
		const auto k50 = ed::decodeMdKit(m.request(ed::mdKitRequest(50), ed::g_mdKitDump));
		check(k50 && k50->models == hw.desk().documents().kits.at(kit).models, "kit paste into K51 over MIDI");
		// Transport: MIDI Start / Stop.
		hw.page(R"({"op":"play","id":984})");
		const auto step0 = m.playhead();
		bool moved = false;
		for(int i = 0; i < 100 && !moved; ++i) { hw.run(20); moved = m.playhead() != step0; }
		std::printf("  PLAY as MIDI Start (0xFA): the machine %s\n", moved ? "plays" : "does not play (its MIDI sync settings decide)");
		hw.page(R"({"op":"stop","id":985})");
		hw.run(300);
		const auto* r = hw.lastResult() ? &*hw.lastResult() : nullptr;
		(void)r;
		// Live recording, chains, the working kit from memory and the boot LCD need the local emulator.
		hw.page(R"({"op":"record","id":986})");
		check(hw.lastResult() && !hw.lastResult()->find("ok")->asBool(), "REC is refused over MIDI, with the reason");
		hw.page(R"({"op":"chain","patterns":[1,2],"id":987})");
		check(hw.lastResult() && !hw.lastResult()->find("ok")->asBool(), "chaining is refused over MIDI, with the reason");
		// P9: a WAV into a ROM slot over DIN, paced by the machine's SDS handshake; the far machine's flash is
		// the oracle (a real one cannot be read back).
		{
			ed::AudioClip c;
			c.rate = 22050;
			c.channels.assign(1, std::vector<float>(2205));
			for(size_t i = 0; i < c.channels[0].size(); ++i)
				c.channels[0][i] = float(0.7 * std::sin(i * 0.2));
			const auto wav = ed::encodeWav16(c);
			const auto at = m.now();
			check(hw.desk().loadSample(44, "Hw Tone.wav", wav).empty(), "HW: a WAV for ROM-45 goes out as SDS");
			const bool done = hw.runUntil([&] { const auto& l = hw.sampleLoad(); return l && l->find("state")->asString() != "sending"; }, 60000);
			const auto& l = hw.sampleLoad();
			std::printf("  SDS over DIN: %s after %.0f ms, %d packets, handshake %d, retries %d\n", l ? l->find("state")->asString().c_str() : "-",
				ms(m.now() - at), l ? int(l->find("total")->asNumber()) : 0, l && l->find("handshake") ? int(l->find("handshake")->asBool()) : -1,
				l && l->find("retries") ? int(l->find("retries")->asNumber()) : -1);
			check(done && l->find("state")->asString() == "done" && l->find("handshake")->asBool(), "HW: the sample is sent with the machine's handshake");
			hw.run(3000);
			const auto x = ed::indexMdSamples(md::sampleMemoryOf(m.hardware()));
			check(x.rom[44] && x.rom[44]->length == 2205 && x.names[44] == "HWTO", "HW: the machine stored 2205 samples in ROM-45, named HWTO");
			check(hw.runUntil([&] { return hw.lifecycle() == "ready"; }, 4000), "HW: and the link is ready after it");
		}
		// Unplugged: HW NO MIDI after a while.
		hw.setConnected(false);
		const bool lost = hw.runUntil([&] { return hw.lifecycle() == "hwLost"; }, 6000);
		check(lost, "no replies for 3.5 s: the link says lost (HW NO MIDI)");
		hw.setConnected(true);
		check(hw.runUntil([&] { return hw.lifecycle() == "ready"; }, 4000), "and ready again when it answers");
		std::printf("  bytes sent to the machine: %zu\n", hw.bytesOut());
	}

	// P5: the GLOBAL panel's settings reach the machine and take effect (a dump plus 0x56).
	void globalSettings(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P5 GLOBAL settings");
		int clocks = 0, pcIn = -1;
		m.onMidi = [&](const synthLib::SMidiEvent& _e) { if(_e.a == 0xf8) ++clocks; };
		_rig.page(R"({"op":"globalSet","field":"tempoOut","on":true,"id":990})");
		check(resultOk(_rig), "TEMPO OUT on accepted");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"play","id":991})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		clocks = 0;
		_rig.run(500);
		const int on = clocks;
		_rig.page(R"({"op":"stop","id":992})");
		_rig.run(400);
		_rig.page(R"({"op":"globalSet","field":"tempoOut","on":false,"id":993})");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"play","id":994})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		clocks = 0;
		_rig.run(500);
		const int off = clocks;
		_rig.page(R"({"op":"stop","id":995})");
		_rig.run(400);
		std::printf("  MIDI clocks in 0.5 s: TEMPO OUT on %d, off %d\n", on, off);
		check(on > 10 && off == 0, "TEMPO OUT: the machine sends MIDI clock only when on");
		const auto gdoc = _rig.pageDoc("global", *_rig.desk().linkState().globalSlot);
		check(gdoc && gdoc->find("control") && !gdoc->find("control")->find("tempoOut")->asBool(), "the page's global document says TEMPO OUT off");
		_rig.page(R"({"op":"globalSet","field":"programChangeIn","on":true,"id":996})");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 2000);
		_rig.run(300);
		m.send({0xc0, 9});
		_rig.run(300);
		pcIn = ed::parseMdStatusResponse(m.request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72))->value;
		check(pcIn == 9, "PRG CHANGE IN on: program change 9 selects A10");
		_rig.page(R"({"op":"globalSet","field":"programChangeIn","on":false,"id":997})");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 2000);
		m.send(ed::mdLoadPattern(0));
		_rig.run(300);
		m.onMidi = nullptr;
	}

	// P7: a full backup .syx imported through the desk (the session's SysEx import: one "set" per document,
	// one gesture) and read back from the firmware equal. The file is the user's own, read in place and
	// never copied (argument 3); skipped without it.
	void syxImport(const Bytes& _rom, const std::string& _romName, const std::string& _file)
	{
		std::puts("syx import");
		namespace ed = elektronData;
		std::ifstream in(_file, std::ios::binary);
		if(!in)
		{
			std::printf("  skip: %s not found\n", _file.c_str());
			return;
		}
		const Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const auto f = ed::parseSyx(bytes);
		check(f.model == ed::SyxModel::Md && f.problems.empty(), "the file is a Machinedrum dump with no problems");
		Rig rig(_rom, _romName);
		auto& desk = rig.desk();
		rig.page(R"({"op":"ready"})");
		rig.runUntil([&] { return desk.isReady() && desk.documents().global && desk.documents().patterns.size() == 128 && desk.documents().kits.size() == 64 && desk.documents().songs.size() == 32; }, 60000);
		const int active = desk.documents().global ? desk.documents().global->position : -1;
		size_t sent = 0;
		const auto set = [&](const char* _kind, const ed::json::Value& _doc)
		{
			rig.page(std::string(R"({"op":"set","g":777,"kind":")") + _kind + R"(","doc":)" + ed::json::write(_doc) + "}");
			++sent;
			rig.runUntil([&] { return !desk.isBusy(); }, 5000);
		};
		for(const auto& [s, g] : f.md.globals)
			if(s == active && g.version == 6 && g.revision == 1)	// the session leaves out an older OS's globals
				set("global", ed::globalToJson(g));
		std::printf("  before: pattern %d kit %d global %d\n", desk.linkState().pattern ? *desk.linkState().pattern : -1, desk.linkState().kit ? *desk.linkState().kit : -1, active);
		for(const auto& [s, k] : f.md.kits) set("kit", ed::kitToJson(k));
		std::printf("  after kits: pattern %d kit %d\n", desk.linkState().pattern ? *desk.linkState().pattern : -1, desk.linkState().kit ? *desk.linkState().kit : -1);
		for(const auto& [s, p] : f.md.patterns) set("pattern", ed::patternToJson(p));
		std::printf("  after patterns: pattern %d kit %d global %d\n", desk.linkState().pattern ? *desk.linkState().pattern : -1, desk.linkState().kit ? *desk.linkState().kit : -1, desk.documents().global ? desk.documents().global->position : -1);
		for(const auto& [s, g] : f.md.songs) set("song", ed::songToJson(g));
		rig.run(2000);
		size_t same = 0, total = 0;
		std::vector<std::string> off;
		const auto& v = desk.documents();
		const auto cmp = [&](const char* _k, const int _s, const Bytes& _a, const std::optional<Bytes>& _b)
		{
			++total;
			if(_b && *_b == _a) ++same;
			else if(off.size() < 8)
			{
				std::string d;
				if(_b)
					for(size_t i = 0, n = 0; i < std::min(_a.size(), _b->size()) && n < 6; ++i)
						if(_a[i] != (*_b)[i]) { char t[40]; std::snprintf(t, sizeof(t), " @%zu %02x->%02x", i, _a[i], (*_b)[i]); d += t; ++n; }
				off.push_back(std::string(_k) + " " + std::to_string(_s + 1) + (_b ? d : " missing") + (_b && _a.size() != _b->size() ? " (size " + std::to_string(_a.size()) + "/" + std::to_string(_b->size()) + ")" : ""));
			}
		};
		for(const auto& [s, k] : f.md.kits) { const auto it = v.kits.find(s); cmp("kit", s, ed::encodeMdKit(k), it == v.kits.end() ? std::nullopt : std::optional<Bytes>(ed::encodeMdKit(it->second))); }
		for(const auto& [s, p] : f.md.patterns) { const auto it = v.patterns.find(s); cmp("pattern", s, ed::encodeMdPattern(p), it == v.patterns.end() ? std::nullopt : std::optional<Bytes>(ed::encodeMdPattern(it->second))); }
		for(const auto& [s, g] : f.md.songs) { const auto it = v.songs.find(s); cmp("song", s, ed::encodeMdSong(g), it == v.songs.end() ? std::nullopt : std::optional<Bytes>(ed::encodeMdSong(it->second))); }
		if(f.md.globals.count(static_cast<uint8_t>(active)) && f.md.globals.at(static_cast<uint8_t>(active)).version == 6)
			cmp("global", active, ed::encodeMdGlobal(f.md.globals.at(static_cast<uint8_t>(active))), v.global ? std::optional<Bytes>(ed::encodeMdGlobal(*v.global)) : std::nullopt);
		std::printf("  %zu documents sent, %zu of %zu read back equal%s", sent, same, total, off.empty() ? "\n" : "; not equal:");
		for(const auto& o : off) std::printf(" %s", o.c_str());
		if(!off.empty()) std::printf("\n");
		// Measured on the AE backup (OS 1.2x era): its globals are format 5/1 (OS 1.63 stores 6/1, converting them),
		// and two kit names hold bytes the contract's name does not carry; everything else is byte-exact.
		check(total > 0 && same * 100 >= total * 98, "the imported documents read back from the firmware as in the file (" + std::to_string(same) + " of " + std::to_string(total) + ")");
		check(desk.coreState().history().size() == 1, "the whole import is one undo step (" + std::to_string(desk.coreState().history().size()) + ")");
	}

	// P7: in a DAW the plug-in sends the host's transport and tempo as MIDI Start, clock and Stop; the
	// session sends followHost so the machine's active global takes them (TEMPO IN external), without an
	// undo step. Steps counted over 4 s of a 100 BPM clock, before and after.
	// DESIGN-edit-flow.md, Control All against the firmware: what the desk's tweak shows at once (its model's
	// apply, mdDeskEdit.cpp) must be what FUNCTION + a DATA ENTRY knob does to every track, which is how the
	// desk delivers it here. A kit with RAM, CTR, MIDI, empty and synthesis machines; values at the ends.
	void controlAllTruth(Rig& _rig)
	{
		std::puts("== Control All: FUNCTION + a DATA ENTRY knob on the firmware, against the model");
		auto& m = _rig.machine();
		const auto modelOf = [](const char* _name)
		{
			for(uint32_t id = 0; id < 256; ++id)
				if(ed::mdMachineName(id) == _name)
					return static_cast<uint8_t>(id);
			return static_cast<uint8_t>(0);
		};
		const auto slot = *_rig.desk().linkState().kit;
		auto kit = *ed::decodeMdKit(m.request(ed::mdKitRequest(slot), ed::g_mdKitDump));
		// CTR-AL is left out: the firmware sets its track's LFO track to 16, which elektronData::validate refuses
		// (a kit with CTR-AL is not editable at all today; DESIGN-edit-flow.md, Built).
		const char* machines[16] = {"TRX-BD", "RAM-P1", "CTR-RE", "CTR-8P", "MID-01", "GND-EMPTY", "GND-SIN", "GND-NS",
			"RAM-R1", "TRX-SD", "EFM-BD", "E12-BD", "P-I-BD", "ROM-01", "TRX-XT", "INP-GA"};
		for(size_t t = 0; t < 16; ++t)
		{
			kit.models[t] = modelOf(machines[t]);
			for(size_t i = 0; i < 24; ++i)
				kit.params[t][i] = 60;
		}
		kit.params[9][5] = 126;
		kit.params[10][5] = 1;
		kit.params[11][8] = 125;
		kit.params[12][21] = 2;
		m.send(ed::encodeMdKit(kit));
		m.send(ed::mdLoadKit(slot));
		_rig.run(300);
		const auto memory = [&]
		{
			Bytes region(ed::g_mdWorkingKitRegionSize);
			for(size_t i = 0; i < region.size(); ++i)
				region[i] = m.read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
			return ed::mdWorkingKitFromMemory(region);
		};
		_rig.runUntil([&] { const auto& w = _rig.desk().documents().working; return w && w->kit.models == kit.models; }, 3000);
		check(_rig.desk().documents().working && _rig.desk().documents().working->kit.models == kit.models, "the test kit plays (memory)");
		// Which selected track leads the gesture (the firmware tweaks from the selected track): each candidate
		// selected with SET STATUS, FUNCTION + knob C +1, the tracks that moved counted. Measured: a MIDI, CTR or
		// RAM recorder machine moves itself alone (MIDI: to 255, which spoils the kit, so it goes last and the
		// probe runs only when asked: TWEAK_LEADERS=1; the desk selects a track that leads, controlAllLeads).
		if(std::getenv("TWEAK_LEADERS"))
			for(const char* group : {"syn", "fx"})
				for(const uint8_t lead : {uint8_t(0), uint8_t(5), uint8_t(6), uint8_t(7), uint8_t(10), uint8_t(11), uint8_t(12), uint8_t(13), uint8_t(15), uint8_t(1)})
				{
					const size_t index = std::string(group) == "fx" ? 10 : 0;
					const auto b = memory();
					_rig.page("{\"op\":\"tweak\",\"k\":" + std::to_string(slot) + ",\"group\":\"" + group + "\",\"knob\":" + std::to_string(index % 8)
						+ ",\"d\":1,\"t\":" + std::to_string(lead) + ",\"g\":" + std::to_string(500 + lead) + "}");
					_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 3000);
					_rig.run(100);
					const auto a = memory();
					int moved = 0;
					for(size_t t = 0; t < 16; ++t) moved += a->params[t][index] != b->params[t][index];
					std::printf("  %s: gesture on T%d %s (selected T%d): %d tracks moved\n", group, lead + 1, machines[lead],
						_rig.desk().linkState().track ? *_rig.desk().linkState().track + 1 : 0, moved);
				}
		// The machine left on another page with a track selected that cannot lead (a MIDI or CTR track: its page key
		// does not reach the synthesis page), as the Sound workspace finds it: the desk selects a leading track
		// first, then the page, then holds FUNCTION.
		int gSel = 700;
		for(const uint8_t sel : {uint8_t(4), uint8_t(2), uint8_t(3)})
		{
			for(const char* group : {"rt", "syn"})
			{
				m.send(ed::mdSetStatus(ed::MdStatus::Track, sel));
				_rig.run(200);
				const auto before = memory();
				mdDesk::Documents docs;
				docs.working = mdDesk::WorkingKit{*before};
				const std::string cmd = "{\"op\":\"tweak\",\"k\":" + std::to_string(slot) + ",\"group\":\"" + group + "\",\"knob\":0,\"d\":2,\"t\":"
					+ std::to_string(sel) + ",\"g\":" + std::to_string(gSel++) + "}";
				const auto model = mdDesk::apply(docs, parse(cmd), {}, {slot});
				_rig.page(cmd);
				const bool settled = _rig.runUntil([&]
				{
					const auto* st = _rig.desk().coreState().state({mdDesk::DocKind::WorkingKit, 0});
					return st && !st->pending && !_rig.desk().isBusy();
				}, 4000);
				_rig.run(100);
				const auto after = memory();
				const auto& want = std::get<mdDesk::WorkingKit>(model.changes.at(0).after).kit;
				const bool same = after->params == want.params;
				std::printf("  %s knob 0 +2 with T%d %s selected (knob page %d): %s, settled %s\n", group, sel + 1, machines[sel],
					m.read8(md::SequencerState::g_knobPageAddress), same ? "as the model" : "NOT as the model", settled ? "yes" : "no");
				check(same && settled, std::string("Control All from a non-leading selected track (") + machines[sel] + ", " + group + ")");
			}
		}
		// The machine's selected track cannot lead (a RAM recorder), and the first gesture is on it: the desk
		// selects one that leads.
		m.send(ed::mdSetStatus(ed::MdStatus::Track, 8));
		_rig.run(200);
		struct Case { const char* group; int knob; int d; int t; };
		const Case cases[] = {{"syn", 5, 3, 8}, {"syn", 5, -5, 9}, {"syn", 4, 7, 2}, {"syn", 1, -61, 0}, {"fx", 0, 4, 4}, {"rt", 5, -3, 1},
			{"rt", 1, 70, 15}};
		int id = 990, g = 77;
		for(const auto& c : cases)
		{
			const auto before = memory();
			if(!before)
			{
				check(false, "the working kit reads from memory");
				return;
			}
			mdDesk::Documents docs;
			docs.working = mdDesk::WorkingKit{*before};
			const std::string cmd = "{\"op\":\"tweak\",\"k\":" + std::to_string(slot) + ",\"group\":\"" + c.group + "\",\"knob\":"
				+ std::to_string(c.knob) + ",\"d\":" + std::to_string(c.d) + ",\"t\":" + std::to_string(c.t) + ",\"g\":" + std::to_string(g++)
				+ ",\"id\":" + std::to_string(id++) + "}";
			const auto model = mdDesk::apply(docs, parse(cmd), {}, {slot});
			_rig.page(cmd);
			const bool settled = _rig.runUntil([&]
			{
				const auto* st = _rig.desk().coreState().state({mdDesk::DocKind::WorkingKit, 0});
				return st && !st->pending && !_rig.desk().isBusy();
			}, 3000);
			_rig.run(100);
			const auto after = memory();
			if(model.changes.size() != 1 || !after)
			{
				for(const auto& e : model.errors)
					std::printf("  model: %s\n", e.c_str());
				std::printf("  lfo tracks:");
				for(size_t t = 0; t < 16; ++t)
					std::printf(" %d", before->lfos[t].track);
				std::printf("\n");
				check(false, std::string("the tweak ") + c.group + " is one change");
				continue;
			}
			const auto& want = std::get<mdDesk::WorkingKit>(model.changes[0].after).kit;
			const size_t index = (std::string(c.group) == "fx" ? 8u : std::string(c.group) == "rt" ? 16u : 0u) + static_cast<size_t>(c.knob);
			std::printf("  %s knob %d %+d (gesture on T%d, selected T%d):", c.group, c.knob, c.d, c.t + 1,
				_rig.desk().linkState().track ? *_rig.desk().linkState().track + 1 : 0);
			int moved = 0, differ = 0;
			for(size_t t = 0; t < 16; ++t)
			{
				moved += after->params[t][index] != before->params[t][index];
				if(after->params[t][index] != want.params[t][index])
				{
					++differ;
					std::printf(" [T%zu %s: firmware %d, model %d]", t + 1, machines[t], after->params[t][index], want.params[t][index]);
				}
			}
			std::printf(" %d tracks moved, %d differ from the model, settled %s\n", moved, differ, settled ? "yes" : "no");
			check(differ == 0 && after->params == want.params, std::string("the firmware's Control All equals the model's (") + c.group + ")");
			check(settled, "the tweak settles from memory, no read-back");
		}
		// A drag: 60 moves a second for 1 s, one tweak each (the page's frame), 1-4 steps a move either way,
		// into the clamp at 0: the machine ends where the model does, one undo step.
		{
			const auto before = memory();
			mdDesk::Documents docs;
			docs.working = mdDesk::WorkingKit{*before};
			const auto undo0 = _rig.desk().coreState().history().size();
			for(int n = 0; n < 60; ++n)
			{
				const int d = (n % 7 == 3 ? -4 : 1 + n % 3) * (n < 30 ? 1 : -1);
				const std::string cmd = "{\"op\":\"tweak\",\"k\":" + std::to_string(slot) + ",\"group\":\"fx\",\"knob\":3,\"d\":" + std::to_string(d)
					+ ",\"t\":0,\"g\":900}";
				auto r = mdDesk::apply(docs, parse(cmd), {}, {slot});
				if(r.changes.size() == 1)
					docs.working = std::get<mdDesk::WorkingKit>(r.changes[0].after);
				_rig.page(cmd);
				_rig.run(1000.0 / 60);
			}
			const bool settled = _rig.runUntil([&]
			{
				const auto* st = _rig.desk().coreState().state({mdDesk::DocKind::WorkingKit, 0});
				return st && !st->pending && !_rig.desk().isBusy();
			}, 5000);
			_rig.run(100);
			const auto after = memory();
			int differ = 0;
			for(size_t t = 0; t < 16; ++t)
				differ += after->params[t][11] != docs.working->kit.params[t][11];
			std::printf("  drag fx knob 3, 60 moves: T1 %d -> %d (model %d), %d tracks differ, %zu undo step(s), settled %s\n", before->params[0][11],
				after->params[0][11], docs.working->kit.params[0][11], differ, _rig.desk().coreState().history().size() - undo0, settled ? "yes" : "no");
			check(differ == 0 && settled && _rig.desk().coreState().history().size() - undo0 == 1, "a Control All drag ends where the model does, one undo step");
		}
		// The owner's sequence (0.2.1): a Mix fader (routing VOL), then the Sound page's boxes (synthesis, effects),
		// each a short drag of six moves 20 ms apart, as the page sends them.
		{
			struct Drag { const char* group; int knob; int d; int t; };
			const Drag drags[] = {{"rt", 1, -4, 6}, {"syn", 0, 2, 0}, {"fx", 0, 2, 0}, {"rt", 5, -2, 0}, {"fx", 4, -6, 0}};
			int gd = 950;
			for(const auto& dr : drags)
			{
				const auto before = memory();
				mdDesk::Documents docs;
				docs.working = mdDesk::WorkingKit{*before};
				for(int n = 0; n < 6; ++n)
				{
					const std::string cmd = "{\"op\":\"tweak\",\"k\":" + std::to_string(slot) + ",\"group\":\"" + dr.group + "\",\"knob\":"
						+ std::to_string(dr.knob) + ",\"d\":" + std::to_string(dr.d) + ",\"t\":" + std::to_string(dr.t) + ",\"g\":" + std::to_string(gd) + "}";
					auto r = mdDesk::apply(docs, parse(cmd), {}, {slot});
					if(r.changes.size() == 1)
						docs.working = std::get<mdDesk::WorkingKit>(r.changes[0].after);
					_rig.page(cmd);
					_rig.run(20);
				}
				++gd;
				const bool settled = _rig.runUntil([&]
				{
					const auto* st = _rig.desk().coreState().state({mdDesk::DocKind::WorkingKit, 0});
					return st && !st->pending && !_rig.desk().isBusy();
				}, 5000);
				_rig.run(100);
				const auto after = memory();
				const bool same = after->params == docs.working->kit.params;
				const size_t index = (std::string(dr.group) == "fx" ? 8u : std::string(dr.group) == "rt" ? 16u : 0u) + static_cast<size_t>(dr.knob);
				std::printf("  drag %s knob %d, 6 x %+d: T1 %d -> %d (model %d), %s, settled %s\n", dr.group, dr.knob, dr.d, before->params[0][index],
					after->params[0][index], docs.working->kit.params[0][index], same ? "as the model" : "NOT as the model", settled ? "yes" : "no");
				check(same && settled, std::string("Mix then Sound: a short drag ends where the model does (") + dr.group + ")");
			}
		}
	}

	void hostClock(const Bytes& _rom, const std::string& _romName)
	{
		std::puts("host clock");
		Rig rig(_rom, _romName);
		auto& desk = rig.desk();
		rig.page(R"({"op":"ready"})");
		rig.runUntil([&] { return desk.isReady() && desk.documents().global && desk.linkState().pattern; }, 8000);
		rig.run(1500);
		auto& m = rig.machine();
		const auto clocked = [&](const double _bpm = 100)
		{
			m.send({0xfa});
			int steps = 0, last = m.playhead();
			const double every = 60000.0 / _bpm / 24;
			for(double t = 0; t < 4000; t += every) { m.send({0xf8}); rig.run(every); const int p = m.playhead(); if(p != last) { ++steps; last = p; } }
			m.send({0xfc});
			rig.run(300);
			return steps / 4.0;
		};
		const double want = 100.0 / 60 * 4;
		const double before = clocked();
		std::printf("  as booted: %.2f steps/s with a 100 BPM clock (%.2f), sync %02x\n", before, want, desk.documents().global->syncFlags);
		const double before150 = clocked(150);
		std::printf("  as booted at a 150 BPM clock: %.2f steps/s (ratio %.2f)\n", before150, before > 0 ? before150 / before : 0);
		check(std::abs(before150 / before - 1.0) < 0.1 && std::abs(before - want) > 0.8, "as booted the machine plays its own tempo, not the host's");
		rig.page(R"({"op":"followHost","id":1})");
		std::printf("  followHost: %s\n", rig.lastResult() ? rig.lastResult()->find("note")->asString().c_str() : "(no result)");
		rig.run(2000);
		check(desk.documents().global && (desk.documents().global->syncFlags & ed::mdGlobalBits::g_tempoInExternal), "followHost sets TEMPO IN external, read back");
		check(desk.coreState().history().size() == 0, "without an undo step");
		const double after = clocked(), at150 = clocked(150);
		std::printf("  after followHost: %.2f steps/s at a 100 BPM clock, %.2f at 150 (ratio %.2f)\n", after, at150, after > 0 ? at150 / after : 0);
		check(after > 1 && std::abs(at150 / after - 1.5) < 0.1, "the machine follows the host's Start and its clock (100 and 150 BPM)");
		const auto p0 = m.playhead();
		rig.run(1000);
		check(m.playhead() == p0, "and stops on the host's Stop");
		rig.page(R"({"op":"followHost","id":2})");
		check(rig.lastResult() && rig.lastResult()->find("note")->asString().empty(), "a second followHost changes nothing");
	}

	// P9: a WAV into a UW ROM slot through the desk (decode, mono 16-bit, SDS with the machine's
	// handshake, the name with 0x73), then the slot's waveform as the desk publishes it from the machine's
	// memory, against the peaks of what was sent. Then RAM-R1 records and RAM 1 shows a take.
	void samples(Rig& _rig)
	{
		std::puts("== SAMPLES: WAV files into ROM slots, the waveforms read back");
		auto& desk = _rig.desk();
		auto& hw = _rig.machine().hardware();
		_rig.runUntil([&] { return hw.isFactoryFlashReadyForReboot(); }, 60000);
		_rig.watchSamples();
		check(_rig.runUntil([&] { return _rig.samplesDoc().has_value(); }, 5000), "the samples document comes from memory");
		if(!_rig.samplesDoc())
			return;
		const auto slotDoc = [&](const char* _kind, const int _n) { return _rig.samplesDoc()->find(_kind)->asArray()[size_t(_n)]; };
		const auto rom0 = slotDoc("rom", 0);
		std::printf("  ROM-01: %s %d samples at %d Hz; used %d of %d\n", rom0.find("name")->isString() ? rom0.find("name")->asString().c_str() : "-",
			int(rom0.find("length")->asNumber()), int(rom0.find("rate")->asNumber()), int(_rig.samplesDoc()->find("used")->asNumber()),
			int(_rig.samplesDoc()->find("capacity")->asNumber()));
		check(!rom0.find("empty")->asBool() && rom0.find("length")->asNumber() > 0 && rom0.find("peaks")->asArray().size() == 2 * ed::g_mdSampleBins,
			"factory ROM-01 has a waveform");
		check(_rig.samplesDoc()->find("ramReadable")->asBool(), "the RAM buffers can be read (DSP table and expander found)");

		// A tone with a shape: a decaying sine, silence, then a half-scale square.
		const auto make = [](const uint32_t _rate, const size_t _frames, const size_t _channels)
		{
			ed::AudioClip c;
			c.rate = _rate;
			c.channels.assign(_channels, std::vector<float>(_frames));
			for(size_t i = 0; i < _frames; ++i)
			{
				const double u = double(i) / _frames;
				const float v = u < 0.5 ? float(0.9 * std::exp(-u * 6) * std::sin(i * 0.07)) : u < 0.6 ? 0.0f : ((i / 40) % 2 ? 0.5f : -0.5f);
				for(size_t ch = 0; ch < _channels; ++ch)
					c.channels[ch][i] = ch ? v * 0.5f : v;
			}
			return c;
		};
		const auto load = [&](const uint8_t _slot, const std::string& _file, const ed::AudioClip& _clip)
		{
			const auto wav = ed::encodeWav16(_clip);
			std::string error;
			// What the desk sends, as the oracle: the file decoded and prepared the same way.
			const auto sent = ed::prepareMdSample(*ed::decodeAudioFile(wav, error), _file, 1u << 20, error);
			const auto t0 = _rig.machine().now();
			const auto why = desk.loadSample(_slot, _file, wav);
			check(why.empty(), "loadSample " + _file + " into ROM-" + std::to_string(_slot + 1) + (why.empty() ? "" : ": " + why));
			const bool done = _rig.runUntil([&] { const auto& l = _rig.sampleLoad(); return l && l->find("state")->asString() != "sending"; }, 120000);
			const auto took = ms(_rig.machine().now() - t0);
			check(done && _rig.sampleLoad()->find("state")->asString() == "done", "SDS done (" + std::to_string(int(took)) + " ms machine time, "
				+ std::to_string(sent->samples.size()) + " samples, " + std::to_string(int(_rig.sampleLoad()->find("total")->asNumber())) + " packets, retries "
				+ std::to_string(int(_rig.sampleLoad()->find("retries")->asNumber())) + ")");
			const auto shows = [&]
			{
				const auto s = slotDoc("rom", _slot);
				return !s.find("empty")->asBool() && size_t(s.find("length")->asNumber()) == sent->samples.size();
			};
			check(_rig.runUntil(shows, 20000), "the slot's waveform shows the new sample");
			const auto s = slotDoc("rom", _slot);
			const auto want = ed::mdPeaks(static_cast<uint32_t>(sent->samples.size()), ed::g_mdSampleBins,
				[&](const uint32_t _i) { return sent->samples[_i] / 32768.0f; });
			const auto& got = s.find("peaks")->asArray();
			int worst = 0;
			for(size_t i = 0; i < got.size() && i < want.size(); ++i)
				worst = std::max(worst, std::abs(int(got[i].asNumber()) - want[i]));
			check(got.size() == want.size() && worst <= 1, "its peaks match what was sent (worst bin off by " + std::to_string(worst) + ")");
			check(int(s.find("rate")->asNumber()) == int(sent->rate) && s.find("name")->isString() && s.find("name")->asString() == sent->name,
				"rate " + std::to_string(sent->rate) + " Hz and name " + sent->name + " (0x73) read back");
		};
		load(40, "TONE.wav", make(32000, 16000, 1));
		{
			// Its detail (sampleWave) and its audition, from the machine's memory, against what was sent.
			std::string error;
			const auto sent = ed::prepareMdSample(*ed::decodeAudioFile(ed::encodeWav16(make(32000, 16000, 1)), error), "TONE.wav", 1u << 20, error);
			ed::MdSampleSlot oracle;
			oracle.empty = false;
			oracle.length = static_cast<uint32_t>(sent->samples.size());
			oracle.pcm = std::make_shared<const std::vector<int16_t>>(sent->samples);
			_rig.page(R"({"op":"sampleWave","bank":"rom","slot":40,"bins":4096,"id":901})");
			const auto& w = _rig.sampleWave();
			const auto want = ed::mdWavePeaks(oracle, 4096);
			bool same = w && w->find("peaks")->asArray().size() == want.size();
			for(size_t i = 0; same && i < want.size(); ++i)
				same = int(w->find("peaks")->asArray()[i].asNumber()) == want[i];
			check(resultOk(_rig) && same && int(w->find("bins")->asNumber()) == 4096, "sampleWave: ROM-41 at 4096 bins, exactly the peaks of what was sent");
			_rig.page(R"({"op":"audition","bank":"rom","slot":40,"id":902})");
			const auto& a = _rig.auditionMessage();
			check(resultOk(_rig) && a && a->find("state")->asString() == "playing" && int(a->find("rate")->asNumber()) == 32000, "audition: ROM-41 plays at its own 32 kHz");
			// The audio thread: 32000 -> 44100, linear; the oracle interpolates what was sent.
			const auto frames = static_cast<size_t>(sent->samples.size() * 44100.0 / 32000.0) + 64;
			std::vector<float> out(frames, 0.0f);
			_rig.auditionMix(out, 44100);
			float worst = 0;
			for(size_t f = 0; f < frames; ++f)
			{
				const double pos = f * (32000.0 / 44100.0);
				const auto i = static_cast<size_t>(pos);
				float v = 0;
				if(i < sent->samples.size())
				{
					const float s0 = sent->samples[i], s1 = i + 1 < sent->samples.size() ? sent->samples[i + 1] : s0;
					v = (s0 + (s1 - s0) * float(pos - double(i))) / 32768.0f;
				}
				worst = std::max(worst, std::fabs(out[f] - v));
			}
			check(worst < 1e-4f, "audition: the output is the slot's samples from memory, resampled (worst " + std::to_string(worst) + ")");
			_rig.run(50);
			check(_rig.auditionMessage()->find("state")->asString() == "stopped", "audition: stopped once it played out");
		}
		load(2, "Kick Big.wav", make(48000, 9000, 2));	// over a factory sample; stereo at 48 kHz
		check(slotDoc("rom", 0).find("length")->asNumber() == rom0.find("length")->asNumber(), "the other slots are untouched");

		// A RAM slot cannot take a file (measured: no answer to SDS sample 48, nothing stored).
		check(!desk.loadSample(48, "x.wav", ed::encodeWav16(make(44100, 100, 1))).empty(), "RAM 1: refused");

		// RAM-R1 records a loop: RAM 1 shows a take.
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto pattern = std::to_string(*desk.linkState().pattern);
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":12,\"model\":" + std::to_string(*ed::mdMachineModel("RAM-R1")) + ",\"keepFx\":true}");
		_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":12,\"s\":0,\"on\":true}");
		_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":12,\"i\":6,\"v\":32}");	// LEN: 8 steps
		_rig.run(1500);
		_rig.page(R"({"op":"play"})");
		_rig.run(3000);
		_rig.page(R"({"op":"stop"})");
		check(_rig.runUntil([&] { return !slotDoc("ram", 0).find("empty")->asBool(); }, 10000), "RAM 1 shows the take RAM-R1 recorded ("
			+ std::to_string(int(slotDoc("ram", 0).find("length")->asNumber())) + " samples)");
	}

	// An unsaved kit edit of the kit that plays survives a trig elsewhere (a pattern dump over the current pattern
	// makes OS 1.63 load the kit it links from its slot; MdMachine::restoreWorkingKit sends the edits again): a
	// synthesis value as well as a routing one, stopped and playing, as the demo videos found.
	void keepEdits(Rig& _rig)
	{
		auto& desk = _rig.desk();
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto pattern = std::to_string(*desk.linkState().pattern);
		const auto working = [&] { return desk.documents().working->kit; };
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 3000); _rig.run(1500); };
		int step = 7;
		for(const bool playing : {false, true})
		{
			if(playing)
			{
				_rig.page(R"({"op":"play"})");
				_rig.run(1000);
			}
			for(const int i : {1, 16})
			{
				std::printf("== KEEP EDITS: T1 param %d, %s\n", i, playing ? "playing" : "stopped");
				// as the page's drag sends it: a run of values in one gesture (g), the last one kept
				const auto v0 = working().params[0][i];
				const auto v = (v0 + 37) & 0x7f;
				const auto g = std::to_string(800 + step);
				for(const int d : {10, 20, 30})
				{
					_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":0,\"i\":" + std::to_string(i) + ",\"v\":" + std::to_string((v0 + d) & 0x7f) + ",\"g\":" + g + "}");
					_rig.run(60);
				}
				_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":0,\"i\":" + std::to_string(i) + ",\"v\":" + std::to_string(v) + ",\"g\":" + g + "}");
				settle();
				check(working().params[0][i] == v, "the edit is in the working kit (" + std::to_string(working().params[0][i]) + ", want " + std::to_string(v) + ")");
				_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":3,\"s\":" + std::to_string(step) + ",\"on\":true}");
				settle();
				_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":3,\"s\":" + std::to_string(step) + ",\"on\":false}");
				settle();
				_rig.run(2000);
				check(working().params[0][i] == v, "after a trig elsewhere the edit is still in the working kit (" + std::to_string(working().params[0][i]) + ", want " + std::to_string(v) + ")");
				++step;
			}
		}
		_rig.page(R"({"op":"stop"})");
		_rig.run(500);
	}

	// The working kit as the machine holds it (its memory, the test's own oracle).
	std::optional<ed::MdKit> workingFromMemory(Rig& _rig)
	{
		Bytes region(ed::g_mdWorkingKitRegionSize);
		for(size_t i = 0; i < region.size(); ++i)
			region[i] = _rig.machine().read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
		return ed::mdWorkingKitFromMemory(region);
	}

	// Undo of a Control All gesture (Alt-drag) reaches the machine: its memory is the kit before the gesture.
	void undoControlAll(Rig& _rig)
	{
		std::puts("== KEEP EDITS: undo of a Control All gesture");
		auto& desk = _rig.desk();
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 4000); _rig.run(1500); };
		// a kick on track 1, played by its note: its level heard before, swept and after the undo
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":0,\"model\":" + std::to_string(*ed::mdMachineModel("TRX-BD")) + ",\"keepFx\":true}");
		settle();
		_rig.page(R"({"op":"mute","t":0,"on":false})");
		_rig.run(200);
		auto& m = _rig.machine();
		const auto heard = [&]
		{
			const auto from = m.left().size();
			_rig.page(R"({"op":"noteOn","t":0,"vel":127,"pitch":0})");
			_rig.run(300);
			double sum = 0;
			size_t n = 0;
			for(size_t i = from; i < m.left().size(); ++i, ++n)
				sum += m.left()[i] * m.left()[i] + m.right()[i] * m.right()[i];
			_rig.page(R"({"op":"noteOff","t":0,"pitch":0})");
			_rig.run(500);
			return 10 * std::log10(sum / std::max<size_t>(1, 2 * n) + 1e-12);
		};
		const auto before = workingFromMemory(_rig);
		require(before.has_value(), "the working kit in memory");
		_rig.seedPluginParams(*before);
		const auto dbBefore = heard();
		// the effects page, knob 4 (FLTF), +20 three times in one gesture, from track 1
		for(int k = 0; k < 3; ++k)
		{
			_rig.page("{\"op\":\"tweak\",\"k\":" + kit + ",\"group\":\"fx\",\"knob\":4,\"d\":20,\"t\":0,\"g\":990}");
			_rig.run(120);
		}
		settle();
		const auto swept = workingFromMemory(_rig);
		int moved = 0;
		for(size_t t = 0; t < 16; ++t)
			moved += swept->params[t][12] != before->params[t][12];
		check(moved >= 2, "Control All moved FLTF on " + std::to_string(moved) + " tracks in the machine");
		const auto dbSwept = heard();
		_rig.page(R"({"op":"undo"})");
		settle();
		_rig.run(1500);
		const auto dbBack = heard();
		std::printf("  track 1's kick: %.1f dB before, %.1f dB swept, %.1f dB after the undo\n", dbBefore, dbSwept, dbBack);
		check(dbSwept < dbBefore - 3, "the sweep is heard");
		check(std::abs(dbBack - dbBefore) < 1.5, "after the undo the kick sounds as before (heard, not only read back)");
		const auto back = workingFromMemory(_rig);
		std::string differs;
		for(size_t t = 0; t < 16; ++t)
			if(back->params[t][12] != before->params[t][12])
				differs += " T" + std::to_string(t + 1) + "=" + std::to_string(back->params[t][12]) + "(was " + std::to_string(before->params[t][12]) + ")";
		check(differs.empty(), "after undo the machine's FLTF is back on every track" + (differs.empty() ? std::string() : ":" + differs));
		check(desk.documents().working->kit.params == back->params, "the page's working kit is the machine's after the undo");
	}

	// A value moved straight after a machine change reaches the machine and stays (seen in the demo videos: a
	// GND-SIN's PTCH and an E12-SD's RTRG read back 0).
	void valueAfterMachine(Rig& _rig)
	{
		auto& desk = _rig.desk();
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto pattern = std::to_string(*desk.linkState().pattern);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 4000); _rig.run(1500); };
		struct Case { uint8_t t; const char* model; uint8_t i; uint8_t v; int gapMs; };
		const Case cases[] = {{11, "GND-SIN", 0, 28, 0}, {14, "E12-SD", 5, 40, 0}, {10, "GND-SIN", 1, 90, 120}, {13, "E12-SD", 6, 24, 400}};
		for(const auto& c : cases)
		{
			std::printf("== KEEP EDITS: T%d %s, then param %d = %d after %d ms (the old machine had it)\n", c.t + 1, c.model, c.i, c.v, c.gapMs);
			// the old machine holds the value first, through the parameter: the parameter then holds it too, and the
			// new machine starts from its own initial value
			_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":" + std::to_string(c.t) + ",\"i\":" + std::to_string(c.i) + ",\"v\":" + std::to_string(c.v) + "}");
			settle();
			_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":" + std::to_string(c.t) + ",\"model\":" + std::to_string(*ed::mdMachineModel(c.model)) + ",\"keepFx\":true}");
			_rig.run(c.gapMs);
			_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":" + std::to_string(c.t) + ",\"i\":" + std::to_string(c.i) + ",\"v\":" + std::to_string(c.v) + "}");
			settle();
			_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":3,\"s\":9,\"on\":true}");
			settle();
			_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":3,\"s\":9,\"on\":false}");
			settle();
			const auto mem = workingFromMemory(_rig);
			const auto w = desk.documents().working->kit;
			check(mem && ed::mdMachineName(mem->models[c.t]) == c.model && mem->params[c.t][c.i] == c.v,
				std::string("the machine plays ") + c.model + " with the value (memory: " + (mem ? ed::mdMachineName(mem->models[c.t]) + " " + std::to_string(mem->params[c.t][c.i]) : std::string("none")) + ")");
			check(w.params[c.t][c.i] == c.v, "the page shows it (" + std::to_string(w.params[c.t][c.i]) + ")");
		}
	}

	// A RAM recorder sampling the main mix is itself in the main mix: what it records comes out of its track and goes
	// back into what it records (the demo videos heard it ring). Its track's VOL at 0 takes it out of the mix; its
	// recording goes on (Set up sampling does this, mdDeskSampler.js).
	void recorderInTheMix(Rig& _rig)
	{
		std::puts("== RECORDER: RAM-R1 sampling the main mix, its own track in the mix");
		auto& desk = _rig.desk();
		auto& m = _rig.machine();
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto pattern = std::to_string(*desk.linkState().pattern);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 4000); _rig.run(500); };
		const auto param = [&](int _t, int _i, int _v) { _rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":" + std::to_string(_t) + ",\"i\":" + std::to_string(_i) + ",\"v\":" + std::to_string(_v) + "}"); settle(); };
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":0,\"model\":" + std::to_string(*ed::mdMachineModel("TRX-BD")) + ",\"keepFx\":true}");
		settle();
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":12,\"model\":" + std::to_string(*ed::mdMachineModel("RAM-R1")) + ",\"keepFx\":true}");
		settle();
		param(0, 17, 110);
		// the recorder: the machine's mix as it is (MLEV 64), no input, one bar, full rate, its track at VOL 127
		param(12, 0, 64); param(12, 1, 64); param(12, 2, 0); param(12, 6, 64); param(12, 7, 127); param(12, 17, 127);
		for(const int st : {0, 4, 8, 12})
			_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":0,\"s\":" + std::to_string(st) + ",\"on\":true}");
		_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":12,\"s\":0,\"on\":true}");
		settle();
		_rig.page(R"({"op":"mute","t":12,"on":false})");
		_rig.page(R"({"op":"play"})");
		_rig.run(2500);
		const auto level = [&](const double _ms)
		{
			const auto from = m.left().size();
			_rig.run(_ms);
			double sum = 0;
			size_t n = 0;
			for(size_t i = from; i < m.left().size(); ++i, ++n)
				sum += m.left()[i] * m.left()[i] + m.right()[i] * m.right()[i];
			return 10 * std::log10(sum / std::max<size_t>(1, 2 * n) + 1e-12);
		};
		const auto loud = level(4000);
		param(12, 17, 0);
		_rig.run(2000);
		const auto silent = level(4000);
		_rig.page(R"({"op":"mute","t":12,"on":true})");
		_rig.run(2000);
		const auto muted = level(4000);
		std::printf("  the main mix: recorder at VOL 127 %.1f dB, at VOL 0 %.1f dB, muted %.1f dB\n", loud, silent, muted);
		check(loud > muted + 1.0, "the recorder at VOL 127 is in the main mix (louder than muted)");
		check(std::abs(silent - muted) < 0.5, "the recorder at VOL 0 is out of the main mix (as muted)");
		_rig.page(R"({"op":"stop"})");
		_rig.run(300);
	}

	// The SAMPLER card's "Set up sampling" (mdDeskSampler.js, [data-setupgo]): two machine ops in one gesture put
	// RAM-R1 and RAM-P1 on two tracks that played ROM machines, a trig on the recorder; then the chop grid's
	// trigs + STRT locks on the player. A pattern dump over the current pattern makes OS 1.63 load the kit it
	// links from its slot, also the kit that plays: the desk sends the unsaved edits again after it
	// (MdMachine::restoreWorkingKit), and the page never sees the slot's kit meanwhile.
	void samplerSetup(Rig& _rig)
	{
		std::puts("== SAMPLER: Set up sampling (RAM-R1/RAM-P1 over ROM machines), then chops");
		auto& desk = _rig.desk();
		const auto kit = std::to_string(*desk.linkState().kit);
		const auto pattern = std::to_string(*desk.linkState().pattern);
		const auto model = [](const char* _n) { return *ed::mdMachineModel(_n); };
		const auto working = [&] { return desk.documents().working->kit; };
		const auto models = [&] { const auto w = working(); return std::pair{w.models[12], w.models[13]}; };
		const auto show = [&](const char* _when)
		{
			const auto [r, p] = models();
			std::printf("  %s: T13 %s, T14 %s\n", _when, ed::mdMachineName(r).c_str(), ed::mdMachineName(p).c_str());
		};
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 3000); _rig.run(1500); };
		// Run while watching the working kit the page is shown: it must stay _want throughout.
		const auto hold = [&](const ed::MdKit& _want, const double _ms)
		{
			bool held = true;
			for(double t = 0; t < _ms; t += 10)
			{
				_rig.run(10);
				const auto w = working();
				if(!ed::mdSameKitSound(w, _want) && held)
				{
					for(size_t tr = 0; tr < 16; ++tr)
					{
						if(w.models[tr] != _want.models[tr])
							std::printf("  differs: T%zu machine %s, not %s\n", tr + 1, ed::mdMachineName(w.models[tr]).c_str(), ed::mdMachineName(_want.models[tr]).c_str());
						for(size_t i = 0; i < 24; ++i)
							if(w.params[tr][i] != _want.params[tr][i])
								std::printf("  differs: T%zu param %zu = %d, not %d\n", tr + 1, i, w.params[tr][i], _want.params[tr][i]);
						if(w.levels[tr] != _want.levels[tr])
							std::printf("  differs: T%zu level\n", tr + 1);
					}
					held = false;
				}
			}
			return held;
		};
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":12,\"model\":" + std::to_string(model("ROM-21")) + ",\"keepFx\":true}");
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":13,\"model\":" + std::to_string(model("ROM-24")) + ",\"keepFx\":true}");
		settle();
		show("before");
		check(models() == std::pair{model("ROM-21"), model("ROM-24")}, "the two tracks play ROM-21 and ROM-24 (not saved)");

		// Any unsaved edit, a CC one too, survives a trig elsewhere.
		const auto dist = (working().params[0][16] + 17) & 0x7f;
		_rig.page("{\"op\":\"param\",\"k\":" + kit + ",\"t\":0,\"i\":16,\"v\":" + std::to_string(dist) + "}");
		settle();
		const auto edited = working();
		_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":3,\"s\":7,\"on\":true}");
		check(hold(edited, 1500), "a trig on another track keeps the unsaved kit edits, shown and held (T1 DIST, T13/T14 machines)");

		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":12,\"model\":" + std::to_string(model("RAM-R1")) + ",\"keepFx\":true,\"g\":901}");
		check(resultOk(_rig), "RAM-R1 accepted");
		_rig.page("{\"op\":\"machine\",\"k\":" + kit + ",\"t\":13,\"model\":" + std::to_string(model("RAM-P1")) + ",\"keepFx\":true,\"g\":901}");
		check(resultOk(_rig), "RAM-P1 accepted");
		_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":12,\"s\":0,\"on\":true,\"g\":901}");
		settle();
		show("set up");
		check(models() == std::pair{model("RAM-R1"), model("RAM-P1")}, "after Set up sampling the working kit has RAM-R1 and RAM-P1");

		const auto setUp = working();
		bool held = true;
		for(int s = 0; s < 16; s += 4)
		{
			_rig.page("{\"op\":\"trig\",\"p\":" + pattern + ",\"t\":13,\"s\":" + std::to_string(s) + ",\"on\":true}");
			_rig.page("{\"op\":\"lock\",\"p\":" + pattern + ",\"t\":13,\"i\":4,\"s\":" + std::to_string(s) + ",\"v\":" + std::to_string(s * 8) + "}");
			held &= hold(setUp, 150);
		}
		held &= hold(setUp, 2000);
		show("chopped");
		check(held, "while chopping the working kit keeps RAM-R1 and RAM-P1 and every value");
		const auto saved = _rig.saveAndReadKit(*desk.linkState().kit);
		check(saved && saved->models[12] == model("RAM-R1") && saved->models[13] == model("RAM-P1") && saved->params[0][16] == dist,
			"the machine holds RAM-R1, RAM-P1 and the DIST edit");
		const auto chops = _rig.readPattern(*desk.linkState().pattern);
		check(chops && ed::hasTrig(*chops, 13, 12) && ed::lockValue(*chops, 13, 4, 12) == uint8_t{96}, "and the chops (trig + STRT lock)");
	}

	// DESIGN-generators.md slice 1: the generators' one edit (steps) on the firmware. The rows the page's
	// GEN strip sends (E(3,8) with accents, E(5,8), a track cleared) in one pattern dump, read back.
	void generatorsTruth(Rig& _rig)
	{
		std::puts("== GEN: a steps edit (the generators' rows), read back from the firmware");
		auto& desk = _rig.desk();
		const auto p = *desk.linkState().pattern;
		const auto ps = std::to_string(p);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 3000); };
		// Locks on track 1 steps 2 and 4: the generator keeps step 4 (E(3,8): 1, 4, 7) and turns step 2 off.
		for(const int s : {1, 3})
		{
			_rig.page("{\"op\":\"trig\",\"p\":" + ps + ",\"t\":0,\"s\":" + std::to_string(s) + ",\"on\":true}");
			_rig.page("{\"op\":\"lock\",\"p\":" + ps + ",\"t\":0,\"i\":12,\"s\":" + std::to_string(s) + ",\"v\":" + std::to_string(20 + s) + "}");
		}
		settle();
		// Accents per track (EDIT ALL off), so acc is the track's own: the pattern goes to the machine
		// with the flag off and the desk reads it again.
		if(desk.documents().patterns.at(p).accentEditAll)
		{
			auto own = desk.documents().patterns.at(p);
			own.accentEditAll = 0;
			_rig.machine().send(ed::encodeMdPattern(own));
			_rig.run(100);
			_rig.page("{\"op\":\"load\",\"kind\":\"pattern\",\"slot\":" + ps + "}");
			_rig.runUntil([&] { return desk.documents().patterns.at(p).accentEditAll == 0; }, 2000);
		}
		const bool editAll = desk.documents().patterns.at(p).accentEditAll != 0;
		const auto dumps0 = _rig.patternDumps();
		_rig.page("{\"op\":\"steps\",\"p\":" + ps + ",\"from\":0,\"to\":16,\"g\":700,\"rows\":[{\"t\":0,\"on\":[0,3,6,8,11,14],\"acc\":[0,8]},"
			"{\"t\":1,\"on\":[0,2,4,5,7,8,10,12,13,15]},{\"t\":2,\"on\":[]}]}");
		check(resultOk(_rig), "steps accepted");
		settle();
		std::printf("  pattern dumps for the edit: %zu\n", _rig.patternDumps() - dumps0);
		check(_rig.patternDumps() - dumps0 == 1, "one pattern dump for three rows");
		const auto want = desk.documents().patterns.at(p);
		const auto back = _rig.readPattern(p);
		check(back.has_value(), "the pattern reads back");
		if(!back)
			return;
		const auto bits = [](const ed::MdPattern& _p, const size_t _t) { return _p.trigs[_t] & 0xffff; };
		check(bits(*back, 0) == 0x4949 && bits(*back, 1) == 0xb5b5 && bits(*back, 2) == 0, "the firmware holds E(3,8), E(5,8) and the cleared track");
		check(editAll || (back->trackAccent[0] & 0xffff) == 0x0101, editAll ? "EDIT ALL: accents left" : "the firmware holds the accents on the first hit of each cycle");
		check(back->trigs == want.trigs && back->trackAccent == want.trackAccent && back->lockMasks == want.lockMasks, "the read-back equals the desk's pattern (trigs, accents, locked rows)");
		check(ed::lockValue(*back, 0, 12, 3) == uint8_t{23} && !ed::lockValue(*back, 0, 12, 1), "a kept step keeps its lock, a step turned off lost it");
		check(*back == want, "the read-back equals the edit, byte for byte in the codec");
		_rig.page(R"({"op":"undo"})");
		settle();
		const auto undone = _rig.readPattern(p);
		check(undone && ed::lockValue(*undone, 0, 12, 1) == uint8_t{21} && ed::hasTrig(*undone, 0, 1), "one undo brings back the steps and the lock");

		// QoL (§7): rotate (Alt + arrows, a train of presses with one g) and double, read back.
		_rig.page("{\"op\":\"totalLength\",\"p\":" + ps + ",\"v\":16}");
		settle();
		const auto before = desk.documents().patterns.at(p);
		for(int n = 0; n < 2; ++n)
			_rig.page("{\"op\":\"rotate\",\"p\":" + ps + ",\"t\":0,\"by\":1,\"g\":701}");
		settle();
		const auto turned = _rig.readPattern(p);
		check(turned && *turned == desk.documents().patterns.at(p) && ed::hasTrig(*turned, 0, 3) && ed::lockValue(*turned, 0, 12, 5) == uint8_t{23},
			"rotate twice: the firmware holds track 1 two steps later, its lock with it");
		_rig.page(R"({"op":"undo"})");
		settle();
		check(desk.documents().patterns.at(p).trigs == before.trigs, "one undo takes back the whole train of rotations");
		_rig.page("{\"op\":\"doublePattern\",\"p\":" + ps + "}");
		check(resultOk(_rig), "doublePattern accepted");
		settle();
		const auto doubled = _rig.readPattern(p);
		check(doubled && doubled->length == 32 && ed::visibleSteps(*doubled) == 32 && *doubled == desk.documents().patterns.at(p)
			&& (doubled->trigs[0] >> 16 & 0xffff) == (doubled->trigs[0] & 0xffff) && ed::lockValue(*doubled, 0, 12, 19) == uint8_t{23},
			"double: the firmware holds 32 steps, the second half a copy (trigs and locks)");
	}

	// B-010: the sequencer's timing while the page edits parameter locks. The pattern plays at 120 BPM
	// with TEMPO OUT on; the machine's MIDI clock (24 a beat, 20.83 ms apart) and its play head (a step
	// every 125 ms) are timed per audio block (64 frames, 1.45 ms) in phases: idle, a lock-lane drag
	// (a lock on the next step every 16 ms, as the page's bridge batches a drag per frame), clicks (one
	// lock every 250 ms, each its own gesture) and the wheel (one step's lock every 30 ms). Prints the
	// traffic to the machine, the jitter and the emulator's CPU time a block per phase (the first,
	// "load", is the page's first read of the library). _strict: fail when an edit phase has a clock tick
	// more than 3 ms off for any reason but a read-back (one a gesture), or its steps drift further
	// than idle's. PLOCK_INGRESS / PLOCK_TRANSMIT set md::Hardware's MIDI rates (0: unpaced, as before
	// B-010); PLOCK_PROBE=1 also times single dumps and read-backs at several rates.
	struct Jitter
	{
		double meanMs = 0, sdMs = 0, maxDevMs = 0;
		size_t n = 0;
		size_t off3 = 0;	// intervals more than 3 ms off
	};
	Jitter jitterOf(const std::vector<uint64_t>& _frames, const double _expectMs)
	{
		Jitter j;
		if(_frames.size() < 3)
			return j;
		std::vector<double> d;
		for(size_t i = 1; i < _frames.size(); ++i)
			d.push_back(double(_frames[i] - _frames[i - 1]) * 1000.0 / g_rate);
		double sum = 0;
		for(const auto x : d)
			sum += x;
		j.n = d.size();
		j.meanMs = sum / double(d.size());
		double var = 0;
		for(const auto x : d)
		{
			var += (x - j.meanMs) * (x - j.meanMs);
			j.maxDevMs = std::max(j.maxDevMs, std::abs(x - _expectMs));
			j.off3 += std::abs(x - _expectMs) > 3.0;
		}
		j.sdMs = std::sqrt(var / double(d.size()));
		return j;
	}

	// B-014: the audio thread's time per block, as percentiles of the 64-frame blocks and as sums over
	// host buffers of 128 and 256 frames (44.1 kHz: 2.90 and 5.80 ms), with how many buffers went over
	// half and over all of their budget.
	void printBlockStats(const char* _what, const std::vector<float>& _us)
	{
		if(_us.empty())
			return;
		const auto pct = [](std::vector<float> _v, const double _p)
		{
			std::sort(_v.begin(), _v.end());
			return _v[std::min(_v.size() - 1, static_cast<size_t>(_p * double(_v.size())))];
		};
		std::printf("         %s us/64: p50 %.0f p99 %.0f p99.9 %.0f max %.0f", _what, pct(_us, .5), pct(_us, .99), pct(_us, .999), pct(_us, 1.0));
		for(const size_t k : {size_t(2), size_t(4)})
		{
			std::vector<float> w;
			for(size_t i = 0; i + k <= _us.size(); i += k)
			{
				float sum = 0;
				for(size_t j = 0; j < k; ++j)
					sum += _us[i + j];
				w.push_back(sum);
			}
			if(w.empty())
				continue;
			const double budget = double(k * g_block) * 1e6 / g_rate;
			size_t half = 0, over = 0;
			for(const auto x : w)
			{
				half += x > budget * .5;
				over += x > budget;
			}
			std::printf(" | /%zu: p50 %.0f p99.9 %.0f max %.0f (%.0f%%) >50%%:%zu >100%%:%zu of %zu",
				k * g_block, pct(w, .5), pct(w, .999), pct(w, 1.0), pct(w, 1.0) * 100.0 / budget, half, over, w.size());
		}
		std::puts("");
	}

	void plockTiming(Rig& _rig, const bool _strict)
	{
		// PLOCK_INGRESS=<bytes/s>: the firmware's SysEx ingress rate for the run (0: unpaced, as before B-010).
		const char* rateEnv = std::getenv("PLOCK_INGRESS");
		const uint32_t ingressRate = rateEnv ? static_cast<uint32_t>(std::atoi(rateEnv)) : md::Hardware::g_defaultSysexIngressBytesPerSecond;
		_rig.machine().hardware().setSysexIngressRate(ingressRate);
		const char* txEnv = std::getenv("PLOCK_TRANSMIT");
		const uint32_t transmitRate = txEnv ? static_cast<uint32_t>(std::atoi(txEnv)) : md::Hardware::g_defaultMidiTransmitBytesPerSecond;
		_rig.machine().hardware().setMidiTransmitRate(transmitRate);
		std::printf("== B-010: sequencer timing while locks are edited (120 BPM, TEMPO OUT; SysEx in %u B/s, MIDI out %u B/s)\n", ingressRate, transmitRate);
		auto& desk = _rig.desk();
		auto& m = _rig.machine();
		const auto p = *desk.linkState().pattern;
		const auto ps = std::to_string(p);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy() && !_rig.pageTx(); }, 5000); };
		// Track 1 has a trig on every step (a lock needs one), the tempo is 120 and TEMPO OUT is on.
		_rig.page("{\"op\":\"steps\",\"p\":" + ps + ",\"from\":0,\"to\":16,\"g\":600,\"rows\":[{\"t\":0,\"on\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}]}");
		settle();
		_rig.page(R"({"op":"tempo","bpm":120})");
		_rig.page(R"({"op":"globalSet","field":"tempoOut","on":true})");
		settle();
		_rig.run(300);
		std::vector<uint64_t> clocks, steps;
		m.onMidi = [&](const synthLib::SMidiEvent& _e) { if(_e.a == 0xf8) clocks.push_back(m.now()); };
		int lastStep = -1;
		auto prevBlock = m.onBlock;
		m.onBlock = [&]
		{
			const int s = m.playhead();
			if(s != lastStep)
			{
				lastStep = s;
				steps.push_back(m.now());
			}
		};
		_rig.page(R"({"op":"play"})");
		_rig.runUntil([&] { return _rig.pageTelemetry().playing; }, 2000);
		_rig.run(500);

		int gesture = 610;
		Jitter idleClock, idleSteps;
		bool first = true;
		const auto phase = [&](const char* _name, const double _ms, const double _everyMs, const std::function<void(int)>& _edit)
		{
			// PLOCK_PHASES=idle,click1s,...: run only these (load always runs)
			if(const char* only = std::getenv("PLOCK_PHASES"); only && std::string(_name) != "load"
				&& ("," + std::string(only) + ",").find("," + std::string(_name) + ",") == std::string::npos)
				return;
			clocks.clear();
			steps.clear();
			const auto b0 = _rig.bytesToMachine, m0 = _rig.messagesToMachine, d0 = _rig.patternDumps(), r0 = _rig.patternRequests;
			const auto start = m.now();
			const auto kinds0 = _rig.messageKinds;
			m.resetAudioTime();
			const int docs0 = _rig.pageDocCount("pattern");
			const auto pb0 = _rig.pageBytes, ppb0 = _rig.pagePatternDocBytes, ppn0 = _rig.pagePatternDocs;
			const auto idle0 = m.hardware().getTransportScorecard().idleSelfBranchInstructions;
			double deskUs = 0;
			int n = 0;
			while(double(m.now() - start) * 1000.0 / g_rate < _ms)
			{
				const double t = double(m.now() - start) * 1000.0 / g_rate;
				if(_edit && t >= n * _everyMs)
				{
					const auto w0 = std::chrono::steady_clock::now();
					_edit(n++);
					deskUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - w0).count();
				}
				_rig.run(1);
			}
			if(_edit)
			{
				// the gesture ends: what the desk still sends after it (the last value, the read-back)
				settle();
			}
			const double secs = double(m.now() - start) / g_rate;
			const auto c = jitterOf(clocks, 1000.0 / 48.0);
			const auto st = jitterOf(steps, 125.0);
			std::printf("  %-6s %5.1f s  edits %4d  to machine: %6zu bytes (%5.0f B/s, DIN carries 3125), %4zu msgs, %3zu pattern dumps, %3zu pattern requests\n",
				_name, secs, n, _rig.bytesToMachine - b0, double(_rig.bytesToMachine - b0) / secs, _rig.messagesToMachine - m0,
				_rig.patternDumps() - d0, _rig.patternRequests - r0);
			std::printf("         clock: %zu ticks, mean %.2f ms (20.83), sd %.2f ms, worst %.2f ms off, %zu > 3 ms off   steps: %zu, mean %.1f ms (125), sd %.2f ms, worst %.2f ms off\n",
				c.n, c.meanMs, c.sdMs, c.maxDevMs, c.off3, st.n, st.meanMs, st.sdMs, st.maxDevMs);
			if(n)
				std::printf("         page: %d pattern documents published; the desk took %.0f us an edit (its thread, not audio)\n",
					_rig.pageDocCount("pattern") - docs0, deskUs / n);
			std::printf("         audio thread: %.1f us a block of %.0f us (mean), worst %.0f us\n", m.audioUs / double(std::max<uint64_t>(1, m.audioBlocks)),
				g_block * 1e6 / g_rate, m.audioMaxUs);
			printBlockStats("cpu ", m.blockCpuUs);
			if(std::getenv("PLOCK_PAGEBYTES"))
				std::printf("         to the page: %.0f KB/s, pattern documents %zu of %.1f KB each\n", double(_rig.pageBytes - pb0) / 1024.0 / secs,
					_rig.pagePatternDocs - ppn0, _rig.pagePatternDocs > ppn0 ? double(_rig.pagePatternDocBytes - ppb0) / 1024.0 / double(_rig.pagePatternDocs - ppn0) : 0.0);
			if(const auto sc = m.hardware().getTransportScorecard(); sc.enabled)
				std::printf("         68k idle-skipped: %.1f %% of its cycles\n",
					double(sc.idleSelfBranchInstructions - idle0) * 2.0 * 100.0 / (secs * double(md::g_ucClockHz)));
			printBlockStats("wall", m.blockWallUs);
			if(const char* dir = std::getenv("PLOCK_TRACE"))
			{
				// one line a block: Minstr, Mcycles, cpu us (B-014: the shape of the cost around an edit)
				std::ofstream f(std::string(dir) + "/" + _name + ".csv");
				f << "block,minstr,mcycles,cpuus\n";
				for(size_t i = 0; i < m.blockMInstr.size(); ++i)
					f << i << ',' << m.blockMInstr[i] << ',' << m.blockMCycles[i] << ',' << m.blockCpuUs[i] << '\n';
			}
			{
				// instructions and cycles (millions) a block: what the CPU time is when nothing else competes
				auto mi = m.blockMInstr, mc = m.blockMCycles;
				std::sort(mi.begin(), mi.end());
				std::sort(mc.begin(), mc.end());
				if(!mi.empty())
				{
					const auto at = [](const std::vector<float>& _v, const double _p) { return _v[std::min(_v.size() - 1, size_t(_p * double(_v.size())))]; };
					double sumI = 0, sumC = 0;
					for(const auto x : m.blockMInstr) sumI += x;
					for(const auto x : m.blockMCycles) sumC += x;
					// sums over 128 and 256 frames, for the worst host buffer
					float w2 = 0, w4 = 0;
					for(size_t i = 0; i + 4 <= m.blockMCycles.size(); i += 4)
					{
						w2 = std::max({w2, m.blockMCycles[i] + m.blockMCycles[i + 1], m.blockMCycles[i + 2] + m.blockMCycles[i + 3]});
						w4 = std::max(w4, m.blockMCycles[i] + m.blockMCycles[i + 1] + m.blockMCycles[i + 2] + m.blockMCycles[i + 3]);
					}
					// instructions over host buffers of 128 and 256 frames: p50, p99, p99.9, max (independent of other load)
					for(const size_t k : {size_t(2), size_t(4)})
					{
						std::vector<float> w;
						for(size_t i = 0; i + k <= m.blockMInstr.size(); i += k)
						{
							float sum = 0;
							for(size_t j = 0; j < k; ++j)
								sum += m.blockMInstr[i + j];
							w.push_back(sum);
						}
						std::sort(w.begin(), w.end());
						if(!w.empty())
							std::printf("         Minstr/%zu: p50 %.1f p99 %.1f p99.9 %.1f max %.1f\n", k * g_block, at(w, .5), at(w, .99), at(w, .999), at(w, 1.0));
					}
					std::printf("         Minstr/64: mean %.2f p50 %.2f p99 %.2f p99.9 %.2f max %.2f   Mcycles/64: mean %.2f p50 %.2f p99 %.2f p99.9 %.2f max %.2f | max /128 %.2f /256 %.2f\n",
						sumI / double(mi.size()), at(mi, .5), at(mi, .99), at(mi, .999), at(mi, 1.0),
						sumC / double(mc.size()), at(mc, .5), at(mc, .99), at(mc, .999), at(mc, 1.0), w2, w4);
				}
			}
			std::printf("         messages by kind:");
			for(const auto& [k, v] : _rig.messageKinds)
			{
				const auto it = kinds0.find(k);
				const auto d = v - (it == kinds0.end() ? 0 : it->second);
				if(d)
					std::printf(" %02x:%zu (ingest %.1f ms)", k, d, double(_rig.ingestFrames[k]) * 1000.0 / g_rate);
			}
			std::puts("");
			if(first)
			{
				idleClock = c;
				idleSteps = st;
				first = false;
			}
			else if(_strict)
			{
				// A read-back is a whole dump the firmware builds; that may make one tick late (two intervals off; B-010: about
				// 7 ms at most, once a gesture). Pushes cost none.
				const auto readBacks = _rig.patternRequests - r0;
				check(c.off3 <= 2 * readBacks && st.maxDevMs <= idleSteps.maxDevMs + 1.5 + (readBacks ? 8.0 : 0.0),
					std::string(_name) + ": the sequencer keeps time while locks are edited");
			}
		};
		const auto lock = [&](const int _s, const int _v, const int _g)
		{
			_rig.page("{\"op\":\"lock\",\"p\":" + ps + ",\"t\":0,\"i\":1,\"s\":" + std::to_string(_s) + ",\"v\":" + std::to_string(_v) + ",\"g\":" + std::to_string(_g) + "}");
		};
		// The page's first load reads the machine's library (patterns, kits, songs) while it plays; time
		// that apart, then wait until the reads are done, so idle is idle.
		phase("load", 6000, 0, {});
		_rig.postOnly = true;
		for(int quiet = 0, i = 0; quiet < 4 && i < 240; ++i)
		{
			const auto before = _rig.messagesToMachine - _rig.messageKinds[0x70];
			_rig.run(500);
			quiet = _rig.messagesToMachine - _rig.messageKinds[0x70] == before ? quiet + 1 : 0;
		}
		first = true;
		phase("idle", 6000, 0, {});
		if(std::getenv("PLOCK_PROBE"))
		{
			// What in a push costs the time: the dump's bytes all at once, the same bytes at DIN speed, a
			// dump of another pattern, a read-back.
			auto pat = desk.documents().patterns.at(p);
			const auto dump = ed::encodeMdPattern(pat);
			auto other = pat;
			other.position = static_cast<uint8_t>((p + 9) % 128);
			const auto otherDump = ed::encodeMdPattern(other);
			const auto raw = [&](const char* _name, const std::function<void()>& _what)
			{
				clocks.clear();
				_rig.run(1000);
				const auto c0 = jitterOf(clocks, 1000.0 / 48.0);
				clocks.clear();
				for(int i = 0; i < 5; ++i)
				{
					_what();
					_rig.run(800);
				}
				const auto c = jitterOf(clocks, 1000.0 / 48.0);
				std::printf("  probe %-28s clock worst %.2f ms off (sd %.2f); before it worst %.2f ms\n", _name, c.maxDevMs, c.sdMs, c0.maxDevMs);
			};
			m.hardware().setSysexIngressRate(0);
			raw("active pattern, whole", [&] { m.send(dump); });
			raw("other pattern, whole", [&] { m.send(otherDump); });
			for(const uint32_t rate : {31250u, 62500u, 125000u, 250000u})
			{
				char name[64];
				std::snprintf(name, sizeof(name), "active, paced %u B/s", rate);
				m.hardware().setSysexIngressRate(rate);
				raw(name, [&] { m.send(dump); });
			}
			m.hardware().setSysexIngressRate(ingressRate);
			m.hardware().setMidiTransmitRate(0);
			raw("pattern request (read-back)", [&] { (void)m.request(ed::mdPatternRequest(p), ed::g_mdPatternDump); });
			for(const uint32_t rate : {31250u, 62500u, 125000u, 250000u})
			{
				char name[64];
				std::snprintf(name, sizeof(name), "read-back, sent at %u B/s", rate);
				m.hardware().setMidiTransmitRate(rate);
				uint64_t frames = 0;
				bool ok = true;
				raw(name, [&] { ok = ok && !m.request(ed::mdPatternRequest(p), ed::g_mdPatternDump, &frames).empty(); });
				std::printf("        (the reply %s, %.0f ms)\n", ok ? "came" : "DID NOT COME", double(frames) * 1000.0 / g_rate);
			}
			m.hardware().setMidiTransmitRate(transmitRate);
			raw("kit request", [&] { (void)m.request(ed::mdKitRequest(0), ed::g_mdKitDump); });
		}
		const int dragG = ++gesture;
		phase("drag", 6000, 16, [&](const int _n) { lock(_n % 16, (_n * 7) % 128, dragG); });
		phase("click", 6000, 250, [&](const int _n) { lock((_n * 5) % 16, (_n * 13) % 128, ++gesture); });
		// B-014: one click a second, so each gets its own read-back (750 ms after it)
		{
			// PLOCK_CLICK_MS: the time between those clicks (1000)
			const double every = std::getenv("PLOCK_CLICK_MS") ? std::atof(std::getenv("PLOCK_CLICK_MS")) : 1000.0;
			phase("click1s", every * 8, every, [&](const int _n) { lock((_n * 3) % 16, (_n * 11) % 128, ++gesture); });
		}
		const int wheelG = ++gesture;
		phase("wheel", 6000, 30, [&](const int _n) { lock(4, 40 + (_n % 40), wheelG); });
		phase("after", 4000, 0, {});
		if(std::getenv("PLOCK_LONG"))
		{
			// B-014 "more and more steps": rounds of 96 new locks (a fresh drag each, an edit every 50 ms), each
			// followed by playing with no edits, to see whether a cost grows with the edits made or the locks the
			// pattern holds, and whether it stays once editing stops. Tracks 1-4 get trigs on every step first.
			for(int t = 1; t < 4; ++t)
			{
				_rig.page("{\"op\":\"steps\",\"p\":" + ps + ",\"from\":0,\"to\":16,\"g\":" + std::to_string(++gesture)
					+ ",\"rows\":[{\"t\":" + std::to_string(t) + ",\"on\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}]}");
				settle();
			}
			const int rounds = std::max(1, std::atoi(std::getenv("PLOCK_LONG")));
			int made = 0;
			for(int r = 0; r < rounds; ++r)
			{
				const int g = ++gesture;
				char name[32];
				std::snprintf(name, sizeof(name), "fill%d", r + 1);
				phase(name, 96 * 50, 50, [&](const int _n)
				{
					const int k = made + _n;	// a new (param, step) each edit: param 0..23 across steps 0..15, tracks 1-4
					const int t = (k / 384) % 4, i = (k / 16) % 24, st = k % 16;
					_rig.page("{\"op\":\"lock\",\"p\":" + ps + ",\"t\":" + std::to_string(t) + ",\"i\":" + std::to_string(i) + ",\"s\":" + std::to_string(st)
						+ ",\"v\":" + std::to_string((k * 37) % 128) + ",\"g\":" + std::to_string(g) + "}");
				});
				made += 96;
				std::snprintf(name, sizeof(name), "play%d", r + 1);
				phase(name, 4000, 0, {});
				const auto& pat = desk.documents().patterns.at(p);
				int locks = 0;
				for(int t = 0; t < 16; ++t)
					for(int i = 0; i < 24; ++i)
						for(int st = 0; st < 16; ++st)
							locks += ed::lockValue(pat, t, i, st).has_value();
				std::printf("         locks the desk's pattern holds: %d (edits made %d)\n", locks, made);
			}
		}
		_rig.postOnly = false;
		m.onMidi = {};
		m.onBlock = prevBlock;
		_rig.page(R"({"op":"stop"})");
		_rig.run(300);
	}

int main(const int _argc, char** _argv)
{
#ifdef __APPLE__
	// as an audio thread: on a performance core when one is free (B-014 timing runs)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdDeskFirmwareTest <MD-1.63-ROM> [probe|hw|p4|tweak|sampler|keepedits|recordermix|samples|gen|hostclock|playload|syximport]");
		return 77;
	}
	try
	{
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		const std::string mode = _argc > 2 ? _argv[2] : "";
		if(mode == "hw")
		{
			hardwareMidi(rom, _argv[1]);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest hw: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "p4")
		{
			bootHold(rom, _argv[1]);
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isInputReady() && rig.desk().linkState().pattern; }, 5000);
			mutesTruth(rig);
			chaining(rig);
			recLockTruth(rig);
			library(rig);
			globalSettings(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest p4: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "syximport")
		{
			syxImport(rom, _argv[1], _argc > 3 ? _argv[3] : "/Users/radek/Downloads/AE_LIVE_ELEKTRONS_BACKUP_010308/md010308.syx");
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest syximport: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "tweak")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isInputReady() && rig.desk().linkState().kit && rig.desk().documents().working; }, 8000);
			controlAllTruth(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest tweak: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "sampler")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().linkState().kit && rig.desk().linkState().pattern && rig.desk().documents().working; }, 8000);
			samplerSetup(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest sampler: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "recordermix")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().linkState().kit && rig.desk().linkState().pattern && rig.desk().documents().working; }, 8000);
			recorderInTheMix(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest recordermix: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "keepedits")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().linkState().kit && rig.desk().linkState().pattern && rig.desk().documents().working; }, 8000);
			rig.pluginLike = true;
			keepEdits(rig);
			undoControlAll(rig);
			valueAfterMachine(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest keepedits: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "gen")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isInputReady() && rig.desk().linkState().pattern && rig.desk().documents().patterns.count(*rig.desk().linkState().pattern); }, 8000);
			generatorsTruth(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest gen: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "samples")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().linkState().kit && rig.desk().linkState().pattern && rig.desk().documents().working; }, 8000);
			samples(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest samples: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "plocktiming" || mode == "plocktiming-strict")
		{
			Rig rig(rom, _argv[1]);
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().linkState().kit && rig.desk().linkState().pattern && rig.desk().documents().working
				&& rig.desk().documents().patterns.count(*rig.desk().linkState().pattern); }, 8000);
			plockTiming(rig, mode == "plocktiming-strict");
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest %s: %s (%d failure(s))\n", mode.c_str(), g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		if(mode == "hostclock")
		{
			hostClock(rom, _argv[1]);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest hostclock: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
			return g_failures ? 1 : 0;
		}
		Rig rig(rom, _argv[1]);
		if(_argc > 2 && std::string(_argv[2]) == "playload")
		{
			// PLAY while the desk loads patterns and songs in the background.
			rig.page(R"({"op":"ready"})");
			rig.runUntil([&] { return rig.desk().isReady() && rig.desk().documents().patterns.size() > 20; }, 5000);
			for(int i = 0; i < 3; ++i)
			{
				if(i == 1)
					rig.page(R"({"op":"saveKit","id":3})");
				if(i == 2)
				{
					const auto p = std::to_string(*rig.desk().linkState().pattern);
					rig.page("{\"op\":\"trig\",\"p\":" + p + ",\"t\":0,\"s\":3,\"id\":5}");
					rig.runUntil([&] { return !rig.desk().isBusy(); }, 1000);
					rig.page("{\"op\":\"param\",\"k\":" + std::to_string(*rig.desk().linkState().kit) + ",\"t\":0,\"i\":16,\"v\":9,\"id\":6}");
					rig.page(R"({"op":"saveKit","id":7})");
				}
				rig.run(300);
				const auto t0 = rig.machine().now();
				rig.page(R"({"op":"play","id":1})");
				const bool on = rig.runUntil([&] { return rig.pageTelemetry().playing; }, 3000);
				std::printf("PLAY %d while loading (%zu patterns, %zu songs): %s after %.0f ms\n", i, rig.desk().documents().patterns.size(),
					rig.desk().documents().songs.size(), on ? "playing" : "NOT playing", ms(rig.machine().now() - t0));
				rig.page(R"({"op":"stop","id":2})");
				rig.run(800);
			}
			return 0;
		}
		smoke(rig);
		{
			const auto kit = *rig.desk().linkState().kit;
			const auto patch = workingKitTruth(rig);
			restoredKitTruth(rom, _argv[1], patch, kit, rig.desk().documents().working->kit.params[0][2]);
		}
		liveRecording(rig);
		keyboard(rig);
		sampleName(rig);
		{
			std::puts("== P3 song selection");
			rig.page(R"({"op":"selectSong","s":2,"id":80})");
			check(resultOk(rig), "song 3 selected while stopped");
			rig.run(200);
			const auto st = ed::parseMdStatusResponse(rig.machine().request(ed::mdStatusRequest(ed::MdStatus::Song), 0x72));
			check(st && st->value == 2, "the machine reports song 3 as current");
			rig.page(R"({"op":"selectSong","s":0,"id":81})");
			rig.run(200);
		}
		appModulators(rig);
		if(_argc > 2 && std::string(_argv[2]) == "probe")
		{
			probeGroups(rig);
			probeTelemetry(rig);
		}
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdDeskFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
	check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
