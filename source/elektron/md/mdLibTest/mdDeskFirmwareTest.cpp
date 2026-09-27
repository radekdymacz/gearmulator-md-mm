// P2 MD Desk smoke test against MD OS 1.63 firmware (manual: needs a
// user-supplied ROM). The page is played by this program: it sends the same
// JSON commands the MD Desk page sends, through mdDesk::Desk, and checks what
// the firmware stored - patterns by dump read-back, working-kit edits by SAVE
// KIT + kit dump (the only way to read the working kit).
//
//   mdDeskFirmwareTest <ROM>          the smoke test (edits, read-backs, timing)
//   mdDeskFirmwareTest <ROM> probe    also: telemetry RAM, group removal
//
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"

#include "mdDesk/mdDesk.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"

#include "mdLib/mdautomation.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>

using namespace mdFirmwareSession;
namespace ed = elektronData;
using ed::json::Value;

namespace
{
	int g_failures = 0;

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
		explicit Rig(const Bytes& _rom, const std::string& _romName) : m_machine(_rom, _romName)
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
				m_keys.push_back(_key == "play" ? md::PanelControl::Play : md::PanelControl::Stop);
				return true;
			};
			port.toPage = [this](const Value& _m) { onPage(_m); };
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
				m_machine.panel(k);
			}
			else
				m_machine.step();
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
			t.playing = m_machine.read8(0x28cdaf) == 0;
			m_desk->onTelemetry(t);
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
		std::deque<md::PanelControl> m_keys;
		uint64_t m_lastTick = 0;
		int m_lastStep = -1;
		uint64_t m_stepChangedAt = 0;
		std::optional<Value> m_lastResult;
		std::optional<Value> m_lastError;
		std::map<std::string, Value> m_docs;
		std::optional<Value> m_machineDoc;
		bool m_tx = false;
		int m_telemetryPattern = -1;
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
			"the page shows the machine stopped (RAM 0x28cdaf)");
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
		Rig rig(rom, _argv[1]);
		smoke(rig);
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
	std::printf("mdDeskFirmwareTest: %s (%d failure(s))\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
