// Pure tests for the Monomachine desk: the SYSEX RECV session and the desk
// against a scripted machine (no firmware). The firmware smoke test is
// mdLibTest/mmDeskFirmwareTest.

#include "mmDesk.h"
#include "mmDeskMachine.h"

#include "deskCore/deskContract.h"
#include "deskCore/deskKinds.h"
#include "deskHost/deskHost.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmValidate.h"

#include "elektronData/jsonSchema.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace
{
	namespace ed = elektronData;
	using Bytes = std::vector<uint8_t>;
	using ed::json::Value;
	int g_failures = 0;
	// Every message a desk published in these tests; main() checks them against the contract (P6).
	std::vector<Value> g_published;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	mmDesk::Telemetry screen(const mmDesk::Screen _s)
	{
		mmDesk::Telemetry t;
		t.valid = true;
		t.screen = _s;
		t.recvActive = _s == mmDesk::Screen::GlobalEdit;
		return t;
	}

	void recvTaking()
	{
		std::puts("RECV session: a dump being taken");
		mmDesk::RecvSession r;
		r.want({0xf0, 1, 0xf7});
		auto t = screen(mmDesk::Screen::GlobalEdit);
		t.recvCount = 7;
		r.tick(0, t);
		check(r.parked() && !r.taking(), "parked, nothing sent yet: not taking");
		auto out = r.tick(10, t);
		check(out.sends.size() == 1 && r.taking(), "the dump sent, the RECV count not moved: taking (a key now is lost)");
		t.recvErrors = 1;
		r.tick(20, t);
		check(!r.taking(), "a message counted (an error counts too): taken");
		r.want({0xf0, 2, 0xf7});
		r.tick(30, t);
		check(r.taking(), "another dump sent: taking again");
		t.recvCount = 8;
		r.tick(40, t);
		check(!r.taking() && r.parked(), "taken: keys work on SYSEX RECV, still parked");
	}

	void recvSession()
	{
		std::puts("RECV session");
		mmDesk::RecvSession r;
		auto out = r.tick(0, screen(mmDesk::Screen::Main));
		check(out.keys.empty() && r.state() == mmDesk::RecvSession::State::Idle, "nothing to send: stays idle");
		r.want({0xf0, 1, 0xf7});
		out = r.tick(10, screen(mmDesk::Screen::Main));
		check(out.keys == mmDesk::RecvSession::enterMacro() && r.state() == mmDesk::RecvSession::State::Entering, "from the main screen: the macro");
		out = r.tick(400, screen(mmDesk::Screen::Global));
		check(out.sends.empty(), "no send before SYSEX RECV");
		out = r.tick(700, screen(mmDesk::Screen::GlobalEdit));
		out = r.tick(710, screen(mmDesk::Screen::GlobalEdit));
		check(out.sends.size() == 1 && r.parked(), "parked: the dump goes out");
		out = r.tick(1500, screen(mmDesk::Screen::GlobalEdit));
		check(out.keys.empty() && r.parked(), "stays parked while quiet for less than the idle time");
		out = r.tick(4000, screen(mmDesk::Screen::GlobalEdit));
		check(out.keys == mmDesk::RecvSession::exitKeys() && r.state() == mmDesk::RecvSession::State::Leaving, "idle: EXIT back");
		r.tick(4300, screen(mmDesk::Screen::Main));
		check(r.state() == mmDesk::RecvSession::State::Idle, "back on the main screen");
		r.want({0xf0, 2, 0xf7});
		out = r.tick(4400, screen(mmDesk::Screen::Other));
		check(out.keys == mmDesk::RecvSession::exitKeys() && r.state() == mmDesk::RecvSession::State::ToMain, "from another screen: EXIT first");
		out = r.tick(4650, screen(mmDesk::Screen::Main));
		check(out.keys == mmDesk::RecvSession::enterMacro(), "then the macro");
		for(double t = 4700; t < 40000 && r.state() != mmDesk::RecvSession::State::Failed; t += 100)
			r.tick(t, screen(mmDesk::Screen::Main));
		check(r.state() == mmDesk::RecvSession::State::Failed, "a screen that never comes fails after three tries");
		check(mmDesk::RecvSession::enterMacro().size() == 29, "the macro is 29 keys (GLOBAL counts as one)");

		// Release review A1: a screen that never comes is given up (the tags say whose dumps they were) and the
		// panel is left alone; so is a dump that waits while telemetry is never valid.
		{
			mmDesk::RecvSession g;
			g.want({0xf0, 3, 0xf7}, 11);
			g.want({0xf0, 4, 0xf7}, 12);
			std::vector<uint32_t> gaveUp;
			double t = 0;
			int failures = 0;
			auto was = g.state();
			for(; t < 120000 && gaveUp.empty(); t += 100)
			{
				out = g.tick(t, screen(mmDesk::Screen::Main));
				gaveUp = out.gaveUp;
				failures += g.state() == mmDesk::RecvSession::State::Failed && was != mmDesk::RecvSession::State::Failed;
				was = g.state();
			}
			check(gaveUp == std::vector<uint32_t>({11, 12}), "a screen that never comes: the queue is given up, with its tags");
			check(failures == 2 && t < 40000, "after two failed tries (bounded)");
			check(g.queued() == 0, "nothing waits any more");
			size_t keys = 0;
			for(; t < 200000; t += 100)
				keys += g.tick(t, screen(mmDesk::Screen::Main)).keys.size();
			check(keys == 0 && g.state() == mmDesk::RecvSession::State::Idle, "then the panel is left alone");

			mmDesk::RecvSession v;
			v.want({0xf0, 5, 0xf7}, 21);
			mmDesk::Telemetry invalid;
			gaveUp.clear();
			for(t = 0; t < 120000 && gaveUp.empty(); t += 100)
				gaveUp = v.tick(t, invalid).gaveUp;
			check(gaveUp == std::vector<uint32_t>({21}) && t <= v.maxWaitMs + 200, "no valid telemetry: given up after maxWaitMs");

			// The give-up clocks are the machine's: an emulator that stands still (a DAW that stopped
			// processing) fails nothing; once it runs again they go on.
			mmDesk::RecvSession f;
			f.want({0xf0, 6, 0xf7}, 31);
			auto frozen = screen(mmDesk::Screen::Main);
			frozen.blocks = 7;
			size_t frozenKeys = 0;
			gaveUp.clear();
			frozenKeys += f.tick(0, frozen).keys.size();	// it runs: the macro goes
			frozen.blocks = 8;
			frozenKeys += f.tick(100, frozen).keys.size();
			for(t = 200; t < 600000 && gaveUp.empty(); t += 100)
			{
				out = f.tick(t, frozen);
				gaveUp = out.gaveUp;
				frozenKeys += out.keys.size();
			}
			check(gaveUp.empty() && f.queued() == 1 && frozenKeys == mmDesk::RecvSession::enterMacro().size(),
				"the emulator stands still for ten minutes: nothing is given up, no more keys");
			double resumed = t;
			for(; t < resumed + 120000 && gaveUp.empty(); t += 100)
			{
				++frozen.blocks;
				gaveUp = f.tick(t, frozen).gaveUp;
			}
			check(gaveUp == std::vector<uint32_t>({31}) && t - resumed < 40000, "running again: given up on the machine's time");

			// maxWaitMs is the oldest dump's age: a newer one does not restart it.
			mmDesk::RecvSession o;
			o.want({0xf0, 7, 0xf7}, 41);
			gaveUp.clear();
			for(t = 0; t < 120000 && gaveUp.empty(); t += 100)
			{
				if(t == 30000)
					o.want({0xf0, 8, 0xf7}, 42);
				gaveUp = o.tick(t, invalid).gaveUp;
			}
			check(gaveUp == std::vector<uint32_t>({41, 42}) && t <= o.maxWaitMs + 200, "the oldest dump's age: given up at maxWaitMs, the newer one with it");
		}
	}

	// A tiny scripted machine: keeps dumps per slot (only on SYSEX RECV), answers requests and status.
	struct FakeMachine
	{
		std::map<std::pair<uint8_t, uint8_t>, Bytes> slots;
		mmDesk::Screen screenWord = mmDesk::Screen::Main;
		int keysPressed = 0;
		uint8_t pattern = 3, kit = 5;
		uint32_t recvTaken = 0;	// dumps taken on SYSEX RECV (the RECV count, RAM 0x26a3c4)
		bool slow = false;		// dumps stay on their way (inFlight) until taken by hand
		int recording = -1;		// the recording mode the telemetry says (-1 unknown)
		std::vector<Bytes> replies;
		std::vector<Bytes> ignored;
		std::vector<Bytes> inFlight;

		std::map<int, int> seen;
		void take(const Bytes& _m)
		{
			if(_m.size() > 6) ++seen[_m[6]];
			if(_m.size() < 8 || _m[4] != 3)
				return;
			const auto cmd = _m[6];
			if(cmd == 0x67 || cmd == 0x52 || cmd == 0x69 || cmd == 0x50)
			{
				if(screenWord == mmDesk::Screen::GlobalEdit && slow)
				{
					inFlight.push_back(_m);
					return;
				}
				if(screenWord == mmDesk::Screen::GlobalEdit)
				{
					slots[{cmd, _m[9]}] = _m;
					++recvTaken;
				}
				else
					ignored.push_back(_m);
				return;
			}
			if(cmd == 0x68 || cmd == 0x53 || cmd == 0x6a || cmd == 0x51)
			{
				const auto it = slots.find({static_cast<uint8_t>(cmd - 1), _m[7]});
				if(it != slots.end())
					replies.push_back(it->second);
				return;
			}
			if(cmd == 0x57 && _m.size() > 7)
			{
				pattern = _m[7];	// LOAD PATTERN while stopped: the machine switches at once
				return;
			}
			if(cmd == 0x70)
			{
				const uint8_t v = _m[7] == 0x04 ? pattern : _m[7] == 0x02 ? kit : 0;
				replies.push_back({0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, _m[7], v, 0xf7});
			}
		}
	};

	ed::MmPattern emptyPattern(const uint8_t _slot)
	{
		ed::MmPattern p;
		p.position = _slot;
		for(auto& t : p.notes) t.fill(0xff);
		for(auto& r : p.lockRows) r.fill(0xff);
		p.midiNotes.fill(0xffff);
		p.chordNotes.fill(0xffff);
		for(size_t t = 0; t < 6; ++t)
			for(auto* a : {&p.arp, &p.midiArp})
			{
				a->speed[t] = 5;
				a->length[t] = 8;
				a->steps[t].fill(0x40);
			}
		return p;
	}

	void desk()
	{
		std::puts("desk against a scripted machine");
		FakeMachine m;
		for(uint8_t s = 0; s < 128; ++s)
		{
			m.slots[{0x67, s}] = ed::encodeMmPattern(emptyPattern(s));
			ed::MmKit k;
			k.position = s;
			k.machines.fill(1);
			k.trigPos.fill(0xff);
			m.slots[{0x52, s}] = ed::encodeMmKit(k);
		}
		for(uint8_t s = 0; s < 24; ++s)
		{
			ed::MmSong so;
			so.position = s;
			so.rows[0].bytes[0] = 0xff;
			// An END row as the firmware writes it: no tempo change (0xffff), the contract's null.
			so.rows[0].bytes[ed::mmSongRow::g_tempo] = so.rows[0].bytes[ed::mmSongRow::g_tempo + 1] = 0xff;
			m.slots[{0x69, s}] = ed::encodeMmSong(so);
		}
		for(uint8_t s = 0; s < 8; ++s)
		{
			ed::MmGlobal g;
			g.position = s;
			m.slots[{0x50, s}] = ed::encodeMmGlobal(g);
		}
		double now = 0;
		std::vector<Value> page;
		std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> params;
		mmDesk::Desk::Port port;
		port.device.sendSysex = [&](const Bytes& _b) { m.take(_b); };
		port.device.sendParam = [&](uint8_t _t, uint8_t _p, uint8_t _i, uint8_t _v) { params.emplace_back(_t, _p, _i, _v); };
		port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
		port.device.pressKeys = [&](const std::vector<mmDesk::Key>& _k)
		{
			m.keysPressed += static_cast<int>(_k.size());
			if(_k == mmDesk::RecvSession::enterMacro())
				m.screenWord = mmDesk::Screen::GlobalEdit;
			else if(_k == mmDesk::RecvSession::exitKeys())
				m.screenWord = mmDesk::Screen::Main;
			return true;
		};
		port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
		port.device.nowMs = [&] { return now; };
		auto noNotes = port;
		std::vector<std::array<uint8_t, 3>> notes;
		port.device.sendNote = [&](uint8_t _c, uint8_t _n, uint8_t _v) { notes.push_back({_c, _n, _v}); };
		mmDesk::Desk d(port);
		const auto run = [&](const double _ms)
		{
			for(double t = 0; t < _ms; t += 10)
			{
				now += 10;
				mmDesk::Telemetry tel = screen(m.screenWord);
				tel.recvCount = m.recvTaken;
				tel.recording = m.recording;
				d.onTelemetry(tel);
				d.tick();
				auto replies = std::move(m.replies);
				m.replies.clear();
				for(const auto& r : replies)
					d.onDeviceSysex(r);
			}
		};
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		msg(R"({"op":"set","kind":"pattern","doc":{}})");
		const auto lastResult = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result")
					return *it;
			return Value();
		};
		check(!lastResult().find("ok")->asBool(), "busy before the engine is ready");
		d.setProbe(mmDesk::Desk::Probe::Running);
		run(400);
		check(d.currentPattern() == 3 && d.currentKit() == 5, "status: pattern and kit");
		std::printf("  (loaded %zu, fake decode %d)", d.loaded(), (int)ed::decodeMmPattern(m.slots[{0x67, 3}]).has_value());
		for(const auto& [c, n] : m.seen) std::printf(" %02x:%d", c, n);
		std::printf("\n");
		check(d.pattern(3).has_value(), "the current pattern loaded first");

		// Edit pattern 3: add a trig. It goes out on SYSEX RECV and is confirmed.
		auto p = *d.pattern(3);
		p.pitch[0] = p.amp[0] = p.filter[0] = p.lfo[0] = 1;
		p.notes[0][0] = 60;
		const auto doc = ed::json::write(ed::mmPatternToJson(p));
		msg(R"({"op":"set","id":7,"kind":"pattern","doc":)" + doc + "}");
		check(lastResult().find("ok")->asBool(), "set pattern accepted");
		run(800);
		check(m.keysPressed >= 28 && d.recvParked(), "the desk drove the machine to SYSEX RECV");
		check(m.slots[{0x67, 3}] == ed::encodeMmPattern(p), "the machine holds the edited pattern");
		run(300);
		check(d.lastRoundTripMs() > 0, "read back and confirmed");
		run(3500);
		check(m.screenWord == mmDesk::Screen::Main && !d.recvParked(), "left SYSEX RECV when idle");

		// Bug 4 (the user journeys): a panel key pressed while the machine is still taking a dump on SYSEX RECV is lost
		// (mmDeskFirmwareTest parked). RECORD then is refused as busy (it was answered ok), and taken once the dump is in.
		{
			m.slow = true;
			m.recording = 0;
			p.notes[0][0] = 62;
			msg(R"({"op":"set","id":8,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
			for(int i = 0; i < 100 && m.inFlight.empty(); ++i)
				run(10);
			check(d.recvParked() && !m.inFlight.empty(), "parked, the dump on its way");
			const int keys0 = m.keysPressed;
			msg(R"({"op":"record","mode":"grid"})");
			const auto r1 = lastResult();
			check(!r1.find("ok")->asBool() && r1.find("errors")->asArray()[0].asString() == "The panel is busy (SYSEX RECV); try again."
				&& m.keysPressed == keys0, "RECORD while the dump is taken: refused as busy, no key pressed");
			// 0.3.4: PLAY then STOP now: accepted, they wait for the panel (the newest, STOP, goes once the dump is in)
			msg(R"({"op":"play"})");
			check(lastResult().find("ok")->asBool() && m.keysPressed == keys0, "PLAY while the dump is taken: accepted, waits (no key yet)");
			msg(R"({"op":"stop"})");
			check(lastResult().find("ok")->asBool() && m.keysPressed == keys0, "then STOP: accepted, waits in PLAY's place");
			m.slow = false;
			for(auto& b : std::exchange(m.inFlight, {}))
				m.take(b);
			run(20);
			check(m.keysPressed == keys0 + 1, "taken: the waiting STOP is pressed, once (PLAY is not)");
			run(200);
			msg(R"({"op":"record","mode":"grid"})");
			check(lastResult().find("ok")->asBool() && m.keysPressed == keys0 + 2 && d.recvParked(), "taken: RECORD is pressed on SYSEX RECV");
			m.recording = -1;
			run(3500);
		}

		// Invalid: 63 locked parameters.
		auto badDoc = ed::mmPatternToJson(p);
		Value locks = Value::array();
		for(int n = 0; n < 63; ++n)
		{
			Value l = Value::object();
			l.set("track", n / 64);
			l.set("page", (n / 8) % 8);
			l.set("param", n % 8);
			l.set("steps", Value::array());
			locks.push(std::move(l));
		}
		Value rebuilt = Value::object();
		for(const auto& [k, v] : badDoc.asObject())
			rebuilt.set(k, k == "locks" ? locks : v);
		const auto sentBefore = m.seen[0x67];
		msg(R"({"op":"set","id":8,"kind":"pattern","doc":)" + ed::json::write(rebuilt) + "}");
		run(100);
		check(m.seen[0x67] == sentBefore, "nothing sent for it");
		check(!lastResult().find("ok")->asBool(), "a document with more than 62 locks is refused, nothing sent");
		check(m.ignored.empty(), "no dump ever reached a normal screen");

		// Select while stopped.
		msg(R"({"op":"select","id":9,"p":10})");
		run(50);	// P6: the current pattern is what the machine reports, not what was asked for
		check(d.currentPattern() == 10, "select while stopped switches at once (status says so)");
		bool sawMachine = false;
		for(const auto& v : page)
			sawMachine |= v.find("type")->asString() == "machine";
		check(sawMachine, "machine documents are published");

		// The note intent (deskCore/deskNotes.h): a MIDI note on the track's own channel, 48 + pitch.
		const auto error = [&]() -> std::string
		{
			const auto r = lastResult();
			const auto* e = r.find("errors");
			return e && e->isArray() && !e->asArray().empty() ? e->asArray()[0].asString() : "";
		};
		msg(R"({"op":"noteOn","id":20,"t":2,"vel":90,"pitch":7})");
		check(lastResult().find("ok")->asBool() && notes.size() == 1 && notes[0] == std::array<uint8_t, 3>{2, 55, 90},
			("noteOn: T3 on base + 2, MIDI note 48 + 7 " + error()).c_str());
		msg(R"({"op":"noteOn","id":21,"t":2,"vel":80,"pitch":12})");
		check(notes.size() == 2 && notes[1] == std::array<uint8_t, 3>{2, 60, 80}, "noteOn: a second pitch on the track sounds with the first (POLY)");
		msg(R"({"op":"noteOn","id":22,"t":2,"vel":70,"pitch":7})");
		check(notes.size() == 4 && notes[2] == std::array<uint8_t, 3>{2, 55, 0} && notes[3] == std::array<uint8_t, 3>{2, 55, 70},
			"noteOn: the same pitch again ends its note first");
		msg(R"({"op":"noteOff","id":23,"t":2,"pitch":12})");
		check(lastResult().find("ok")->asBool() && notes.size() == 5 && notes[4] == std::array<uint8_t, 3>{2, 60, 0}, "noteOff: that pitch's note off");
		msg(R"({"op":"noteOff","id":24,"t":2})");
		check(notes.size() == 6 && notes[5] == std::array<uint8_t, 3>{2, 55, 0}, "noteOff without pitch: every note of the track");
		msg(R"({"op":"noteOff","id":25,"t":2,"pitch":7})");
		check(lastResult().find("ok")->asBool() && notes.size() == 6, "noteOff: nothing sounds, nothing sent");
		msg(R"({"op":"noteOn","id":26,"t":2,"vel":70,"pitch":-49})");
		check(!lastResult().find("ok")->asBool() && notes.size() == 6, "noteOn: a pitch out of range is refused by the table");
		{
			mmDesk::Desk none(noNotes);
			none.onPageMessage(*ed::json::parse(R"({"op":"ready"})"));
			none.setProbe(mmDesk::Desk::Probe::Running);
			none.onPageMessage(*ed::json::parse(R"({"op":"noteOn","id":27,"t":0,"vel":100,"pitch":0})"));
			check(!lastResult().find("ok")->asBool(), "noteOn: an engine without notes refuses");
		}
	}
}

// "playing" follows the step byte when the RAM running flag stays 0 (as in the plug-in).
void playingFromSteps()
{
	std::printf("playing from the step byte\n");
	double now = 0;
	std::vector<Value> page;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [](const Bytes&) {};
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [](const std::vector<mmDesk::Key>&) { return true; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	port.device.nowMs = [&] { return now; };
	mmDesk::Desk d(port);
	d.onPageMessage(*ed::json::parse(R"({"op":"ready"})"));
	d.setProbe(mmDesk::Desk::Probe::Running);
	const auto playing = [&]
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == "telemetry")
				return it->find("playing")->asBool();
		return false;
	};
	const auto feed = [&](const int _step, const double _ms)
	{
		mmDesk::Telemetry t = screen(mmDesk::Screen::Main);
		t.step = _step;
		t.tempo = 120 * 24;	// a 16th = 125 ms
		for(double e = 0; e < _ms; e += 10)
		{
			now += 10;
			d.onTelemetry(t);
			d.tick();
		}
	};
	feed(5, 500);
	feed(0, 600);
	check(!playing(), "a stop that resets the step is not playing");
	feed(1, 130);
	check(!playing(), "one step forward is not playing yet");
	feed(2, 130);
	check(playing(), "steps advancing: playing, with the running flag 0");
	feed(3, 400);
	d.tick();
	for(double e = 0; e < 300; e += 10) { now += 10; d.tick(); }
	feed(3, 60);
	check(!playing(), "the step stands still for three step times: stopped");
}

// Review finding 9: the adapter's small state values and their pure steps (mmDeskWatch.h).
void watchSteps()
{
	std::printf("the adapter's state values (mmDeskWatch.h)\n");
	// RECORD: read while recording, every _everyMs; once when it stops; the mode change is said once
	auto r = mmDesk::watchRecord({}, 1, true, 0, 1000);
	check(r.read && r.changed, "recording starts: read now, the mode changed");
	r = mmDesk::watchRecord(r.next, 1, true, 500, 1000);
	check(!r.read && !r.changed, "within the interval: no read");
	r = mmDesk::watchRecord(r.next, 1, true, 1001, 1000);
	check(r.read, "the interval passed: read again");
	r = mmDesk::watchRecord(r.next, 0, true, 1100, 1000);
	check(r.read && r.changed, "stopped recording: one last read");
	r = mmDesk::watchRecord(r.next, 0, true, 5000, 1000);
	check(!r.read && !r.changed, "off: no reads");
	check(!mmDesk::watchRecord({}, 1, false, 0, 1000).read, "no current pattern: nothing to read");
	// the transport message: at most every 25 ms, at once when the recording mode changed
	auto due = mmDesk::telemetryDue({}, 0, false, false, 0, 25);
	check(due && due->step == 0, "the first playhead goes out");
	check(!mmDesk::telemetryDue(*due, 1, false, false, 10, 25), "a step within 25 ms waits");
	check(mmDesk::telemetryDue(*due, 1, false, false, 30, 25).has_value(), "after 25 ms it goes");
	check(mmDesk::telemetryDue(*due, 0, false, true, 1, 25).has_value(), "a recording mode change goes at once");
	check(!mmDesk::telemetryDue(*due, 0, false, false, 100, 25), "nothing changed: nothing goes");
	// the keyboard's notes: the note off goes where the note on went; a repeat ends the first
	auto n = mmDesk::pressNote({}, 0, 7, 3, 55, 100);
	check(n.sends.size() == 1 && n.sends[0].channel == 3 && n.sends[0].note == 55 && n.sends[0].velocity == 100, "note on");
	n = mmDesk::pressNote(n.next, 0, 7, 4, 55, 90);
	check(n.sends.size() == 2 && n.sends[0].channel == 3 && n.sends[0].velocity == 0 && n.sends[1].channel == 4, "the same pitch again ends the first");
	n = mmDesk::pressNote(n.next, 0, 9, 4, 57, 90);
	n = mmDesk::releaseNotes(n.next, deskCore::NoteOff{0, 7});
	check(n.sends.size() == 1 && n.sends[0].channel == 4 && n.sends[0].note == 55 && n.next.size() == 1, "note off: that pitch, where it went");
	n = mmDesk::releaseNotes(n.next, deskCore::NoteOff{0, std::nullopt});
	check(n.sends.size() == 1 && n.next.empty(), "note off without pitch: every note of the track");
	// playing from the step byte: two moves forward within three step times
	mmDesk::Telemetry t;
	t.valid = true;
	t.tempo = 120 * 24;
	t.step = 4;
	auto w = mmDesk::watchStep({}, t, 0);
	check(w.stepped && !w.playing, "a first step is not playing");
	t.step = 5;
	w = mmDesk::watchStep(w.next, t, 125);
	check(!w.playing, "one move forward is not playing yet");
	t.step = 6;
	w = mmDesk::watchStep(w.next, t, 250);
	check(w.playing, "two moves forward: playing");
	w = mmDesk::watchStep(w.next, t, 700);
	check(!w.playing && !w.stepped, "standing still for three step times: stopped");
}

// B-027: the Monomachine loads the kit a pattern links when it takes a dump of the pattern that plays (measured,
// mmDeskFirmwareTest machine), so the unsaved edits of the kit that plays go again once the dump is in: here a machine
// change, its 0x5B a second time. A dump of another pattern reloads nothing.
void kitAfterPatternDump()
{
	std::puts("B-027: the kit's edits after a dump of the pattern that plays");
	FakeMachine m;
	for(uint8_t s = 0; s < 128; ++s)
	{
		auto p = emptyPattern(s);
		p.kit = 5;	// the kit that plays
		m.slots[{0x67, s}] = ed::encodeMmPattern(p);
		ed::MmKit k;
		k.position = s;
		k.machines.fill(1);
		k.trigPos.fill(0xff);
		m.slots[{0x52, s}] = ed::encodeMmKit(k);
	}
	for(uint8_t s = 0; s < 24; ++s)
	{
		ed::MmSong so;
		so.position = s;
		so.rows[0].bytes[0] = 0xff;
		so.rows[0].bytes[ed::mmSongRow::g_tempo] = so.rows[0].bytes[ed::mmSongRow::g_tempo + 1] = 0xff;
		m.slots[{0x69, s}] = ed::encodeMmSong(so);
	}
	for(uint8_t s = 0; s < 8; ++s)
	{
		ed::MmGlobal g;
		g.position = s;
		m.slots[{0x50, s}] = ed::encodeMmGlobal(g);
	}
	double now = 0;
	std::vector<Bytes> machines;	// the 0x5B messages, in order
	mmDesk::Desk::Port port;
	port.device.sendSysex = [&](const Bytes& _b)
	{
		if(_b.size() > 6 && _b[6] == 0x5b)
			machines.push_back(_b);
		m.take(_b);
	};
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [&](const std::vector<mmDesk::Key>& _k)
	{
		if(_k == mmDesk::RecvSession::enterMacro())
			m.screenWord = mmDesk::Screen::GlobalEdit;
		else if(_k == mmDesk::RecvSession::exitKeys())
			m.screenWord = mmDesk::Screen::Main;
		return true;
	};
	port.toPage = [&](const Value& _v) { g_published.push_back(_v); };
	port.device.nowMs = [&] { return now; };
	mmDesk::Desk d(port);
	const auto run = [&](const double _ms)
	{
		for(double t = 0; t < _ms; t += 10)
		{
			now += 10;
			mmDesk::Telemetry tel = screen(m.screenWord);
			tel.recvCount = m.recvTaken;
			d.onTelemetry(tel);
			d.tick();
			auto replies = std::move(m.replies);
			m.replies.clear();
			for(const auto& r : replies)
				d.onDeviceSysex(r);
		}
	};
	const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
	msg(R"({"op":"ready"})");
	d.setProbe(mmDesk::Desk::Probe::Running);
	run(600);
	check(d.currentPattern() == 3 && d.currentKit() == 5 && d.workingKit() && d.kit(5), "pattern 3 plays kit 5");
	msg(R"({"op":"machine","g":1,"k":5,"t":0,"model":3,"keepFx":true})");
	run(300);
	check(machines.size() == 1 && machines[0] == ed::mmAssignMachine(0, 3, 0), "the machine change: 0x5B");
	msg(R"({"op":"step","g":2,"p":3,"t":2,"s":7,"v":{"n":[60],"a":1,"f":1,"l":1}})");
	run(2000);
	check(ed::decodeMmPattern(m.slots[{0x67, 3}])->notes[2][7] == 60, "the step's dump taken on SYSEX RECV");
	check(machines.size() == 2 && machines[1] == machines[0], "then the machine change again, after the dump (the machine reloaded the kit)");
	for(int i = 0; i < 1000 && !d.pattern(4); ++i)
		run(10);	// the library's background read
	const auto sent = machines.size();
	msg(R"({"op":"step","g":3,"p":4,"t":2,"s":7,"v":{"n":[60],"a":1,"f":1,"l":1}})");
	run(2000);
	check(ed::decodeMmPattern(m.slots[{0x67, 4}])->notes[2][7] == 60 && machines.size() == sent, "a dump of another pattern: nothing again");
}

// LOAD KIT of a never-written slot (name byte 0 is 0xff) plays it as NEW KIT (measured, mmDeskFirmwareTest p4).
void kitAsLoaded()
{
	std::printf("a kit as LOAD KIT plays it\n");
	ed::MmKit k;
	k.name = {0xff, 0x00, 'L', 'T', 'I', ' ', 'S', 'I', 'X', '4', 0};
	const auto l = ed::mmKitAsLoaded(k);
	check(std::string(l.name.begin(), l.name.begin() + 7) == "NEW KIT" && l.name[7] == 0 && l.name[8] == 'X' && l.name[9] == '4',
		"a slot marked unused loads as NEW KIT, the rest of its name bytes as stored");
	k.name = {'B', 'A', 'S', 'S', 0};
	check(ed::mmKitAsLoaded(k) == k, "a written kit loads as it is");
}

// P6: the Control workspace's LFOs run in the plug-in on the machine's steps (deskCore's ModEngine,
// as the Machinedrum's): a link moves a kit value by CC, the page gets the source values, and
// nothing of it is an edit or an undo step.
void modulators()
{
	std::puts("app modulators in the plug-in");
	double now = 0;
	std::vector<Value> page;
	std::vector<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>> params;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [](const Bytes&) {};
	port.device.sendParam = [&](uint8_t _t, uint8_t _p, uint8_t _i, uint8_t _v) { params.emplace_back(_t, _p, _i, _v); };
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [](const std::vector<mmDesk::Key>&) { return true; };
	port.device.nowMs = [&] { return now; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	Value saved;
	port.saveSetup = [&](const Value& _s) { saved = _s; };
	mmDesk::Desk d(port);
	const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
	msg(R"({"op":"ready"})");
	d.setProbe(mmDesk::Desk::Probe::Running);
	d.onTelemetry(screen(mmDesk::Screen::Main));
	d.onDeviceSysex({0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, 0x04, 1, 0xf7});	// pattern 1
	d.onDeviceSysex({0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, 0x02, 2, 0xf7});	// kit 2
	ed::MmKit k;
	k.position = 2;
	k.machines.fill(1);
	k.trigPos.fill(0xff);
	d.onDeviceSysex(ed::encodeMmKit(k));
	check(d.workingKit() && d.workingKit()->position == 2, "the kit that plays starts as its slot");
	msg(R"({"op":"modSet","id":1,"doc":{"schema":"mm-desk/modulators","version":1,
		"sources":[{"id":"l1","kind":"lfo","shape":1,"rate":"1/16","depth":100}],
		"links":[{"source":"l1","track":0,"param":16,"min":0,"max":127}]}})");
	const auto last = [&](const char* _type) -> const Value*
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == _type)
				return &*it;
		return nullptr;
	};
	check(last("result") && last("result")->find("ok")->asBool() && saved.isObject(), "modSet is taken and kept with the project");
	check(last("mod") && last("mod")->find("runs")->asString() == "plug-in", "the page gets the setup back (mod)");
	for(int step = 0; step < 8; ++step)
	{
		now += 125;
		auto t = screen(mmDesk::Screen::Main);
		t.step = step;
		t.running = true;
		d.onTelemetry(t);
	}
	bool moved = false;
	for(const auto& [t, pg, i, v] : params)
		moved |= t == 0 && pg == 2 && i == 0;
	check(moved, "the link moves FLT page value 1 of track 1 by CC on the machine's steps");
	check(!d.coreState().history().canUndo(), "modulation is not an undo step");
	check(last("mod") && last("mod")->find("values")->asArray().size() == 1, "the page gets the source's value");
}

// P6: the questions, errors, the machine's own screen and a restart, as the page gets them.
void asksAndErrors()
{
	std::puts("asks, errors, the LCD and a restart");
	double now = 0;
	std::vector<Value> page;
	std::vector<Bytes> wire;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [&](const Bytes& _b) { wire.push_back(_b); };
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [](const std::vector<mmDesk::Key>&) { return true; };
	port.device.nowMs = [&] { return now; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	const auto last = [&](const char* _type) -> const Value*
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == _type)
				return &*it;
		return nullptr;
	};
	const auto status = [](const uint8_t _p, const uint8_t _v) { return Bytes{0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, _p, _v, 0xf7}; };
	ed::MmKit stored;
	stored.position = 2;
	stored.machines.fill(1);
	stored.trigPos.fill(0xff);
	stored.name = {'S', 'T', 'O', 'R', 'E', 'D'};
	ed::MmKit other = stored;
	other.position = 4;
	{
		mmDesk::Desk d(port);
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.showLcd(Bytes(1024, 0x33));
		check(last("lcd") != nullptr, "while the machine starts, its own LCD goes to the page");
		d.setProbe(mmDesk::Desk::Probe::Running);
		d.onTelemetry(screen(mmDesk::Screen::Main));
		d.onDeviceSysex(status(0x04, 1));
		d.onDeviceSysex(status(0x02, 2));
		d.onDeviceSysex(ed::encodeMmKit(stored));
		d.onDeviceSysex(ed::encodeMmKit(other));
		auto edited = stored;
		edited.tracks[0].pages[2][0] = 99;
		msg(R"({"op":"set","id":1,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(edited)) + "}");
		msg(R"({"op":"loadKit","k":4,"id":2})");
		const auto* a = last("ask");
		check(a && a->find("ask")->asString() == "loadKit" && a->find("command")->find("k")->asNumber() == 4 && !a->find("message")->asString().empty(),
			"LOAD KIT over unsaved edits asks first, with the command to resend");
		{
			// its other way on: Save and load = SAVE KIT to the current slot first (MM-PORT-PLAN f)
			const auto* alts = a ? a->find("alternatives") : nullptr;
			const auto* alt = alts && alts->isArray() && alts->asArray().size() == 1 ? &alts->asArray()[0] : nullptr;
			const auto* first = alt ? alt->find("first") : nullptr;
			check(alt && alt->find("label")->asString() == "Save and load" && first && first->isArray() && first->asArray().size() == 1
				&& first->asArray()[0].find("op")->asString() == "saveKit" && !first->asArray()[0].find("k"),
				"LOAD KIT's question offers Save and load: SAVE KIT to the current slot, then the load with force");
			msg(R"({"op":"loadKit","k":2,"id":4})");
			const auto* re = last("ask");
			check(re && re->find("ask")->asString() == "reloadKit" && !re->find("alternatives"), "a reload asks without it (there is nothing to save first)");
		}
		msg(R"({"op":"saveKit","k":4,"id":3})");
		check(last("ask") && last("ask")->find("ask")->asString() == "overwriteSlot", "SAVE KIT over a slot that holds a kit asks first");
		// The library's slot ops ask before they lose something (DESIGN-UNIFY.md phase 7, as the Machinedrum's), the
		// page sends them again with force
		const auto result = [&](const int _id) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result" && it->find("id") && it->find("id")->asNumber() == _id)
					return &*it;
			return nullptr;
		};
		msg(R"({"op":"kitClear","k":4,"id":5,"g":50})");
		check(last("ask") && last("ask")->find("ask")->asString() == "clearSlot" && last("ask")->find("command")->find("op")->asString() == "kitClear"
			&& d.kit(4)->name == other.name, "kitClear asks first (clearSlot); nothing is cleared before the answer");
		msg(R"({"op":"kitClear","k":4,"id":6,"g":50,"force":true})");
		check(result(6) && result(6)->find("ok")->asBool() && d.kit(4)->name[0] == 0 && d.kit(4)->machines[3] == 1, "with force: K5 is six GND-SIN tracks, no name");
		msg(R"({"op":"kitRename","k":4,"id":7,"g":51,"name":"solo"})");
		check(result(7) && result(7)->find("ok")->asBool() && d.kit(4)->name[0] == 'S' && d.kit(4)->name[3] == 'O', "a stored kit renamed: no question");
		msg(R"({"op":"kitCopyTo","from":4,"to":2,"id":8,"g":52})");
		const auto* over = last("ask");
		check(over && over->find("ask")->asString() == "overwriteSlot" && over->find("message")->asString().find("kit that plays") != std::string::npos,
			"a kit copied over the kit that plays asks (it is loaded too)");
		msg(R"({"op":"patClear","p":1,"id":9,"g":53})");
		check(result(9) && result(9)->find("ok")->asBool() == false && last("ask")->find("command")->find("op")->asString() != "patClear",
			"a pattern not read yet: refused, no question");
		page.clear();
		d.setProbe(mmDesk::Desk::Probe::Loading);
		d.setProbe(mmDesk::Desk::Probe::Running);
		d.flush();
		check(last("reset") != nullptr && !d.workingKit(), "a restart: the page starts over");
	}
	{
		// Over a wire nothing drives SYSEX RECV (MM-P4): a dump waits for the person (recv.waiting, SEND n) until
		// the page says the machine is on it (hwSend); one the machine then never reads back is an error.
		mmDesk::Desk d(port, mmDesk::wireProfile());
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.onDeviceSysex(status(0x04, 1));
		d.onDeviceSysex(status(0x02, 2));
		d.onDeviceSysex(ed::encodeMmPattern(emptyPattern(1)));
		auto p = emptyPattern(1);
		p.amp[0] = 1;
		msg(R"({"op":"set","id":4,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		const auto sent = wire.size();
		for(int i = 0; i < 20; ++i)
		{
			now += 100;
			d.onDeviceSysex(status(0x04, 1));
			d.tick();
		}
		const auto* m = last("machine");
		const auto* recv = m ? m->find("doc")->find("recv") : nullptr;
		bool dumped = false;
		for(size_t i = sent; i < wire.size(); ++i)
			dumped = dumped || (wire[i].size() > 6 && wire[i][6] == ed::g_mmPatternDump);
		check(recv && recv->find("state")->asString() == "waitingUser" && recv->find("waiting")->asNumber() == 1 && !dumped,
			"over HW MIDI the pattern dump waits for the person's SYSEX RECV (SEND 1)");
		msg(R"({"op":"hwSend","id":5})");
		dumped = false;
		for(size_t i = sent; i < wire.size(); ++i)
			dumped = dumped || (wire[i].size() > 6 && wire[i][6] == ed::g_mmPatternDump);
		check(dumped, "hwSend sends it");
		for(int i = 0; i < 200; ++i)
		{
			now += 100;
			d.onDeviceSysex(status(0x04, 1));
			d.tick();
		}
		check(last("error") != nullptr, "a push the machine never reads back is reported");
	}
}

// Release review A1, A2, A3: a SYSEX RECV screen that never comes fails the push and leaves the panel alone; a
// load with no reply is retried, then reported, and asked again at the next status; over HW MIDI a kit dump
// replaced while it waits keeps one LOAD KIT.
void stuckDelivery()
{
	std::puts("a SYSEX RECV that never comes, loads with no reply, one LOAD KIT (A1, A2, A3)");
	double now = 0;
	std::vector<Value> page;
	std::vector<Bytes> wire;
	size_t keys = 0;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [&](const Bytes& _b) { wire.push_back(_b); };
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [&](const std::vector<mmDesk::Key>& _k) { keys += _k.size(); return true; };	// the screen never changes
	port.device.nowMs = [&] { return now; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	const auto status = [](const uint8_t _p, const uint8_t _v) { return Bytes{0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, _p, _v, 0xf7}; };
	const auto errors = [&](const std::string& _part)
	{
		size_t n = 0;
		for(const auto& v : page)
			n += v.find("type")->asString() == "error" && v.find("message")->asString().find(_part) != std::string::npos;
		return n;
	};
	const auto pending = [&](const char* _kind, const int _slot)
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == "doc" && it->find("kind")->asString() == _kind && it->find("slot")->asNumber() == _slot)
				return it->find("pending")->asBool();
		return false;
	};
	const auto lastMachine = [&]() -> const Value*
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == "machine")
				return it->find("doc");
		return nullptr;
	};
	const auto requested = [](const Bytes& _m, const uint8_t _cmd, const uint8_t _slot) { return _m.size() > 7 && _m[6] == _cmd && _m[7] == _slot; };
	{
		// A1: the emulator's panel never reaches SYSEX RECV.
		mmDesk::Desk d(port);
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.setProbe(mmDesk::Desk::Probe::Running);
		d.onTelemetry(screen(mmDesk::Screen::Main));
		d.onDeviceSysex(status(0x04, 1));
		d.onDeviceSysex(status(0x02, 2));
		d.onDeviceSysex(ed::encodeMmPattern(emptyPattern(1)));
		const auto run = [&](const double _ms)
		{
			for(double t = 0; t < _ms; t += 100)
			{
				now += 100;
				d.onTelemetry(screen(mmDesk::Screen::Main));
				d.tick();
			}
		};
		auto p = emptyPattern(1);
		p.amp[0] = 1;
		msg(R"({"op":"set","id":4,"kind":"pattern","doc":)" + ed::json::write(ed::mmPatternToJson(p)) + "}");
		run(1000);
		check(keys > 0 && pending("pattern", 1), "the push waits for SYSEX RECV; the panel is driven there");
		const auto wireBefore = wire.size();
		run(60000);
		check(errors("SYSEX RECV") == 1, "a SYSEX RECV that never comes fails the push, with the reason");
		check(!pending("pattern", 1), "the edit is no longer pending");
		bool reread = false;
		for(size_t i = wireBefore; i < wire.size(); ++i)
			reread = reread || requested(wire[i], 0x68, 1);
		check(reread, "the pattern is read again");
		const auto keysThen = keys;
		run(30000);
		check(keys == keysThen, "the panel is left alone (no more EXIT and macro)");
		check(d.recvState() == "idle" || d.recvState() == "failed", "the session does not hold the panel (PLAY, STOP, RECORD, chains)");
		check(d.pattern(1).has_value(), "the pattern keeps what the machine holds");
	}
	page.clear();
	wire.clear();
	{
		// A2: the current pattern's request is never answered, then the machine answers again.
		mmDesk::Desk d(port);
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.setProbe(mmDesk::Desk::Probe::Running);
		bool answer = false;
		size_t seen = 0;
		int asked = 0;
		const auto run = [&](const double _ms)
		{
			for(double t = 0; t < _ms; t += 100)
			{
				now += 100;
				d.onTelemetry(screen(mmDesk::Screen::Main));
				d.onDeviceSysex(status(0x04, 1));
				d.onDeviceSysex(status(0x02, 2));
				d.tick();
				for(; seen < wire.size(); ++seen)
				{
					if(!requested(wire[seen], 0x68, 1))
						continue;
					++asked;
					if(answer)
						d.onDeviceSysex(ed::encodeMmPattern(emptyPattern(1)));
				}
			}
		};
		double waited = 0;
		for(; waited < 60000 && !errors("did not answer the request for pattern 2"); waited += 100)
			run(100);
		check(asked > 1 && asked <= 9 && waited < 15000, "a request with no reply is resent a bounded number of times");
		check(errors("did not answer the request for pattern 2") == 1, "then the current pattern is reported");
		const auto* m = lastMachine();
		const auto* l = m ? m->find("loading") : nullptr;
		check(l && l->find("failed") && l->find("failed")->asNumber() >= 1 && l->find("done")->asNumber() >= l->find("failed")->asNumber(),
			"loading counts it as failed and done, so the progress can end");
		check(errors("pattern 2: it cannot be edited until it is read") == 1, "the error says the pattern cannot be edited until it is read");
		const int before = asked;
		run(120000);
		check(asked > before && asked - before <= 50, "asked again at status replies, with a backoff (at most one round a minute in the end)");
		answer = true;
		run(70000);
		check(d.pattern(1).has_value(), "the next status asks again; once the machine answers it is read");
		check(errors("did not answer the request for pattern 2") == 1, "reported once, not every round");
	}
	page.clear();
	wire.clear();
	{
		// A slot given up in the background (no error then) is reported when the machine plays it and its read
		// is given up again.
		FakeMachine m;
		m.pattern = 1;
		m.kit = 2;
		for(uint8_t s = 0; s < 128; ++s)
		{
			if(s != 5)
				m.slots[{0x67, s}] = ed::encodeMmPattern(emptyPattern(s));
			ed::MmKit k;
			k.position = s;
			k.machines.fill(1);
			k.trigPos.fill(0xff);
			m.slots[{0x52, s}] = ed::encodeMmKit(k);
		}
		for(uint8_t s = 0; s < 24; ++s)
		{
			ed::MmSong so;
			so.position = s;
			so.rows[0].bytes[0] = 0xff;
			so.rows[0].bytes[ed::mmSongRow::g_tempo] = so.rows[0].bytes[ed::mmSongRow::g_tempo + 1] = 0xff;
			m.slots[{0x69, s}] = ed::encodeMmSong(so);
		}
		for(uint8_t s = 0; s < 8; ++s)
		{
			ed::MmGlobal g;
			g.position = s;
			m.slots[{0x50, s}] = ed::encodeMmGlobal(g);
		}
		auto fake = port;
		fake.device.sendSysex = [&](const Bytes& _b) { m.take(_b); };
		mmDesk::Desk d(fake);
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.setProbe(mmDesk::Desk::Probe::Running);
		const auto run = [&](const double _ms)
		{
			for(double t = 0; t < _ms; t += 100)
			{
				now += 100;
				d.onTelemetry(screen(mmDesk::Screen::Main));
				d.tick();
				auto replies = std::move(m.replies);
				m.replies.clear();
				for(const auto& r : replies)
					d.onDeviceSysex(r);
			}
		};
		const auto failed = [&]
		{
			const auto* doc = lastMachine();
			const auto* l = doc ? doc->find("loading") : nullptr;
			const auto* f = l ? l->find("failed") : nullptr;
			return f ? static_cast<int>(f->asNumber()) : 0;
		};
		for(double t = 0; t < 300000 && failed() == 0; t += 1000)
			run(1000);
		check(failed() == 1 && errors("pattern 6") == 0, "a slot given up in the background: counted as failed, no error");
		m.pattern = 5;
		run(20000);
		check(errors("did not answer the request for pattern 6") == 1, "once it plays and its read is given up again: reported");
	}
	page.clear();
	wire.clear();
	{
		// A3: over HW MIDI a kit dump waiting for SYSEX RECV is replaced in place; its LOAD KIT is not repeated.
		mmDesk::Desk d(port, mmDesk::wireProfile());
		const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
		msg(R"({"op":"ready"})");
		d.onDeviceSysex(status(0x04, 1));
		d.onDeviceSysex(status(0x02, 2));
		ed::MmKit k;
		k.position = 2;
		k.machines.fill(1);
		k.trigPos.fill(0xff);
		d.onDeviceSysex(ed::encodeMmKit(k));
		for(int i = 0; i < 5; ++i)
		{
			now += 100;
			d.onDeviceSysex(status(0x04, 1));
			d.tick();
		}
		for(uint8_t v = 0; v < 4; ++v)
		{
			auto e = k;
			e.trigPos[0] = v;	// no live path: a dump to the slot, then LOAD KIT
			msg(R"({"op":"set","id":)" + std::to_string(10 + v) + R"(,"kind":"workingKit","doc":)" + ed::json::write(ed::mmKitToJson(e)) + "}");
			now += 100;
			d.onDeviceSysex(status(0x04, 1));
			d.tick();
		}
		const auto* m = lastMachine();
		const auto* recv = m ? m->find("recv") : nullptr;
		check(recv && recv->find("waiting")->asNumber() == 2, "four edits with no live path wait as one kit dump and one LOAD KIT (SEND 2)");
	}
}

// MM-P8: pattern chaining as on the machine (hold BANK, press the TRIG keys): the keys a chain is, the
// latest wins while keys are on their way, CLEAR is a pick of the pattern that plays, a pick asks
// while a chain plays and ends it with its own keys, and HW MIDI refuses with the reason.
void chains()
{
	std::puts("pattern chaining");
	double now = 0;
	std::vector<Value> page;
	std::vector<Bytes> wire;
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> bankTrigs;
	std::vector<mmDesk::Key> keys;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [&](const Bytes& _b) { wire.push_back(_b); };
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.device.pressKeys = [&](const std::vector<mmDesk::Key>& _k) { keys.insert(keys.end(), _k.begin(), _k.end()); return true; };
	port.device.pressBankTrigs = [&](const uint8_t _b, const std::vector<uint8_t>& _t) { bankTrigs.emplace_back(_b, _t); return true; };
	port.device.nowMs = [&] { return now; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	const auto last = [&](const char* _type) -> const Value*
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == _type)
				return &*it;
		return nullptr;
	};
	const auto status = [](const uint8_t _p, const uint8_t _v) { return Bytes{0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, _p, _v, 0xf7}; };
	mmDesk::Desk d(port);
	const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
	auto tel = screen(mmDesk::Screen::Main);
	tel.bankGroup = 0;
	tel.chainKnown = true;
	const auto run = [&](const double _ms)
	{
		for(double e = 0; e < _ms; e += 10)
		{
			now += 10;
			d.onTelemetry(tel);
			d.tick();
		}
	};
	msg(R"({"op":"ready"})");
	d.setProbe(mmDesk::Desk::Probe::Running);
	d.onTelemetry(tel);
	d.onDeviceSysex(status(0x04, 4));	// A05
	d.onDeviceSysex(status(0x02, 2));
	d.onDeviceSysex(status(0x10, 0));	// pattern mode
	run(20);
	const auto* m = last("machine");
	const auto* can = m ? m->find("doc")->find("capabilities")->find("can") : nullptr;
	check(can && can->find("chains")->asBool(), "the emulator can chain (capabilities.chains)");
	const auto* desk = m ? m->find("doc")->find("desk") : nullptr;
	check(desk && desk->find("chain")->isObject() && desk->find("bankGroup")->asNumber() == 0, "the machine document has the chain and BANK GROUP (desk)");

	msg(R"({"op":"chain","id":1,"patterns":[2,4]})");
	check(bankTrigs.size() == 1 && bankTrigs[0].first == 0 && bankTrigs[0].second == std::vector<uint8_t>{2, 4} && keys.empty(),
		"a chain in A-D: BANK A held, TRIG 3 and 5 (no BANK GROUP)");
	msg(R"({"op":"chain","id":2,"patterns":[2,4,1]})");
	msg(R"({"op":"chain","id":3,"patterns":[2,4,1,7]})");
	check(bankTrigs.size() == 1 && last("result")->find("note")->asString().find("Chain next") == 0, "re-sent while its keys are on their way: it waits");
	run(300);
	check(bankTrigs.size() == 2 && bankTrigs[1].second == std::vector<uint8_t>{2, 4, 1, 7}, "then only the latest goes out (latest wins)");
	tel.chain = {true, 0, {2, 4, 1, 7}};
	run(20);
	desk = last("machine")->find("doc")->find("desk");
	check(desk->find("chain")->find("active")->asBool() && desk->find("chain")->find("patterns")->asArray().size() == 4, "the machine's chain shows (desk.chain)");

	// E-H: BANK GROUP first; then the group is E-H
	msg(R"({"op":"chain","id":4,"patterns":[66,64]})");
	run(300);
	check(keys.size() == 1 && keys[0] == mmDesk::Key::BankGroup && bankTrigs.size() == 3 && bankTrigs[2].first == 0
		&& bankTrigs[2].second == std::vector<uint8_t>{2, 0}, "a chain in E-H: BANK GROUP, then BANK A/E held, TRIG 3 and 1");
	tel.bankGroup = 1;
	run(20);

	// refusals: the machine's rules
	msg(R"({"op":"chain","id":5,"patterns":[3]})");
	check(!last("result")->find("ok")->asBool(), "one pattern is not a chain");
	msg(R"({"op":"chain","id":6,"patterns":[3,20]})");
	check(!last("result")->find("ok")->asBool(), "two banks are refused");
	msg(R"({"op":"chain","id":7,"patterns":[3,5,3]})");
	check(!last("result")->find("ok")->asBool(), "a pattern twice is refused");

	// a pick while chained: asks, then ends the chain with its own keys (BANK + its TRIG) and LOAD PATTERN
	tel.chain = {true, 1, {66, 64}};
	run(20);
	page.clear();
	msg(R"({"op":"select","id":8,"p":67})");
	check(last("ask") && last("ask")->find("ask")->asString() == "breakChain", "a pick while a chain plays asks first (breakChain)");
	const auto picks = bankTrigs.size();
	wire.clear();
	msg(R"({"op":"select","id":9,"p":67,"force":true})");
	bool load = false;
	for(const auto& b : wire)
		load = load || (b.size() > 7 && b[6] == 0x57 && b[7] == 67);
	check(bankTrigs.size() == picks + 1 && bankTrigs.back().first == 0 && bankTrigs.back().second == std::vector<uint8_t>{3} && load,
		"confirmed: BANK + TRIG 4 ends the chain (a SysEx LOAD PATTERN alone would not), and LOAD PATTERN E04");
	tel.chain.active = false;
	run(300);

	// CLEAR: a pick of the pattern that plays
	tel.chain = {true, 0, {66, 64}};
	d.onDeviceSysex(status(0x04, 66));
	run(20);
	msg(R"({"op":"chainClear","id":10})");
	check(bankTrigs.back().second == std::vector<uint8_t>{2} && last("result")->find("note")->asString().find("E03 plays on") != std::string::npos,
		"CLEAR: BANK + the TRIG key of the pattern that plays (E03 plays on)");
	// CLEAR right after a chain: after its keys
	msg(R"({"op":"chain","id":11,"patterns":[68,69]})");
	const auto before = bankTrigs.size();
	msg(R"({"op":"chainClear","id":12})");
	check(bankTrigs.size() == before, "CLEAR while the chain's keys are on their way waits");
	run(300);
	check(bankTrigs.size() == before + 1 && bankTrigs.back().second == std::vector<uint8_t>{2}, "then picks the pattern that plays");

	// song mode: a chain switches to pattern mode first (the machine plays no chain in song mode)
	d.onDeviceSysex(status(0x10, 1));
	run(20);
	wire.clear();
	msg(R"({"op":"chain","id":13,"patterns":[64,65]})");
	bool patternMode = false;
	for(const auto& b : wire)
		patternMode = patternMode || (b.size() > 8 && b[6] == 0x71 && b[7] == 0x10 && b[8] == 0);
	check(patternMode, "from song mode: SET STATUS pattern mode first");
	run(300);

	// a new chain drops a queued pick
	d.onDeviceSysex(status(0x10, 0));
	tel.chain.active = false;
	run(20);
	tel.running = true;
	run(20);
	msg(R"({"op":"select","id":14,"p":70})");
	check(last("machine")->find("doc")->find("pattern")->find("queued")->asNumber() == 70, "a pick while playing is queued");
	tel.chain = {true, 0, {71, 72}};
	run(20);
	check(last("machine")->find("doc")->find("pattern")->find("queued")->isNull(), "a chain the machine now holds drops the queued pick");
	tel.running = false;
	run(20);

	// HW MIDI: no keys reach the machine
	{
		mmDesk::Desk w(port, mmDesk::wireProfile());
		page.clear();
		w.onPageMessage(*ed::json::parse(R"({"op":"ready"})"));
		w.onDeviceSysex(status(0x04, 1));
		w.onPageMessage(*ed::json::parse(R"({"op":"chain","id":1,"patterns":[1,2]})"));
		const auto* r = last("result");
		check(r && !r->find("ok")->asBool() && r->find("errors")->asArray()[0].asString().find("Appendix C") != std::string::npos,
			"over HW MIDI a chain is refused with the reason");
		const auto* mw = last("machine");
		check(mw && !mw->find("doc")->find("capabilities")->find("can")->find("chains")->asBool() && mw->find("doc")->find("desk")->find("chain")->isNull(),
			"and capabilities.chains is false, desk.chain null");
	}
}

// The executable spec (deskCore::contract, shared with the Machinedrum's): every published message
// against the contract, the contract's machine document against what was published, $defs/command
// generated from the tables (mmDeskTest --write-schema), the adapter's functions against the table.
void checkContract(const bool _write)
{
	namespace contract = deskCore::contract;
	std::ifstream in(MMDESK_SCHEMA);
	const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	auto root = ed::json::parse(text);
	check(root.has_value(), "the contract schema loads");
	if(!root)
		return;
	const auto generated = deskHost::contractCommands(mmDesk::commandTable().schema());
	std::vector<std::string> kinds;
	for(const auto* n : deskCore::kindNames<mmDesk::MmModel>())
		kinds.push_back(n);
	// The catalogue the page gets (MmModel::catalogue), kept as a file too: the page's own tests
	// check the mockup's tables against it (mmConvertTest.js).
	const auto catalogue = ed::json::write(mmDesk::MmModel::catalogue(), 2) + "\n";
	if(_write)
	{
		std::ofstream out(MMDESK_SCHEMA);
		auto written = contract::withAsks(contract::withDocKinds(contract::withGenerated(*root, generated), kinds), mmDesk::MmModel::asks());
		written = contract::withContractVersion(contract::withOpenDocuments(std::move(written)), mmDesk::MmModel::contractVersion);
		out << ed::json::write(written, 2) << "\n";
		std::ofstream cat(MMDESK_CATALOGUE);
		cat << catalogue;
		return;
	}
	{
		std::ifstream cin(MMDESK_CATALOGUE);
		const std::string committed{std::istreambuf_iterator<char>(cin), std::istreambuf_iterator<char>()};
		check(committed == catalogue, "doc/modern-ux/mm-catalogue.json is the catalogue the plug-in sends (--write-schema)");
	}
	check(contract::sameCommands(*root, generated), "the schema's $defs/command is generated from the command tables (--write-schema)");
	check(contract::sameLifecycle(*root), "the schema's lifecycle enum is the lifecycle rows (--write-schema)");
	check(contract::sameAsks(*root, mmDesk::MmModel::asks()), "the schema's ask enum is the model's questions (--write-schema)");
	check(contract::sameContractVersion(*root, mmDesk::MmModel::contractVersion), "the machine document's contract is the model's contractVersion (--write-schema)");
	for(const auto& closed : contract::closedDocuments(*root))
		check(false, ("what the plug-in writes is open for readers, " + closed + " is closed (--write-schema)").c_str());
	for(const auto& gap : contract::docKindGaps(*root, kinds))
		check(false, gap.c_str());
	// The plug-in's host sends these; this test has no host.
	const auto r = contract::checkMessages(*root, g_published, {"learn", "audio", "audioLevel", "openAudio", "romInstall", "romInfo", "notice", "syxPreview", "syxProgress", "syxExport", "host", "audioRun", "editorMenu", "zoom"});
	for(const auto& p : r.off)
		std::printf("    %s\n", p.c_str());
	for(const auto& u : r.unseen)
		std::printf("    declared, never published: %s\n", u.c_str());
	std::printf("  %zu published messages of %zu types, %zu off the contract\n", r.messages, r.types, r.offCount);
	check(r.offCount == 0 && r.messages > 0, "every published message is on the contract");
	check(r.unseen.empty(), "every message type and machine member the contract declares is published");
	auto gaps = contract::handlerGaps(mmDesk::commandTable(), deskCore::Owner::Machine, mmDesk::MmMachine::commandsHandled());
	for(const auto& g : contract::handlerGaps(mmDesk::commandTable(), deskCore::Owner::Setup, mmDesk::Desk::setupOps()))
		gaps.push_back(g);
	for(const auto& g : contract::unknownOps(mmDesk::commandTable(), mmDesk::MmMachine::commandsAsking()))
		gaps.push_back(g);
	for(const auto& g : gaps)
		std::printf("    %s\n", g.c_str());
	check(gaps.empty(), "every machine command of the table has its adapter function, and no other; every ask is a command");
}

// DESIGN-UNIFY.md 4.2: the edit intents as data (doc/modern-ux/intent-cases.json), shared with the page's view test
// (mmViewTest.js). Each case's commands go through apply (mmDeskEdit.cpp) on the base documents, and the documents
// after are compared whole with the base plus the case's "after" values; a refused case names a part of the error.
namespace intentCases
{
	// _root's member at _path (object keys and array indices), made where it is missing; nullptr when the path crosses a value
	Value* at(Value& _root, const Value& _path)
	{
		Value* v = &_root;
		for(const auto& k : _path.asArray())
		{
			if(k.isString())
			{
				if(!v->isObject())
					return nullptr;
				auto* next = v->find(k.asString());
				v = next ? next : &v->set(k.asString(), Value());
			}
			else
			{
				const auto i = static_cast<size_t>(k.asNumber());
				if(!v->isArray() || i >= v->asArray().size())
					return nullptr;
				v = &v->asArray()[i];
			}
		}
		return v;
	}
	// [kind, path, value] entries set on the documents by kind
	bool patch(std::map<std::string, Value>& _docs, const Value& _entries)
	{
		for(const auto& e : _entries.asArray())
		{
			const auto& a = e.asArray();
			auto* v = at(_docs[a[0].asString()], a[1]);
			if(!v)
				return false;
			*v = a[2];
		}
		return true;
	}
	// the documents as the core holds them (and, back to JSON, in the contract's one form). A document's name in the
	// cases is its kind ("pattern", "workingKit", "global", "song") or, for another slot of the library, any name
	// ("kit5"); its schema says what it is.
	std::optional<mmDesk::Documents> documents(const std::map<std::string, Value>& _json, std::vector<std::string>& _errors)
	{
		mmDesk::Documents d;
		for(const auto& [name, doc] : _json)
		{
			const auto schema = doc.find("schema") ? doc.find("schema")->asString() : std::string();
			if(name == "workingKit")
			{
				if(auto k = ed::mmKitFromJson(doc, _errors))
					d.working = mmDesk::WorkingKit{*k};
			}
			else if(schema == "mm-desk/pattern")
			{
				if(auto p = ed::mmPatternFromJson(doc, _errors))
				{
					for(const auto& problem : ed::validate(*p))
						_errors.push_back(name + ": " + problem);
					d.patterns[p->position] = *p;
				}
			}
			else if(schema == "mm-desk/kit")
			{
				if(auto k = ed::mmKitFromJson(doc, _errors))
					d.kits[k->position] = *k;
			}
			else if(schema == "mm-desk/song")
			{
				if(auto x = ed::mmSongFromJson(doc, _errors))
					d.songs[x->position] = *x;
			}
			else if(schema == "mm-desk/global")
			{
				if(auto g = ed::mmGlobalFromJson(doc, _errors))
					d.globals[g->position] = *g;
			}
			else
				_errors.push_back(name + ": no schema");
		}
		return _errors.empty() ? std::optional<mmDesk::Documents>(d) : std::nullopt;
	}
	std::map<std::string, std::string> written(const mmDesk::Documents& _d)
	{
		std::map<std::string, std::string> out;
		for(const auto& [slot, p] : _d.patterns)
			out["pattern " + std::to_string(slot)] = ed::json::write(ed::mmPatternToJson(p));
		for(const auto& [slot, k] : _d.kits)
			out["kit " + std::to_string(slot)] = ed::json::write(ed::mmKitToJson(k));
		for(const auto& [slot, x] : _d.songs)
			out["song " + std::to_string(slot)] = ed::json::write(ed::mmSongToJson(x));
		for(const auto& [slot, g] : _d.globals)
			out["global " + std::to_string(slot)] = ed::json::write(ed::mmGlobalToJson(g));
		if(_d.working)
			out["workingKit"] = ed::json::write(ed::mmKitToJson(_d.working->kit));
		return out;
	}
	// the first place two JSON texts differ, for the failure line
	std::string firstDiff(const std::string& _a, const std::string& _b)
	{
		size_t i = 0;
		while(i < _a.size() && i < _b.size() && _a[i] == _b[i])
			++i;
		const auto from = i > 60 ? i - 60 : 0;
		return "..." + _a.substr(from, 100) + "\n       expected ..." + _b.substr(from, 100);
	}

	void run()
	{
		std::puts("edit intents (doc/modern-ux/intent-cases.json)");
		std::ifstream in(MMDESK_INTENT_CASES);
		const auto file = ed::json::parse(std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
		check(file.has_value(), "the intent cases load");
		if(!file)
			return;
		const auto& mm = *file->find("mm");
		std::map<std::string, Value> base;
		for(const auto& [kind, doc] : mm.find("docs")->asObject())
			base[kind] = doc;
		check(patch(base, *mm.find("given")), "the cases' given values are on the documents");
		std::vector<std::string> errors;
		const auto docs = documents(base, errors);
		check(docs.has_value(), errors.empty() ? "the base documents read" : ("the base documents read: " + errors.front()).c_str());
		if(!docs)
			return;
		mmDesk::EditContext context;
		context.currentKit = static_cast<int>(mm.find("context")->find("kit")->asNumber());
		context.currentGlobal = static_cast<int>(mm.find("context")->find("global")->asNumber());
		size_t ran = 0;
		for(const auto& c : mm.find("cases")->asArray())
		{
			const auto name = c.find("name")->asString();
			std::vector<Value> commands;
			if(const auto* one = c.find("command"))
				commands.push_back(*one);
			else
				commands = c.find("commands")->asArray();
			auto d = *docs;
			mmDesk::Clipboard clip;
			std::vector<std::string> refusedWith;
			for(const auto& cmd : commands)
			{
				const auto r = mmDesk::apply(d, cmd, clip, context);
				if(r.clipboard)
					clip = *r.clipboard;
				if(!r.errors.empty())
				{
					refusedWith = r.errors;
					break;
				}
				for(const auto& ch : r.changes)
					d.set(ch.after);
			}
			++ran;
			if(const auto* want = c.find("refused"))
			{
				bool named = false;
				for(const auto& e : refusedWith)
					named = named || e.find(want->asString()) != std::string::npos;
				check(named, ("refused: " + name + (refusedWith.empty() ? " (taken)" : " (" + refusedWith.front() + ")")).c_str());
				continue;
			}
			if(!refusedWith.empty())
			{
				check(false, (name + ": refused, " + refusedWith.front()).c_str());
				continue;
			}
			auto expectJson = base;
			std::vector<std::string> e2;
			const auto expected = patch(expectJson, *c.find("after")) ? documents(expectJson, e2) : std::nullopt;
			if(!expected)
			{
				check(false, (name + ": its after values make no valid documents" + (e2.empty() ? "" : ": " + e2.front())).c_str());
				continue;
			}
			const auto got = written(d), want = written(*expected);
			std::string diff;
			for(const auto& [kind, text] : want)
				if(!got.count(kind) || got.at(kind) != text)
					diff += "\n       " + kind + ": " + (got.count(kind) ? firstDiff(got.at(kind), text) : std::string("missing"));
			check(diff.empty(), (name + diff).c_str());
		}
		check(ran >= 50, "every case ran");
		// the edits the table declares are the ones apply has
		for(const auto& gap : deskCore::contract::editGaps(mmDesk::commandTable(), mmDesk::editOps()))
			check(false, gap.c_str());
	}
}

// DESIGN-UNIFY.md 4.4: the mutes, the MIDI mutes, POLY and the tempo come from memory, but what the page set is
// what the machine document says from the moment the command is taken (published before its result), until
// memory shows it; memory that disagrees for longer than the profile's settleMs wins (a press on the panel).
// The clock is the session's, a fake one here.
void memoryFields()
{
	std::puts("mutes, POLY and tempo: expected until memory shows them");
	double now = 0;
	std::vector<Value> page;
	mmDesk::Desk::Port port;
	port.device.sendSysex = [](const Bytes&) {};
	port.device.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.device.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	int muteWindows = 0;
	port.device.pressKeys = [&](const std::vector<mmDesk::Key>& _keys) { muteWindows += !_keys.empty() && _keys[0] == mmDesk::Key::MuteWindow; return true; };
	port.device.nowMs = [&] { return now; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	const auto status = [](const uint8_t _p, const uint8_t _v) { return Bytes{0xf0, 0, 0x20, 0x3c, 3, 0, 0x72, _p, _v, 0xf7}; };
	mmDesk::Desk d(port);
	const auto msg = [&](const std::string& _json) { d.onPageMessage(*ed::json::parse(_json)); };
	auto tel = screen(mmDesk::Screen::Main);
	tel.mutes = 0;
	tel.tempo = 120 * 24;
	const auto run = [&](const double _ms)
	{
		for(double e = 0; e < _ms; e += 10)
		{
			now += 10;
			d.onTelemetry(tel);
			d.tick();
		}
	};
	const auto machine = [&]() -> const Value*
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == "machine")
				return it->find("doc");
		return nullptr;
	};
	const auto synth = [&] { return static_cast<int>(machine()->find("mutes")->find("synth")->asNumber()); };
	const auto midi = [&] { return static_cast<int>(machine()->find("mutes")->find("midi")->asNumber()); };
	const auto poly = [&] { const auto* p = machine()->find("poly"); return p->isNull() ? -1 : p->asBool() ? 1 : 0; };
	const auto tempo = [&] { return machine()->find("tempo")->asNumber(); };
	// the machine document that came before the last result (the page has it when the result comes)
	const auto beforeResult = [&]() -> const Value*
	{
		bool result = false;
		for(auto it = page.rbegin(); it != page.rend(); ++it)
		{
			const auto type = it->find("type")->asString();
			if(type == "result")
				result = true;
			else if(result && type == "machine")
				return it->find("doc");
		}
		return nullptr;
	};
	msg(R"({"op":"ready"})");
	d.setProbe(mmDesk::Desk::Probe::Running);
	d.onTelemetry(tel);
	d.onDeviceSysex(status(0x04, 1));
	d.onDeviceSysex(status(0x02, 2));
	d.onDeviceSysex(status(0x20, 0));
	run(20);
	check(synth() == 0 && midi() == 0 && poly() == 0 && tempo() == 120, "memory: no mutes, mono, 120 BPM");

	// a mute: said at once, before the result; memory catches up and settles it
	msg(R"({"op":"mute","id":1,"t":2,"on":true})");
	const auto* before = beforeResult();
	check(before && static_cast<int>(before->find("mutes")->find("synth")->asNumber()) == 4, "mute T3: the machine document says it before the result");
	run(1000);
	check(synth() == 4, "memory has not shown it yet (1 s): still expected");
	tel.mutes = 4;
	run(20);
	check(synth() == 4, "memory shows it: settled");
	tel.mutes = 0;
	run(20);
	check(synth() == 0, "settled, then a press on the panel unmutes T3: memory wins at once");

	// memory never shows it: given up after settleMs on the session's clock
	msg(R"({"op":"mute","id":2,"t":3,"on":true})");
	run(1400);
	check(synth() == 8, "mute T4, memory disagrees for 1.4 s: still expected");
	run(200);
	check(synth() == 0, "memory disagrees for longer than settleMs (1.5 s): memory wins");

	// a MIDI track's mute (the MUTE window's keys)
	msg(R"({"op":"muteMidi","id":3,"t":1,"on":true})");
	check(midi() == 2, "mute M2: said at once");
	tel.mutes = 2 << 6;
	run(20);
	tel.mutes = 0;
	run(20);
	check(midi() == 0, "settled by memory, then memory again");
	// taken back before memory shows the first: the keys go again (each press toggles), so it ends unmuted
	const auto windows = muteWindows;
	msg(R"({"op":"muteMidi","id":30,"t":1,"on":true})");
	msg(R"({"op":"muteMidi","id":31,"t":1,"on":false})");
	check(muteWindows == windows + 2 && midi() == 0, "a MIDI mute taken back at once: the MUTE window's keys twice, unmuted");
	msg(R"({"op":"muteMidi","id":32,"t":1,"on":false})");
	check(muteWindows == windows + 2, "unmuting what is unmuted: no keys");
	run(1600);

	// POLY: SET STATUS, read back by the status reply
	msg(R"({"op":"poly","id":4,"on":true})");
	check(poly() == 1, "POLY on: said at once");
	run(500);
	check(poly() == 1, "no status reply yet: still expected");
	d.onDeviceSysex(status(0x20, 1));
	run(20);
	d.onDeviceSysex(status(0x20, 0));
	run(20);
	check(poly() == 0, "settled by the status reply; then the machine's own change shows");
	msg(R"({"op":"poly","id":5,"on":true})");
	run(1600);
	check(poly() == 0, "no status reply for longer than settleMs: memory wins");

	// the tempo (BPM x 24 in memory)
	msg(R"({"op":"tempo","id":6,"bpm":130})");
	check(tempo() == 130, "tempo 130: said at once");
	tel.tempo = 130 * 24;
	run(20);
	tel.tempo = 125 * 24;
	run(20);
	check(tempo() == 125, "settled; then the machine's own tempo");
	msg(R"({"op":"tempo","id":7,"bpm":140})");
	run(1600);
	check(tempo() == 125, "memory disagrees for longer than settleMs: memory's tempo");
}

int main(const int _argc, char** _argv)
{
	if(_argc > 1 && std::string(_argv[1]) == "--write-schema")
	{
		checkContract(true);
		std::puts("mmDeskTest: wrote $defs/command");
		return 0;
	}
	recvSession();
	recvTaking();
	desk();
	playingFromSteps();
	watchSteps();
	kitAsLoaded();
	kitAfterPatternDump();
	modulators();
	asksAndErrors();
	stuckDelivery();
	chains();
	memoryFields();
	intentCases::run();
	checkContract(false);
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
