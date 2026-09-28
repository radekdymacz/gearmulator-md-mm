// deskCore without a machine: the load queue, the lifecycle table, sequences on facts,
// the command table, undo, and the core against a toy model and a scripted adapter
// (observed vs pending, refusal, settle, undo).

#include "deskCore.h"
#include "deskDesk.h"
#include "deskLoadQueue.h"
#include "deskPush.h"
#include "deskRef.h"
#include "deskSequence.h"

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

	void loadQueue()
	{
		std::puts("load queue");
		using R = Ref<int>;
		LoadQueue<R> q;
		const LoadQueue<R>::Policy p{30, 1};
		q.want({0, 1}, false);
		q.want({0, 2}, false);
		q.want({0, 2}, true);
		check(q.pending() == 2, "two queued, no duplicates");
		auto n = q.next(0, 800, true, p);
		check(n && n->slot == 2, "urgent first");
		check(!q.next(10, 800, true, p), "one in flight");
		n = q.next(900, 800, true, p);
		check(n && n->slot == 2, "resent after the timeout");
		n = q.next(1800, 800, true, p);
		check(n && n->slot == 1, "given up after one retry; the next goes out");
		q.arrived({0, 1});
		check(q.idle(), "answered: idle");
		q.want({0, 3}, false);
		check(!q.next(1810, 800, true, p), "the gap between requests");
		check(!q.next(1900, 800, false, p), "held while busy");
		check(q.next(1900, 800, true, p).has_value(), "then it goes");
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
		check(std::string(legacyBoot(Lifecycle::Animating)) == "animation" && std::string(legacyLink(Lifecycle::Ready, true)) == "ready"
			&& std::string(legacyLink(Lifecycle::Ready, false)) == "local", "the contract's older strings");
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
		static void decorate(Value&, const History<Change>&) {}
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
		static std::string refusal(Lifecycle) { return "not now"; }
	};

	class ToyMachine final : public Machine<ToyModel>
	{
	public:
		Outcome review(const Value&, const std::vector<Change>&, const Documents&) override { return {}; }
		Outcome submit(const Change& _c, const Documents&) override
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
			Value a = Value::object();
			a.set("type", "ask");
			return {{}, {}, a};
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
		Desk<ToyModel, Machine<ToyModel>> d(std::move(first), [&](const Value& _m) { page.push_back(_m); }, [&] { return now; });
		const auto send = [&](const char* _json) { return d.onPageMessage(*elektronData::json::parse(_json)); };
		const auto last = [&](const char* _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == _type)
					return &*it;
			return nullptr;
		};
		check(send(R"({"op":"ready","id":1})") && last("catalogue") && d.readyCount() == 1, "ready: the catalogue, counted");
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
}

int main()
{
	loadQueue();
	lifecycle();
	sequence();
	commands();
	core();
	desk();
	std::printf("deskCoreTest: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
