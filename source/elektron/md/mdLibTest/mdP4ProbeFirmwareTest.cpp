// P4 Machinedrum Editor probes against MD OS 1.63 firmware (manual: needs a
// user-supplied ROM). Discovery harness: it finds and measures; the smoke test
// (mdDeskFirmwareTest) checks.
//
//   mdP4ProbeFirmwareTest <ROM> [boot|keys|chain|mutes|library]
//
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdKit.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdWorkingKit.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mdfrontpanel.h"

#include <algorithm>
#include <functional>
#include <cstring>
#include <map>
#include <optional>
#include <set>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	std::string g_romName;
	int g_failures = 0;

	void check(const bool _condition, const std::string& _what)
	{
		std::printf("  %s %s\n", _condition ? "ok  " : "FAIL", _what.c_str());
		if(!_condition)
			++g_failures;
	}

	double ms(const uint64_t _frames) { return _frames * 1000.0 / g_rate; }

	int status(Machine& _m, const ed::MdStatus _param)
	{
		const auto r = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(_param), 0x72));
		return r ? r->value : -1;
	}

	std::optional<ed::MdPattern> readPattern(Machine& _m, const uint8_t _slot)
	{
		return ed::decodeMdPattern(_m.request(ed::mdPatternRequest(_slot), ed::g_mdPatternDump));
	}

	std::optional<ed::MdKit> readKit(Machine& _m, const uint8_t _slot)
	{
		return ed::decodeMdKit(_m.request(ed::mdKitRequest(_slot), ed::g_mdKitDump));
	}

	void rawPanel(Machine& _m, const uint8_t _row, const uint8_t _mask, const double _holdMs = 40)
	{
		_m.hardware().trySendPanelEvent(_row, _mask);
		_m.run(_holdMs);
	}

	void press(Machine& _m, const md::PanelControl _c)
	{
		_m.panel(_c);
	}

	md::FrontPanel panelNow(Machine& _m) { return _m.hardware().getFrontPanelSnapshot(); }

	uint64_t lcdHash(const md::FrontPanel& _p)
	{
		uint64_t h = 1469598103934665603ull;
		for(uint32_t half = 0; half < 2; ++half)
			for(uint32_t page = 0; page < 8; ++page)
				for(uint32_t col = 0; col < 64; ++col)
					h = (h ^ _p.getLcdVram(half, page, col)) * 1099511628211ull;
		return h;
	}

	// The LCD as 64 rows of '#'/'.'; every second column and row, so it fits a terminal.
	void printLcd(const md::FrontPanel& _p)
	{
		for(uint32_t y = 0; y < 64; y += 2)
		{
			std::string line = "    ";
			for(uint32_t x = 0; x < 128; x += 1)
				line += _p.getLcdPixel(x, y) ? '#' : '.';
			std::puts(line.c_str());
		}
	}

	bool playheadMoves(Machine& _m, const double _ms)
	{
		const auto a = _m.playhead();
		for(double t = 0; t < _ms; t += 20)
		{
			_m.run(20);
			if(_m.playhead() != a)
				return true;
		}
		return false;
	}

	void stopMachine(Machine& _m)
	{
		press(_m, md::PanelControl::Stop);
		press(_m, md::PanelControl::Stop);
		_m.run(100);
	}

	// ---- boot: when does the start-up animation end, and what says so? ----

	void boot(const Bytes& _rom)
	{
		std::puts("== probe: the start-up animation after the firmware takes MIDI");
		Machine m(_rom, g_romName, {}, false);
		std::vector<Bytes> snaps;
		std::vector<double> snapAt;
		uint64_t last = 0;
		double lastChange = 0;
		int changes = 0;
		std::map<int, int> changesPerSecond;
		const auto start = m.now();
		for(int i = 0; i < 400; ++i)
		{
			m.run(100);
			const double t = ms(m.now() - start);
			const auto p = panelNow(m);
			const auto h = lcdHash(p);
			if(h != last)
			{
				last = h;
				lastChange = t;
				++changes;
				++changesPerSecond[static_cast<int>(t / 1000)];
			}
			if(i % 5 == 4)
			{
				snaps.push_back(m.snapshotRam());
				snapAt.push_back(t);
			}
			if(i == 20 || i == 100 || i == 180 || i == 399)
			{
				std::printf("  LCD at %.1f s (%u lit):\n", t / 1000, p.countLitPixels());
				printLcd(p);
			}
		}
		std::printf("  LCD changed %d times; last change at %.1f s after MIDI ready\n", changes, lastChange / 1000);
		std::printf("  changes per second:");
		for(const auto& [s, n] : changesPerSecond)
			std::printf(" %d:%d", s, n);
		std::printf("\n");

		// Bytes with one step: constant before a time, constant (other value) after.
		struct Step { uint32_t address; uint8_t before, after; double at; };
		std::vector<Step> steps;
		for(uint32_t a = 0; a < 0x100000; ++a)
		{
			const auto v0 = snaps[1][a];
			size_t k = 2;
			while(k < snaps.size() && snaps[k][a] == v0)
				++k;
			if(k >= snaps.size() - 4)
				continue;
			const auto v1 = snaps[k][a];
			bool ok = true;
			for(size_t j = k; j < snaps.size() && ok; ++j)
				ok = snaps[j][a] == v1;
			if(ok)
				steps.push_back({a + 0x200000, v0, v1, snapAt[k]});
		}
		std::map<int, int> byTime;
		for(const auto& s : steps)
			++byTime[static_cast<int>(s.at / 500)];
		std::printf("  %zu one-step bytes; by switch time (s):", steps.size());
		for(const auto& [t, n] : byTime)
			std::printf(" %.1f:%d", t * 0.5, n);
		std::printf("\n");
		for(const auto& s : steps)
			if(std::abs(s.at - lastChange) < 1500)
				std::printf("    0x%06x %02x -> %02x at %.1f s\n", s.address, s.before, s.after, s.at / 1000);
	}

	// Candidates for "the animation is over": one step between 12 and 16 s, constant around it.
	void bootFlag(const Bytes& _rom, const Bytes& _patch)
	{
		std::puts("== probe: a RAM byte that marks the end of the start-up animation");
		std::vector<Bytes> coarse;
		{
			Machine m(_rom, g_romName, _patch, false);
			for(int s = 0; s < 32; ++s)
			{
				m.run(1000);
				coarse.push_back(m.snapshotRam());
			}
		}
		std::vector<uint32_t> cand;
		for(uint32_t a = 0; a < 0x100000; ++a)
		{
			bool ok = true;
			for(int s = 2; s < 12 && ok; ++s)
				ok = coarse[s][a] == coarse[2][a];
			for(int s = 16; s < 32 && ok; ++s)
				ok = coarse[s][a] == coarse[16][a];
			if(ok && coarse[2][a] != coarse[16][a])
				cand.push_back(a);
		}
		std::printf("  %zu candidates\n", cand.size());
		Machine m(_rom, g_romName, _patch, false);
		std::vector<double> switchAt(cand.size(), -1);
		std::vector<uint8_t> first(cand.size());
		for(size_t i = 0; i < cand.size(); ++i)
			first[i] = m.read8(0x200000 + cand[i]);
		uint64_t lastHash = 0;
		double lcdLast = 0;
		const auto start = m.now();
		while(ms(m.now() - start) < 20000)
		{
			m.run(10);
			const double t = ms(m.now() - start);
			const auto h = lcdHash(panelNow(m));
			if(h != lastHash) { lastHash = h; lcdLast = t; }
			for(size_t i = 0; i < cand.size(); ++i)
				if(switchAt[i] < 0 && m.read8(0x200000 + cand[i]) != first[i])
					switchAt[i] = t;
		}
		std::printf("  LCD last change %.2f s\n", lcdLast / 1000);
		std::vector<size_t> order(cand.size());
		for(size_t i = 0; i < order.size(); ++i) order[i] = i;
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return std::abs(switchAt[a] - lcdLast) < std::abs(switchAt[b] - lcdLast); });
		size_t shown = 0;
		for(size_t n = 0; n < order.size() && shown < 60; ++n)
		{
			const auto i = order[n];
			if(cand[i] >= 0xaa000 && cand[i] < 0xab000)
				continue;
			++shown;
			std::printf("    0x%06x %02x -> %02x at %.2f s\n", 0x200000 + cand[i], coarse[2][cand[i]], coarse[16][cand[i]], switchAt[i] / 1000);
		}
	}

	// 0x2a68e3 (bootflag: 02 during the animation, 00 after): every transition, and PLAY around it.
	void bootCheck(const Bytes& _rom, const Bytes& _patch)
	{
		constexpr uint32_t flag = 0x2a68e3;
		std::puts("== probe: the animation flag 0x2a68e3");
		double flip = -1;
		{
			Machine m(_rom, g_romName, _patch, false);
			const auto start = m.now();
			int last = m.read8(flag);
			std::printf("  at MIDI ready: %02x\n", last);
			while(ms(m.now() - start) < 30000)
			{
				m.run(5);
				const int v = m.read8(flag);
				if(v != last)
				{
					std::printf("  %.3f s: %02x -> %02x\n", ms(m.now() - start) / 1000, last, v);
					if(v == 0 && flip < 0)
						flip = ms(m.now() - start);
					last = v;
				}
			}
		}
		require(flip > 0, "flag never cleared");
		for(const double d : {-400.0, -100.0, 10.0, 60.0, 200.0})
		{
			Machine m(_rom, g_romName, _patch, false);
			const auto start = m.now();
			m.runUntil(start + static_cast<uint64_t>((flip + d) * g_rate / 1000));
			const int v = m.read8(flag);
			press(m, md::PanelControl::Play);
			std::printf("  PLAY at flip %+.0f ms (flag %02x): %s\n", d, v, playheadMoves(m, 800) ? "plays" : "ignored");
		}
	}

	// What else sets 0x2a68e3? Dumps, screens.
	void flagUse(const Bytes& _rom)
	{
		constexpr uint32_t flag = 0x2a68e3;
		std::puts("== probe: 0x2a68e3 after boot");
		Machine m(_rom, g_romName);
		std::vector<Bytes> rx;
		m.onSysex = [&](const Bytes& _b) { rx.push_back(_b); };
		const auto trace = [&](const char* _what, const double _ms)
		{
			std::printf("  %-34s", _what);
			int last = -1;
			const auto start = m.now();
			while(ms(m.now() - start) < _ms)
			{
				m.run(2);
				const int v = m.read8(flag);
				if(v != last) { std::printf(" %.0f:%02x", ms(m.now() - start), v); last = v; }
			}
			std::printf("\n");
		};
		trace("idle", 300);
		m.hardware().sendMidi([]{ synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); auto r = ed::mdKitRequest(3); e.sysex.assign(r.begin(), r.end()); return e; }());
		trace("kit dump request", 600);
		m.hardware().sendMidi([]{ synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); auto r = ed::mdPatternRequest(3); e.sysex.assign(r.begin(), r.end()); return e; }());
		trace("pattern dump request", 600);
		m.hardware().sendMidi([]{ synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); auto r = ed::mdStatusRequest(ed::MdStatus::Kit); e.sysex.assign(r.begin(), r.end()); return e; }());
		trace("status request", 300);
		const auto pat = readPattern(m, 5);
		m.hardware().sendMidi([&]{ synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); auto r = ed::encodeMdPattern(*pat); e.sysex.assign(r.begin(), r.end()); return e; }());
		trace("pattern dump in", 600);
		// PLAY while the flag is set by a dump request.
		for(const double d : {5.0, 20.0, 60.0})
		{
			m.hardware().sendMidi([]{ synthLib::SMidiEvent e(synthLib::MidiEventSource::Host); auto r = ed::mdKitRequest(7); e.sysex.assign(r.begin(), r.end()); return e; }());
			const auto start = m.now();
			while(m.read8(flag) == 0 && ms(m.now() - start) < 300) m.run(1);
			m.run(d);
			const int v = m.read8(flag);
			press(m, md::PanelControl::Play);
			std::printf("  PLAY %.0f ms into a kit dump (flag %02x): %s\n", d, v, playheadMoves(m, 600) ? "plays" : "ignored");
			stopMachine(m);
			m.run(300);
		}
		press(m, md::PanelControl::Kit);
		trace("KIT key (kit menu)", 400);
		press(m, md::PanelControl::Exit);
		trace("EXIT", 300);
	}

	// Where the main UI starts: a few candidates' first change after MIDI ready, with and
	// without a restored state, plus the first time PLAY is taken (bisection).
	void bootUi(const Bytes& _rom, const Bytes& _patch)
	{
		std::puts("== probe: first change of main-UI candidates");
		const std::vector<uint32_t> cands = {0x28998a, 0x28998b, 0x289990, 0x289991, 0x289992, 0x289993, 0x289995, 0x2a68e3};
		Machine m(_rom, g_romName, _patch, false);
		std::vector<int> first(cands.size());
		std::vector<double> at(cands.size(), -1);
		for(size_t i = 0; i < cands.size(); ++i)
			first[i] = m.read8(cands[i]);
		const auto start = m.now();
		while(ms(m.now() - start) < 20000)
		{
			m.run(5);
			for(size_t i = 0; i < cands.size(); ++i)
				if(at[i] < 0 && m.read8(cands[i]) != first[i])
				{
					at[i] = ms(m.now() - start);
					std::printf("  0x%06x %02x -> %02x at %.3f s\n", cands[i], first[i], m.read8(cands[i]), at[i] / 1000);
				}
		}
		double lo = 5000, hi = 16000;
		while(hi - lo > 50)
		{
			const double mid = (lo + hi) / 2;
			Machine k(_rom, g_romName, _patch, false);
			k.runUntil(k.now() + static_cast<uint64_t>(mid * g_rate / 1000));
			press(k, md::PanelControl::Play);
			(playheadMoves(k, 600) ? hi : lo) = mid;
		}
		std::printf("  PLAY first taken at %.2f s\n", hi / 1000);
	}

	// Live recording: when must a DATA ENTRY turn come for the firmware to lock a note? A
	// programmed trig on track 15 step 9 (and optionally a live note there), knob A turned
	// at an offset from the start of step 9; which step gets the lock.
	void lockWindow(const Bytes& _rom, const bool _liveNote)
	{
		std::printf("== probe: the knob lock window (%s)\n", _liveNote ? "live note on step 9" : "programmed trig on step 9");
		Machine m(_rom, g_romName);
		const auto g = ed::decodeMdGlobal(m.request(ed::mdGlobalRequest(0), ed::g_mdGlobalDump));
		const int patSlot = status(m, ed::MdStatus::Pattern);
		auto base = *readPattern(m, static_cast<uint8_t>(patSlot));
		base.length = 16;
		for(uint8_t t = 0; t < 16; ++t)
			for(uint8_t st = 0; st < 64; ++st)
				if(ed::hasTrig(base, t, st))
					base = ed::withTrig(base, t, st, false);
		if(!_liveNote)
			base = ed::withTrig(base, 14, 8, true);
		m.send(ed::mdSetStatus(ed::MdStatus::Track, 14));
		int note = -1;
		for(int n = 0; n < 128; ++n)
			if(g->keymap[n] == 14) { note = n; break; }
		for(const double d : {-375.0, -250.0, -190.0, -125.0, -95.0, -63.0, -40.0, -20.0, -8.0, 0.0, 8.0, 20.0, 40.0})
		{
			m.send(ed::encodeMdPattern(base));
			m.run(50);
			rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x06); rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x00);
			// Period from two step edges, then step 8's start.
			auto edge = [&](const int _step) { for(int i = 0; i < 40000 && m.playhead() != _step; ++i) m.step(); return m.now(); };
			const auto t6 = edge(6), t7 = edge(7);
			const auto period = t7 - t6;
			const auto at = static_cast<int64_t>(t7 + period) + static_cast<int64_t>(d * g_rate / 1000);
			m.runUntil(static_cast<uint64_t>(std::max<int64_t>(at, static_cast<int64_t>(m.now()))));
			for(int i = 0; i < 10; ++i)
				m.hardware().trySendPanelEvent(0x30, 0x01);
			if(_liveNote)
			{
				edge(8);
				m.send({static_cast<uint8_t>(0x90 | g->baseChannel), static_cast<uint8_t>(note), 100});
				m.run(20);
				m.send({static_cast<uint8_t>(0x80 | g->baseChannel), static_cast<uint8_t>(note), 0});
			}
			edge(13);
			m.panel(md::PanelControl::Stop);
			m.panel(md::PanelControl::Stop);
			m.run(150);
			const auto after = *readPattern(m, static_cast<uint8_t>(patSlot));
			std::printf("  turn at %+6.0f ms from step 9 (step %.0f ms): lock on", d, ms(period));
			bool any = false;
			for(uint8_t st = 0; st < 16; ++st)
				if(const auto v = ed::lockValue(after, 14, 0, st)) { std::printf(" step %u (%u)", st + 1, *v); any = true; }
			std::printf("%s; trigs", any ? "" : " none");
			for(uint8_t st = 0; st < 16; ++st)
				if(ed::hasTrig(after, 14, st)) std::printf(" %u", st + 1);
			std::printf("\n");
		}
	}

	// ---- P5: the GLOBAL settings bytes, by behaviour ----
	void globals(const Bytes& _rom)
	{
		std::puts("== probe: GLOBAL settings (sync, local control, program change, base channel)");
		Machine m(_rom, g_romName);
		const int slot = status(m, ed::MdStatus::GlobalSlot);
		const auto readG = [&] { return *ed::decodeMdGlobal(m.request(ed::mdGlobalRequest(static_cast<uint8_t>(slot)), ed::g_mdGlobalDump)); };
		const auto g0 = readG();
		std::printf("  slot %d: base %d unused %d sync %02x local %d inputs", slot, g0.baseChannel, g0.unused, g0.syncFlags, g0.localControl);
		for(const auto v : g0.inputSettings) std::printf(" %d", v);
		std::printf(" prgch %02x trigmode %d\n", g0.programChange, g0.trigMode);
		std::printf("  keymap:");
		for(int n = 0; n < 128; ++n) if(g0.keymap[n] != 0xff) std::printf(" %d:%d", n, g0.keymap[n]);
		std::printf("\n");
		int fa = 0, fc = 0, f8 = 0, pcOut = -1, notes = 0;
		m.onMidi = [&](const synthLib::SMidiEvent& _e)
		{
			if(_e.a == 0xfa || _e.a == 0xfb) ++fa;
			else if(_e.a == 0xfc) ++fc;
			else if(_e.a == 0xf8) ++f8;
			else if((_e.a & 0xf0) == 0xc0) pcOut = _e.b;
			else if((_e.a & 0xf0) == 0x90) ++notes;
		};
		const auto put = [&](const ed::MdGlobal& _g, const bool _reselect)
		{
			m.send(ed::encodeMdGlobal(_g));
			if(_reselect)
				m.send({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x56, static_cast<uint8_t>(slot), 0xf7});
			m.run(1000);
		};
		const auto playing = [&] { const auto a = m.playhead(); m.run(400); return m.playhead() != a; };
		// Sync bits.
		for(int b = -1; b < 8; ++b)
		{
			for(const bool re : {false, true})
			{
				auto g = g0;
				g.syncFlags = b < 0 ? 0 : static_cast<uint8_t>(1 << b);
				put(g, re);
				fa = fc = f8 = 0;
				press(m, md::PanelControl::Play);
				m.run(500);
				const int clocks = f8, starts = fa;
				press(m, md::PanelControl::Stop);
				press(m, md::PanelControl::Stop);
				m.run(200);
				const int stops = fc;
				m.send({0xfa});
				const bool extStart = playing();
				m.send({0xfc});
				m.run(200);
				press(m, md::PanelControl::Stop);
				m.run(100);
				const auto rb = readG();
				std::printf("  [stored %02x] sync %02x%s: out clock %d start %d stop %d; MIDI Start in plays %d\n", rb.syncFlags, g.syncFlags, re ? " +0x56" : "", clocks, starts, stops, extStart ? 1 : 0);
			}
		}
		put(g0, true);
		// Local control: TRIG 1 with local on and off.
		for(const int local : {1, 0})
		{
			auto g = g0;
			g.localControl = static_cast<uint8_t>(local);
			for(const bool re : {false, true})
			{
				put(g, re);
				m.run(300);
				const auto from = m.left().size();
				notes = 0;
				press(m, md::PanelControl::Trigger1);
				m.run(200);
				double e = 0;
				for(size_t i = from; i < m.left().size(); ++i) e += m.left()[i] * m.left()[i];
				std::printf("  local %d%s: TRIG 1 energy %.4f, notes out %d\n", local, re ? " +0x56" : "", e, notes);
			}
		}
		put(g0, true);
		// Program change.
		for(const int v : {0, 1, 2, 3, 4, 8, 16, 17, 32, 64})
		{
			auto g = g0;
			g.programChange = static_cast<uint8_t>(v);
			put(g, true);
			m.send(ed::mdLoadPattern(0));
			m.run(100);
			m.send({static_cast<uint8_t>(0xc0 | g0.baseChannel), 5});
			m.run(200);
			const int in = status(m, ed::MdStatus::Pattern);
			pcOut = -1;
			rawPanel(m, 0x23, 0x01, 60); rawPanel(m, 0x20, 0x04, 60); rawPanel(m, 0x20, 0, 60); rawPanel(m, 0x23, 0, 200);
			std::printf("  prgch %02x: PC 5 in -> pattern %d; select A03 on the panel -> PC out %d\n", v, in, pcOut);
		}
		put(g0, true);
		// Base channel.
		for(const int bc : {0, 2})
		{
			auto g = g0;
			g.baseChannel = static_cast<uint8_t>(bc);
			put(g, true);
			const auto before = m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 16);
			m.send({0xb0, 16 + 16, static_cast<uint8_t>((before + 11) & 0x7f)});	// track 1 DIST on channel 1
			m.run(80);
			const auto after0 = m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 16);
			m.send({static_cast<uint8_t>(0xb0 | 2), 16 + 16, static_cast<uint8_t>((before + 22) & 0x7f)});
			m.run(80);
			const auto after2 = m.read8(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + 16);
			std::printf("  base %d: DIST CC on ch1 %s, on ch3 %s\n", bc + 1, after0 != before ? "taken" : "ignored", after2 != after0 ? "taken" : "ignored");
		}
		put(g0, true);
		// Sync bits with an external clock, and TRIG energy per bit (local control?).
		const auto trigEnergy = [&]
		{
			m.run(300);
			const auto from = m.left().size();
			press(m, md::PanelControl::Trigger1);
			m.run(200);
			double e = 0;
			for(size_t i = from; i < m.left().size(); ++i) e += m.left()[i] * m.left()[i];
			return e;
		};
		for(const int v : {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x11})
		{
			auto g = g0;
			g.syncFlags = static_cast<uint8_t>(v);
			put(g, true);
			m.send({0xfa});
			const bool alone = playing();
			m.send({0xfc}); m.run(100); press(m, md::PanelControl::Stop); m.run(100);
			m.send({0xfa});
			const auto a = m.playhead();
			for(int i = 0; i < 48; ++i) { m.send({0xf8}); m.run(20); }
			const bool clocked = m.playhead() != a;
			m.send({0xfc}); m.run(100); press(m, md::PanelControl::Stop); m.run(100);
			const auto e = trigEnergy();
			std::printf("  sync %02x: MIDI Start alone plays %d, with MIDI clock plays %d; TRIG 1 energy %.2f\n", v, alone ? 1 : 0, clocked ? 1 : 0, e);
		}
		put(g0, true);
		for(const int lc : {0, 1, 2, 127})
		{
			auto g = g0;
			g.localControl = static_cast<uint8_t>(lc);
			put(g, true);
			std::printf("  local byte %d: TRIG 1 energy %.2f (stored %d)\n", lc, trigEnergy(), readG().localControl);
		}
		put(g0, true);
		for(const int v : {0x01, 0x05, 0x09, 0x0d, 0x11, 0x21, 0x41, 0x03})
		{
			auto g = g0;
			g.programChange = static_cast<uint8_t>(v);
			put(g, true);
			std::printf("  prgch %02x: PC 7 in accepted on channels", v);
			for(int ch = 0; ch < 16; ++ch)
			{
				m.send(ed::mdLoadPattern(0)); m.run(60);
				m.send({static_cast<uint8_t>(0xc0 | ch), 7}); m.run(120);
				if(status(m, ed::MdStatus::Pattern) == 7) std::printf(" %d", ch + 1);
			}
			pcOut = -1;
			int pcCh = -1;
			m.onMidi = [&](const synthLib::SMidiEvent& _e) { if((_e.a & 0xf0) == 0xc0) { pcOut = _e.b; pcCh = _e.a & 15; } };
			rawPanel(m, 0x23, 0x01, 60); rawPanel(m, 0x20, 0x04, 60); rawPanel(m, 0x20, 0, 60); rawPanel(m, 0x23, 0, 200);
			std::printf("; PC out %d on channel %d\n", pcOut, pcCh + 1);
		}
		put(g0, true);
		for(const int u : {1, 127})
		{
			auto g = g0;
			g.unused = static_cast<uint8_t>(u);
			put(g, true);
			std::printf("  byte 1 = %d: TRIG 1 energy %.2f\n", u, trigEnergy());
		}
		put(g0, true);
		// Notes mapped to 16-31, per trig mode.
		for(const int tm : {0, 1, 2})
		{
			auto g = g0;
			g.trigMode = static_cast<uint8_t>(tm);
			put(g, true);
			m.send(ed::mdLoadPattern(10)); m.run(100);
			m.send({static_cast<uint8_t>(0x90 | g0.baseChannel), 65, 100}); m.run(300);
			const int onPat = status(m, ed::MdStatus::Pattern);
			const bool pl = playing();
			m.send({static_cast<uint8_t>(0x80 | g0.baseChannel), 65, 0}); m.run(300);
			const bool pl2 = playing();
			press(m, md::PanelControl::Stop); press(m, md::PanelControl::Stop); m.run(200);
			std::printf("  trig mode %d: note 65 (map %d) -> pattern %d, playing %d, after note off %d\n", tm, g0.keymap[65], onPat, pl ? 1 : 0, pl2 ? 1 : 0);
		}
		put(g0, true);
		// Trig mode (MAP EDITOR TRIG: GATE/START/QUE) for a note mapped to a pattern: which notes map patterns?
		for(int n = 0; n < 128; ++n)
			if(g0.keymap[n] >= 16 && g0.keymap[n] != 0xff) { std::printf("  first non-track map: note %d -> %d\n", n, g0.keymap[n]); break; }
	}

	// ---- keys: which panel keys does the firmware take, when? ----

	void keys(const Bytes& _rom)
	{
		std::puts("== probe: is a panel key taken during the start-up animation?");
		for(const double at : {10000.0, 11000.0, 12000.0, 12500.0, 12800.0, 13000.0, 13200.0, 14000.0})
		{
			Machine m(_rom, g_romName, {}, false);
			const auto start = m.now();
			m.runUntil(start + static_cast<uint64_t>(at * g_rate / 1000));
			const auto before = lcdHash(panelNow(m));
			press(m, md::PanelControl::Play);
			const bool moved = playheadMoves(m, 800);
			bool moved2 = moved;
			if(!moved)
			{
				press(m, md::PanelControl::Play);
				moved2 = playheadMoves(m, 800);
			}
			std::printf("  PLAY at %5.1f s: %s%s; LCD %s\n", at / 1000, moved ? "plays" : "ignored",
				moved ? "" : moved2 ? ", the second PLAY plays" : ", the second PLAY too",
				lcdHash(panelNow(m)) != before ? "changed" : "same");
		}
	}

	// ---- chain: BANK held + TRIG keys ----

	std::vector<size_t> findSeq(const Bytes& _hay, const Bytes& _needle)
	{
		std::vector<size_t> at;
		auto it = _hay.begin();
		while((it = std::search(it, _hay.end(), _needle.begin(), _needle.end())) != _hay.end())
		{
			at.push_back(static_cast<size_t>(it - _hay.begin()));
			++it;
		}
		return at;
	}

	void printDiff(const char* _what, const Bytes& _a, const Bytes& _b, const size_t _max = 60)
	{
		size_t n = 0;
		std::printf("  %s:", _what);
		for(size_t i = 0; i < _a.size(); ++i)
		{
			if(_a[i] == _b[i])
				continue;
			if(n++ < _max)
				std::printf(" %06zx:%02x>%02x", i + 0x200000, _a[i], _b[i]);
		}
		std::printf(" (%zu bytes)\n", n);
	}

	// Hold a bank key, press TRIG keys in order, release the bank key.
	void chainKeys(Machine& _m, const uint8_t _bankMask, const std::vector<int>& _trigs)
	{
		rawPanel(_m, 0x23, _bankMask, 60);
		for(const int t : _trigs)
		{
			rawPanel(_m, static_cast<uint8_t>(0x20 + (t >> 3)), static_cast<uint8_t>(1u << (t & 7)), 60);
			rawPanel(_m, static_cast<uint8_t>(0x20 + (t >> 3)), 0, 60);
		}
		rawPanel(_m, 0x23, 0, 60);
	}

	// The pattern the sequencer plays at each wrap, for _wraps wraps.
	std::vector<int> wraps(Machine& _m, const int _wraps, const double _limitMs = 30000)
	{
		std::vector<int> seen;
		int last = _m.playhead();
		double t = 0;
		while(static_cast<int>(seen.size()) < _wraps && t < _limitMs)
		{
			_m.run(10);
			t += 10;
			const int s = _m.playhead();
			if(s < last)
			{
				_m.run(30);
				seen.push_back(status(_m, ed::MdStatus::Pattern));
			}
			last = s;
		}
		return seen;
	}

	void printList(const char* _what, const std::vector<int>& _v)
	{
		std::printf("  %s:", _what);
		for(const int p : _v)
			std::printf(" %s", p >= 0 ? ed::mdPatternName(static_cast<uint8_t>(p)).c_str() : "?");
		std::printf("\n");
	}

	ed::MdPattern shortPattern(const ed::MdPattern& _base, const uint8_t _slot, const int _track)
	{
		auto p = _base;
		p.position = _slot;
		p.length = 16;
		p.trigs.fill(0);
		p.lockMasks.fill(0);
		p.trigs[static_cast<size_t>(_track)] = 0x1111;
		return p;
	}

	void chain(const Bytes& _rom)
	{
		std::puts("== probe: pattern chaining (hold BANK, press TRIG keys)");
		Machine m(_rom, g_romName);
		const auto base = readPattern(m, 0);
		require(base.has_value(), "no pattern A01");
		// Short patterns, a different track each, so wraps come quickly.
		for(uint8_t s = 0; s < 8; ++s)
			m.send(ed::encodeMdPattern(shortPattern(*base, s, s)));
		m.send(ed::mdLoadPattern(0));
		m.run(200);
		std::printf("  current %d\n", status(m, ed::MdStatus::Pattern));
		const auto idle = m.snapshotRam();
		m.run(300);
		const auto idle2 = m.snapshotRam();

		// Stopped: A03, A05, A02.
		chainKeys(m, 0x01, {2, 4, 1});
		m.run(300);
		const auto chained = m.snapshotRam();
		std::printf("  status after the chain keys (stopped): pattern %d\n", status(m, ed::MdStatus::Pattern));
		std::printf("  LCD after the chain keys:\n");
		printLcd(panelNow(m));
		{
			Bytes noise(idle.size());
			for(size_t i = 0; i < idle.size(); ++i)
				noise[i] = idle[i] != idle2[i];
			size_t n = 0;
			std::printf("  changed by the chain (without idle noise):");
			for(size_t i = 0; i < idle.size(); ++i)
				if(!noise[i] && idle2[i] != chained[i] && n++ < 80)
					std::printf(" %06zx:%02x>%02x", i + 0x200000, idle2[i], chained[i]);
			std::printf(" (%zu)\n", n);
		}
		for(const auto& seq : std::vector<Bytes>{{2, 4, 1}, {0, 2, 4, 1}, {2, 4}, {4, 1}})
		{
			std::printf("  sequence");
			for(const auto b : seq)
				std::printf(" %d", b);
			std::printf(" at:");
			for(const auto a : findSeq(chained, seq))
				if(findSeq(Bytes(idle2.begin() + static_cast<std::ptrdiff_t>(a), idle2.begin() + static_cast<std::ptrdiff_t>(a + seq.size())), seq).empty())
					std::printf(" 0x%06zx(new)", a + 0x200000);
			std::printf("\n");
		}
		press(m, md::PanelControl::Play);
		m.run(50);
		printList("playing, pattern at each wrap", wraps(m, 7));
		stopMachine(m);
		std::printf("  after STOP: pattern %d\n", status(m, ed::MdStatus::Pattern));

		// While playing: chain A06, A07.
		m.send(ed::mdLoadPattern(0));
		m.run(100);
		press(m, md::PanelControl::Play);
		m.run(200);
		const auto before = m.snapshotRam();
		chainKeys(m, 0x01, {5, 6});
		const auto after = m.snapshotRam();
		printDiff("RAM by the chain while playing (with noise)", before, after, 40);
		printList("chain A06 A07 while playing, pattern at each wrap", wraps(m, 6));
		// A SysEx LOAD PATTERN while chained.
		m.send(ed::mdLoadPattern(7));
		printList("after SysEx LOAD PATTERN A08", wraps(m, 4));
		stopMachine(m);

		// STOP with a queued next pattern, then PLAY.
		m.send(ed::mdLoadPattern(0));
		m.run(100);
		press(m, md::PanelControl::Play);
		m.run(300);
		chainKeys(m, 0x01, {3});
		m.run(100);
		stopMachine(m);
		press(m, md::PanelControl::Play);
		m.run(300);
		std::printf("  queued A04, STOP, PLAY: plays %d\n", status(m, ed::MdStatus::Pattern));
		stopMachine(m);
		// One key only: is that a normal select?
		m.send(ed::mdLoadPattern(0));
		m.run(100);
		press(m, md::PanelControl::Play);
		m.run(200);
		chainKeys(m, 0x01, {1});
		printList("one TRIG with BANK held, while playing", wraps(m, 4));
		stopMachine(m);
		// Bank group LEDs.
		const auto p = panelNow(m);
		std::printf("  bank group LEDs: A-D %d E-H %d\n", p.getModeLed(md::FrontPanel::ModeLed::BankGroupAD),
			p.getModeLed(md::FrontPanel::ModeLed::BankGroupEH));
		press(m, md::PanelControl::BankGroup);
		m.run(100);
		const auto q = panelNow(m);
		std::printf("  after BANK GROUP: A-D %d E-H %d\n", q.getModeLed(md::FrontPanel::ModeLed::BankGroupAD),
			q.getModeLed(md::FrontPanel::ModeLed::BankGroupEH));
		press(m, md::PanelControl::BankGroup);
		m.run(100);
	}

	// Chaining gestures: which one does the firmware take?
	void chainVariants(const Bytes& _rom)
	{
		std::puts("== probe: chaining gestures");
		Machine m(_rom, g_romName);
		const auto base = readPattern(m, 0);
		require(base.has_value(), "no pattern A01");
		for(uint8_t s = 0; s < 8; ++s)
			m.send(ed::encodeMdPattern(shortPattern(*base, s, s)));
		const auto trigRow = [](const int _t) { return static_cast<uint8_t>(0x20 + (_t >> 3)); };
		const auto trigBit = [](const int _t) { return static_cast<uint8_t>(1u << (_t & 7)); };
		struct Variant { const char* name; std::function<void()> run; };
		const std::vector<Variant> variants = {
			{"bank held, TRIG 3 5 2 pressed one by one, 150 ms", [&]
			{
				rawPanel(m, 0x23, 0x01, 150);
				for(const int t : {2, 4, 1}) { rawPanel(m, trigRow(t), trigBit(t), 150); rawPanel(m, trigRow(t), 0, 150); }
				rawPanel(m, 0x23, 0, 150);
			}},
			{"bank held, TRIG 3 5 2 held together (cumulative)", [&]
			{
				rawPanel(m, 0x23, 0x01, 100);
				uint8_t mask = 0;
				for(const int t : {2, 4, 1}) { mask |= trigBit(t); rawPanel(m, 0x20, mask, 150); }
				rawPanel(m, 0x20, 0, 100);
				rawPanel(m, 0x23, 0, 100);
			}},
			{"TRIG 3 held, bank pressed, TRIG 5 2 added", [&]
			{
				rawPanel(m, 0x23, 0x01, 100);
				rawPanel(m, 0x20, trigBit(2), 150);
				rawPanel(m, 0x23, 0, 100);
				rawPanel(m, 0x20, trigBit(2) | trigBit(4), 150);
				rawPanel(m, 0x20, trigBit(2) | trigBit(4) | trigBit(1), 150);
				rawPanel(m, 0x20, 0, 100);
			}},
			{"bank tapped (sticky), then TRIG 3 5 2 one by one", [&]
			{
				rawPanel(m, 0x23, 0x01, 60);
				rawPanel(m, 0x23, 0, 100);
				for(const int t : {2, 4, 1}) { rawPanel(m, trigRow(t), trigBit(t), 100); rawPanel(m, trigRow(t), 0, 100); }
			}},
		};
		for(const auto& v : variants)
		{
			m.send(ed::mdLoadPattern(0));
			m.run(100);
			press(m, md::PanelControl::Play);
			m.run(300);
			v.run();
			m.run(50);
			std::printf("  %s: status %d\n", v.name, status(m, ed::MdStatus::Pattern));
			printLcd(panelNow(m));
			printList("    pattern at each wrap", wraps(m, 7));
			stopMachine(m);
			std::printf("    after STOP: %d\n", status(m, ed::MdStatus::Pattern));
		}
	}

	// Hold BANK, hold TRIG keys together in order (the gesture chain2 found), release.
	void chainHeld(Machine& _m, const uint8_t _bankMask, const std::vector<int>& _trigs)
	{
		rawPanel(_m, 0x23, _bankMask, 100);
		uint8_t rows[2] = {0, 0};
		for(const int t : _trigs)
		{
			rows[t >> 3] |= static_cast<uint8_t>(1u << (t & 7));
			rawPanel(_m, static_cast<uint8_t>(0x20 + (t >> 3)), rows[t >> 3], 150);
		}
		rawPanel(_m, 0x20, 0, 60);
		rawPanel(_m, 0x21, 0, 60);
		rawPanel(_m, 0x23, 0, 100);
	}

	void chainRam(const Bytes& _rom)
	{
		std::puts("== probe: where is the chain?");
		Machine m(_rom, g_romName);
		const auto base = readPattern(m, 0);
		require(base.has_value(), "no pattern A01");
		for(uint8_t s = 0; s < 16; ++s)
			m.send(ed::encodeMdPattern(shortPattern(*base, s, s & 15)));
		m.send(ed::mdLoadPattern(0));
		m.run(200);
		const std::vector<std::vector<int>> chains = {{2, 4, 1}, {6, 3}, {9, 12, 0, 5}, {14, 2, 7, 1, 11}};
		std::vector<Bytes> snaps;
		press(m, md::PanelControl::Play);
		m.run(200);
		auto before = m.snapshotRam();
		{
			const auto pr = m.hardware().copyPatchRam();
			before.insert(before.end(), pr.begin(), pr.end());
		}
		for(const auto& c : chains)
		{
			chainHeld(m, 0x01, c);
			m.run(100);
			auto sn = m.snapshotRam();
			const auto pr = m.hardware().copyPatchRam();
			sn.insert(sn.end(), pr.begin(), pr.end());
			snaps.push_back(std::move(sn));
			std::printf("  chain");
			for(const int p : c) std::printf(" %s", ed::mdPatternName(static_cast<uint8_t>(p)).c_str());
			std::printf(": status %d\n", status(m, ed::MdStatus::Pattern));
		}
		for(const int k : {0, 1, 0x80})
		{
			for(const bool reversed : {false, true})
			{
				std::map<size_t, int> hits;
				for(size_t i = 0; i < chains.size(); ++i)
				{
					Bytes needle;
					for(const int p : chains[i]) needle.push_back(static_cast<uint8_t>(p + k));
					if(reversed) std::reverse(needle.begin(), needle.end());
					for(const auto a : findSeq(snaps[i], needle))
						++hits[a];
				}
				for(const auto& [a, n] : hits)
					if(n >= 3)
						std::printf("  list (+%d%s) in %d of 4 chains at 0x%06zx\n", k, reversed ? ", reversed" : "", n, a + 0x200000);
			}
			for(const size_t stride : {size_t(1), size_t(2), size_t(4), size_t(8), size_t(16)})
			{
				std::map<size_t, int> hits;
				for(size_t i = 0; i < chains.size(); ++i)
				{
					const auto& c = chains[i];
					for(size_t b = 0; b + stride * 8 < snaps[i].size(); ++b)
					{
						bool ok = true;
						for(size_t j = 0; j < c.size() && ok; ++j)
							ok = snaps[i][b + j * stride] == static_cast<uint8_t>(c[j] + k);
						if(ok) ++hits[b];
					}
				}
				for(const auto& [a, n] : hits)
					if(n >= 2)
						std::printf("  strided list (stride %zu, +%d) in %d of 4 chains at 0x%06zx\n", stride, k, n, a + 0x200000);
			}
			// order[p] table: base + p holds p's position in the chain (+k).
			for(size_t b = 0; b + 128 < snaps[0].size(); ++b)
			{
				int n = 0;
				for(size_t i = 0; i < chains.size(); ++i)
				{
					const auto& c = chains[i];
					bool ok = true;
					for(size_t j = 0; j < c.size() && ok; ++j)
						ok = snaps[i][b + static_cast<size_t>(c[j])] == static_cast<uint8_t>(j + k);
					n += ok;
				}
				if(n >= 3)
					std::printf("  order table (+%d) in %d of 4 chains at 0x%06zx\n", k, n, b + 0x200000);
			}
			// next[p] table: base + p holds the next pattern (+k).
			for(size_t b = 0; b + 128 < snaps[0].size(); ++b)
			{
				int n = 0;
				for(size_t i = 0; i < chains.size(); ++i)
				{
					const auto& c = chains[i];
					bool ok = true;
					for(size_t j = 0; j < c.size() && ok; ++j)
						ok = snaps[i][b + static_cast<size_t>(c[j])] == static_cast<uint8_t>(c[(j + 1) % c.size()] + k);
					n += ok;
				}
				if(n >= 3)
					std::printf("  next table (+%d) in %d of 4 chains at 0x%06zx\n", k, n, b + 0x200000);
			}
		}
		// Bytes that differ between every two chains and are stable in between (bounded list).
		size_t shown = 0;
		std::printf("  bytes that changed with every chain:");
		for(size_t a = 0; a < before.size() && shown < 80; ++a)
		{
			bool all = before[a] != snaps[0][a];
			for(size_t i = 1; i < snaps.size() && all; ++i)
				all = snaps[i][a] != snaps[i - 1][a];
			if(!all)
				continue;
			++shown;
			std::printf(" %06zx:%02x", a + 0x200000, before[a]);
			for(const auto& sn : snaps)
				std::printf(">%02x", sn[a]);
		}
		std::printf("\n");
	}

	// Two identical runs, one with a chain gesture, one with the same keys but only one TRIG:
	// the difference at the same frame is the chain.
	void chainDiff(const Bytes& _rom, const std::vector<int>& _a, const std::vector<int>& _b)
	{
		std::puts("== probe: chain by deterministic difference");
		std::vector<Bytes> out;
		for(const auto* c : {&_a, &_b})
		{
			Machine m(_rom, g_romName);
			const auto base = readPattern(m, 0);
			for(uint8_t s = 0; s < 16; ++s)
				m.send(ed::encodeMdPattern(shortPattern(*base, s, s & 15)));
			m.send(ed::mdLoadPattern(0));
			m.run(200);
			press(m, md::PanelControl::Play);
			m.run(200);
			chainHeld(m, 0x01, *c);
			m.run(20);
			Bytes sn;
			for(uint32_t a = 0x310000; a < 0x400000; ++a) sn.push_back(m.read8(a));
			for(uint32_t a = 0x1000000; a < 0x1010000; ++a) sn.push_back(m.read8(a));
			out.push_back(std::move(sn));
		}
		size_t n = 0;
		for(size_t a = 0; a < out[0].size(); ++a)
			if(out[0][a] != out[1][a] && n++ < 200)
				std::printf("  0x%06zx: %02x | %02x\n", a < 0xf0000 ? a + 0x310000 : a - 0xf0000 + 0x1000000, out[0][a], out[1][a]);
		std::printf("  %zu bytes differ\n", n);
	}

	void dumpChainRegion(Machine& _m, const char* _what)
	{
		std::printf("  %-28s", _what);
		for(uint32_t a = 0x1001f40; a < 0x1001fb0; ++a)
			std::printf("%s%02x", (a & 7) == 0 ? " " : "", _m.read8(a));
		std::printf("\n");
	}

	void chainRegion(const Bytes& _rom)
	{
		std::puts("== probe: the chain region 0x2a0200");
		Machine m(_rom, g_romName);
		const auto base = readPattern(m, 0);
		for(uint8_t s = 0; s < 32; ++s)
			m.send(ed::encodeMdPattern(shortPattern(*base, s, s & 15)));
		m.send(ed::mdLoadPattern(0));
		m.run(200);
		dumpChainRegion(m, "idle A01");
		press(m, md::PanelControl::Play);
		m.run(200);
		dumpChainRegion(m, "playing A01");
		chainHeld(m, 0x01, {2, 4, 1});
		dumpChainRegion(m, "chain A03 A05 A02");
		int last = m.playhead();
		for(int w = 0; w < 4;)
		{
			m.run(10);
			const int s = m.playhead();
			if(s < last) { ++w; m.run(30); char b[40]; std::snprintf(b, sizeof(b), "wrap %d (pattern %d)", w, status(m, ed::MdStatus::Pattern)); dumpChainRegion(m, b); }
			last = s;
		}
		chainHeld(m, 0x01, {6, 3});
		dumpChainRegion(m, "chain A07 A04");
		m.send(ed::mdLoadPattern(9));
		m.run(50);
		dumpChainRegion(m, "SysEx LOAD A10");
		stopMachine(m);
		dumpChainRegion(m, "stopped");
		press(m, md::PanelControl::Play);
		m.run(100);
		chainHeld(m, 0x02, {0, 15, 8});
		dumpChainRegion(m, "chain B01 B16 B09");
		chainHeld(m, 0x01, {0});
		dumpChainRegion(m, "one key A01");
		stopMachine(m);
		dumpChainRegion(m, "stopped");
	}

	// The firmware sends F0 00 20 3C 02 00 40 <key> <state> F7 for panel keys. Does it take them?
	void remoteKeys(const Bytes& _rom)
	{
		std::puts("== probe: key SysEx 0x40 out and in");
		Machine m(_rom, g_romName);
		std::vector<Bytes> rx;
		m.onSysex = [&](const Bytes& _b) { rx.push_back(_b); };
		const auto show = [&](const char* _what)
		{
			std::printf("  %-22s ->", _what);
			for(const auto& b : rx)
				if(b.size() == 10 && b[6] == 0x40)
					std::printf(" %02x:%02x", b[7], b[8]);
			std::printf("\n");
			rx.clear();
		};
		for(const auto& [name, c] : std::vector<std::pair<const char*, md::PanelControl>>{{"PLAY", md::PanelControl::Play}, {"STOP", md::PanelControl::Stop},
			{"RECORD", md::PanelControl::Record}, {"FUNCTION", md::PanelControl::Function}, {"KIT", md::PanelControl::Kit}, {"TRIG 1", md::PanelControl::Trigger1},
			{"TRIG 16", md::PanelControl::Trigger16}, {"BANK A/E", md::PanelControl::BankA}, {"BANK GROUP", md::PanelControl::BankGroup}, {"ENTER", md::PanelControl::Enter}, {"EXIT", md::PanelControl::Exit}})
		{
			press(m, c);
			show(name);
		}
		stopMachine(m);
		rx.clear();
		// In: PLAY down / up as the firmware sends them.
		m.send({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x40, 0x09, 0x7f, 0xf7});
		m.run(40);
		m.send({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x40, 0x09, 0x3f, 0xf7});
		std::printf("  SysEx PLAY in: %s\n", playheadMoves(m, 800) ? "plays" : "ignored");
	}

	// ---- mutes: CC 12-15, the MUTE window ----

	void mutes(const Bytes& _rom)
	{
		std::puts("== probe: where are the track mutes?");
		Machine m(_rom, g_romName);
		const auto muteCc = [&](const int _track, const bool _on)
		{
			m.send({static_cast<uint8_t>(0xb0 | (_track >> 2)), static_cast<uint8_t>(12 + (_track & 3)), static_cast<uint8_t>(_on ? 1 : 0)});
			m.run(50);
		};
		const auto a = m.snapshotRam();
		m.run(200);
		const auto a2 = m.snapshotRam();
		const auto quiet = [&](const Bytes& _x, const Bytes& _y, const char* _what)
		{
			size_t n = 0;
			std::printf("  %s:", _what);
			for(size_t i = 0; i < _x.size(); ++i)
				if(a[i] == a2[i] && _x[i] != _y[i] && n++ < 40)
					std::printf(" %06zx:%02x>%02x", i + 0x200000, _x[i], _y[i]);
			std::printf(" (%zu)\n", n);
		};
		muteCc(2, true);
		const auto b = m.snapshotRam();
		quiet(a2, b, "mute CC track 3");
		muteCc(9, true);
		const auto c = m.snapshotRam();
		quiet(b, c, "mute CC track 10");
		muteCc(2, false);
		const auto d = m.snapshotRam();
		quiet(c, d, "unmute CC track 3");
		muteCc(9, false);
		m.run(100);
		// Patch RAM too.
		const auto pa = m.hardware().copyPatchRam();
		muteCc(14, true);
		const auto pb = m.hardware().copyPatchRam();
		size_t pn = 0;
		for(size_t i = 0; i < pa.size(); ++i)
			if(pa[i] != pb[i] && pn++ < 20)
				std::printf("  patch RAM %06zx:%02x>%02x\n", i, pa[i], pb[i]);
		muteCc(14, false);

		// The MUTE window: FUNCTION + A/E, then TRIG 5.
		const auto e = m.snapshotRam();
		rawPanel(m, 0x24, 0x02, 60);	// FUNCTION
		rawPanel(m, 0x23, 0x01, 60);	// + A/E
		rawPanel(m, 0x23, 0x00, 60);
		rawPanel(m, 0x24, 0x00, 200);
		std::printf("  LCD after FUNCTION + A/E:\n");
		printLcd(panelNow(m));
		const auto f = m.snapshotRam();
		quiet(e, f, "MUTE window open");
		rawPanel(m, 0x20, 0x10, 60);
		rawPanel(m, 0x20, 0x00, 200);
		const auto g = m.snapshotRam();
		quiet(f, g, "TRIG 5 in the MUTE window");
		printLcd(panelNow(m));
		// Held: FUNCTION + TRIG 6, 7, then release FUNCTION.
		rawPanel(m, 0x24, 0x02, 60);
		rawPanel(m, 0x20, 0x20, 60);
		rawPanel(m, 0x20, 0x00, 60);
		const auto h = m.snapshotRam();
		quiet(g, h, "FUNCTION held + TRIG 6 (prepared)");
		rawPanel(m, 0x24, 0x00, 200);
		const auto i = m.snapshotRam();
		quiet(h, i, "FUNCTION released");
		press(m, md::PanelControl::Exit);
		m.run(200);
	}

	void mutesCheck(const Bytes& _rom)
	{
		std::puts("== probe: patch RAM 0x0723ef + track as the mute of each track");
		Machine m(_rom, g_romName);
		const auto show = [&](const char* _what)
		{
			const auto pr = m.hardware().copyPatchRam();
			std::printf("  %-40s", _what);
			for(int t = 0; t < 16; ++t)
				std::printf(" %02x", pr[0x0723ef + t]);
			std::printf("  | 0x7723e0..: ");
			for(int t = 0; t < 16; ++t)
				std::printf("%02x", pr[0x0723e0 + t]);
			std::printf("\n");
		};
		const auto muteCc = [&](const int _track, const bool _on)
		{
			m.send({static_cast<uint8_t>(0xb0 | (_track >> 2)), static_cast<uint8_t>(12 + (_track & 3)), static_cast<uint8_t>(_on ? 1 : 0)});
			m.run(50);
		};
		show("idle");
		muteCc(0, true); show("CC mute 1");
		muteCc(2, true); show("CC mute 3");
		muteCc(15, true); show("CC mute 16");
		muteCc(2, false); show("CC unmute 3");
		rawPanel(m, 0x24, 0x02, 60); rawPanel(m, 0x23, 0x01, 60); rawPanel(m, 0x23, 0x00, 60); rawPanel(m, 0x24, 0x00, 200);
		show("MUTE window open");
		rawPanel(m, 0x20, 0x10, 60); rawPanel(m, 0x20, 0x00, 200); show("window TRIG 5");
		rawPanel(m, 0x21, 0x02, 60); rawPanel(m, 0x21, 0x00, 200); show("window TRIG 10");
		rawPanel(m, 0x20, 0x01, 60); rawPanel(m, 0x20, 0x00, 200); show("window TRIG 1 (unmute)");
		rawPanel(m, 0x24, 0x02, 60); rawPanel(m, 0x20, 0x20, 60); rawPanel(m, 0x20, 0x00, 60); show("FUNCTION held + TRIG 6");
		rawPanel(m, 0x24, 0x00, 200); show("FUNCTION released");
		press(m, md::PanelControl::Exit); m.run(200); show("EXIT");
		m.send(ed::mdLoadPattern(3)); m.run(200); show("LOAD PATTERN A04");
		m.send(ed::mdLoadKit(5)); m.run(200); show("LOAD KIT 6");
	}

	void mutesSearch(const Bytes& _rom)
	{
		std::puts("== probe: search RAM for the mute set");
		Machine m(_rom, g_romName);
		const auto muteCc = [&](const int _track, const bool _on)
		{
			m.send({static_cast<uint8_t>(0xb0 | (_track >> 2)), static_cast<uint8_t>(12 + (_track & 3)), static_cast<uint8_t>(_on ? 1 : 0)});
			m.run(60);
		};
		std::vector<std::pair<uint32_t, Bytes>> states;	// mask, main RAM + patch RAM
		const auto snap = [&](const uint32_t _mask)
		{
			auto r = m.snapshotRam();
			const auto p = m.hardware().copyPatchRam();
			r.insert(r.end(), p.begin(), p.end());
			states.emplace_back(_mask, std::move(r));
		};
		snap(0);
		muteCc(0, true); snap(0x0001);
		muteCc(2, true); snap(0x0005);
		muteCc(15, true); snap(0x8005);
		muteCc(0, false); snap(0x8004);
		muteCc(9, true); snap(0x8204);
		const auto rev16 = [](uint32_t _v) { uint32_t r = 0; for(int i = 0; i < 16; ++i) if(_v & (1u << i)) r |= 1u << (15 - i); return r; };
		const auto name = [](size_t _a) { return _a < 0x100000 ? _a + 0x200000 : _a - 0x100000 + 0x700000; };
		const size_t n = states[0].second.size();
		for(size_t a = 0; a + 16 < n; ++a)
		{
			bool be = true, le = true, rbe = true, rle = true, bytes = true, invBytes = true;
			for(const auto& [mask, r] : states)
			{
				const uint32_t wbe = (r[a] << 8) | r[a + 1], wle = (r[a + 1] << 8) | r[a];
				be &= wbe == mask; le &= wle == mask; rbe &= wbe == rev16(mask); rle &= wle == rev16(mask);
				for(int t = 0; t < 16 && (bytes || invBytes); ++t)
				{
					const bool muted = mask & (1u << t);
					bytes &= (r[a + t] != 0) == muted;
					invBytes &= (r[a + t] == 0) == muted;
				}
			}
			if(be || le || rbe || rle)
				std::printf("  16-bit mask at 0x%06zx (%s)\n", name(a), be ? "big-endian" : le ? "little-endian" : rbe ? "big-endian, bit 15 = track 1" : "little-endian, bit 15 = track 1");
			if(bytes)
				std::printf("  byte per track at 0x%06zx (non-zero = muted)\n", name(a));
			if(invBytes)
				std::printf("  byte per track at 0x%06zx (zero = muted)\n", name(a));
		}
		// And the MUTE window: does it write the same place? Open it, TRIG 5.
		rawPanel(m, 0x24, 0x02, 60); rawPanel(m, 0x23, 0x01, 60); rawPanel(m, 0x23, 0x00, 60); rawPanel(m, 0x24, 0x00, 200);
		rawPanel(m, 0x20, 0x10, 60); rawPanel(m, 0x20, 0x00, 200);
		snap(0x8214);
		const auto& r = states.back().second;
		for(size_t a = 0; a + 16 < n; ++a)
		{
			bool be = true, bytes = true;
			for(const auto& [mask, rr] : states)
			{
				be &= static_cast<uint32_t>((rr[a] << 8) | rr[a + 1]) == mask;
				for(int t = 0; t < 16 && bytes; ++t)
					bytes &= (rr[a + t] != 0) == ((mask & (1u << t)) != 0);
			}
			if(be || bytes)
				std::printf("  also after the MUTE window TRIG 5: 0x%06zx\n", name(a));
		}
		(void)r;
	}

	// ---- library: kit and pattern slot operations the design agent listed ----

	void library(const Bytes& _rom)
	{
		std::puts("== probe: kit library and pattern chooser operations");
		Machine m(_rom, g_romName);
		const int kit = status(m, ed::MdStatus::Kit);
		const int pat = status(m, ed::MdStatus::Pattern);
		std::printf("  current kit %d, pattern %d, extended %d\n", kit, pat, status(m, ed::MdStatus::LockMode));
		const auto k0 = readKit(m, static_cast<uint8_t>(kit));
		require(k0.has_value(), "kit read");

		// Save As: SAVE KIT n makes n current?
		m.send(ed::mdSetKitName("SAVEAS PROBE"));
		m.send(ed::mdSaveKit(40));
		m.run(100);
		std::printf("  SAVE KIT 41: current kit %d\n", status(m, ed::MdStatus::Kit) + 1);
		const auto k40 = readKit(m, 40);
		std::printf("  slot 41 name \"%.16s\"\n", k40 ? reinterpret_cast<const char*>(k40->name.data()) : "?");
		const auto p0 = readPattern(m, static_cast<uint8_t>(pat));
		std::printf("  pattern %d kit link after SAVE KIT 41: %d\n", pat + 1, p0 ? p0->kit + 1 : -1);

		// LOAD KIT in EXTENDED: relinked?
		m.send(ed::mdLoadKit(12));
		m.run(100);
		const auto p1 = readPattern(m, static_cast<uint8_t>(pat));
		std::printf("  LOAD KIT 13: current kit %d, pattern link %d\n", status(m, ed::MdStatus::Kit) + 1, p1 ? p1->kit + 1 : -1);

		// A kit dump into the current slot: heard without LOAD KIT?
		auto k12 = readKit(m, 12);
		require(k12.has_value(), "kit 13");
		auto changed = *k12;
		changed.params[0][0] = static_cast<uint8_t>(k12->params[0][0] ^ 0x15);
		changed.name[0] = 'Z';
		m.send(ed::encodeMdKit(changed));
		m.run(100);
		const auto pr = m.hardware().copyPatchRam();
		std::printf("  kit dump into the current slot: working param %02x (dump %02x, before %02x)\n",
			pr[0x0a + ed::g_mdWorkingKitParamsOffset], changed.params[0][0], k12->params[0][0]);
		m.send(ed::mdLoadKit(12));
		m.run(100);
		const auto pr2 = m.hardware().copyPatchRam();
		std::printf("  after LOAD KIT: working param %02x\n", pr2[0x0a + ed::g_mdWorkingKitParamsOffset]);

		// Immediate switch while playing: STOP, LOAD PATTERN, PLAY.
		m.send(ed::mdLoadPattern(0));
		m.run(100);
		press(m, md::PanelControl::Play);
		m.run(500);
		const auto t0 = m.now();
		press(m, md::PanelControl::Stop);
		m.send(ed::mdLoadPattern(5));
		m.run(20);
		press(m, md::PanelControl::Play);
		m.run(100);
		std::printf("  STOP, LOAD A06, PLAY: status %d, playing %d, %.0f ms\n", status(m, ed::MdStatus::Pattern) + 1,
			playheadMoves(m, 500) ? 1 : 0, ms(m.now() - t0));
		stopMachine(m);

		// Pattern dump over the current pattern with another kit link: kit loaded?
		const int kitNow = status(m, ed::MdStatus::Kit);
		auto cur = readPattern(m, 5);
		require(cur.has_value(), "pattern A06");
		auto relinked = *cur;
		relinked.kit = static_cast<uint8_t>((kitNow + 7) % 64);
		m.send(ed::encodeMdPattern(relinked));
		m.run(200);
		std::printf("  dump over the current pattern linking kit %d: current kit %d (was %d)\n", relinked.kit + 1,
			status(m, ed::MdStatus::Kit) + 1, kitNow + 1);
		m.send(ed::mdLoadPattern(5));
		m.run(200);
		std::printf("  then LOAD PATTERN A06 again: current kit %d\n", status(m, ed::MdStatus::Kit) + 1);
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdP4ProbeFirmwareTest <ROM> [boot|keys|chain|mutes|library]");
		return 77;
	}
	try
	{
		g_romName = _argv[1];
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		const std::string only = _argc > 2 ? _argv[2] : "";
		if(only.empty() || only == "boot")
			boot(rom);
		if(only.empty() || only == "keys")
			keys(rom);
		if(only.empty() || only == "chain")
			chain(rom);
		if(only == "bootflag")
			bootFlag(rom, _argc > 3 ? patchRamFromState(_argv[3], rom) : Bytes{});
		if(only == "bootcheck")
			bootCheck(rom, _argc > 3 ? patchRamFromState(_argv[3], rom) : Bytes{});
		if(only == "mutes2")
			mutesCheck(rom);
		if(only == "mutes3")
			mutesSearch(rom);
		if(only == "chain3")
			chainRam(rom);
		if(only == "chain4")
		{
			chainDiff(rom, {2, 4, 1}, {2, 4, 3});
			chainDiff(rom, {2, 4, 1}, {2, 6, 1});
			chainDiff(rom, {2, 4, 1}, {2, 4, 1});
		}
		if(only == "chain5")
			chainRegion(rom);
		if(only == "remotekeys")
			remoteKeys(rom);
		if(only == "flaguse")
			flagUse(rom);
		if(only == "bootui")
			bootUi(rom, _argc > 3 ? patchRamFromState(_argv[3], rom) : Bytes{});
		if(only == "lockwindow")
		{
			lockWindow(rom, false);
			lockWindow(rom, true);
		}
		if(only == "globals")
			globals(rom);
		if(only == "chain2")
			chainVariants(rom);
		if(only.empty() || only == "mutes")
			mutes(rom);
		if(only.empty() || only == "library")
			library(rom);
		std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
		return g_failures ? 1 : 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdP4ProbeFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
