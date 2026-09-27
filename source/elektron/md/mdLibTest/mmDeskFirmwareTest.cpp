// MM-P2 smoke test: the Monomachine Editor's desk (mmDesk::Desk) against the
// real MM OS 1.32B firmware, headless. The same Desk code as the plug-in; the
// Port here drives an emulated machine. Manual: needs a user-supplied ROM.
//
//   mmDeskFirmwareTest <MM-ROM>
//
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmValidate.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mmtelemetry.h"

#include "mdDesk/mdDeskPacer.h"
#include "mmDesk/mmDesk.h"
#include "mmDesk/mmRecv.h"

#include <cmath>
#include <deque>
#include <map>

using namespace mdFirmwareSession;
namespace ed = elektronData;
using ed::json::Value;

namespace
{
	int g_failures = 0;
	constexpr auto g_mm = md::MachineModel::Monomachine;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

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
		case K::Global: return C::Kit;	// with FUNCTION
		case K::Record: return C::Record;
			case K::LiveRecord: return C::Play;	// with RECORD held
		case K::MuteWindow: return C::BankGroup;	// with FUNCTION
		case K::Trig9: case K::Trig10: case K::Trig11: case K::Trig12: case K::Trig13: case K::Trig14:
			return static_cast<C>(static_cast<int>(C::Trigger1) + 8 + (static_cast<int>(_k) - static_cast<int>(K::Trig9)));
		}
		return std::nullopt;
	}

	struct Rig
	{
		Machine m;
		std::deque<Bytes> out;							// MIDI to send, in order
		std::deque<std::pair<uint64_t, md::PanelPacket>> panel;	// row states at machine frames
		std::vector<Value> page;
		std::unique_ptr<mmDesk::Desk> desk;
		md::MmTelemetry tel;

		// _hw: the desk drives the machine as a real Monomachine over MIDI (MM-P4): DIN speed both
		// ways, no panel keys, no telemetry, no memory. The panel queue stays, for the "user".
		explicit Rig(const Bytes& _rom, const bool _hw = false) : m(_rom, "mm", {}, true, g_mm), hw(_hw)
		{
			mmDesk::Desk::Port port;
			port.sendSysex = [this](const Bytes& _b) { if(hw) pacer.push(_b); else out.push_back(_b); };
			port.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				const md::automation::ParameterChange c{_p, _t, _i, _v};
				if(const auto cc = md::automation::encodeParameterChange(g_mm, c, 0))
					send({(*cc)[0], (*cc)[1], (*cc)[2]});
			};
			port.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				send({0xb0, 99, _t});
				send({0xb0, 98, _p});
				send({0xb0, 6, _v});
			};
			port.pressKeys = [this](const std::vector<mmDesk::Key>& _keys)
			{
				if(hw)
				{
					// Over MIDI only PLAY and STOP: MIDI Start / Stop.
					if(_keys.size() != 1 || (_keys[0] != mmDesk::Key::Play && _keys[0] != mmDesk::Key::Stop))
						return false;
					pacer.push({static_cast<uint8_t>(_keys[0] == mmDesk::Key::Play ? 0xfa : 0xfc)});
					return true;
				}
				return userKeys(_keys);
			};
			port.toPage = [this](const Value& _v) { page.push_back(_v); };
			port.nowMs = [this] { return ms(); };
			desk = std::make_unique<mmDesk::Desk>(port);
			if(hw)
				desk->setHardwareLink(true);
			m.onSysex = [this](const Bytes& _b)
			{
				if(unplugged)
					return;
				if(hw)
					inPacer.push(_b);	// the machine's MIDI out is DIN too
				else
					replies.push_back(_b);
			};
			m.onBlock = [this]
			{
				while(!panel.empty() && panel.front().first <= m.now())
				{
					m.hardware().trySendPanelEvent(panel.front().second.row, panel.front().second.mask);
					panel.pop_front();
				}
			};
		}

		bool hw = false;
		bool unplugged = false;
		mdDesk::DinPacer pacer, inPacer;
		size_t hwBytesIn = 0;
		size_t hwBytesOut = 0;

		void send(Bytes _b) { if(hw) pacer.push(std::move(_b)); else out.push_back(std::move(_b)); }

		// Keys on the machine's panel: the desk's (emulator) or a person's (HW MIDI tests).
		bool userKeys(const std::vector<mmDesk::Key>& _keys)
		{
			{
				uint64_t at = std::max(m.now(), panel.empty() ? 0 : panel.back().first) + 64;
				const auto hold = static_cast<uint64_t>(g_rate / 100);	// 10 ms
				const auto fn = *md::panelPacket(g_mm, md::PanelControl::Function);
				const auto rec = *md::panelPacket(g_mm, md::PanelControl::Record);
				for(const auto k : _keys)
				{
					const auto c = control(k);
					const auto pk = *md::panelPacket(g_mm, *c);
					if(mmDesk::isChord(k))
					{
						md::PanelRowState rows;
						for(const auto s : {rows.press(k == mmDesk::Key::LiveRecord ? rec : fn), rows.press(pk), rows.release(pk), rows.release(k == mmDesk::Key::LiveRecord ? rec : fn)})
						{
							panel.emplace_back(at, s);
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
		}

		std::vector<Bytes> replies;
		double ms() const { return m.now() * 1000.0 / g_rate; }

		mmDesk::Telemetry readTelemetry()
		{
			tel.publish([this](const uint32_t _a) { return m.read8(_a); });
			mmDesk::Telemetry t;
			t.valid = true;
			t.step = tel.step.load();
			t.running = tel.running.load() == 1;
			t.screen = tel.screen.load();
			t.recvCount = tel.recvCount.load();
			t.recvErrors = tel.recvErrors.load();
			t.recvActive = tel.recvActive.load() == 1;
			t.tempo = tel.tempo.load();
			t.mutes = tel.mutes.load();
			t.recording = tel.recording.load();
			return t;
		}

		// 10 ms of desk ticks and machine time.
		void run(const double _ms)
		{
			const auto end = m.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(m.now() < end)
			{
				if(!hw)
					desk->onTelemetry(readTelemetry());
				else
					readTelemetry();	// for the test's own checks
				for(auto& b : pacer.take(ms()))
				{
					hwBytesOut += b.size();
					if(!unplugged)
						out.push_back(std::move(b));
				}
				Bytes region;
				uint32_t seq = 0;
				if(tel.readWorkingKit(region, seq) && seq != m_lastSeq)
				{
					m_lastSeq = seq;
					desk->onWorkingKit(region);
				}
				desk->tick();
				while(!out.empty())
				{
					synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
					const auto& b = out.front();
					if(b[0] == 0xf0)
						e.sysex = b;
					else
					{
						e.a = b[0];
						e.b = b[1];
						e.c = b.size() > 2 ? b[2] : 0;
					}
					m.hardware().sendMidi(e);
					out.pop_front();
				}
				m.run(10);
				for(auto& b : inPacer.take(ms()))
				{
					hwBytesIn += b.size();
					replies.push_back(std::move(b));
				}
				auto r = std::move(replies);
				replies.clear();
				for(const auto& x : r)
					desk->onDeviceSysex(x);
			}
		}

		void msg(const std::string& _json) { desk->onPageMessage(*ed::json::parse(_json)); }

		Value lastResult() const
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result")
					return *it;
			return {};
		}

		Value lastMachine() const
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "machine")
					return *it->find("doc");
			return {};
		}

		double rms(const double _ms) const
		{
			const auto n = static_cast<size_t>(_ms * g_rate / 1000);
			const auto& l = m.left();
			const auto from = l.size() > n ? l.size() - n : 0;
			double e = 0;
			for(size_t i = from; i < l.size(); ++i)
				e += double(l[i]) * l[i];
			return std::sqrt(e / double(std::max<size_t>(1, l.size() - from)));
		}

		uint32_t m_lastSeq = 0;
	};

	void smoke(const Bytes& _rom)
	{
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setEngine(mmDesk::Desk::Engine::Ready);
		r.run(600);
		check(r.desk->currentPattern() == 0 && r.desk->currentKit() >= 0, "status: current pattern and kit");
		check(r.desk->pattern(0).has_value(), "the current pattern is loaded");
		check(r.desk->workingKit().has_value(), "the working kit comes from memory");
		const auto t0 = r.ms();
		while(r.desk->loaded() < 288 && r.ms() - t0 < 30000)
			r.run(100);
		std::printf("  all 288 documents loaded after %.0f ms\n", r.ms() - t0);
		check(r.desk->loaded() == 288, "every pattern, kit, song and global loads");

		// Play pattern 1 (a factory demo), then edit it through the desk.
		r.msg(R"({"op":"select","p":1})");
		r.run(300);
		r.msg(R"({"op":"play"})");
		r.run(1500);
		const double playing = r.rms(800);
		check(playing > 0.01, "pattern 1 plays (rms " + std::to_string(playing) + ")");
		auto p = *r.desk->pattern(1);
		for(size_t t = 0; t < 6; ++t)
			p.amp[t] = p.filter[t] = p.lfo[t] = p.pitch[t] = p.chord[t] = 0;
		for(auto& t : p.notes) t.fill(0xff);
		for(auto& m : p.lockMasks) m.fill(0);		// locks need trigs to sit on
		p.lockRowCount = 0;
		p.chordNoteCount = 0;
		std::printf("  before the edit: screen %08x recv %s\n", r.tel.screen.load(), r.desk->recv().stateName());
		const auto tEdit = r.ms();
		r.msg(R"({"op":"set","id":1,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		check(r.lastResult().find("ok")->asBool(), "the edit is accepted");
		if(!r.lastResult().find("ok")->asBool())
			std::printf("    %s\n", ed::json::write(r.lastResult()).c_str());
		double confirmed = -1;
		bool parkedThen = false;
		while(r.ms() - tEdit < 3000)
		{
			r.run(10);
			if(r.desk->lastRoundTripMs() > 0 && confirmed < 0)
			{
				confirmed = r.ms() - tEdit;
				parkedThen = r.desk->recv().parked();
			}
			if(std::getenv("MMDESK_TRACE") && (r.ms() - tEdit < 120 || static_cast<int>(r.ms() - tEdit) % 100 < 10))
				std::printf("    +%.0f recv %s screen %08x count %u\n", r.ms() - tEdit, r.desk->recv().stateName(), r.tel.screen.load(), r.tel.recvCount.load());
		}
		std::printf("  edit -> SYSEX RECV -> stored -> read back: %.0f ms (desk round trip %.0f ms)\n", confirmed, r.desk->lastRoundTripMs());
		check(confirmed > 0, "confirmed by read-back");
		check(parkedThen, "parked on SYSEX RECV when confirmed");
		const double cleared = r.rms(500);
		check(cleared < playing * 0.3, "the cleared pattern is heard while it plays (rms " + std::to_string(cleared) + ")");
		check(r.tel.running.load() == 1, "still playing");

		// Kit: a live CC edit and a structure edit (TRIG POS: dump + LOAD KIT).
		auto k = *r.desk->workingKit();
		const auto kitSlot = k.position;
		k.tracks[0].pages[2][0] = static_cast<uint8_t>(k.tracks[0].pages[2][0] ^ 0x20);	// FLT BASE
		r.msg(R"({"op":"set","id":2,"kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(400);
		check(r.desk->workingKit() && r.desk->workingKit()->tracks[0].pages[2][0] == k.tracks[0].pages[2][0],
			"a kit value reaches the working kit by CC");
		k.trigPos[2] = 1;
		r.msg(R"({"op":"set","id":3,"kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		const auto note = r.lastResult().find("note")->asString();
		r.run(1500);
		check(r.desk->workingKit() && r.desk->workingKit()->trigPos[2] == 1, "TRIG POS reaches the working kit (" + note + ")");
		check(r.desk->kit(kitSlot) && r.desk->kit(kitSlot)->trigPos[2] == 1, "and the stored slot");
		k.machines[3] = 3;	// SID
		r.msg(R"({"op":"set","id":4,"kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(500);
		check(r.desk->workingKit() && r.desk->workingKit()->machines[3] == 3, "a machine change by 0x5B");

		// Song 2: a row edit.
		auto s = *r.desk->song(2);
		s.rows[0].bytes = {};
		s.rows[0].bytes[0] = 7;
		s.rows[0].bytes[ed::mmSongRow::g_length] = 16;
		s.rows[0].bytes[22] = s.rows[0].bytes[23] = 0xff;
		s.rows[1].bytes = {};
		s.rows[1].bytes[0] = 0xff;
		r.msg(R"({"op":"set","id":5,"kind":"song","doc":)" + ed::json::write(ed::mmSongToJson(s)) + "}");
		r.run(800);
		check(r.desk->song(2) && r.desk->song(2)->rows[0].bytes[0] == 7, "a song dump through SYSEX RECV");

		// Idle: back to the main screen, still playing.
		r.run(2500);
		check(r.tel.screen.load() == mmDesk::g_screenMain, "left SYSEX RECV after the idle time");
		check(r.tel.running.load() == 1, "still playing after leaving");

		// Queue a pattern while playing.
		r.msg(R"({"op":"select","p":2})");
		r.run(200);
		bool sawQueued = false;
		for(auto it = r.page.rbegin(); it != r.page.rend(); ++it)
			if(it->find("type")->asString() == "machine")
			{
				sawQueued = it->find("doc")->find("pattern")->find("queued")->isNumber();
				break;
			}
		check(sawQueued, "the queued pattern is shown");
		const auto tq = r.ms();
		while(r.desk->currentPattern() != 2 && r.ms() - tq < 12000)
			r.run(50);
		std::printf("  queued switch after %.0f ms (at the end of the 64-step pattern)\n", r.ms() - tq);
		check(r.desk->currentPattern() == 2, "the queue switches at the pattern end");
		r.msg(R"({"op":"stop"})");
		r.run(600);
		check(r.tel.running.load() == 0, "STOP");
	}

	// Zero crossings per second / 2 over a window of the left channel.
	double frequency(const std::vector<float>& _l, const size_t _from, const size_t _n)
	{
		int crossings = 0;
		for(size_t i = _from + 1; i < _from + _n && i < _l.size(); ++i)
			crossings += (_l[i - 1] < 0) != (_l[i] < 0);
		return crossings * 0.5 * g_rate / static_cast<double>(_n);
	}

	double rmsAt(const std::vector<float>& _l, const size_t _from, const size_t _n)
	{
		double e = 0;
		for(size_t i = _from; i < _from + _n && i < _l.size(); ++i)
			e += double(_l[i]) * _l[i];
		return std::sqrt(e / static_cast<double>(_n));
	}

	// Trigless and pitchless trigs as the firmware plays them (a sine on track 1).
	void trigKinds(const Bytes& _rom)
	{
		std::puts("trig kinds");
		Rig r(_rom);
		r.msg(R"({"op":"ready"})");
		r.desk->setEngine(mmDesk::Desk::Engine::Ready);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);
		// Kit: T1 GND-SIN, a short amp envelope; the other tracks silent.
		auto k = *r.desk->workingKit();
		k.machines[0] = 1;
		k.tracks[0].pages[0] = {0, 0, 0, 0, 0, 0, 0, 64};		// GND-SIN: TUNE centre
		k.tracks[0].pages[1] = {0, 0, 24, 0, 0, 110, 64, 0};	// ATK HOLD DEC REL DIST VOL PAN PORT
		k.tracks[0].pages[2] = {0, 127, 0, 0, 0, 0, 64, 64};	// open filter
		k.tracks[0].pages[3] = {64, 64, 0, 0, 64, 0, 0, 0};		// no delay
		for(size_t pg = 4; pg < 7; ++pg)
			k.tracks[0].pages[pg][7] = 0;						// LFO depth 0
		for(size_t t = 1; t < 6; ++t)
			k.levels[t] = 0;
		k.levels[0] = 120;
		r.msg(R"({"op":"set","kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(800);
		{
			const auto& w = *r.desk->workingKit();
			std::printf("  working kit: T1 machine %d levels %d %d %d %d %d %d, AMP %d %d %d\n", w.machines[0], w.levels[0], w.levels[1],
				w.levels[2], w.levels[3], w.levels[4], w.levels[5], w.tracks[0].pages[1][0], w.tracks[0].pages[1][2], w.tracks[0].pages[1][5]);
		}
		// Pattern E01 (empty): 16 steps.
		auto p = *r.desk->pattern(64);
		p.length = 16;
		const auto full = [&](const size_t _s, const uint8_t _note)
		{
			p.pitch[0] |= ed::mmStepBit(_s);
			p.amp[0] |= ed::mmStepBit(_s);
			p.filter[0] |= ed::mmStepBit(_s);
			p.lfo[0] |= ed::mmStepBit(_s);
			p.notes[0][_s] = _note;
		};
		full(0, 48);							// C-3, a full trig
		p.pitch[0] |= ed::mmStepBit(4);			// step 5: trigless, pitch 72
		p.notes[0][4] = 72;
		p.pitch[0] |= ed::mmStepBit(8);			// step 9: pitchless (envelopes, no note)
		p.amp[0] |= ed::mmStepBit(8);
		p.filter[0] |= ed::mmStepBit(8);
		p.lfo[0] |= ed::mmStepBit(8);
		full(12, 60);							// step 13: C-4
		check(ed::validate(p).empty(), "the trig-kind pattern validates");
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(1500);
		check(r.desk->pattern(64) && ed::encodeMmPattern(*r.desk->pattern(64)) == ed::encodeMmPattern(p), "stored as sent");
		r.msg(R"({"op":"select","p":64})");
		r.run(300);
		r.msg(R"({"op":"tempo","bpm":133})");
		r.run(100);
		r.desk->onTelemetry(r.readTelemetry());
		check(r.readTelemetry().tempo == 133 * 24, "tempo 133 read back from RAM (0x2bc2a6)");
		r.msg(R"({"op":"tempo","bpm":120})");
		r.run(100);
		check(r.readTelemetry().tempo == 120 * 24, "tempo 120 read back from RAM");
		// Play; skip the first pass, then note where each step of the second pass starts.
		r.msg(R"({"op":"play"})");
		int last = -1, passes = 0;
		uint64_t stepStart[16]{};
		while(passes < 3)
		{
			r.run(2);
			const int st = r.tel.step.load();
			if(st == last || st < 0 || st > 15)
				continue;
			if(st == 0)
				++passes;
			if(passes == 2)
				stepStart[st] = r.m.now();
			last = st;
		}
		const auto& l = r.m.left();
		if(std::getenv("MMDESK_TRACE"))
			for(uint64_t f = stepStart[0]; f < stepStart[15] + g_rate / 8; f += g_rate / 80)
			{
				int st = 0;
				for(int k = 0; k < 16; ++k) if(stepStart[k] && stepStart[k] <= f) st = k;
				std::printf("    t %6.1f ms step %2d rms %.4f f %.0f\n", (f - stepStart[0]) * 1000.0 / g_rate, st,
					rmsAt(l, static_cast<size_t>(f), g_rate / 80), frequency(l, static_cast<size_t>(f), g_rate / 80));
			}
		// Measured (MM-P2-RESULT §3): an envelope's attack is heard about a step and 50 ms after
		// the playhead byte reaches its step; a pitch change on a trigless step is heard at once.
		const size_t slice = g_rate / 80;	// 12.5 ms
		const auto maxRms = [&](const uint64_t _a, const uint64_t _b)
		{
			double m = 0;
			for(auto f = _a; f + slice <= _b; f += slice)
				m = std::max(m, rmsAt(l, static_cast<size_t>(f), slice));
			return m;
		};
		const auto freq = [&](const uint64_t _a, const uint64_t _b)
		{
			return frequency(l, static_cast<size_t>(_a), static_cast<size_t>(_b - _a));
		};
		const auto step = g_rate / 8;	// 125 ms at 120 BPM
		const double before4 = rmsAt(l, static_cast<size_t>(stepStart[4] - slice), slice);
		const double trigless = maxRms(stepStart[5], stepStart[8]);	// after the pitch change
		const double fTrigless = freq(stepStart[5], stepStart[8]);
		const double before8 = rmsAt(l, static_cast<size_t>(stepStart[9] - slice), slice);
		const double pitchless = maxRms(stepStart[9], stepStart[11]);
		const double fPitchless = freq(stepStart[10], stepStart[12]);
		const double fFull = freq(stepStart[14], stepStart[15] + step);
		const double fullRms = maxRms(stepStart[13], stepStart[15]);
		std::printf("  trigless step 5: rms %.4f before, max %.4f after, %.0f Hz | pitchless step 9: %.4f -> %.4f, %.0f Hz | full step 13: max %.4f, %.0f Hz\n",
			before4, trigless, fTrigless, before8, pitchless, fPitchless, fullRms, fFull);
		check(trigless < before4 * 1.2, "a trigless trig fires no envelope: the note keeps decaying");
		check(std::abs(fTrigless - 520) < 60, "a trigless trig still changes the pitch (to C-5)");
		check(pitchless > before8 * 2.5, "a pitchless trig fires the envelopes");
		check(std::abs(fPitchless - fTrigless) < 60, "a pitchless trig keeps the pitch before it");
		check(fullRms > before8 * 2.5 && std::abs(fFull - fTrigless) > 150, "a full trig plays its own note");
	}
}


	// MM-P4: POLY, MIDI track mutes, RECORD, MULTI TRIG and PORTAMENTO in the kit, the MULTI MAP.
	void p4(const Bytes& _rom)
	{
		std::puts("p4");
		Rig r(_rom);
		std::map<int, int> noteOns;	// channel -> note ons from the machine's MIDI out
		r.m.onMidiEvent = [&](const synthLib::SMidiEvent& _e, uint64_t) { if((_e.a & 0xf0) == 0x90 && _e.c) ++noteOns[_e.a & 0x0f]; };
		r.msg(R"({"op":"ready"})");
		r.desk->setEngine(mmDesk::Desk::Engine::Ready);
		r.run(600);
		while(r.desk->loaded() < 288)
			r.run(100);

		// POLY: SET STATUS 0x20, read back by status.
		r.msg(R"({"op":"poly","on":1})");
		r.run(1500);
		check(r.lastMachine().find("poly")->isBool() && r.lastMachine().find("poly")->asBool(), "POLY on (SET STATUS 0x20, status reads 1)");
		r.msg(R"({"op":"poly","on":0})");
		r.run(1500);
		check(!r.lastMachine().find("poly")->asBool(), "POLY off again");

		// A pattern with MIDI track 1 notes every 4 steps (E01).
		auto p = *r.desk->pattern(64);
		p.length = 16;
		for(size_t t = 0; t < 6; ++t)
			p.amp[t] = p.filter[t] = p.lfo[t] = p.pitch[t] = p.chord[t] = p.midiTrig[t] = p.midiNote[t] = 0;
		for(auto& m : p.lockMasks) m.fill(0);
		p.lockRowCount = 0;
		p.chordNoteCount = 0;
		p.midiNoteCount = 0;
		for(uint8_t st = 0; st < 16; st += 4)
		{
			p.midiTrig[0] |= ed::mmStepBit(st);
			p.midiNote[0] |= ed::mmStepBit(st);
			p.midiNotes[p.midiNoteCount++] = ed::mmNoteEntryWord({0, st, 60});
		}
		const auto problems = ed::validate(p);
		check(problems.empty(), "the MIDI pattern validates" + (problems.empty() ? std::string() : ": " + problems.front()));
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(1500);
		r.msg(R"({"op":"select","p":64})");
		r.run(300);
		const auto g = *r.desk->global(static_cast<uint8_t>(std::max(0, r.desk->currentGlobal())));
		const int ch = g.midiSeqChannels[0] & 15;
		r.msg(R"({"op":"play"})");
		r.run(2000);
		noteOns.clear();
		r.run(2000);
		const int before = noteOns[ch];
		check(before >= 3, "MIDI track 1 plays on channel " + std::to_string(ch + 1) + " (" + std::to_string(before) + " notes in 2 s)");
		r.msg(R"({"op":"muteMidi","t":0,"on":1})");
		r.run(800);
		check(((r.tel.mutes.load() >> 6) & 1) == 1, "MIDI track 1 muted through the MUTE window (RAM 0x2bfedd)");
		noteOns.clear();
		r.run(2000);
		check(noteOns[ch] == 0, "muted: no notes on its channel (" + std::to_string(noteOns[ch]) + ")");
		r.msg(R"({"op":"muteMidi","t":0,"on":0})");
		r.run(800);
		noteOns.clear();
		r.run(2000);
		check(((r.tel.mutes.load() >> 6) & 1) == 0 && noteOns[ch] >= 3, "unmuted: its notes again (" + std::to_string(noteOns[ch]) + ")");
		// Synth track mute: CC 3 through the mute parameter; the RAM shows it.
		r.msg(R"({"op":"mute","t":2,"on":1})");
		r.run(300);
		check(((r.tel.mutes.load() >> 2) & 1) == 1, "synth track 3 muted (CC 3, RAM 0x2bfeb5)");
		r.msg(R"({"op":"mute","t":2,"on":0})");
		r.run(300);

		// RECORD while playing: a note on track 1's channel is recorded into the pattern.
		const auto trigsBefore = r.desk->pattern(64)->pitch[0];
		r.msg(R"({"op":"record","mode":"live"})");
		r.run(500);
		check(r.tel.recording.load() == 2, "LIVE RECORDING on (RECORD + PLAY; RAM 0x2bff01)");
		const uint8_t base = g.baseChannel & 15;
		r.out.push_back({static_cast<uint8_t>(0x90 | base), 67, 100});
		r.run(120);
		r.out.push_back({static_cast<uint8_t>(0x80 | base), 67, 0});
		r.run(1500);
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(2500);
		check(r.tel.recording.load() == 0, "recording off (RECORD twice)");
		const auto trigsAfter = r.desk->pattern(64)->pitch[0];
		check(trigsAfter != trigsBefore, "the live-recorded note is in the pattern read back (T1 trigs " + std::to_string(trigsBefore) + " -> " + std::to_string(trigsAfter) + ")");
		r.msg(R"({"op":"stop"})");
		r.run(500);
		// GRID RECORDING (stopped): the machine's TRIG keys write steps; the desk reads them back.
		r.msg(R"({"op":"record","mode":"grid"})");
		r.run(500);
		check(r.tel.recording.load() == 1, "GRID RECORDING on (RECORD; RAM 0x26bbb3)");
		const auto gridBefore = r.desk->pattern(64)->pitch[0];
		{
			const auto pk = *md::panelPacket(g_mm, md::PanelControl::Trigger12);
			r.panel.emplace_back(r.m.now() + 64, pk);
			r.panel.emplace_back(r.m.now() + 64 + 441, md::PanelPacket{pk.row, 0});
		}
		r.run(2500);
		check(r.desk->pattern(64)->pitch[0] == (gridBefore ^ ed::mmStepBit(11)), "a TRIG key in GRID RECORDING is read back while recording");
		r.msg(R"({"op":"record","mode":"off"})");
		r.run(600);
		check(r.tel.recording.load() == 0, "GRID RECORDING off");

		// MULTI TRIG and PORTAMENTO: kit fields without a live message (dump + LOAD KIT).
		auto k = *r.desk->workingKit();
		k.multiTrigMode = 1;
		k.multiTrigTiming = 3;
		k.splitKey = 48;
		k.splitTrack = 2;
		k.portamentoMask = static_cast<uint8_t>(k.portamentoMask & ~1);
		r.msg(R"({"op":"set","kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(3000);
		const auto& w = *r.desk->workingKit();
		check(w.multiTrigMode == 1 && w.multiTrigTiming == 3 && w.splitKey == 48 && w.splitTrack == 2, "MULTI TRIG mode, timing and zones in the working kit");
		check((w.portamentoMask & 1) == 0, "PORTAMENTO ONLY LEGATO on track 1 in the working kit");

		// MULTI MAP: range 1 offset 2, length 8, transpose -3, timing 4/16, pattern A03.
		auto gl = g;
		gl.multiMap[1][0] = 2;
		gl.multiMap[2][0] = 2;
		gl.multiMap[3][0] = 8;
		gl.multiMap[4][0] = static_cast<uint8_t>(-3);
		gl.multiMap[5][0] = 3;
		r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(gl)) + "}");
		r.run(3000);
		check(r.desk->global(gl.position) && *r.desk->global(gl.position) == gl, "MULTI MAP fields stored and read back");
	}

	// MM-P4: HW MIDI. The desk drives the emulated machine as if it were a real one on MIDI:
	// DIN speed, no panel keys, no telemetry. The test plays the person at the machine.
	void hwLink(const Bytes& _rom)
	{
		std::puts("hw");
		Rig r(_rom, true);
		r.msg(R"({"op":"ready"})");
		const auto t0 = r.ms();
		while(r.ms() - t0 < 8000 && !(r.desk->currentPattern() >= 0 && r.desk->pattern(static_cast<uint8_t>(r.desk->currentPattern()))
			&& r.desk->workingKit()))
			r.run(50);
		check(r.lastMachine().find("hw")->asString() == "ready", "connected: the machine answers status requests");
		check(r.desk->workingKit().has_value(), "the current pattern and its kit arrive over DIN (" + std::to_string(int(r.ms() - t0)) + " ms)");
		const auto tk = r.ms();
		while(r.ms() - tk < 180000)
		{
			bool all = true;
			for(uint8_t i = 0; i < 128 && all; ++i)
				all = r.desk->kit(i).has_value();
			if(all)
				break;
			r.run(200);
		}
		std::printf("  all 128 kits after %.1f s at DIN speed\n", (r.ms() - tk) / 1000);

		// A kit value goes out as a CC; the machine plays it (its working kit in RAM).
		auto k = *r.desk->workingKit();
		k.tracks[0].pages[1][5] = static_cast<uint8_t>(k.tracks[0].pages[1][5] == 90 ? 91 : 90);
		r.msg(R"({"op":"set","kind":"kit","doc":)" + ed::json::write(ed::mmKitToJson(k)) + "}");
		r.run(300);
		Bytes region;
		uint32_t seq = 0;
		r.tel.readWorkingKit(region, seq);
		check(region.size() > 5 && region[5 + 0x11 + 8 + 5] == k.tracks[0].pages[1][5], "a kit value as a CC reaches the machine's working kit");

		// A pattern edit waits for SYSEX RECV, which only the person can open.
		const auto slot = static_cast<uint8_t>(r.desk->currentPattern());
		auto p = *r.desk->pattern(slot);
		p.amp[0] ^= ed::mmStepBit(15);
		p.pitch[0] |= p.amp[0] & ed::mmStepBit(15);
		if(!(p.amp[0] & ed::mmStepBit(15)))
			p.pitch[0] &= ~ed::mmStepBit(15);
		p.filter[0] &= ~ed::mmStepBit(15);
		p.lfo[0] &= ~ed::mmStepBit(15);
		r.msg(R"({"op":"set","kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		r.run(500);
		check(r.lastMachine().find("recv")->find("state")->asString() == "waitingUser", "the dump waits for SYSEX RECV (SEND 1)");
		r.userKeys(mmDesk::RecvSession::enterMacro());
		r.run(1500);
		check(r.tel.recvActive.load() == 1, "the person opened GLOBAL > FILE > SYSEX RECV");
		const auto ts = r.ms();
		r.msg(R"({"op":"hwSend"})");
		while(r.ms() - ts < 10000 && r.desk->lastRoundTripMs() < 0)
			r.run(50);
		check(r.desk->lastRoundTripMs() > 0 && r.desk->pattern(slot)->amp[0] == p.amp[0], "sent and confirmed by read-back (" + std::to_string(int(r.ms() - ts)) + " ms)");
		r.userKeys(mmDesk::RecvSession::exitKeys());
		r.run(500);

		// POLY over MIDI: SET STATUS.
		r.msg(R"({"op":"poly","on":1})");
		r.run(2500);
		check(r.lastMachine().find("poly")->isBool() && r.lastMachine().find("poly")->asBool(), "POLY over MIDI (SET STATUS)");
		r.msg(R"({"op":"poly","on":0})");
		r.run(2500);
		// Keys that have no MIDI command say so.
		r.msg(R"({"op":"muteMidi","t":0,"on":1})");
		check(!r.lastResult().find("ok")->asBool(), "MIDI track mutes refused over MIDI, with the reason");
		r.msg(R"({"op":"record","on":1})");
		check(!r.lastResult().find("ok")->asBool(), "RECORD refused over MIDI, with the reason");
		// PLAY as MIDI Start: ignored while CONTROL IN TRANSPORT is IGNORE (the factory setting).
		r.msg(R"({"op":"play"})");
		r.run(1500);
		check(r.tel.running.load() == 0, "MIDI Start is ignored with TRANSPORT IGNORE (global 0x06 = 0)");
		// The editor sets TRANSPORT ACCEPT in the active global (a dump, so through SYSEX RECV).
		auto g = *r.desk->global(static_cast<uint8_t>(r.desk->currentGlobal()));
		g.transportIn = 1;
		r.msg(R"({"op":"set","kind":"global","doc":)" + ed::json::write(ed::mmGlobalToJson(g)) + "}");
		r.run(300);
		r.userKeys(mmDesk::RecvSession::enterMacro());
		r.run(1500);
		r.msg(R"({"op":"hwSend"})");
		r.run(3000);
		r.userKeys(mmDesk::RecvSession::exitKeys());
		r.run(800);
		check(r.desk->global(g.position)->transportIn == 1, "TRANSPORT ACCEPT stored (read back)");
		r.msg(R"({"op":"play"})");
		r.run(1500);
		check(r.tel.running.load() == 1, "PLAY as MIDI Start plays the machine");
		r.msg(R"({"op":"stop"})");
		r.run(800);
		check(r.tel.running.load() == 0, "STOP as MIDI Stop");

		// Unplugged, then back.
		r.unplugged = true;
		r.run(5000);
		check(r.lastMachine().find("hw")->asString() == "none", "unplugged: HW NO MIDI");
		r.unplugged = false;
		r.run(2500);
		check(r.lastMachine().find("hw")->asString() == "ready", "back: HW MIDI");
		std::printf("  %zu bytes out, %zu bytes in, at DIN speed both ways\n", r.hwBytesOut, r.hwBytesIn);
	}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mmDeskFirmwareTest <MM-ROM>");
		return 77;
	}
	try
	{
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MM 1.32B image");
		const std::string only = _argc > 2 ? _argv[2] : "";
		if(only.empty() || only == "smoke")
			smoke(rom);
		if(only.empty() || only == "trigkinds")
			trigKinds(rom);
		if(only.empty() || only == "p4")
			p4(rom);
		if(only.empty() || only == "hw")
			hwLink(rom);
		std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
		return g_failures ? 1 : 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mmDeskFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
