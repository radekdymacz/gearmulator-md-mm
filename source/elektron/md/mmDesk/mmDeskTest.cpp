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
		check(m.keysPressed >= 28 && d.recvParked(), "the desk drove the machine to SYSEX RECV");
		check(m.slots[{0x67, 3}] == ed::encodeMmPattern(p), "the machine holds the edited pattern");
		run(300);
		check(d.lastRoundTripMs() > 0, "read back and confirmed");
		run(3500);
		check(m.screenWord == mmDesk::Screen::Main && !d.recvParked(), "left SYSEX RECV when idle");

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
		msg(R"({"op":"saveKit","k":4,"id":3})");
		check(last("ask") && last("ask")->find("ask")->asString() == "overwriteSlot", "SAVE KIT over a slot that holds a kit asks first");
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
		out << ed::json::write(contract::withAsks(contract::withDocKinds(contract::withGenerated(*root, generated), kinds), mmDesk::MmModel::asks()), 2) << "\n";
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
	for(const auto& gap : contract::docKindGaps(*root, kinds))
		check(false, gap.c_str());
	// The plug-in's host sends these; this test has no host.
	const auto r = contract::checkMessages(*root, g_published, {"learn", "audio", "audioLevel", "openAudio", "romInstall", "syxPreview", "syxProgress", "syxExport"});
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

int main(const int _argc, char** _argv)
{
	if(_argc > 1 && std::string(_argv[1]) == "--write-schema")
	{
		checkContract(true);
		std::puts("mmDeskTest: wrote $defs/command");
		return 0;
	}
	recvSession();
	desk();
	playingFromSteps();
	modulators();
	asksAndErrors();
	checkContract(false);
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
