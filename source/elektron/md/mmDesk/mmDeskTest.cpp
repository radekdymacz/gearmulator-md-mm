// Pure tests for the Monomachine desk: the SYSEX RECV session and the desk
// against a scripted machine (no firmware). The firmware smoke test is
// mdLibTest/mmDeskFirmwareTest.

#include "mmDesk.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmJson.h"

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
	}

	// A tiny scripted machine: keeps dumps per slot (only on SYSEX RECV), answers requests and status.
	struct FakeMachine
	{
		std::map<std::pair<uint8_t, uint8_t>, Bytes> slots;
		mmDesk::Screen screenWord = mmDesk::Screen::Main;
		int keysPressed = 0;
		uint8_t pattern = 3, kit = 5;
		std::vector<Bytes> replies;
		std::vector<Bytes> ignored;

		std::map<int, int> seen;
		void take(const Bytes& _m)
		{
			if(_m.size() > 6) ++seen[_m[6]];
			if(_m.size() < 8 || _m[4] != 3)
				return;
			const auto cmd = _m[6];
			if(cmd == 0x67 || cmd == 0x52 || cmd == 0x69 || cmd == 0x50)
			{
				if(screenWord == mmDesk::Screen::GlobalEdit)
					slots[{cmd, _m[9]}] = _m;
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
		port.sendSysex = [&](const Bytes& _b) { m.take(_b); };
		port.sendParam = [&](uint8_t _t, uint8_t _p, uint8_t _i, uint8_t _v) { params.emplace_back(_t, _p, _i, _v); };
		port.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
		port.pressKeys = [&](const std::vector<mmDesk::Key>& _k)
		{
			m.keysPressed += static_cast<int>(_k.size());
			if(_k == mmDesk::RecvSession::enterMacro())
				m.screenWord = mmDesk::Screen::GlobalEdit;
			else if(_k == mmDesk::RecvSession::exitKeys())
				m.screenWord = mmDesk::Screen::Main;
			return true;
		};
		port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
		port.nowMs = [&] { return now; };
		mmDesk::Desk d(port);
		const auto run = [&](const double _ms)
		{
			for(double t = 0; t < _ms; t += 10)
			{
				now += 10;
				mmDesk::Telemetry tel = screen(m.screenWord);
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
		check(m.keysPressed >= 28 && d.recv().parked(), "the desk drove the machine to SYSEX RECV");
		check(m.slots[{0x67, 3}] == ed::encodeMmPattern(p), "the machine holds the edited pattern");
		run(300);
		check(d.lastRoundTripMs() > 0, "read back and confirmed");
		run(3500);
		check(m.screenWord == mmDesk::Screen::Main && !d.recv().parked(), "left SYSEX RECV when idle");

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
	}
}

// "playing" follows the step byte when the RAM running flag stays 0 (as in the plug-in).
void playingFromSteps()
{
	std::printf("playing from the step byte\n");
	double now = 0;
	std::vector<Value> page;
	mmDesk::Desk::Port port;
	port.sendSysex = [](const Bytes&) {};
	port.sendParam = [](uint8_t, uint8_t, uint8_t, uint8_t) {};
	port.sendNrpn = [](uint8_t, uint8_t, uint8_t) {};
	port.pressKeys = [](const std::vector<mmDesk::Key>&) { return true; };
	port.toPage = [&](const Value& _v) { page.push_back(_v); g_published.push_back(_v); };
	port.nowMs = [&] { return now; };
	mmDesk::Desk d(port);
	d.onPageMessage(*ed::json::parse(R"({"op":"ready"})"));
	d.setProbe(mmDesk::Desk::Probe::Running);
	const auto playing = [&]
	{
		for(auto it = page.rbegin(); it != page.rend(); ++it)
			if(it->find("type")->asString() == "tel")
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
	bool machineStopped = false;
	for(auto it = page.rbegin(); it != page.rend(); ++it)
		if(it->find("type")->asString() == "machine")
		{
			const auto* doc = it->find("doc");
			const auto* p = doc ? doc->find("playing") : nullptr;
			machineStopped = p && !p->asBool();
			break;
		}
	check(machineStopped, "the step stands still for three step times: stopped");
}

// The executable spec: every published message validates against the contract's
// JSON Schema ($defs/message). GEARMULATOR_DUMP_MESSAGES=1 prints one of each type.
void checkPublished()
{
	std::ifstream in(MMDESK_SCHEMA);
	const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	const auto root = ed::json::parse(text);
	check(root.has_value(), "the contract schema loads");
	if(!root)
		return;
	const ed::json::Schema schema(*root);
	std::map<std::string, size_t> types;
	size_t bad = 0;
	for(const auto& m : g_published)
	{
		const auto* type = m.find("type");
		const auto name = type && type->isString() ? type->asString() : std::string("?");
		if(types[name]++ == 0 && std::getenv("GEARMULATOR_DUMP_MESSAGES"))
			std::printf("%s\n", ed::json::write(m).c_str());
		const auto problems = schema.validate(m, "message");
		if(problems.empty())
			continue;
		if(bad++ < 5)
			for(const auto& p : problems)
				std::printf("    %s: %s\n", name.c_str(), p.c_str());
	}
	std::printf("  %zu published messages of %zu types, %zu off the contract\n", g_published.size(), types.size(), bad);
	check(bad == 0 && !g_published.empty(), "every published message is on the contract");
}


// The command table is the vocabulary (P6): the contract's $defs/command must be what it generates.
// mmDeskTest --write-schema rewrites it.
void checkCommandSchema(const bool _write)
{
	std::ifstream in(MMDESK_SCHEMA);
	const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	auto root = ed::json::parse(text);
	check(root.has_value(), "the contract schema loads");
	if(!root)
		return;
	const auto generated = mmDesk::commandTable().schema();
	auto* defs = root->find("$defs");
	const auto* current = defs ? defs->find("command") : nullptr;
	const bool same = current && ed::json::write(*current) == ed::json::write(generated);
	if(_write && !same && defs)
	{
		defs->put("command", generated);
		std::ofstream out(MMDESK_SCHEMA);
		out << ed::json::write(*root, 2) << "\n";
		std::printf("  wrote $defs/command (%zu commands)\n", mmDesk::commandTable().commands().size());
		return;
	}
	check(same, "the schema's $defs/command is generated from the command table (mmDeskTest --write-schema)");
}

int main(const int _argc, char** _argv)
{
	if(_argc > 1 && std::string(_argv[1]) == "--write-schema")
	{
		checkCommandSchema(true);
		return 0;
	}
	recvSession();
	desk();
	playingFromSteps();
	checkPublished();
	checkCommandSchema(false);
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
