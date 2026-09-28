// P2 MD Desk smoke test against MD OS 1.63 firmware (manual: needs a
// user-supplied ROM). The page is played by this program: it sends the same
// JSON commands the MD Desk page sends, through mdDesk::Desk, and checks what
// the firmware stored - patterns by dump read-back, working-kit edits by SAVE
// KIT + kit dump (the only way to read the working kit).
//
//   mdDeskFirmwareTest <ROM>          the smoke test (edits, read-backs, timing)
//   mdDeskFirmwareTest <ROM> probe    also: telemetry RAM, group removal
//   mdDeskFirmwareTest <ROM> playload PLAY while the desk loads in the background
//
// Exits 77 (skip) without arguments.

#include "contractCheck.h"
#include "mdFirmwareSession.h"

#include "mdDesk/mdDesk.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdWorkingKit.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mdfrontpanel.h"
#include "mdLib/mdsequencerstate.h"

#include <algorithm>
#include <deque>
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
			port.sendSysex = [this](const Bytes& _b) { m_out.push_back(_b); };
			port.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				const md::automation::ParameterChange change{
					static_cast<uint8_t>(_i == 24 ? md::automation::machinedrum::Level : _i / 8), _t,
					static_cast<uint8_t>(_i == 24 ? 0 : _i % 8), _v};
				const auto cc = md::automation::encodeParameterChange(md::MachineModel::Machinedrum, change,
					baseChannel());
				if(cc)
					m_out.push_back({(*cc)[0], (*cc)[1], (*cc)[2]});
			};
			port.pressKey = [this](const std::string& _key)
			{
				const auto states = md::panelKeySequence(md::MachineModel::Machinedrum, _key);
				m_keys.insert(m_keys.end(), states.begin(), states.end());
				return !states.empty();
			};
			port.turnKnob = [this](const uint8_t _e, const int _steps)
			{
				const auto command = md::panelEncoderCommand(md::MachineModel::Machinedrum, static_cast<md::PanelEncoder>(_e));
				for(int i = 0; command && i < std::abs(_steps); ++i)
					m_machine.hardware().trySendPanelEvent(*command, _steps > 0 ? 0x01 : 0xff);
				return command.has_value();
			};
			port.sendMute = [this](const uint8_t _t, const bool _on)
			{
				m_out.push_back({static_cast<uint8_t>(0xb0 | (baseChannel() + (_t >> 2))), static_cast<uint8_t>(12 + (_t & 3)),
					static_cast<uint8_t>(_on ? 1 : 0)});
			};
			port.toPage = [this](const Value& _m) { g_contract(_m); onPage(_m); };
			port.nowMs = [this] { return ms(m_machine.now()); };
			m_desk = std::make_unique<mdDesk::Desk>(port);
			m_machine.onSysex = [this](const Bytes& _b) { m_in.push_back(_b); };
		}

		Machine& machine() { return m_machine; }
		mdDesk::Desk& desk() { return *m_desk; }

		void page(const std::string& _json)
		{
			m_lastResult.reset();
			m_desk->onPageMessage(parse(_json));
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
		const std::optional<Value>& lastError() const { return m_lastError; }
		bool pageTx() const { return m_tx; }
		std::optional<Value> pageDoc(const std::string& _kind, const int _slot) const
		{
			const auto it = m_docs.find(_kind + ":" + std::to_string(_slot));
			return it == m_docs.end() ? std::nullopt : std::optional<Value>(it->second);
		}
		const std::optional<Value>& machineDoc() const { return m_machineDoc; }
		int publishedTelemetryPattern() const { return m_telemetryPattern; }

		// A direct read, outside the desk (the test's own oracle).
		std::optional<ed::MdKit> saveAndReadKit(const uint8_t _slot)
		{
			flushOut();
			m_machine.send(ed::mdSaveKit(_slot));
			return ed::decodeMdKit(m_machine.request(ed::mdKitRequest(_slot), ed::g_mdKitDump));
		}

		std::optional<ed::MdPattern> readPattern(const uint8_t _slot)
		{
			flushOut();
			return ed::decodeMdPattern(m_machine.request(ed::mdPatternRequest(_slot), ed::g_mdPatternDump));
		}

	private:
		uint8_t baseChannel() const
		{
			const auto& g = m_desk->documents().global;
			return g ? g->baseChannel : 0;
		}

		void flushOut()
		{
			while(!m_out.empty())
			{
				const auto b = m_out.front();
				m_out.pop_front();
				m_machine.send(b);
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
			if(!m_out.empty())
			{
				const auto b = m_out.front();
				m_out.pop_front();
				m_machine.send(b);
			}
			else if(!m_keys.empty())
			{
				const auto k = m_keys.front();
				m_keys.pop_front();
				m_machine.hardware().trySendPanelEvent(k.row, k.mask);
				m_machine.run(40);
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
			m_desk->tick();
		}

		void onPage(const Value& _m)
		{
			const auto* type = _m.find("type");
			if(!type)
				return;
			const auto& t = type->asString();
			if(t == "result")
				m_lastResult = _m;
			else if(t == "error")
			{
				m_lastError = _m;
				std::printf("  page error: %s\n", _m.find("message")->asString().c_str());
			}
			else if(t == "doc")
			{
				const auto& doc = *_m.find("doc");
				m_docs[_m.find("kind")->asString() + ":" + std::to_string(int(doc.find("slot")->asNumber()))] = doc;
			}
			else if(t == "machine")
			{
				m_machineDoc = *_m.find("doc");
				m_tx = m_machineDoc->find("desk")->find("tx")->asBool();
			}
			else if(t == "telemetry")
				m_telemetryPattern = static_cast<int>(_m.find("pattern")->asNumber());
		}

		Machine m_machine;
		std::unique_ptr<mdDesk::Desk> m_desk;
		std::deque<Bytes> m_out;
		std::deque<Bytes> m_in;
		std::deque<md::PanelPacket> m_keys;
		uint64_t m_lastTick = 0;
		int m_lastStep = -1;
		uint64_t m_stepChangedAt = 0;
		std::optional<Value> m_lastResult;
		std::optional<Value> m_lastError;
		std::map<std::string, Value> m_docs;
		std::optional<Value> m_machineDoc;
		bool m_tx = false;
		int m_telemetryPattern = -1;
		Bytes m_lastRegion;
		md::SequencerState m_leds;
		uint64_t m_ledsAt = 0;
		md::BootAnimation m_boot;
		uint64_t m_bootAt = 0;
		mdDesk::Telemetry m_lastTelemetry;

	public:
		const mdDesk::Telemetry& telemetry() const { return m_lastTelemetry; }
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
			const auto& s = desk.session().state();
			return desk.isReady() && s.pattern && s.kit && desk.documents().patterns.count(*s.pattern)
				&& desk.documents().kits.count(*s.kit) && desk.documents().global;
		}, 3000);
		check(ready, "ready: current pattern, its kit and the global settings loaded");
		if(!ready)
			return;
		const auto pattern = *desk.session().state().pattern;
		const auto kit = *desk.session().state().kit;
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
			_rig.runUntil([&] { return !desk.isBusy(); }, 1000);
		}
		_rig.page("{\"op\":\"lock\",\"p\":" + std::to_string(pattern) + ",\"t\":0,\"i\":12,\"s\":5,\"v\":33}");
		check(resultOk(_rig), "lock command accepted");
		_rig.runUntil([&] { return !desk.isBusy(); }, 1000);
		const auto locked = _rig.readPattern(pattern);
		check(locked && ed::lockValue(*locked, 0, 12, 5) == uint8_t{33}, "the firmware holds the lock");

		// 3. A working-kit value via the CC path; SAVE KIT is the only way to read it.
		const auto kitBefore = desk.documents().kits.at(kit);
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
		const auto& want = desk.documents().kits.at(kit);
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
		const auto undoneTo = desk.documents().kits.at(kit);
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
		const auto song = desk.session().state().song.value_or(0);
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
		check(desk.session().state().songReloadNeeded, "the current song is marked reload-needed");

		// 9. Pattern select while playing: queued until the sequencer switches.
		auto next = desk.documents().patterns.at(pattern);
		next.position = static_cast<uint8_t>((pattern + 1) & 127);
		next.kit = kit;
		next.scale = 0;
		next.length = 16;
		m.send(ed::encodeMdPattern(next));
		_rig.run(100);
		_rig.page(R"({"op":"play"})");
		_rig.runUntil([&] { return _rig.machineDoc() && _rig.machineDoc()->find("desk")->find("playing")->asBool(); },
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
			if(statusAt < 0 && desk.session().state().pattern == next.position)
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
		check(_rig.machineDoc() && !_rig.machineDoc()->find("desk")->find("playing")->asBool(),
			"the page shows the machine stopped");
	}

	// How does the firmware store "no group"? Candidates for the target byte.
	void probeGroups(Rig& _rig)
	{
		std::puts("== probe: removing a mute/trig group");
		auto& m = _rig.machine();
		const auto kit = *_rig.desk().session().state().kit;
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
		const auto kit = *desk.session().state().kit;
		const auto before = desk.documents().kits.at(kit).params[0][2];
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
			const auto doc = _rig.pageDoc("kit", kit);
			return doc && doc->find("tracks")->asArray()[0].find("synth")->asArray()[2].asNumber() == want;
		}, 1000);
		check(seen, "panel encoder edit shows in the page's kit document");
		std::printf("  panel encoder -> page kit document: %.1f ms emulated (value %u -> %u)\n", ms(m.now() - t0), before, want);
		const auto& md = _rig.machineDoc();
		check(md && md->find("kit")->find("working")->asString() == "edited", "the kit is 'edited' without SAVE KIT");
		check(md && md->find("desk")->find("kitSource")->asString() == "memory", "kit source: memory");
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
			const auto doc = rig.pageDoc("kit", _kit);
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
		const auto pattern = *desk.session().state().pattern;
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
		const auto recording = [&]
		{
			const auto& d = _rig.machineDoc();
			return d && d->find("desk")->find("recording")->asBool();
		};
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
		_rig.page("{\"op\":\"param\",\"k\":" + std::to_string(*desk.session().state().kit) + R"(,"t":14,"i":12,"v":99,"id":43})");
		check(resultOk(_rig), "knob move accepted while recording");
		const bool turned = _rig.runUntil([&] { return m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 14 * 24 + 12) == 99; }, 1500);
		check(turned, "the knob move reaches the machine as DATA ENTRY turns");
		std::printf("  knob move -> value in the machine: %.1f ms emulated (select track, page key, turn)\n", ms(m.now() - k0));
		// The note on the next step (a person plays it after turning the knob).
		const auto at = m.playhead();
		_rig.runUntil([&] { return m.playhead() != at; }, 1000);
		_rig.page(R"({"op":"recTrig","t":14,"id":45})");
		_rig.run(50);
		_rig.page("{\"op\":\"trig\",\"p\":" + p + R"(,"t":0,"s":3,"id":46})");
		check(_rig.lastResult() && !_rig.lastResult()->find("ok")->asBool(), "grid edits wait while recording");
		_rig.runUntil(stepIs(12), 3000);
		_rig.page(R"({"op":"record","id":47})");
		check(_rig.runUntil([&] { return !recording(); }, 1000), "REC again leaves recording");
		const auto& md = _rig.machineDoc();
		check(md && md->find("desk")->find("playing")->asBool(), "and the pattern keeps playing");
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
		check(rig.machineString({"desk", "firmware"}) == "booting" && rig.machineString({"desk", "boot"}) == "animation",
			"the firmware answers, the engine still says BOOTING OS (animation)");
		rig.page(R"({"op":"play","id":900})");
		check(rig.lastResult() && !rig.lastResult()->find("ok")->asBool(), "PLAY during the animation is held back, with the reason");
		const bool ready = rig.runUntil([&] { return rig.desk().isInputReady(); }, 30000);
		const auto readyMs = ms(m.now() - t0);
		std::printf("  status reply %.0f ms, input ready %.0f ms after the firmware took MIDI\n", answered, readyMs);
		check(ready && rig.machineString({"desk", "firmware"}) == "ready", "the engine says ready when the animation is over");
		rig.page(R"({"op":"play","id":901})");
		const bool plays = rig.runUntil([&] { return rig.telemetry().playing; }, 2000);
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
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
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
		_rig.page(R"({"op":"stop","id":928})");
		_rig.runUntil([&] { return !_rig.telemetry().playing; }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"play","id":929})");
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
		_rig.run(100);
		const auto after = wrapPatterns(_rig, 3);
		std::printf("  STOP, PLAY with a chain: plays %d, then", _rig.telemetry().pattern);
		for(const int p : after) std::printf(" %d", p);
		std::printf("; chain %s\n", _rig.telemetry().chain.active ? "active" : "gone");
		// Stopped: does the gesture chain?
		_rig.page(R"({"op":"stop","id":930})");
		_rig.runUntil([&] { return !_rig.telemetry().playing; }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"chain","patterns":[2,4],"id":931})");
		_rig.run(800);
		std::printf("  chain while stopped: firmware chain %s, current %d\n", chainOf(_rig) == std::vector<int>{2, 4} ? "A03 A05" : "not made",
			_rig.telemetry().pattern);
		_rig.page(R"({"op":"play","id":932})");
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
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
	}

	// P4: while live recording, a value moved in the page locks the trig the desk names.
	void recLockTruth(Rig& _rig)
	{
		auto& m = _rig.machine();
		std::puts("== P4 live recording: the lock lands where the desk says");
		const auto slot = *_rig.desk().session().state().pattern;
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
		const auto kit = *_rig.desk().session().state().kit;
		const int before = _rig.desk().documents().kits.at(kit).params[14][0];
		_rig.page(R"({"op":"record","id":940})");
		_rig.runUntil([&] { return _rig.telemetry().recording; }, 3000);
		_rig.runUntil([&] { return _rig.telemetry().step == 2; }, 5000);
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
		_rig.runUntil([&] { return _rig.telemetry().step == 14; }, 5000);
		_rig.page(R"({"op":"record","id":942})");
		_rig.run(200);
		_rig.page(R"({"op":"stop","id":943})");
		_rig.runUntil([&] { return !_rig.telemetry().playing; }, 3000);
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
		const auto kitStatus = [&] { return ed::parseMdStatusResponse(m.request(ed::mdStatusRequest(ed::MdStatus::Kit), 0x72))->value; };
		const auto readKit = [&](const uint8_t _k) { return *ed::decodeMdKit(m.request(ed::mdKitRequest(_k), ed::g_mdKitDump)); };
		const auto cur = *desk.session().state().kit;
		const auto working = desk.documents().kits.at(cur);
		const auto settle = [&] { _rig.runUntil([&] { return !desk.isBusy(); }, 2000); _rig.run(200); };
		_rig.page("{\"op\":\"kitCopy\",\"k\":" + std::to_string(cur) + ",\"id\":950}");
		_rig.page(R"({"op":"kitPaste","k":40,"id":951})");
		check(resultOk(_rig), "paste into K41 accepted");
		settle();
		const auto k40 = readKit(40);
		check(k40.params == working.params && k40.models == working.models, "K41 holds the copied kit (machine dump)");
		_rig.page(R"({"op":"kitRename","k":40,"name":"lib test","id":952})");
		settle();
		check(kitNameOf(readKit(40)) == "LIB TEST", "rename of a slot that does not play: dump with the new name");
		_rig.page(R"({"op":"kitClear","k":40,"id":953})");
		settle();
		const auto cleared = readKit(40);
		check(std::all_of(cleared.models.begin(), cleared.models.end(), [](const uint32_t _m) { return _m == 0; }) && kitNameOf(cleared).empty(),
			"clear: every track GND-EMPTY, no name");
		_rig.page(R"({"op":"undo","id":954})");
		settle();
		check(kitNameOf(readKit(40)) == "LIB TEST", "undo brings the renamed kit back");
		_rig.page(R"({"op":"kitCopyTo","from":40,"to":41,"id":955})");
		settle();
		check(kitNameOf(readKit(41)) == "LIB TEST", "drag-copy K41 -> K42");
		_rig.page(R"({"op":"kitSaveAs","k":42,"id":956})");
		settle();
		check(kitStatus() == 42, "Save as K43: it is the current kit");
		const auto pat = *desk.session().state().pattern;
		check(_rig.readPattern(pat)->kit == 42, "and the current pattern links to it (EXTENDED)");
		_rig.page("{\"op\":\"kitLoad\",\"k\":" + std::to_string(cur) + ",\"force\":true,\"id\":957}");
		settle();
		check(kitStatus() == cur, "LOAD KIT back to the first kit");
		// Paste into the kit that plays: a dump plus LOAD KIT, heard at once.
		_rig.page("{\"op\":\"kitCopy\",\"k\":40,\"id\":970}");
		_rig.page("{\"op\":\"kitPaste\",\"k\":" + std::to_string(cur) + ",\"force\":true,\"id\":971}");
		settle();
		const auto image = ed::mdWorkingKitFromMemory([&]
		{
			Bytes r(ed::g_mdWorkingKitRegionSize);
			for(size_t i = 0; i < r.size(); ++i)
				r[i] = m.read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
			return r;
		}());
		check(image && kitNameOf(*image) == "LIB TEST" && image->models == readKit(40).models, "paste into the kit that plays: heard at once (working kit in memory)");
		_rig.page("{\"op\":\"kitRename\",\"k\":" + std::to_string(cur) + ",\"name\":\"LIVE NAME\",\"id\":972}");
		settle();
		_rig.run(300);
		check(kitNameOf(desk.documents().kits.at(cur)) == "LIVE NAME", "rename of the kit that plays: live (0x55), the working kit shows it");
		// Patterns.
		_rig.page("{\"op\":\"patCopy\",\"p\":" + std::to_string(pat) + ",\"id\":958}");
		_rig.page(R"({"op":"patPaste","p":100,"id":959})");
		settle();
		const auto src = *_rig.readPattern(pat), p100 = *_rig.readPattern(100);
		check(p100.trigs == src.trigs && p100.lockMasks == src.lockMasks && p100.kit == src.kit, "pattern paste into G05: notes, locks, kit link");
		_rig.page(R"({"op":"patClear","p":100,"id":960})");
		settle();
		const auto c100 = *_rig.readPattern(100);
		check(std::all_of(c100.trigs.begin(), c100.trigs.end(), [](const uint64_t _t) { return _t == 0; }) && c100.length == src.length,
			"pattern clear: no trigs, length kept");
		// Switch now while playing.
		_rig.page(R"({"op":"play","id":961})");
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
		_rig.run(500);
		const auto target = static_cast<uint8_t>((pat + 3) % 128);
		const auto t0 = m.now();
		_rig.page("{\"op\":\"select\",\"p\":" + std::to_string(target) + ",\"now\":true,\"force\":true,\"id\":962}");
		const bool now = _rig.runUntil([&] { return _rig.telemetry().playing && _rig.telemetry().pattern == target; }, 3000);
		std::printf("  switch now: playing %s after %.0f ms\n", ed::mdPatternName(target).c_str(), ms(m.now() - t0));
		check(now, "Now while playing: STOP, LOAD PATTERN, PLAY plays the new pattern");
		_rig.page(R"({"op":"stop","id":963})");
		_rig.run(300);
		m.send(ed::mdLoadPattern(pat));
		_rig.run(300);
	}

	// P4 HW MIDI against the emulated MD as the MIDI peer ("a real Machinedrum"): the desk has
	// no telemetry, no memory, no panel keys; SysEx and CCs go both ways at DIN speed
	// (mdDesk::DinPacer, 3125 bytes a second each way).
	class HwRig
	{
	public:
		HwRig(const Bytes& _rom, const std::string& _romName) : m_machine(_rom, _romName)
		{
			mdDesk::Desk::Port port;
			port.sendSysex = [this](const Bytes& _b) { m_out.push(_b); };
			port.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				const md::automation::ParameterChange change{static_cast<uint8_t>(_i == 24 ? md::automation::machinedrum::Level : _i / 8), _t,
					static_cast<uint8_t>(_i == 24 ? 0 : _i % 8), _v};
				if(const auto cc = md::automation::encodeParameterChange(md::MachineModel::Machinedrum, change, 0))
					m_out.push({(*cc)[0], (*cc)[1], (*cc)[2]});
			};
			port.sendMute = [this](const uint8_t _t, const bool _on) { m_out.push({static_cast<uint8_t>(0xb0 | (_t >> 2)), static_cast<uint8_t>(12 + (_t & 3)), static_cast<uint8_t>(_on ? 1 : 0)}); };
			port.pressKey = [this](const std::string& _k)
			{
				if(_k == "play") { m_out.push({0xfa}); return true; }
				if(_k == "stop") { m_out.push({0xfc}); return true; }
				return false;
			};
			port.toPage = [this](const Value& _m)
			{
				g_contract(_m);
				const auto* t = _m.find("type");
				if(t && t->asString() == "machine")
					m_machineDoc = *_m.find("doc");
				if(t && t->asString() == "result")
					m_result = _m;
			};
			port.nowMs = [this] { return ms(m_machine.now()); };
			m_desk = std::make_unique<mdDesk::Desk>(port);
			m_desk->setHardwareLink(true);
			m_machine.onSysex = [this](const Bytes& _b) { if(m_connected) m_toDesk.send(ms(m_machine.now()), _b); };
		}

		void page(const std::string& _json) { m_result.reset(); m_desk->onPageMessage(parse(_json)); }
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
		std::string link() const
		{
			const auto* d = m_machineDoc ? m_machineDoc->find("desk") : nullptr;
			const auto* l = d ? d->find("link") : nullptr;
			return l && l->isString() ? l->asString() : std::string();
		}
		mdDesk::Desk& desk() { return *m_desk; }
		Machine& machine() { return m_machine; }
		const std::optional<Value>& lastResult() const { return m_result; }
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
				freeAt = std::max(freeAt, _now) + mdDesk::DinPacer::wireMs(_b.size());
				flight.emplace_back(freeAt, std::move(_b));
			}
		};

		void step()
		{
			const double now = ms(m_machine.now());
			for(auto& b : m_out.take(now))
				m_toMachine.send(now, std::move(b));
			while(!m_toMachine.flight.empty() && m_toMachine.flight.front().first <= now)
			{
				auto b = std::move(m_toMachine.flight.front().second);
				m_toMachine.flight.pop_front();
				m_bytesOut += b.size();
				if(m_connected)
					m_machine.send(b);
			}
			m_machine.step();
			while(!m_toDesk.flight.empty() && m_toDesk.flight.front().first <= ms(m_machine.now()))
			{
				auto b = std::move(m_toDesk.flight.front().second);
				m_toDesk.flight.pop_front();
				m_desk->onDeviceSysex(b);
			}
			if(m_machine.now() - m_lastTick >= g_rate / 30)
			{
				m_lastTick = m_machine.now();
				m_desk->onTelemetry(mdDesk::Telemetry{});
				m_desk->tick();
			}
		}

		Machine m_machine;
		std::unique_ptr<mdDesk::Desk> m_desk;
		mdDesk::DinPacer m_out;
		Wire m_toMachine, m_toDesk;
		std::optional<Value> m_machineDoc, m_result;
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
		check(hw.link() == "connect", "HW CONNECT until the machine answers");
		const auto t0 = m.now();
		const bool up = hw.runUntil([&] { return hw.link() == "ready" && hw.desk().session().state().pattern && hw.desk().session().state().kit
			&& hw.desk().documents().patterns.count(*hw.desk().session().state().pattern) && hw.desk().documents().kits.count(*hw.desk().session().state().kit); }, 20000);
		std::printf("  status, current pattern and kit over DIN: %.0f ms\n", ms(m.now() - t0));
		check(up, "HW MIDI: status, the current pattern and its kit read");
		if(!up)
			return;
		const auto pat = *hw.desk().session().state().pattern;
		const auto kit = *hw.desk().session().state().kit;
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
		const uint8_t v = static_cast<uint8_t>((hw.desk().documents().kits.at(kit).params[0][16] + 17) & 0x7f);
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
		// Unplugged: HW NO MIDI after a while.
		hw.setConnected(false);
		const bool lost = hw.runUntil([&] { return hw.link() == "lost"; }, 6000);
		check(lost, "no replies for 3.5 s: the link says lost (HW NO MIDI)");
		hw.setConnected(true);
		check(hw.runUntil([&] { return hw.link() == "ready"; }, 4000), "and ready again when it answers");
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
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
		clocks = 0;
		_rig.run(500);
		const int on = clocks;
		_rig.page(R"({"op":"stop","id":992})");
		_rig.run(400);
		_rig.page(R"({"op":"globalSet","field":"tempoOut","on":false,"id":993})");
		_rig.runUntil([&] { return !_rig.desk().isBusy(); }, 2000);
		_rig.run(300);
		_rig.page(R"({"op":"play","id":994})");
		_rig.runUntil([&] { return _rig.telemetry().playing; }, 2000);
		clocks = 0;
		_rig.run(500);
		const int off = clocks;
		_rig.page(R"({"op":"stop","id":995})");
		_rig.run(400);
		std::printf("  MIDI clocks in 0.5 s: TEMPO OUT on %d, off %d\n", on, off);
		check(on > 10 && off == 0, "TEMPO OUT: the machine sends MIDI clock only when on");
		const auto gdoc = _rig.pageDoc("global", *_rig.desk().session().state().globalSlot);
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

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdDeskFirmwareTest <MD-1.63-ROM> [probe]");
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
			rig.runUntil([&] { return rig.desk().isInputReady() && rig.desk().session().state().pattern; }, 5000);
			mutesTruth(rig);
			chaining(rig);
			recLockTruth(rig);
			library(rig);
			globalSettings(rig);
			check(g_contract.loaded() && g_contract.bad() == 0, g_contract.summary());
			std::printf("mdDeskFirmwareTest p4: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
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
					const auto p = std::to_string(*rig.desk().session().state().pattern);
					rig.page("{\"op\":\"trig\",\"p\":" + p + ",\"t\":0,\"s\":3,\"id\":5}");
					rig.runUntil([&] { return !rig.desk().isBusy(); }, 1000);
					rig.page("{\"op\":\"param\",\"k\":" + std::to_string(*rig.desk().session().state().kit) + ",\"t\":0,\"i\":16,\"v\":9,\"id\":6}");
					rig.page(R"({"op":"saveKit","id":7})");
				}
				rig.run(300);
				const auto t0 = rig.machine().now();
				rig.page(R"({"op":"play","id":1})");
				const bool on = rig.runUntil([&] { return rig.machineDoc() && rig.machineDoc()->find("desk")->find("playing")->asBool(); }, 3000);
				std::printf("PLAY %d while loading (%zu patterns, %zu songs): %s after %.0f ms\n", i, rig.desk().documents().patterns.size(),
					rig.desk().documents().songs.size(), on ? "playing" : "NOT playing", ms(rig.machine().now() - t0));
				rig.page(R"({"op":"stop","id":2})");
				rig.run(800);
			}
			return 0;
		}
		smoke(rig);
		{
			const auto kit = *rig.desk().session().state().kit;
			const auto patch = workingKitTruth(rig);
			restoredKitTruth(rom, _argv[1], patch, kit, rig.desk().documents().kits.at(kit).params[0][2]);
		}
		liveRecording(rig);
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
