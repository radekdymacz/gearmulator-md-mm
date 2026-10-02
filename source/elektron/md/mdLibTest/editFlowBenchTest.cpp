// Edit-flow bench (doc/modern-ux/DESIGN-edit-flow.md): the real firmware plus the real desk, headless,
// while a "page" fires edits at a set rate. It measures what the owner hears in a DAW:
//   - the emulator's cost per 64-frame block (wall clock, the audio thread's deadline is 1.451 ms),
//     and per 512-frame window (a DAW buffer, 11.6 ms);
//   - the MIDI the desk sends the machine (bytes/s, messages, dumps and dump requests);
//   - the desk's own work (the plug-in's message thread) and what it publishes to the page.
//
//   editFlowBenchTest md <MD-1.63-ROM> [seconds=3]
//   editFlowBenchTest mm <MM-1.32B-ROM> [seconds=3]
//
// Scenarios: idle (playing, no edits), knob (60 kit-parameter edits/s on one parameter), lock (a lock
// lane drawn across 16 steps each second, 60 lock edits/s). Exits 77 without a ROM. Manual.

#include "mdFirmwareSession.h"

#include "mdDesk/mdDesk.h"
#include "mmDesk/mmDesk.h"
#include "deskWire/mdWire.h"
#include "deskWire/mmWire.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdWorkingKit.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmKit.h"

#include "mdLib/mdpanelsequence.h"
#include "mdLib/mdsequencerstate.h"
#include "mdLib/mmtelemetry.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <memory>

using namespace mdFirmwareSession;
namespace ed = elektronData;
using ed::json::Value;

namespace
{
	using Clock = std::chrono::steady_clock;
	double usSince(const Clock::time_point _t) { return std::chrono::duration<double, std::micro>(Clock::now() - _t).count(); }
	constexpr double g_stepMs = 8;	// the session's step (mdDeskSession.h g_stepMs)

	struct Stats
	{
		std::vector<double> blockUs;		// per 64-frame block
		double deskUs = 0, deskMaxUs = 0;	// the desk's calls (message thread)
		double editUs = 0; int edits = 0;	// of that, the page messages
		size_t outBytes = 0, outMsgs = 0, outSysex = 0, inBytes = 0;
		std::map<int, int> outSysexByCmd;	// SysEx command byte -> count
		size_t pubMsgs = 0, pubBytes = 0, docMsgs = 0, docBytes = 0, machineMsgs = 0;
		std::map<std::string, int> docKinds;

		void desk(const double _us) { deskUs += _us; deskMaxUs = std::max(deskMaxUs, _us); }

		void print(const char* _name, const double _seconds) const
		{
			auto b = blockUs;
			std::sort(b.begin(), b.end());
			double sum = 0;
			for(const auto x : blockUs) sum += x;
			const auto pct = [&](const double _p) { return b.empty() ? 0.0 : b[std::min(b.size() - 1, static_cast<size_t>(_p * b.size()))]; };
			// 512-frame windows (8 blocks), as a DAW buffer
			double worstWin = 0; int overWin = 0, overBlock = 0;
			for(size_t i = 0; i + 8 <= blockUs.size(); i += 8)
			{
				double w = 0;
				for(size_t j = 0; j < 8; ++j) w += blockUs[i + j];
				worstWin = std::max(worstWin, w);
				if(w > 512.0 * 1e6 / g_rate) ++overWin;
			}
			for(const auto x : blockUs) if(x > 64.0 * 1e6 / g_rate) ++overBlock;
			const double audioUs = blockUs.size() * 64.0 * 1e6 / g_rate;
			std::printf("%-6s emu load %5.1f %% | block us p50 %5.0f p99 %5.0f max %6.0f (>1451: %d) | worst 512-window %5.1f %% (>100%%: %d)\n",
				_name, 100.0 * sum / audioUs, pct(.5), pct(.99), b.empty() ? 0 : b.back(), overBlock, 100.0 * worstWin / (512.0 * 1e6 / g_rate), overWin);
			std::printf("       to machine %6.0f B/s, %5.0f msg/s, %4.1f SysEx/s", outBytes / _seconds, outMsgs / _seconds, outSysex / _seconds);
			for(const auto& [c, n] : outSysexByCmd) std::printf(" [0x%02x x%d]", c, n);
			std::printf(" | from machine %6.0f B/s\n", inBytes / _seconds);
			std::printf("       desk %6.1f ms/s (max call %5.0f us), %d edits at %4.0f us each | to page %5.0f msg/s %7.0f B/s, doc %4.0f/s %7.0f B/s, machine doc %4.0f/s",
				deskUs / 1000 / _seconds, deskMaxUs, edits, edits ? editUs / edits : 0, pubMsgs / _seconds, pubBytes / _seconds, docMsgs / _seconds, docBytes / _seconds, machineMsgs / _seconds);
			for(const auto& [k, n] : docKinds) std::printf(" [%s x%d]", k.c_str(), n);
			std::printf("\n");
		}
	};

	Stats* g_stats = nullptr;

	void countPage(const Value& _m)
	{
		if(!g_stats) return;
		const auto n = ed::json::write(_m).size();	// the plug-in writes every message into its javascript: URL
		++g_stats->pubMsgs; g_stats->pubBytes += n;
		const auto* t = _m.find("type");
		if(t && t->asString() == "doc") { ++g_stats->docMsgs; g_stats->docBytes += n; ++g_stats->docKinds[_m.find("kind")->asString()]; }
		if(t && t->asString() == "machine") ++g_stats->machineMsgs;
	}

	void countOut(const Bytes& _b)
	{
		if(!g_stats) return;
		g_stats->outBytes += _b.size(); ++g_stats->outMsgs;
		if(!_b.empty() && _b[0] == 0xf0) { ++g_stats->outSysex; if(_b.size() > 6) ++g_stats->outSysexByCmd[_b[6]]; }
	}

	void toMachine(Machine& _m, const Bytes& _b)
	{
		countOut(_b);
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		if(_b[0] == 0xf0)
			e.sysex = _b;
		else
		{
			e.a = _b[0];
			e.b = _b.size() > 1 ? _b[1] : 0;
			e.c = _b.size() > 2 ? _b[2] : 0;
		}
		_m.hardware().sendMidi(e);
	}

	// The machine's time up to _toFrame, each block timed.
	void render(Machine& _m, const uint64_t _toFrame)
	{
		while(_m.now() < _toFrame)
		{
			const auto t = Clock::now();
			_m.step();
			if(g_stats) g_stats->blockUs.push_back(usSince(t));
		}
	}

	// ---------------- Machinedrum ----------------
	struct MdRig
	{
		Machine m;
		std::unique_ptr<mdDesk::Desk> desk;
		std::deque<Bytes> out, in;
		std::deque<md::PanelPacket> keys;
		uint8_t channel = 0;
		Bytes lastRegion;
		uint64_t tick = 0;
		bool playing = false;

		explicit MdRig(const Bytes& _rom, const std::string& _name) : m(_rom, _name)
		{
			mdDesk::Desk::Port port;
			port.device.sendSysex = [this](const Bytes& _b) { out.push_back(_b); };
			port.device.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				// the plug-in's route (StudioLink::setKitParam -> the controller's CC) is the same message
				if(const auto cc = deskWire::md::kitParam(channel, _t, _i, _v)) out.push_back(*cc);
			};
			port.device.sendMute = [this](const uint8_t _t, const bool _on) { if(const auto cc = deskWire::md::mute(channel, _t, _on)) out.push_back(*cc); };
			port.device.pressKey = [this](const std::string& _key)
			{
				const auto s = md::panelKeySequence(md::MachineModel::Machinedrum, _key);
				keys.insert(keys.end(), s.begin(), s.end());
				return !s.empty();
			};
			port.device.baseChannel = [this](const uint8_t _c) { channel = _c; };
			port.device.nowMs = [this] { return ms(); };
			port.toPage = [](const Value& _v) { countPage(_v); };
			desk = std::make_unique<mdDesk::Desk>(port);
			m.onSysex = [this](const Bytes& _b) { in.push_back(_b); };
		}

		double ms() const { return m.now() * 1000.0 / g_rate; }

		template<typename F> void timed(F&& _f, const bool _edit = false)
		{
			const auto t = Clock::now();
			_f();
			const auto us = usSince(t);
			if(g_stats) { g_stats->desk(us); if(_edit) { g_stats->editUs += us; ++g_stats->edits; } }
		}

		void page(const std::string& _json, const bool _edit = false)
		{
			timed([&] { desk->onPageMessage(*ed::json::parse(_json)); }, _edit);
		}

		// One session step (8 ms): the device's facts in, the desk's work, MIDI out, the machine renders.
		void stepOnce()
		{
			++tick;
			while(!in.empty()) { if(g_stats) g_stats->inBytes += in.front().size(); const auto b = std::move(in.front()); in.pop_front(); timed([&] { desk->onDeviceSysex(b); }); }
			mdDesk::Telemetry t;
			t.valid = true;
			t.step = m.read8(md::SequencerState::g_stepAddress);
			t.pattern = m.read8(0x28d205);
			t.playing = playing;
			t.bootAnimation = 0;
			t.panelPending = static_cast<int>(keys.size());
			t.mutes = 0;
			timed([&] { desk->onTelemetry(t); });
			if(tick % 1 == 0)
			{
				Bytes region(ed::g_mdWorkingKitRegionSize);
				for(size_t i = 0; i < region.size(); ++i)
					region[i] = m.read8(ed::g_mdWorkingKitRegionAddress + static_cast<uint32_t>(i));
				if(region != lastRegion) { lastRegion = region; timed([&] { desk->onWorkingKitMemory(region); }); }
			}
			if(tick % 4 == 0)
				timed([&] { desk->tick(); });
			while(!out.empty()) { toMachine(m, out.front()); out.pop_front(); }
			if(!keys.empty()) { m.hardware().trySendPanelEvent(keys.front().row, keys.front().mask); keys.pop_front(); }
			render(m, m.now() + static_cast<uint64_t>(g_stepMs * g_rate / 1000));
		}

		void run(const double _ms) { const auto end = ms() + _ms; while(ms() < end) stepOnce(); }
	};

	void runMd(const Bytes& _rom, const std::string& _name, const double _seconds)
	{
		MdRig r(_rom, _name);
		// Track 1 on every step of the current pattern (locks need trigs), before the desk reads it.
		const auto slot = static_cast<uint8_t>(ed::parseMdStatusResponse(r.m.request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72))->value);
		auto p = *ed::decodeMdPattern(r.m.request(ed::mdPatternRequest(slot), ed::g_mdPatternDump));
		p.length = 16;
		p.trigs[0] = 0xffff;
		r.m.send(ed::encodeMdPattern(p));
		r.desk->setProbe(mdDesk::Desk::Probe::Running);
		r.page(R"({"op":"ready","id":1})");
		// The background read of the library: measured as its own scenario (the firmware builds dumps).
		const auto t0 = r.ms();
		Stats load;
		g_stats = &load;
		while(r.ms() - t0 < 60000 && !(r.desk->documents().patterns.size() >= 128 && r.desk->documents().kits.size() >= 64))
			r.run(100);
		const double loadS = (r.ms() - t0) / 1000;
		g_stats = nullptr;
		std::printf("MD background library read: %zu patterns, %zu kits in %.1f s\n", r.desk->documents().patterns.size(), r.desk->documents().kits.size(), loadS);
		load.print("read", loadS);
		r.m.panel(md::PanelControl::Play);
		r.playing = true;
		r.run(500);
		const int kit = r.desk->linkState().kit ? *r.desk->linkState().kit : 0;
		const int pat = r.desk->linkState().pattern ? *r.desk->linkState().pattern : slot;
		std::printf("MD: playing pattern %d, kit %d; %.0f s per scenario\n", pat, kit, _seconds);
		{
			const auto& pd = r.desk->documents().patterns.at(static_cast<uint8_t>(pat));
			std::printf("  one pattern: %zu B of SysEx, %zu B as the doc message's JSON; one kit: %zu B SysEx\n", ed::encodeMdPattern(pd).size(),
				ed::json::write(ed::patternToJson(pd)).size(), r.desk->documents().kits.count(static_cast<uint8_t>(kit)) ? ed::encodeMdKit(r.desk->documents().kits.at(static_cast<uint8_t>(kit))).size() : 0);
		}

		const auto scenario = [&](const char* _name, const std::function<void(int, uint64_t)>& _edit, const double _ratePerS)
		{
			Stats s;
			g_stats = &s;
			const auto start = r.ms();
			int n = 0;
			const uint64_t g = 1000 + static_cast<uint64_t>(start);	// one gesture: one undo step
			while(r.ms() - start < _seconds * 1000)
			{
				while(_ratePerS > 0 && n < (r.ms() - start) / 1000 * _ratePerS)
					_edit(n++, g);
				r.stepOnce();
			}
			g_stats = nullptr;
			s.print(_name, _seconds);
			// How long the machine takes to show the last edit (the desk has nothing pending, TX off).
			const auto end = r.ms();
			while(r.ms() - end < 10000 && r.desk->coreState().anyPending())
				r.run(g_stepMs);
			std::printf("       settled %.0f ms after the gesture's last edit\n", r.ms() - end);
		};
		scenario("idle", {}, 0);
		scenario("knob", [&](const int _n, const uint64_t _g)
		{
			const int v = static_cast<int>(64 + 60 * std::sin(_n * 0.1));
			r.page("{\"op\":\"param\",\"k\":" + std::to_string(kit) + ",\"t\":0,\"i\":12,\"v\":" + std::to_string(v) + ",\"g\":" + std::to_string(_g) + "}", true);
		}, 60);
		scenario("lock", [&](const int _n, const uint64_t _g)
		{
			const int s = (_n * 16 / 60) % 16, v = (_n * 7) % 128;
			r.page("{\"op\":\"lock\",\"p\":" + std::to_string(pat) + ",\"t\":0,\"i\":0,\"s\":" + std::to_string(s) + ",\"v\":" + std::to_string(v) + ",\"g\":" + std::to_string(_g) + "}", true);
		}, 60);
		// The same draw at lower rates: what a coalescing page or desk would send (one dump per edit).
		for(const double rate : {10.0, 4.0})
			scenario(rate > 5 ? "lock10" : "lock4", [&](const int _n, const uint64_t _g)
			{
				const int s = _n % 16, v = (_n * 7) % 128;
				r.page("{\"op\":\"lock\",\"p\":" + std::to_string(pat) + ",\"t\":0,\"i\":0,\"s\":" + std::to_string(s) + ",\"v\":" + std::to_string(v) + ",\"g\":" + std::to_string(_g) + "}", true);
			}, rate);
		// The dump alone, no read-back (bypassing the desk): what the firmware's receive costs by itself.
		{
			auto pd = r.desk->documents().patterns.at(static_cast<uint8_t>(pat));
			for(const double rate : {20.0, 4.0})
				scenario(rate > 5 ? "raw20" : "raw4", [&](const int _n, const uint64_t)
				{
					pd.lockRows[0][static_cast<size_t>(_n % 16)] = static_cast<uint8_t>((_n * 7) % 128);
					r.out.push_back(ed::encodeMdPattern(pd));
				}, rate);
		}
		scenario("idle2", {}, 0);
	}

	// ---------------- Monomachine ----------------
	std::optional<md::PanelControl> control(const mmDesk::Key _k)
	{
		using K = mmDesk::Key;
		using C = md::PanelControl;
		switch(_k)
		{
		case K::Exit: return C::Exit;
		case K::Enter: return C::Enter;
		case K::Up: return C::Up;
		case K::Down: return C::Down;
		case K::Left: return C::Left;
		case K::Right: return C::Right;
		case K::Play: return C::Play;
		case K::Stop: return C::Stop;
		case K::Global: return C::Kit;
		case K::Record: return C::Record;
		case K::LiveRecord: return C::Play;
		case K::MuteWindow: return C::BankGroup;
		case K::BankGroup: return C::BankGroup;
		case K::Trig9: case K::Trig10: case K::Trig11: case K::Trig12: case K::Trig13: case K::Trig14:
			return static_cast<C>(static_cast<int>(C::Trigger1) + 8 + (static_cast<int>(_k) - static_cast<int>(K::Trig9)));
		}
		return std::nullopt;
	}

	struct MmRig
	{
		static constexpr auto g_mm = md::MachineModel::Monomachine;
		Machine m;
		std::deque<Bytes> out, in;
		std::deque<std::pair<uint64_t, md::PanelPacket>> panel;
		std::unique_ptr<mmDesk::Desk> desk;
		md::MmTelemetry tel;
		uint8_t channel = 0;
		uint32_t lastSeq = 0;
		uint64_t tick = 0;

		explicit MmRig(const Bytes& _rom) : m(_rom, "mm", {}, true, g_mm)
		{
			mmDesk::Desk::Port port;
			port.device.sendSysex = [this](const Bytes& _b) { out.push_back(_b); };
			port.device.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				if(const auto cc = deskWire::mm::param(channel, _t, _p, _i, _v)) out.push_back(*cc);
			};
			port.device.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				for(auto& b : deskWire::mm::nrpn(channel, _t, _p, _v)) out.push_back(std::move(b));
			};
			port.device.baseChannel = [this](const uint8_t _c) { channel = _c; };
			port.device.pressKeys = [this](const std::vector<mmDesk::Key>& _keys) { return pressKeys(_keys); };
			port.device.nowMs = [this] { return ms(); };
			port.toPage = [](const Value& _v) { countPage(_v); };
			desk = std::make_unique<mmDesk::Desk>(port);
			m.onSysex = [this](const Bytes& _b) { in.push_back(_b); };
			m.onBlock = [this]
			{
				while(!panel.empty() && panel.front().first <= m.now())
				{
					m.hardware().trySendPanelEvent(panel.front().second.row, panel.front().second.mask);
					panel.pop_front();
				}
			};
		}

		bool pressKeys(const std::vector<mmDesk::Key>& _keys)
		{
			uint64_t at = std::max(m.now(), panel.empty() ? 0 : panel.back().first) + 64;
			const auto hold = static_cast<uint64_t>(g_rate / 100);
			const auto fn = *md::panelPacket(g_mm, md::PanelControl::Function);
			const auto rec = *md::panelPacket(g_mm, md::PanelControl::Record);
			for(const auto k : _keys)
			{
				const auto pk = *md::panelPacket(g_mm, *control(k));
				if(mmDesk::isChord(k))
				{
					const auto held = mmDesk::heldIsRecord(k) ? rec : fn;
					md::PanelRowState rows;
					for(const auto st : {rows.press(held), rows.press(pk), rows.release(pk), rows.release(held)})
					{
						panel.emplace_back(at, st);
						at += hold;
					}
					continue;
				}
				panel.emplace_back(at, pk);
				at += hold;
				panel.emplace_back(at, md::PanelPacket{pk.row, 0});
				at += hold;
			}
			return true;
		}

		double ms() const { return m.now() * 1000.0 / g_rate; }

		template<typename F> void timed(F&& _f, const bool _edit = false)
		{
			const auto t = Clock::now();
			_f();
			const auto us = usSince(t);
			if(g_stats) { g_stats->desk(us); if(_edit) { g_stats->editUs += us; ++g_stats->edits; } }
		}

		void page(const std::string& _json, const bool _edit = false)
		{
			// the page's JSON is parsed here as the plug-in parses the bridge's batch
			timed([&] { desk->onPageMessage(*ed::json::parse(_json)); }, _edit);
		}

		mmDesk::Telemetry readTelemetry()
		{
			tel.publish([this](const uint32_t _a) { return m.read8(_a); });
			mmDesk::Telemetry t;
			t.valid = true;
			t.step = tel.step.load();
			t.running = tel.running.load() == 1;
			t.screen = md::MmTelemetry::screenOf(tel.screen.load());
			t.recvCount = tel.recvCount.load();
			t.recvErrors = tel.recvErrors.load();
			t.recvActive = tel.recvActive.load() == 1;
			t.tempo = tel.tempo.load();
			t.mutes = tel.mutes.load();
			t.recording = tel.recording.load();
			return t;
		}

		void stepOnce()
		{
			++tick;
			while(!in.empty()) { if(g_stats) g_stats->inBytes += in.front().size(); const auto b = std::move(in.front()); in.pop_front(); timed([&] { desk->onDeviceSysex(b); }); }
			const auto t = readTelemetry();
			timed([&] { desk->onTelemetry(t); });
			Bytes region;
			uint32_t seq = 0;
			if(tel.readWorkingKit(region, seq) && seq != lastSeq) { lastSeq = seq; timed([&] { desk->onWorkingKit(region); }); }
			if(tick % 4 == 0)
				timed([&] { desk->tick(); });
			while(!out.empty()) { toMachine(m, out.front()); out.pop_front(); }
			render(m, m.now() + static_cast<uint64_t>(g_stepMs * g_rate / 1000));
		}

		void run(const double _ms) { const auto end = ms() + _ms; while(ms() < end) stepOnce(); }
	};

	void runMm(const Bytes& _rom, const double _seconds)
	{
		MmRig r(_rom);
		r.page(R"({"op":"ready"})");
		r.desk->setProbe(mmDesk::Desk::Probe::Running);
		r.run(600);
		const auto t0 = r.ms();
		Stats load;
		g_stats = &load;
		while(r.desk->loaded() < 288 && r.ms() - t0 < 60000)
			r.run(100);
		const double loadS = (r.ms() - t0) / 1000;
		g_stats = nullptr;
		std::printf("MM background library read: %d documents in %.1f s\n", static_cast<int>(r.desk->loaded()), loadS);
		load.print("read", loadS);
		r.page(R"({"op":"select","p":1,"force":true})");
		r.run(300);
		r.page(R"({"op":"play"})");
		r.run(1500);
		const int pat = r.desk->currentPattern();
		auto p = *r.desk->pattern(static_cast<uint8_t>(pat));
		int lockRow = -1;
		for(int i = 0; i < p.lockRowCount && lockRow < 0; ++i)
			for(int s = 0; s < 64; ++s)
				if(p.lockRows[i][s] != ed::MmPattern::g_noLock) { lockRow = i; break; }
		std::printf("MM: playing pattern %d (lock rows %d, drawing on row %d); %.0f s per scenario\n", pat, p.lockRowCount, lockRow, _seconds);
		std::printf("  one pattern: %zu B of SysEx, %zu B of JSON (the page's set and the doc message each); one kit: %zu B SysEx, %zu B JSON\n",
			ed::encodeMmPattern(p).size(), ed::json::write(ed::mmPatternToJson(p)).size(),
			r.desk->workingKit() ? ed::encodeMmKit(*r.desk->workingKit()).size() : 0, r.desk->workingKit() ? ed::json::write(ed::mmKitToJson(*r.desk->workingKit())).size() : 0);

		const auto scenario = [&](const char* _name, const std::function<void(int, uint64_t)>& _edit, const double _ratePerS)
		{
			Stats s;
			g_stats = &s;
			const auto start = r.ms();
			int n = 0;
			const uint64_t g = 1000 + static_cast<uint64_t>(start);
			while(r.ms() - start < _seconds * 1000)
			{
				while(_ratePerS > 0 && n < (r.ms() - start) / 1000 * _ratePerS)
					_edit(n++, g);
				r.stepOnce();
			}
			g_stats = nullptr;
			s.print(_name, _seconds);
			const auto end = r.ms();
			while(r.ms() - end < 15000 && r.desk->coreState().anyPending())
				r.run(g_stepMs);
			std::printf("       settled %.0f ms after the gesture's last edit (recv %s)\n", r.ms() - end, r.desk->recvState().c_str());
		};
		scenario("idle", {}, 0);
		scenario("knob", [&](const int _n, const uint64_t _g)
		{
			// the MM page sends the whole working kit at the gesture ({"op":"set","kind":"workingKit"})
			auto k = *r.desk->workingKit();
			k.tracks[0].pages[2][0] = static_cast<uint8_t>(64 + 60 * std::sin(_n * 0.1));	// FLT BASE
			r.page(R"({"op":"set","kind":"workingKit","g":)" + std::to_string(_g) + R"(,"doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}", true);
		}, 60);
		scenario("lock", [&](const int _n, const uint64_t _g)
		{
			// the whole pattern at the gesture ({"op":"set","kind":"pattern"}), one lock value changed
			auto q = *r.desk->pattern(static_cast<uint8_t>(pat));
			const int s = (_n * 16 / 60) % 16;
			if(lockRow >= 0)
				q.lockRows[static_cast<size_t>(lockRow)][static_cast<size_t>(s)] = static_cast<uint8_t>((_n * 7) % 128);
			else
				q.notes[0][static_cast<size_t>(s)] = static_cast<uint8_t>(36 + _n % 24);
			r.page(R"({"op":"set","kind":"pattern","g":)" + std::to_string(_g) + R"(,"doc":)" + ed::json::write(ed::mmPatternToJson(q)) + "}", true);
		}, 60);
		scenario("idle2", {}, 0);
	}

	// MD parameter tweaking on the firmware itself (manual p.37): hold FUNCTION, turn a DATA ENTRY knob.
	// Does one panel gesture move the same knob on every track? What the page does instead is 15 extra CCs.
	void tweakProbe(const Bytes& _rom, const std::string& _name)
	{
		Machine m(_rom, _name);
		const auto params = [&]
		{
			std::vector<int> v;
			for(uint32_t t = 0; t < 16; ++t)
				v.push_back(m.read8(ed::g_mdWorkingKitRegionAddress + 2 + ed::g_mdWorkingKitParamsOffset + t * 24 + 1));
			return v;
		};
		const auto show = [](const char* _w, const std::vector<int>& _v) { std::printf("  %-22s", _w); for(const auto x : _v) std::printf(" %3d", x); std::printf("\n"); };
		const auto enc = *md::panelEncoderCommand(md::MachineModel::Machinedrum, md::PanelEncoder::DataEntryB);
		const auto fn = *md::panelPacket(md::MachineModel::Machinedrum, md::PanelControl::Function);
		m.run(500);
		const auto before = params();
		show("param 2, before", before);
		// the encoder alone: the selected track only
		for(int i = 0; i < 5; ++i) { m.hardware().trySendPanelEvent(enc, 0x01); m.run(10); }
		m.run(200);
		const auto plain = params();
		show("B +5, plain", plain);
		// FUNCTION held (row state), the encoder turned, FUNCTION released
		md::PanelRowState rows;
		const auto down = rows.press(fn);
		m.hardware().trySendPanelEvent(down.row, down.mask);
		m.run(40);
		const auto tStart = m.now();
		for(int i = 0; i < 5; ++i) { m.hardware().trySendPanelEvent(enc, 0x01); m.run(10); }
		const auto up = rows.release(fn);
		m.hardware().trySendPanelEvent(up.row, up.mask);
		m.run(300);
		const auto tweaked = params();
		show("FUNCTION + B +5", tweaked);
		int moved = 0;
		for(size_t t = 0; t < 16; ++t) moved += tweaked[t] != plain[t];
		std::printf("  tracks moved by the one panel gesture: %d of 16 (%.0f ms of machine time, 7 panel packets)\n", moved, (m.now() - tStart) * 1000.0 / g_rate);
		// Can one encoder packet carry several steps (a value of +5 instead of five +1s)? Yes, alone; but packets
		// of several steps less than ~10 ms apart lose steps, and in the desk's flow (mdDeskFirmwareTest tweak,
		// after SET STATUS track) a lone +3 packet was lost: the plug-in keeps one packet a step.
		{
			const auto d2 = rows.press(fn);
			m.hardware().trySendPanelEvent(d2.row, d2.mask);
			m.run(40);
			m.hardware().trySendPanelEvent(enc, 0x05);
			m.run(10);
			const auto u2 = rows.release(fn);
			m.hardware().trySendPanelEvent(u2.row, u2.mask);
			m.run(300);
			const auto multi = params();
			show("FUNCTION + B one +5 packet", multi);
			// and five +1 packets one 128-frame block apart (the plug-in's pace at 128 frames)
			const auto d3 = rows.press(fn);
			m.hardware().trySendPanelEvent(d3.row, d3.mask);
			m.run(40);
			for(int i = 0; i < 5; ++i) { m.hardware().trySendPanelEvent(enc, 0x01); m.run(128.0 * 1000 / g_rate); }
			const auto u3 = rows.release(fn);
			m.hardware().trySendPanelEvent(u3.row, u3.mask);
			m.run(300);
			show("FUNCTION + B 5x+1 fast", params());
			for(const int v : {16, 10, 8, 7, -8, -16})
			{
				const auto d4 = rows.press(fn);
				m.hardware().trySendPanelEvent(d4.row, d4.mask);
				m.run(40);
				const auto b4 = params();
				m.hardware().trySendPanelEvent(enc, static_cast<uint8_t>(static_cast<int8_t>(v)));
				m.run(3);
				const auto u4 = rows.release(fn);
				m.hardware().trySendPanelEvent(u4.row, u4.mask);
				m.run(300);
				const auto a4 = params();
				std::printf("  one packet %+d: track 1 %d -> %d, track 2 %d -> %d\n", v, b4[0], a4[0], b4[1], a4[1]);
			}
			const auto burst = [&](const char* _what, const std::vector<int>& _packets, const double _gapMs, const int _select)
			{
				if(_select >= 0) { m.send(ed::mdSetStatus(ed::MdStatus::Track, static_cast<uint8_t>(_select))); m.run(60); }
				const auto d5 = rows.press(fn);
				m.hardware().trySendPanelEvent(d5.row, d5.mask);
				m.run(40);
				const auto b5 = params();
				for(const int v : _packets) { m.hardware().trySendPanelEvent(enc, static_cast<uint8_t>(static_cast<int8_t>(v))); m.run(_gapMs); }
				m.run(150);
				const auto u5 = rows.release(fn);
				m.hardware().trySendPanelEvent(u5.row, u5.mask);
				m.run(300);
				const auto a5 = params();
				int sum = 0; for(const int v : _packets) sum += v;
				std::printf("  %s (sum %+d, %.1f ms apart): track 3 %d -> %d, track 5 %d -> %d\n", _what, sum, _gapMs, b5[2], a5[2], b5[4], a5[4]);
			};
			burst("select T4, one +3", {3}, 3, 3);
			burst("select T4 again, one +3", {3}, 3, 3);
			burst("-16 -16 -16 -13", {-16, -16, -16, -13}, 3, -1);
			burst("-16 -16 -16 -13 slow", {-16, -16, -16, -13}, 20, -1);
			burst("+16 x4 +6", {16, 16, 16, 16, 6}, 10.7, -1);
		}
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: editFlowBenchTest md|mm <ROM> [seconds]");
		return 77;
	}
	try
	{
		const std::string which = _argv[1];
		const auto rom = load(_argv[2]);
		const double seconds = _argc > 3 ? std::atof(_argv[3]) : 3;
		if(which == "tweak")
			tweakProbe(rom, _argv[2]);
		else if(which == "md")
			runMd(rom, _argv[2], seconds);
		else
			runMm(rom, seconds);
	}
	catch(const std::exception& _e)
	{
		std::printf("editFlowBenchTest: %s\n", _e.what());
		return 1;
	}
	return 0;
}
