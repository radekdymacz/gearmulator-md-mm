// deskCore without a machine: the load queue, the lifecycle table, sequences on facts,
// the command table, undo, and the core against a toy model and a scripted adapter
// (observed vs pending, refusal, settle, undo).

#include "deskChain.h"
#include "deskCore.h"
#include "deskDesk.h"
#include "deskLoadQueue.h"
#include "deskMod.h"
#include "deskNotes.h"
#include "deskPush.h"
#include "deskRef.h"
#include "deskSequence.h"
#include "deskWorkingCopy.h"

#include <cstdio>
#include <string>
#include <variant>

namespace
{
	using namespace deskCore;
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	// The note intent and the held layer (deskNotes.h).
	void noteIntent()
	{
		std::puts("notes");
		struct Kit
		{
			uint8_t v[2][2]{};
			bool operator==(const Kit& _o) const { return v[0][0] == _o.v[0][0] && v[0][1] == _o.v[0][1] && v[1][0] == _o.v[1][0] && v[1][1] == _o.v[1][1]; }
		};
		const auto at = [](auto& _k, const uint8_t _t, const uint8_t _i) -> auto& { return _k.v[_t][_i]; };
		const auto on = noteOnOf(*elektronData::json::parse(R"({"op":"noteOn","t":3,"vel":90,"pitch":-5})"));
		const auto off = noteOffOf(*elektronData::json::parse(R"({"op":"noteOff","t":3})"));
		const auto off7 = noteOffOf(*elektronData::json::parse(R"({"op":"noteOff","t":3,"pitch":7})"));
		check(on.track == 3 && on.velocity == 90 && on.pitch == -5, "noteOn as a value");
		check(off.releases(-5) && off.releases(7) && off7.releases(7) && !off7.releases(-5), "noteOff: without pitch every note, with pitch that one");
		HeldOverrides held;
		check(!held.any(), "nothing held");
		held.hold({1, 0, 90, 64});
		held.hold({1, 0, 100, 64});
		check(held.list().size() == 1 && held.list()[0].value == 100, "one override per (track, index): the later key's");
		check(held.echo(1, 0, 100) && !held.echo(1, 0, 64) && !held.echo(0, 0, 100), "the held value reported back is an echo; another is not");
		Kit image, document;
		image.v[1][0] = 100;
		image.v[0][1] = 9;
		document.v[1][0] = 50;
		const auto masked = held.masked(image, &document, at);
		check(masked.v[1][0] == 50 && masked.v[0][1] == 9, "masked: the document's value where a key holds one, the rest as the image");
		check(held.masked(image, static_cast<const Kit*>(nullptr), at).v[1][0] == 64, "masked without a document: the value the key replaced");
		const auto released = held.release(1);
		check(released.size() == 1 && !held.any(), "release: the track's overrides, taken out");
		check(HeldOverrides::restoreValue(released[0], &document, at) == 50 && HeldOverrides::restoreValue(released[0], static_cast<const Kit*>(nullptr), at) == 64,
			"restore: the document's value (an edit made meanwhile wins), else the value replaced");
		WorkingCopy<Kit> w;
		w.held.hold({0, 1, 3, 4});
		check(!switched(w).held.any(), "another kit plays: nothing is held any more");
	}

	void mailboxes()
	{
		std::puts("mailboxes");
		Latest<int> l;
		check(l.offer(1, false) == std::optional<int>(1) && !l.waiting(), "nothing on its way: the value starts now");
		check(!l.offer(2, true) && !l.offer(3, true) && l.waiting(), "a run on its way: the value waits");
		check(!l.takeWhen(false) && l.waiting(), "still on its way: nothing is taken");
		check(l.takeWhen(true) == std::optional<int>(3) && !l.waiting(), "the latest wins, once");
		(void)l.offer(4, true);
		check(l.offer(5, false) == std::optional<int>(5) && !l.takeWhen(true), "a value started now replaces what waited");
		(void)l.offer(6, true);
		l.drop();
		check(!l.takeWhen(true), "dropped: nothing waits");

		ChainRequest c = ChainClear{};
		Latest<ChainRequest> chains;
		(void)chains.offer(ChainOf{{1, 2}}, true);
		(void)chains.offer(c, true);
		const auto taken = chains.takeWhen(true);
		check(taken && std::holds_alternative<ChainClear>(*taken), "CLEAR is a variant of its own, and it wins over the chain before it");

		check(validateChain({1, 3}, 16, 128).empty(), "a chain of two in one bank");
		check(!validateChain({1}, 16, 128).empty() && !validateChain({1, 17}, 16, 128).empty() && !validateChain({2, 2}, 16, 128).empty()
			&& !validateChain({1, 128}, 16, 128).empty(), "too short, two banks, twice, out of range");
		check(validateChain({0, 1, 2, 3, 4, 5, 6, 7, 8}, 8, 64).size() == 2, "the bank size is the model's: nine is too many and two banks");
		Value cmd = Value::object();
		Value list = Value::array();
		list.push(Value(3));
		list.push(Value("x"));
		cmd.set("patterns", list);
		check(chainPatterns(cmd) == std::vector<int>{3, -1}, "the patterns of a chain command; not a number is -1");

		std::puts("pushes");
		using R = Ref<int>;
		Pushes<R, int> pushes;
		const PushPolicy policy{200, 150};
		const auto policyOf = [&](const R&) { return policy; };
		const auto timeoutOf = [](const R&) { return 1000.0; };
		check(pushes.want({0, 1}, 10, 0, policy), "the first value goes at once");
		check(!pushes.want({0, 1}, 11, 50, policy) && !pushes.want({0, 1}, 12, 60, policy), "newer values wait their turn");
		auto e = pushes.pump(100, policyOf, timeoutOf);
		check(e.empty(), "before minIntervalMs: nothing");
		e = pushes.pump(200, policyOf, timeoutOf);
		check(e.size() == 1 && e[0].kind == Pushes<R, int>::Effect::Kind::Send && e[0].value == std::optional<int>(12), "the latest waiting goes");
		e = pushes.pump(400, policyOf, timeoutOf);
		check(e.size() == 1 && e[0].kind == Pushes<R, int>::Effect::Kind::AskBack, "quiet: the read-back is asked for, once");
		check(pushes.pump(500, policyOf, timeoutOf).empty(), "asked: nothing more");
		e = pushes.pump(1500, policyOf, timeoutOf);
		check(e.size() == 1 && e[0].kind == Pushes<R, int>::Effect::Kind::TimedOut && !pushes.busy({0, 1}), "no read-back: given up");

		check(pushes.want({0, 2}, 20, 0, policy), "a second document");
		pushes[{0, 2}].parked = true;
		check(!pushes.want({0, 2}, 21, 5000, policy), "parked: a newer value waits, however long after (Held)");
		check(pushes.pump(6000, policyOf, timeoutOf).empty(), "parked: the pump leaves it alone");
		pushes[{0, 2}].parked = false;
		e = pushes.pump(6000, policyOf, timeoutOf);
		check(e.size() == 1 && e[0].value == std::optional<int>(21), "unparked: the value that waited goes");
		check(pushes.anyBusy(), "busy until the read-back");
	}

	void loadQueue()
	{
		std::puts("load queue");
		using R = Ref<int>;
		LoadQueue<R> q;
		LoadQueue<R>::Policy p{30, 1};
		p.rounds = 0;
		q.want({0, 1}, false);
		q.want({0, 2}, false);
		q.want({0, 2}, true);
		check(q.pending() == 2, "two queued, no duplicates");
		auto n = q.next(0, 800, true, p);
		check(n.send && n.send->slot == 2, "urgent first");
		check(!q.next(10, 800, true, p).send, "one in flight");
		n = q.next(900, 800, true, p);
		check(n.send && n.send->slot == 2, "resent after the timeout");
		n = q.next(1800, 800, true, p);
		check(n.gaveUp && n.gaveUp->slot == 2, "given up after one retry (no rounds), and said so");
		check(n.send && n.send->slot == 1, "the next goes out");
		q.arrived({0, 1});
		check(q.idle(), "answered: idle");
		q.want({0, 3}, false);
		check(!q.next(1810, 800, true, p).send, "the gap between requests");
		check(!q.next(1900, 800, false, p).send, "held while busy");
		check(q.next(1900, 800, true, p).send.has_value(), "then it goes");
	}

	// A2: a request without a reply goes back into the queue with a backoff, a bounded number of
	// times, and is then given up (said once), so a slow wire or a pulled cable is not silent.
	void loadQueueRounds()
	{
		std::puts("load queue: rounds and give up");
		using R = Ref<int>;
		LoadQueue<R> q;
		LoadQueue<R>::Policy p{30, 1};
		p.rounds = 2;
		p.backoffMs = 2000;
		q.want({0, 7}, true);
		double t = 0;
		int sends = 0;
		std::optional<R> gaveUp;
		double gaveUpAt = -1;
		for(; t < 60000 && !gaveUp; t += 10)
		{
			const auto s = q.next(t, 800, true, p);
			sends += s.send.has_value();
			if(s.gaveUp)
			{
				gaveUp = s.gaveUp;
				gaveUpAt = t;
			}
		}
		check(gaveUp && gaveUp->slot == 7, "a request with no reply is given up in the end");
		check(sends == 6, "two sends a round, three rounds (the first and two more)");
		check(gaveUpAt > 2 * 2000 && gaveUpAt < 20000, "each round waits out the backoff; bounded");
		check(q.idle(), "given up: nothing left in the queue");
		bool again = false;
		for(double u = t; u < t + 60000; u += 10)
			again = again || q.next(u, 800, true, p).gaveUp.has_value();
		check(!again, "given up once");

		// The backoff does not hold the others back, and an answer during it resets the rounds.
		LoadQueue<R> r;
		r.want({0, 1}, false);
		r.want({0, 2}, false);
		auto s = r.next(0, 800, true, p);
		check(s.send && s.send->slot == 1, "first");
		r.next(800, 800, true, p);
		s = r.next(1600, 800, true, p);
		check(s.send && s.send->slot == 2 && !s.gaveUp, "out of resends: it waits its backoff, the next goes now");
		r.arrived({0, 2});
		check(!r.next(1700, 800, true, p).send, "slot 1 waits out its backoff");
		s = r.next(3700, 800, true, p);
		check(s.send && s.send->slot == 1, "then it is asked again");
		r.arrived({0, 1});
		check(r.idle(), "an answer ends it");

		// A late reply while the request waits out its backoff: it is not asked for again.
		LoadQueue<R> l;
		l.want({0, 4}, false);
		l.next(0, 800, true, p);
		l.next(800, 800, true, p);
		l.next(1600, 800, true, p);
		check(l.pending() == 1 && !l.loading(), "out of resends: waiting out its backoff");
		l.arrived({0, 4});
		check(l.idle(), "a late reply during the backoff: nothing left to ask");
		bool asked = false;
		for(double u = 1600; u < 20000; u += 10)
			asked = asked || l.next(u, 800, true, p).send.has_value();
		check(!asked, "and it is not asked for again");
	}

	void lifecycle()
	{
		std::puts("lifecycle");
		using P = LifeFacts::Probe;
		using A = LifeFacts::Animation;
		const auto of = [](const P _p, const bool _replied, const A _a, const double _silent = 0)
		{
			LifeFacts f;
			f.probe = _p;
			f.replied = _replied;
			f.animation = _a;
			f.silentMs = _silent;
			return lifecycleOf(f);
		};
		check(of(P::Missing, false, A::Unseen) == Lifecycle::Missing, "no ROM");
		check(of(P::Running, false, A::Over) == Lifecycle::Booting, "running, no reply yet: booting");
		check(of(P::Running, true, A::Unseen) == Lifecycle::Animating, "replied, telemetry not looked at: not ready");
		check(of(P::Running, true, A::Running) == Lifecycle::Animating, "the start-up animation runs");
		check(of(P::Running, true, A::Over) == Lifecycle::Ready, "animation over: ready");
		check(of(P::Running, true, A::Absent) == Lifecycle::Ready, "no telemetry: the first reply is all there is");
		check(of(P::Wire, false, A::Absent, 100) == Lifecycle::HwConnecting, "HW: waiting for the first reply");
		check(of(P::Wire, false, A::Absent, 5100) == Lifecycle::HwLost, "HW: nothing for 5 s");
		check(of(P::Wire, true, A::Absent, 100) == Lifecycle::Ready, "HW: answers");
		check(of(P::Wire, true, A::Absent, 3600) == Lifecycle::HwLost, "HW: silent for 3.5 s");
		check(takesInput(Lifecycle::Ready) && !takesInput(Lifecycle::Animating) && takesMidi(Lifecycle::Animating), "gates");
		bool rows = lifecycleRows().size() == 8;
		for(const auto& r : lifecycleRows())
			rows = rows && lifecycleRow(r.lifecycle).name == r.name;
		check(rows && std::string(lifecycleName(Lifecycle::HwLost)) == "hwLost" && takesInput(Lifecycle::HwLost), "one row per state");
	}

	enum class Act { Stop, Load, Play };

	void sequence()
	{
		std::puts("sequence on facts");
		Sequencer<Act> s;
		s.start({{Act::Stop, 0, Wait::Stopped, 0, 1000}, {Act::Load, 5, Wait::StatusReply, 0, 1000}, {Act::Play}});
		SeqFacts f;
		f.playing = true;
		auto d = s.due(0, f);
		check(d.size() == 1 && d[0].action == Act::Stop, "stop at once");
		check(s.due(100, f).empty(), "waits while it plays");
		f.playing = false;
		d = s.due(150, f);
		check(d.size() == 1 && d[0].action == Act::Load && d[0].arg == 5, "load when it stopped");
		check(s.due(200, f).empty(), "waits for the status reply");
		f.statusReplies = 1;
		d = s.due(230, f);
		check(d.size() == 1 && d[0].action == Act::Play && !d[0].afterTimeout, "play after the reply");
		check(!s.running() || s.due(240, f).empty(), "done");
		s.start({{Act::Stop, 0, Wait::Stopped, 0, 500}, {Act::Play}});
		f.playing = true;
		s.due(1000, f);
		d = s.due(1600, f);
		check(d.size() == 1 && d[0].afterTimeout, "a fact that never comes: the timeout goes on, and says so");
	}

	void commands()
	{
		std::puts("command table");
		const CommandTable<> t({{"trig", Owner::Core, Gate::Input, 0, {{"p", ArgType::Integer, 0, 127}, {"t", ArgType::Integer, 0, 15}}, "toggle"},
			{"lock", Owner::Core, Gate::Input, 0, {{"v", ArgType::IntegerOrNull, 0, 127}}, ""}});
		const auto* c = t.find("trig");
		check(c && !t.find("nope"), "lookup");
		const auto e = CommandTable<>::check(*c, *elektronData::json::parse(R"({"op":"trig","p":200})"));
		check(e.size() == 2 && e[0] == "p: 200 is outside 0..127" && e[1] == "t: missing number", "argument lines");
		check(CommandTable<>::check(*t.find("lock"), *elektronData::json::parse(R"({"op":"lock","v":null})")).empty(), "null clears");
		const auto s = t.schema();
		check(s.find("oneOf") && s.find("oneOf")->asArray().size() == 2, "the schema lists every command");
	}

	// ---- the core against a toy model: documents are {slot, value} ----
	struct Toy
	{
		uint8_t slot = 0;
		int value = 0;
		bool operator==(const Toy& _o) const { return slot == _o.slot && value == _o.value; }
	};

	struct ToyModel
	{
		enum class Kind : uint8_t { Toy };
		using Ref = deskCore::Ref<Kind>;
		using Document = Toy;
		struct Documents { std::map<uint8_t, Toy> toys; };
		struct Change
		{
			Toy before, after;
			Ref ref() const { return {Kind::Toy, after.slot}; }
		};
		struct Clipboard {};
		struct Context {};
		struct EditResult
		{
			std::vector<Change> changes;
			std::vector<std::string> errors;
			std::string note;
			std::optional<Clipboard> clipboard;
		};
		static Ref refOf(const Toy& _t) { return {Kind::Toy, _t.slot}; }
		static void set(Documents& _d, const Toy& _t) { _d.toys[_t.slot] = _t; }
		static void erase(Documents& _d, const Ref& _r) { _d.toys.erase(_r.slot); }
		static std::optional<Toy> get(const Documents& _d, const Ref& _r)
		{
			const auto it = _d.toys.find(_r.slot);
			return it == _d.toys.end() ? std::nullopt : std::optional<Toy>(it->second);
		}
		static EditResult apply(const Documents& _d, const Value& _cmd, const Clipboard&, const Context&)
		{
			EditResult r;
			const auto slot = static_cast<uint8_t>(_cmd.find("s")->asNumber());
			const auto it = _d.toys.find(slot);
			if(it == _d.toys.end())
			{
				r.errors.push_back("not loaded");
				return r;
			}
			auto after = it->second;
			after.value = static_cast<int>(_cmd.find("v")->asNumber());
			if(!(after == it->second))
				r.changes.push_back({it->second, after});
			return r;
		}
		static Value docMessage(const Ref& _r, const Toy& _t, const bool _pending, const Source _s)
		{
			Value m = Value::object();
			m.set("type", "doc");
			m.set("slot", _r.slot);
			m.set("value", _t.value);
			m.set("pending", _pending);
			m.set("source", sourceName(_s));
			return m;
		}
		static std::optional<Value> clipboardDocument(const Clipboard&) { return {}; }
		static constexpr int contractVersion = 1;
		static const std::vector<Unsupported>& unsupported() { static const std::vector<Unsupported> u{{"flying", "not yet"}}; return u; }
		static EditResult setDocument(const Documents& _d, const Value& _cmd, const Context& _c) { return apply(_d, _cmd, {}, _c); }

		// The router's vocabulary: an edit, undo, ready, a machine command that asks, and a gated one.
		using Table = CommandTable<>;
		static const Table& commands()
		{
			static const Table t({
				{"ready", Owner::Core, Gate::None, -1, {}, "", CoreOp::Ready},
				{"undo", Owner::Core, Gate::Input, -1, {}, "", CoreOp::Undo},
				{"set", Owner::Core, Gate::Input, 0, {{"s", ArgType::Integer, 0, 9}, {"v", ArgType::Integer, -100, 100}}, ""},
				{"wipe", Owner::Machine, Gate::Input, -1, {}, "asks first"},
				{"hold", Owner::Machine, Gate::Midi, -1, {{"mode", ArgType::Text, 0, 0, false, {"a", "b"}}}, ""}});
			return t;
		}
		static Value catalogue() { return Value::object(); }
		static std::string lifecycleText(Lifecycle) { return "not now"; }
	};

	class ToyMachine final : public Machine<ToyModel>
	{
	public:
		Review review(const Value&, const std::vector<Change>&, const Documents&) override { return {}; }
		Outcome submit(const Change& _c, const Intent&, const Documents&) override
		{
			if(_c.after.value < 0)
				return {{"negative values are refused"}, {}, {}};
			sent.push_back(_c.after);
			return {};
		}
		Outcome command(const Value& _c, const Documents&) override { commands.push_back(opOf(_c)); return {}; }
		Outcome askFor(const Value& _c, const Documents&) override
		{
			if(opOf(_c) != "wipe")
				return {};
			Outcome o;
			o.ask = Ask{"wipe", "Wipe it all?", "Wipe"};
			return o;
		}
		void onSysex(const Bytes&) override {}
		void tick(double, const Documents&) override {}
		std::vector<Ev> drain() override { auto e = std::move(events); events.clear(); return e; }
		Value state(const Documents&) const override { Value v = Value::object(); v.set("schema", "toy"); return v; }
		Capabilities capabilities() const override { Capabilities c; c.engine = "toy"; return c; }
		Lifecycle lifecycle() const override { return life; }
		Context context() const override { return {}; }
		bool busy() const override { return !sent.empty(); }

		std::vector<Ev> events;
		std::vector<Toy> sent;
		std::vector<std::string> commands;
		Lifecycle life = Lifecycle::Ready;
	};

	// The one router (deskCore::Desk) against the toy model: gates, argument checks, the core and
	// machine owners, asks answered with force, and an engine's adapter swapped in.
	void desk()
	{
		std::puts("desk: the router reads the table");
		std::vector<Value> page;
		double now = 0;
		auto first = std::make_unique<ToyMachine>();
		auto* m = first.get();
		int readies = 0;
		Desk<ToyModel, Machine<ToyModel>> d(std::move(first), [&](const Value& _m) { page.push_back(_m); }, [&] { return now; },
			[&] { ++readies; });
		const auto send = [&](const char* _json) { return d.onPageMessage(*elektronData::json::parse(_json)); };
		const auto last = [&](const char* _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == _type)
					return &*it;
			return nullptr;
		};
		check(send(R"({"op":"ready","id":1})") && last("catalogue") && readies == 1, "ready: the catalogue, then the ready hook");
		check(!send(R"({"op":"learnStart"})"), "an op the model does not know is the caller's");
		send(R"({"op":"set","s":42,"v":1,"id":2})");
		check(!last("result")->find("ok")->asBool() && last("result")->find("errors")->asArray()[0].asString().find("s:") == 0,
			"arguments are checked against the table");
		send(R"({"op":"hold","mode":"c","id":3})");
		check(!last("result")->find("ok")->asBool(), "a text argument outside its values is refused");
		m->events.push_back(ToyMachine::Ev::observed({1, 10}, Source::Dump));
		d.flush();
		send(R"({"op":"set","s":1,"v":5,"id":4})");
		check(last("result")->find("ok")->asBool() && m->sent.size() == 1, "an edit goes to the core and the machine");
		send(R"({"op":"wipe","id":5})");
		check(last("ask") && m->commands.empty(), "a machine command asks first");
		check(last("ask")->find("message")->asString() == "Wipe it all?" && last("ask")->find("confirm")->asString() == "Wipe"
			&& last("ask")->find("command")->find("op")->asString() == "wipe" && !last("ask")->find("command")->find("id"),
			"the ask carries its words and the command to resend (without its id)");
		send(R"({"op":"wipe","force":true,"id":6})");
		check(m->commands.size() == 1, "with force it runs (the core applies force, not the adapter)");
		m->life = Lifecycle::Booting;
		send(R"({"op":"undo","id":7})");
		check(last("result")->find("errors")->asArray()[0].asString() == "not now", "a gated command waits with the model's refusal");
		send(R"({"op":"hold","mode":"a","id":8})");
		check(last("result")->find("ok")->asBool() == false, "Gate::Midi waits while booting too");
		// An engine switch: the new adapter replaces the old one; the page starts over.
		page.clear();
		auto second = std::make_unique<ToyMachine>();
		auto* m2 = second.get();
		d.setEngine(std::move(second));
		check(last("reset") && last("catalogue") && d.documents().toys.empty() && &d.machine() == m2, "a new engine: reset, the page starts over");
		check(!send(R"({"op":"set","s":1,"v":5,"extra":1,"id":9})") || !last("result")->find("ok")->asBool(), "an undeclared argument is refused");
		const Value* machine = nullptr;
		for(auto it = page.rbegin(); it != page.rend() && !machine; ++it)
			if(it->find("type")->asString() == "machine")
				machine = it->find("doc");
		check(machine && machine->find("input") && machine->find("lifecycleText")->asString() == "not now"
			&& machine->find("capabilities")->find("can")->find("flying")->asBool() == false
			&& machine->find("capabilities")->find("reasons")->find("flying")->asString() == "not yet",
			"the machine document: input, lifecycle text, capabilities nested with the model's unsupported list");
	}

	void core()
	{
		std::puts("core: observed and pending");
		std::vector<Value> page;
		Core<ToyModel> c([&](const Value& _m) { page.push_back(_m); });
		ToyMachine m;
		c.setMachine(&m);
		c.pageReady();
		m.events.push_back(ToyMachine::Ev::observed({1, 10}, Source::Dump));
		c.pump();
		c.flush();
		const auto lastDoc = [&]() -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "doc")
					return &*it;
			return nullptr;
		};
		check(lastDoc() && lastDoc()->find("value")->asNumber() == 10 && !lastDoc()->find("pending")->asBool(), "observed is published");
		c.edit(*elektronData::json::parse(R"({"op":"set","s":1,"v":20,"g":1})"));
		c.flush();
		check(lastDoc()->find("value")->asNumber() == 20 && lastDoc()->find("pending")->asBool(), "an edit is pending until seen");
		check(!page.empty() && page.back().find("type")->asString() == "result", "the result comes after the documents it changed");
		check(c.state({ToyModel::Kind::Toy, 1})->observed->value == 10, "observed keeps what the machine holds");
		c.edit(*elektronData::json::parse(R"({"op":"set","s":1,"v":-5})"));
		check(c.view().toys.at(1).value == 20, "a refused edit leaves no value the machine never took");
		m.events.push_back(ToyMachine::Ev::observed({1, 20}, Source::Dump));
		c.pump();
		c.flush();
		check(!lastDoc()->find("pending")->asBool() && !c.state({ToyModel::Kind::Toy, 1})->pending, "the read-back confirms");
		c.edit(*elektronData::json::parse(R"({"op":"set","s":1,"v":30})"));
		m.events.push_back(ToyMachine::Ev::failed({ToyModel::Kind::Toy, 1}, "no read-back"));
		c.pump();
		c.flush();
		check(lastDoc()->find("value")->asNumber() == 20, "a failed push falls back to what was observed");
		bool error = false;
		for(const auto& p : page)
			error = error || (p.find("type")->asString() == "error" && p.find("message")->asString() == "no read-back");
		check(error, "and says so");
		const auto sentBefore = m.sent.size();
		c.undo(*elektronData::json::parse(R"({"op":"undo"})"));
		check(m.sent.size() == sentBefore, "undo from what the page shows: the failed push is already undone, nothing is sent");
		c.undo(*elektronData::json::parse(R"({"op":"undo","id":9})"));
		check(m.sent.back().value == 10, "and the step before");
		c.flush();
		check(page.back().find("type")->asString() == "result" && page.back().find("id")->asNumber() == 9, "undo answers");
	}
	// The working-copy policy on a toy kit (an int per knob): status is the truth, an image that
	// predates the editor's edits waits, the image taken is what the machine holds and the view
	// keeps the overlay (edits the image cannot show yet).
	void workingCopy()
	{
		using Kit = std::vector<int>;
		const auto reflects = [](const Kit& _image, const Kit&, const Kit& _to) { return _image == _to; };
		WorkingCopy<Kit> w;
		auto r = fromImage(w, Kit{1, 2}, 3, std::optional<int>(4), static_cast<const Kit*>(nullptr), 0, false, reflects);
		check(r.askStatus && !r.take && !r.next.image, "an image of another kit than status says: ask, take nothing");
		r = fromImage(w, Kit{1, 2}, 3, std::optional<int>(3), static_cast<const Kit*>(nullptr), 0, false, reflects);
		check(r.take == Kit({1, 2}) && r.next.image == Kit({1, 2}) && !r.next.seed && !r.settles, "the image is taken and reported");
		w = r.next;
		w.expect.sent(Kit{1, 2}, Kit{1, 9}, 100);
		const Kit shown{1, 9};
		r = fromImage(w, Kit{1, 2}, 3, std::optional<int>(3), &shown, 150, false, reflects);
		check(!r.take && r.next.expect.any(), "an image from before the edit waits");
		r = fromImage(w, Kit{1, 9}, 3, std::optional<int>(3), &shown, 160, false, reflects);
		check(r.settles && r.take == Kit({1, 9}) && !r.next.expect.any(), "the image that shows it settles the edit");
		auto held = r.next;
		held.expect.sent(Kit{1, 9}, Kit{7, 9}, 170);
		r = fromImage(held, Kit{2, 9}, 3, std::optional<int>(3), &shown, 175, true, reflects);
		check(r.next.image == Kit({2, 9}) && r.take == Kit({2, 9}) && !r.settles && r.next.expect.any(),
			"knob turns on their way: the image is observed as it is, the edit stays pending");
		const auto sw = switched(r.next);
		check(sw.seed && !sw.image && !sw.expect.any(), "another kit plays: nothing of the old one holds");
	}
}

int main()
{
	workingCopy();
	noteIntent();
	{
		Outcome a, b, none;
		a.ask = Ask{"breakChain", "ends the chain", "Go", Value::object(), {}};
		b.ask = Ask{"discardKit", "loses kit edits", "Switch", Value::object(), {}};
		b.ask->details.set("kit", 3);
		const auto both = withAsk(withAsk(none, a), b);
		const auto m = askMessage(*both.ask, *elektronData::json::parse(R"({"op":"select","p":2,"id":7,"force":false})"));
		check(m.find("ask")->asString() == "breakChain" && m.find("also")->asArray().size() == 1
			&& m.find("message")->asString() == "ends the chain<br>loses kit edits" && m.find("kit")->asNumber() == 3
			&& !m.find("command")->find("id") && !m.find("command")->find("force"),
			"two questions of one command are one ask naming both (force answers both)");
		Outcome n1, n2;
		n1.note = "first";
		n2.note = "second";
		check(withAsk(n1, n2).note == "first. second", "joined outcomes keep both notes");
	}
	{
		ModEngine mods;
		ModSetup setup;
		ModSource src;
		src.id = "lfo1";
		setup.sources.push_back(src);
		ModLink link;
		link.source = "lfo1";
		setup.links.push_back(link);
		mods.setSetup(setup);
		struct Sink { int sent = 0; void sendModulation(uint8_t, uint8_t, uint8_t, int) { ++sent; } } sink;
		double t = 0;
		for(int s = 0; s < 32; ++s, t += 100)
			mods.step(sink, 0, ModLimits{}, s, true, t);
		mods.setSetup({});
		bool fellToZero = false;
		for(int s = 0; s < 40 && !fellToZero; ++s, t += 100)
		{
			const auto m = mods.step(sink, 0, ModLimits{}, s, true, t);
			if(m && m->find("ccPerSecond")->asNumber() == 0)
				fellToZero = true;
			else if(!m)
				break;
		}
		check(fellToZero && !mods.step(sink, 0, ModLimits{}, 1, true, t + 100), "the CC readout falls to zero after the last modulator, then stops");
	}
	loadQueue();
	loadQueueRounds();
	mailboxes();
	lifecycle();
	sequence();
	commands();
	core();
	desk();
	std::printf("deskCoreTest: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
