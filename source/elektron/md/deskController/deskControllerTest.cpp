// deskController's unit test (doc/modern-ux/DESIGN-tr06.md): the TR-06 profile's data, the setup's
// JSON, the routes from a machine's global, the input translation a MIDI thread runs and the knob
// pump that paces edits. Simulated MIDI only: no TR-06 was connected.

#include "deskController.h"

#include "elektronData/jsonSchema.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mmGlobal.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	using namespace deskController;
	using V = Input::Verdict;

	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	elektronData::MdGlobal mdGlobal(const uint8_t _base)
	{
		elektronData::MdGlobal g;
		g.keymap.fill(elektronData::MdGlobal::g_unmapped);
		// a keymap like the machine's own: tracks 1-16 on notes 36.. with a gap, and 35 also track 1
		for(int t = 0; t < 16; ++t)
			g.keymap[static_cast<size_t>(36 + t * 2)] = static_cast<uint8_t>(t);
		g.keymap[35] = 0;
		g.baseChannel = _base;
		return g;
	}

	elektronData::MmGlobal mmGlobal(const uint8_t _base)
	{
		elektronData::MmGlobal g;
		g.baseChannel = _base;
		g.channelSpan = 6;
		g.autoChannel = 16;
		g.multiTrigChannel = 16;
		g.multiMapChannel = 16;
		return g;
	}

	Setup on(Setup _s)
	{
		_s.on = true;
		return _s;
	}

	void data()
	{
		std::printf("profile data (tr06.json)\n");
		const auto& p = tr06();
		check(p.id == "tr06" && p.channel == 9, "the TR-06, channel 10 by default");
		check(p.voices[0].id == "BD" && p.voices[0].notes.size() == 2 && p.voices[0].notes[0] == 36 && p.voices[0].notes[1] == 35, "BD sends 36, 35 is heard as BD too");
		check(p.voices[6].id == "CH" && p.voices[6].notes[0] == 42 && p.voices[6].notes[1] == 44, "CH 42, alias 44");
		check(p.knobs.size() == 41, "41 knobs sent (" + std::to_string(p.knobs.size()) + ")");
		check(p.blockedCc[120] && p.blockedCc[121] && !p.blockedCc[24], "All Sound Off and Reset All Controllers are blocked");
		const auto md = defaults(Machine::Md);
		check(!md.on && md.channel == 9, "defaults are off, channel 10");
		check(md.voices[0].t == 0 && md.voices[6].t == 6 && md.voices[0].note == -1, "MD: the seven voices on tracks 1-7, the keymap decides the note");
		check(md.knobs[24] == Target{-1, 0} && md.knobs[63] == Target{-1, 6} && md.knobs[71] == Target{-1, 7}, "MD: INST levels to SYN 1-7, ACC to SYN 8");
		check(md.knobs[17] == Target{-1, 16} && md.knobs[18] == Target{-1, 19} && md.knobs[19] == Target{-1, 20}, "MD: DRIVE DIST, TIME DEL, DEPTH REV");
		check(!md.knobs[20].mapped() && !md.knobs[113].mapped(), "MD: the other knobs are not mapped");
		const auto mm = defaults(Machine::Mm);
		check(mm.voices[4].t == 4 && mm.voices[5].t == 5 && mm.voices[6].t == 5, "MM: BD-CY tracks 1-5, OH and CH track 6");
		check(mm.voices[0].note == 60 && mm.voices[5].note == 72, "MM: C4, OH an octave up");
		check(mm.knobs[24] == Target{0, 0} && mm.knobs[71] == Target{0, 7}, "MM: INST levels to SYN A-G, ACC SYN H");
		check(mm.knobs[17] == Target{2, 0} && mm.knobs[18] == Target{2, 1} && mm.knobs[19] == Target{7, 0}, "MM: DRIVE filter BASE, TIME WIDTH, DEPTH level");
		check(targetName(Machine::Md, {-1, 16}) == "DIST" && targetName(Machine::Md, {-1, 24}) == "LEVEL" && targetName(Machine::Mm, {2, 0}) == "FLTR BASE",
			"target names");
		check(targets(Machine::Md).size() == 25 && targets(Machine::Mm).size() == 57, "every target of each machine");
	}

	void json()
	{
		std::printf("setup JSON\n");
		auto s = on(defaults(Machine::Mm));
		s.channel = 3;
		s.voices[2] = {4, 48};
		s.knobs[20] = {1, 5};
		s.knobs[24] = {};
		const auto doc = setupToJson(s, Machine::Mm);
		std::vector<std::string> errors;
		const auto back = setupFromJson(doc, Machine::Mm, errors);
		check(back && *back == s && errors.empty(), "a user's mapping round-trips");
		errors.clear();
		check(!setupFromJson(doc, Machine::Md, errors) && !errors.empty(), "a Monomachine setup is not a Machinedrum's");
		const auto bad = [&](const std::string& _text, const std::string& _path)
		{
			std::vector<std::string> e;
			const auto v = elektronData::json::parse(_text);
			const bool refused = v && !setupFromJson(*v, Machine::Md, e) && !e.empty() && e.front().find(_path) != std::string::npos;
			check(refused, "refused: " + _path + (e.empty() ? std::string() : " (" + e.front() + ")"));
		};
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"tr06","channel":17})", "$.channel");
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"tr08","channel":10})", "$.profile");
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"off","channel":10,"knobs":[{"cc":120,"i":0}]})", "$.knobs[0].cc");
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"off","channel":10,"knobs":[{"cc":24,"i":25}]})", "$.knobs[0]");
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"off","channel":10,"voices":[{"voice":"BD","t":16}]})", "$.voices[0].t");
		bad(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"off","channel":10,"voices":[{"voice":"XX","t":1}]})", "$.voices[0].voice");
		errors.clear();
		const auto unmapped = setupFromJson(*elektronData::json::parse(R"({"schema":"desk/controller","version":1,"machine":"md","profile":"tr06","channel":10,"knobs":[{"cc":24,"i":null}]})"),
			Machine::Md, errors);
		check(unmapped && unmapped->on && !unmapped->knobs[24].mapped() && !unmapped->knobs[71].mapped(), "a knob list replaces the defaults; null unmaps");
	}

	void routes()
	{
		std::printf("routes from the machine's global\n");
		const auto s = on(defaults(Machine::Md));
		check(!mdRoute(s, nullptr).known, "no global: no route");
		const auto g = mdGlobal(4);
		const auto r = mdRoute(s, &g);
		check(r.known && r.channel[0] == 4 && r.note[0] == 35, "MD BD: base channel, the first keymap note of track 1 (35)");
		check(r.channel[1] == 4 && r.note[1] == 38 && r.note[6] == 48, "MD SD track 2 note 38, CH track 7 note 48");
		const auto pads = mdRoute(s, &g, true);
		check(pads.channel[1] == 4 && pads.note[0] == 36 && pads.note[1] == 37 && pads.note[6] == 42, "the emulated MD: the track's TRIG note (36 + track), whatever the keymap");
		auto noKey = g;
		noKey.keymap.fill(elektronData::MdGlobal::g_unmapped);
		check(mdRoute(s, &noKey).channel[0] == -1, "MD: a track the keymap has no note for goes nowhere");
		check(r.listens.size() == 4 && r.listens.front().first == 4 && r.listens.back().first == 7, "MD listens on its four base channels");
		check(overlapWarning(s, r, Machine::Md).empty(), "channel 10 and base 5-8: no warning");
		auto clash = s;
		clash.channel = 6;
		check(overlapWarning(clash, r, Machine::Md).find("Channel 7") == 0, "channel 7 inside base 5-8: a warning");

		const auto m = on(defaults(Machine::Mm));
		auto mg = mmGlobal(2);
		const auto mr = mmRoute(m, &mg);
		check(mr.channel[0] == 2 && mr.channel[4] == 6 && mr.channel[5] == 7 && mr.channel[6] == 7 && mr.note[5] == 72, "MM: base + track, the voice's note");
		check(overlapWarning(m, mr, Machine::Mm).empty(), "MM tracks on 3-8, TR-06 on 10: no warning");
		mg.multiTrigChannel = 9;
		check(overlapWarning(m, mmRoute(m, &mg), Machine::Mm).find("MULTI TRIG") != std::string::npos, "the MULTI TRIG channel on 10: a warning");
		mg = mmGlobal(12);
		const auto high = mmRoute(m, &mg);
		check(high.channel[3] == 15 && high.channel[4] == -1, "MM: a track past channel 16 goes nowhere");
	}

	void input()
	{
		std::printf("input translation\n");
		Input in;
		const auto g = mdGlobal(0);
		auto s = defaults(Machine::Md);
		in.configure(s, mdRoute(s, &g));
		check(in.translate(0x99, 36, 100).verdict == V::Pass && in.translate(0xb9, 24, 5).verdict == V::Pass && in.translate(0xfe, 0, 0).verdict == V::Pass,
			"off: everything passes (the processor's own path)");
		s.on = true;
		in.configure(s, mdRoute(s, &g));
		check(in.translate(0x90, 36, 100).verdict == V::Pass && in.translate(0xb0, 24, 5).verdict == V::Pass, "another channel passes untouched");
		const auto n = in.translate(0x99, 36, 100);
		check(n.verdict == V::Note && n.a == 0x90 && n.b == 35 && n.c == 100, "BD (36) on 10 -> the MD's base channel, track 1's note, same velocity");
		const auto alias = in.translate(0x99, 40, 90);
		check(alias.verdict == V::Note && alias.b == 38 && alias.c == 90, "the Rx alias 40 is SD");
		const auto off = in.translate(0x89, 36, 64);
		check(off.verdict == V::Note && off.a == 0x80 && off.b == 35 && off.c == 64, "note off, with its release velocity");
		const auto zero = in.translate(0x99, 38, 0);
		check(zero.verdict == V::Note && zero.a == 0x90 && zero.b == 38 && zero.c == 0, "note on at velocity 0 is a note off");
		check(in.translate(0x99, 60, 100).verdict == V::Block, "a note that is no voice is blocked");
		// a note-off after the mapping changed goes where its note-on went
		in.translate(0x99, 50, 100);
		auto remap = s;
		remap.voices[3].t = 9;
		in.configure(remap, mdRoute(remap, &g));
		const auto late = in.translate(0x89, 50, 0);
		check(late.verdict == V::Note && late.b == 42, "HT's note off follows its note on (track 4's note 42), not the new track");
		check(in.translate(0x99, 50, 100).b == 54, "the next HT goes to track 10 (note 54)");
		in.configure(s, mdRoute(s, &g));

		check(in.translate(0xb9, 24, 70).verdict == V::Knob && in.translate(0xb9, 24, 71).verdict == V::Knob, "BD LEVEL is a knob");
		check(in.knobsMoved() && !in.knobsMoved(), "a knob move is flagged once");
		check(in.takeKnob(24) == 71 && in.takeKnob(24) == -1, "latest wins, taken once");
		check(in.translate(0xb9, 20, 5).verdict == V::Block, "BD TUNE (unmapped) is blocked, not passed raw");
		check(in.translate(0xb9, 120, 0).verdict == V::Block && in.translate(0xb9, 121, 0).verdict == V::Block, "All Sound Off and Reset All Controllers are blocked");
		check(in.translate(0xfe, 0, 0).verdict == V::Block, "Active Sensing is blocked");
		bool realtime = true;
		for(const uint8_t b : {0xf8, 0xfa, 0xfb, 0xfc})
			realtime &= in.translate(b, 0, 0).verdict == V::Pass;
		check(realtime && in.translate(0xf2, 0x10, 0x02).verdict == V::Pass, "Clock, Start, Continue, Stop and Song Position pass");
		check(in.translate(0xc9, 3, 0).verdict == V::Block && in.translate(0xe9, 0, 64).verdict == V::Block, "other channel messages on 10 are blocked");
		check(in.notes() > 0 && in.blocked() > 0, "counts");

		// no route yet (no global): the voices are blocked, never raw to the machine
		Input none;
		none.configure(s, mdRoute(s, nullptr));
		check(none.translate(0x99, 36, 100).verdict == V::Block, "no global yet: the note is dropped");

		// the Monomachine: the track's channel
		Input mm;
		const auto ms = on(defaults(Machine::Mm));
		const auto mg = mmGlobal(0);
		mm.configure(ms, mmRoute(ms, &mg));
		const auto cy = mm.translate(0x99, 49, 110);
		const auto oh = mm.translate(0x99, 46, 80);
		const auto ch = mm.translate(0x99, 42, 80);
		check(cy.a == 0x94 && cy.b == 60 && oh.a == 0x95 && oh.b == 72 && ch.a == 0x95 && ch.b == 60, "MM: CY track 5, OH and CH track 6 at their pitches");
	}

	void pump()
	{
		std::printf("knob pump\n");
		KnobPump p;
		const Target syn1{-1, 0};
		p.in(2, syn1, 10, 1000);
		auto out = p.take(1000);
		check(out.size() == 1 && out[0].track == 2 && out[0].value == 10 && out[0].gesture == 1, "the first move goes at once");
		for(int v = 11; v < 30; ++v)
			p.in(2, syn1, static_cast<uint8_t>(v), 1000 + (v - 10));
		check(p.take(1020).empty(), "a burst within 50 ms waits");
		out = p.take(1050);
		check(out.size() == 1 && out[0].value == 29, "then only the newest value (latest wins)");
		p.in(2, syn1, 29, 1060);
		check(p.take(1110).empty(), "a repeat of the value just sent is dropped");
		p.in(2, syn1, 30, 1120);
		p.in(2, {-1, 16}, 64, 1121);
		p.in(5, syn1, 1, 1122);
		out = p.take(1170);
		check(out.size() == 3 && out[0].gesture == 1 && out[1].gesture == 1 && out[2].gesture == 1, "several targets and tracks: one edit each, one gesture");
		p.in(2, syn1, 30, 2000);
		out = p.take(2000);
		check(out.size() == 1 && out[0].value == 30 && out[0].gesture == 2, "after a pause: a new gesture (a new undo step), the same value goes again");
		// 60 moves a second for 2 s: at most one edit per 50 ms
		KnobPump q;
		size_t edits = 0;
		for(int n = 0; n < 120; ++n)
		{
			const double t = 5000 + n * 1000.0 / 60;
			q.in(0, syn1, static_cast<uint8_t>(n), t);
			edits += q.take(t).size();
		}
		edits += q.take(8000).size();
		check(edits <= 42 && edits >= 38, "a 60 Hz knob for 2 s becomes about 40 edits (" + std::to_string(edits) + ")");
		check(!q.pending(), "nothing left once taken");
	}

	void document()
	{
		std::printf("page document\n");
		const auto s = on(defaults(Machine::Mm));
		const auto g = mmGlobal(0);
		const auto d = pageDocument(s, Machine::Mm, mmRoute(s, &g), 3, Value());
		check(d.find("profile")->asString() == "tr06" && d.find("channel")->asNumber() == 10 && d.find("selected")->asNumber() == 3, "profile, channel, selected track");
		const auto& v = d.find("voices")->asArray();
		check(v.size() == 7 && v[5].find("out")->find("ch")->asNumber() == 6 && v[5].find("out")->find("note")->asNumber() == 72, "OH: channel 6, note 72");
		const auto& k = d.find("knobs")->asArray();
		check(k.size() == 41 && k[1].find("name")->asString() == "FLTR BASE", "every knob with its target's name");
		check(d.find("warning")->asString().empty(), "no warning");
	}

	void activity()
	{
		std::printf("device names and activity\n");
		check(looksLikeTr06("TR-06") && looksLikeTr06("Roland tr06 MIDI") && looksLikeTr06("TR-06 CTRL"), "a TR-06's input names");
		check(!looksLikeTr06("TR-8S") && !looksLikeTr06("IAC Driver Bus 1") && !looksLikeTr06(""), "other inputs are not");
		Monitor m;
		m.seen(0xb9, 24, 50);	// before the panel watched: old
		Activity a;
		a.start(m, 0);
		check(!a.toJson().find("last")->isObject(), "what came before the panel opened is not shown");
		check(a.update(m, 9, 10), "the first look: the channel is set");
		check(!a.update(m, 9, 20), "nothing new: nothing changes");
		m.seen(0xb9, 24, 87);
		check(a.update(m, 9, 30), "a CC on the channel is news");
		auto j = a.toJson();
		check(j.find("last")->find("kind")->asString() == "cc" && j.find("last")->find("n")->asNumber() == 24 && j.find("last")->find("v")->asNumber() == 87, "CC 24 = 87");
		const auto seq = j.find("seq")->asNumber();
		m.seen(0xb9, 24, 87);
		check(a.update(m, 9, 40) && a.toJson().find("seq")->asNumber() > seq, "the same message again is news too (the row blinks again)");
		m.seen(0x99, 38, 100);
		a.update(m, 9, 50);
		check(a.toJson().find("last")->find("kind")->asString() == "note" && a.toJson().find("last")->find("n")->asNumber() == 38, "NOTE 38");
		m.seen(0x99, 38, 0);
		a.update(m, 9, 60);
		check(a.toJson().find("last")->find("kind")->asString() == "off", "a note-on of velocity 0 is a note-off");
		m.seen(0xf8, 0, 0);
		check(!a.update(m, 9, 70), "the clock is no channel's");
		check(a.toJson().find("elsewhere")->isNull(), "the channel is busy: nothing elsewhere");
		// another channel, the controller's quiet for longer than g_elsewhereMs
		m.seen(0x92, 36, 100);
		a.update(m, 9, 3000);
		j = a.toJson();
		check(j.find("elsewhere")->isNumber() && j.find("elsewhere")->asNumber() == 3, "quiet on CH 10, busy on CH 3: elsewhere 3");
		check(j.find("last")->find("n")->asNumber() == 38, "the channel's own last message stays");
		check(a.update(m, 2, 3010) && !a.toJson().find("last")->isObject(), "the channel changed to 3: its last is not the old channel's");
		check(a.toJson().find("elsewhere")->isNull(), "and nothing is elsewhere");
		check(!a.update(m, 2, 9000) && a.toJson().find("elsewhere")->isNull(), "long quiet everywhere: nothing elsewhere, nothing new");
	}

	std::optional<Value> schemaFile(const std::string& _name)
	{
		std::ifstream in(std::string(DESK_SCHEMA_DIR) + "/" + _name);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		return elektronData::json::parse(text);
	}

	void contract()
	{
		std::printf("the contract (doc/modern-ux/*-data-contract.schema.json)\n");
		const auto md = schemaFile("md-data-contract.schema.json");
		const auto mm = schemaFile("mm-data-contract.schema.json");
		check(md && mm, "both schemas load");
		if(!md || !mm)
			return;
		const auto* a = md->find("$defs")->find("controller");
		const auto* b = mm->find("$defs")->find("controller");
		check(a && b && *a == *b, "$defs/controller is the same in both");
		for(const auto m : {Machine::Md, Machine::Mm})
		{
			const elektronData::json::Schema schema(m == Machine::Md ? *md : *mm);
			const auto mdg = mdGlobal(0);
			const auto mmg = mmGlobal(0);
			std::vector<std::string> problems;
			auto s = defaults(m);
			const auto doc = [&](const Setup& _s, const bool _global, const Value& _last)
			{
				const auto r = m == Machine::Md ? mdRoute(_s, _global ? &mdg : nullptr) : mmRoute(_s, _global ? &mmg : nullptr);
				Value msg = Value::object();
				msg.set("type", "controller");
				msg.set("doc", pageDocument(_s, m, r, 2, _last));
				for(const auto& p : schema.validate(msg, "message"))
					problems.push_back(p);
			};
			doc(s, false, Value());
			s.on = true;
			s.channel = 0;
			Value last = Value::object();
			last.set("cc", 24);
			last.set("v", 99);
			doc(s, true, last);
			{
				// what is seen: a TR-06 among the inputs, the activity
				Monitor mon;
				Activity act;
				act.start(mon, 0);
				mon.seen(0xb0, 24, 87);
				act.update(mon, 0, 10);
				Seen seen{true, "TR-06", act.toJson()};
				Value msg = Value::object();
				msg.set("type", "controller");
				msg.set("doc", pageDocument(s, m, Route{}, 2, Value(), seen));
				for(const auto& p : schema.validate(msg, "message"))
					problems.push_back(p);
			}
			for(const auto& p : problems)
				std::printf("    %s\n", p.c_str());
			check(problems.empty(), std::string(machineName(m)) + ": the page document is on the contract (off, no global; on, a warning, a knob moved)");
			Value bad = Value::object();
			bad.set("type", "controller");
			auto d = pageDocument(s, m, Route{}, 2, Value());
			d.put("channel", 17);
			bad.set("doc", std::move(d));
			check(!schema.validate(bad, "message").empty(), std::string(machineName(m)) + ": channel 17 is off the contract (the check checks)");
		}
	}
}

int main()
{
	data();
	json();
	routes();
	input();
	pump();
	document();
	activity();
	contract();
	std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
	return g_failures ? 1 : 0;
}
