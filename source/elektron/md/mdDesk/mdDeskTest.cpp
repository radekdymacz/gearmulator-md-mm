#include <algorithm>
// MD Desk editing model without firmware: commands -> documents, validation,
// undo/redo, copy/paste, the live edits a kit change needs, and the Desk
// orchestrator against a scripted device. Firmware behaviour is covered by
// mdLibTest/mdDeskFirmwareTest.cpp (manual, needs the ROM).

#include "mdDesk.h"
#include "mdDeskLibrary.h"
#include "mdDeskMachine.h"

#include "deskCore/deskContract.h"
#include "deskCore/deskKinds.h"
#include "deskHost/deskHost.h"

#include "elektronData/jsonSchema.h"
#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdSamples.h"
#include "elektronData/mdValidate.h"
#include "elektronData/mdWorkingKit.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace
{
	namespace ed = elektronData;
	using ed::json::Value;
	using namespace mdDesk;

	int g_failures = 0;
	// Every message a desk published in these tests; main() checks them against the contract (P6).
	std::vector<Value> g_published;

	void check(const bool _condition, const char* _what)
	{
		if(_condition)
			return;
		std::fprintf(stderr, "FAIL: %s\n", _what);
		++g_failures;
	}
	void check(const bool _condition, const std::string& _what) { check(_condition, _what.c_str()); }

	// The machine document's kit.working, as the page reads it.
	std::string kitWorking(const Desk& _desk)
	{
		const auto doc = _desk.machine().state(_desk.documents());
		const auto* k = doc.find("kit");
		const auto* w = k ? k->find("working") : nullptr;
		return w && w->isString() ? w->asString() : std::string();
	}

	// apply is pure: the clipboard a copy leaves comes back in the result.
	EditResult run(const Documents& _docs, const Value& _cmd, Clipboard& _clip, const EditContext& _context = {})
	{
		auto r = apply(_docs, _cmd, _clip, _context);
		if(r.clipboard)
			_clip = *r.clipboard;
		return r;
	}


	std::vector<uint8_t> load(const char* _name)
	{
		std::ifstream s(std::string(MDDESK_TESTDATA_DIR) + "/" + _name, std::ios::binary);
		return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
	}

	Value cmd(const std::string& _json)
	{
		auto v = ed::json::parse(_json);
		if(!v)
			std::fprintf(stderr, "bad test JSON: %s\n", _json.c_str());
		return v ? *v : Value();
	}

	Documents fixtureDocs()
	{
		Documents d;
		d.patterns[0] = *ed::decodeMdPattern(load("programmed_pattern_0.syx"));	// 64 locks
		d.patterns[1] = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));	// no locks
		d.kits[0] = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		d.songs[0] = *ed::decodeMdSong(load("programmed_song_0.syx"));
		d.songs[1] = *ed::decodeMdSong(load("programmed_song_1.syx"));
		d.global = *ed::decodeMdGlobal(load("programmed_global_0.syx"));
		return d;
	}

	const ed::MdPattern& pat(const EditResult& _r)
	{
		return std::get<ed::MdPattern>(_r.changes.at(0).after);
	}

	void testTrigsAndLocks()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		const auto& p1 = docs.patterns[1];
		const bool had = ed::hasTrig(p1, 2, 3);

		auto r = run(docs, cmd(R"({"op":"trig","p":1,"t":2,"s":3})"), clip);
		check(r.errors.empty() && r.changes.size() == 1, "trig toggle changes the pattern");
		check(ed::hasTrig(pat(r), 2, 3) != had, "trig toggle flips the step");
		docs.set(r.changes[0].after);

		// Locks live on trigs.
		r = run(docs, cmd(R"({"op":"trig","p":1,"t":2,"s":3,"on":true})"), clip);
		if(!r.changes.empty())
			docs.set(r.changes[0].after);
		r = run(docs, cmd(R"({"op":"lock","p":1,"t":2,"i":12,"s":3,"v":40})"), clip);
		check(r.errors.empty() && ed::lockValue(pat(r), 2, 12, 3) == uint8_t{40}, "lock on a trig");
		docs.set(r.changes[0].after);
		check(ed::usedLockRows(docs.patterns[1]) == 1, "one lock row in use");

		r = run(docs, cmd(R"({"op":"trig","p":1,"t":2,"s":3,"on":false})"), clip);
		check(r.errors.empty() && !ed::hasTrig(pat(r), 2, 3), "trig off");
		check(!ed::lockValue(pat(r), 2, 12, 3) && ed::usedLockRows(pat(r)) == 0,
			"removing a trig removes its locks and frees the row");
		docs.set(r.changes[0].after);

		r = run(docs, cmd(R"({"op":"lock","p":1,"t":2,"i":12,"s":3,"v":40})"), clip);
		check(!r.errors.empty() && r.changes.empty(), "a step without a trig cannot hold a lock");

		// The 64-lock budget: pattern 0 uses all 64 rows.
		const auto& full = docs.patterns[0];
		check(ed::usedLockRows(full) == 64, "fixture pattern 0 has a full lock pool");
		size_t t = 0, param = 0;
		while(t < 16 && (full.lockMasks[t] >> param & 1))
		{
			if(++param == 24)
			{
				param = 0;
				++t;
			}
		}
		size_t s = 0;
		while(s < ed::visibleSteps(full) && !ed::hasTrig(full, t, s))
			++s;
		if(s < ed::visibleSteps(full))
		{
			const auto c = "{\"op\":\"lock\",\"p\":0,\"t\":" + std::to_string(t) + ",\"i\":" + std::to_string(param)
				+ ",\"s\":" + std::to_string(s) + ",\"v\":1}";
			r = run(docs, cmd(c), clip);
			check(!r.errors.empty() && r.errors[0].find("64") != std::string::npos,
				"a 65th locked parameter is refused");
		}

		// Erasing a lock step keeps the row while other steps hold locks.
		for(size_t row = 0; row < 16 * 24; ++row)
		{
			const auto tt = row / 24, pp = row % 24;
			if(!(full.lockMasks[tt] >> pp & 1))
				continue;
			std::vector<size_t> steps;
			for(size_t ss = 0; ss < ed::visibleSteps(full); ++ss)
				if(ed::lockValue(full, tt, pp, ss))
					steps.push_back(ss);
			if(steps.size() < 2)
				continue;
			const auto erased = ed::withoutLock(full, tt, pp, steps[0]);
			check(ed::usedLockRows(erased) == 64 && !ed::lockValue(erased, tt, pp, steps[0])
				&& ed::lockValue(erased, tt, pp, steps[1]), "erasing one lock step keeps the row");
			const auto cleared = ed::withoutLockRow(full, tt, pp);
			check(ed::usedLockRows(cleared) == 63 && ed::validate(cleared).empty(), "clearing a lane frees its row");
			break;
		}
	}

	void testPatternSettingsAndValidation()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		auto r = run(docs, cmd(R"({"op":"totalLength","p":1,"v":48})"), clip);
		check(r.errors.empty() && pat(r).scale == 2 && pat(r).length == 48, "total length sets scale and length");
		r = run(docs, cmd(R"({"op":"totalLength","p":1,"v":20})"), clip);
		check(!r.errors.empty() && r.changes.empty(), "total length 20 is refused");
		r = run(docs, cmd(R"({"op":"length","p":1,"v":0})"), clip);
		check(!r.errors.empty(), "length 0 is refused");
		r = run(docs, cmd(R"({"op":"swing","p":1,"v":65})"), clip);
		check(r.errors.empty() && ed::swingPercent(pat(r).swingAmount) == 65, "swing in percent");
		r = run(docs, cmd(R"({"op":"swing","p":1,"v":81})"), clip);
		check(!r.errors.empty(), "swing above 80 % is refused");
		r = run(docs, cmd(R"({"op":"speed","p":1,"v":"3/4X"})"), clip);
		check(r.errors.empty() && pat(r).tempoMultiplier == 2, "speed by name");
		r = run(docs, cmd(R"({"op":"accentAmount","p":1,"v":15})"), clip);
		check(r.errors.empty() && pat(r).accentAmount == 127, "accent 15 is 127");
		r = run(docs, cmd(R"({"op":"trig","p":9,"t":0,"s":0})"), clip);
		check(!r.errors.empty(), "an unloaded pattern is refused");
		r = run(docs, cmd(R"({"op":"nonsense","p":1})"), clip);
		check(!r.errors.empty(), "unknown commands are refused");
		r = run(docs, cmd(R"({"op":"trig","p":1,"t":16,"s":0})"), clip);
		check(!r.errors.empty(), "track 17 is refused");
	}

	void testCopyPaste()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		auto r = run(docs, cmd(R"({"op":"copySteps","p":0,"t":0,"from":0,"to":16})"), clip);
		check(r.errors.empty() && r.changes.empty() && clip.steps && clip.steps->length == 16,
			"copy fills the clipboard, changes nothing");
		const auto& src = docs.patterns[0];
		r = run(docs, cmd(R"({"op":"pasteSteps","p":1,"t":5,"from":0})"), clip);
		check(r.errors.empty() && r.changes.size() == 1, "paste into another pattern");
		bool same = true;
		for(size_t s = 0; s < 16; ++s)
		{
			same &= ed::hasTrig(src, 0, s) == ed::hasTrig(pat(r), 5, s);
			for(size_t param = 0; param < 24; ++param)
				same &= ed::lockValue(src, 0, param, s) == ed::lockValue(pat(r), 5, param, s);
		}
		check(same, "pasted trigs and locks match the copied page");

		// Pasting into the full pool skips the locks that need new rows.
		r = run(docs, cmd(R"({"op":"copySteps","p":0,"t":0,"from":0,"to":16})"), clip);
		r = run(docs, cmd(R"({"op":"pasteSteps","p":0,"t":15,"from":0})"), clip);
		check(r.errors.empty() && ed::usedLockRows(pat(r)) <= 64 && ed::validate(pat(r)).empty(),
			"paste into a full pool stays within the budget");

		r = run(docs, cmd(R"({"op":"clearSteps","p":0,"t":0,"from":0,"to":16})"), clip);
		bool empty = true;
		for(size_t s = 0; s < 16; ++s)
			empty &= !ed::hasTrig(pat(r), 0, s);
		check(r.errors.empty() && empty, "clear empties the track page");

		// Alt + the lock lane's clear key: every lock of the track; Alt + CLR: the whole pattern. One edit each.
		const auto& before = docs.patterns[0];
		size_t lt = 0, other = 0;
		while(lt < 15 && before.lockMasks[lt] == 0) ++lt;
		other = lt + 1;
		while(other < 15 && before.lockMasks[other] == 0) ++other;
		check(before.lockMasks[lt] != 0 && before.lockMasks[other] != 0, "pattern A01 has locks on two tracks");
		r = run(docs, cmd(R"({"op":"clearLocks","p":0,"t":)" + std::to_string(lt) + "}"), clip);
		check(r.errors.empty() && r.changes.size() == 1 && pat(r).lockMasks[lt] == 0 && pat(r).trigs[lt] == before.trigs[lt]
			&& pat(r).lockMasks[other] == before.lockMasks[other] && ed::validate(pat(r)).empty(), "clearLocks: every lock of the track, its trigs and other tracks stay");
		r = run(docs, cmd(R"({"op":"clearPattern","p":0})"), clip);
		bool cleared = true;
		for(size_t t = 0; t < 16; ++t)
			cleared &= pat(r).trigs[t] == 0 && pat(r).lockMasks[t] == 0 && pat(r).trackAccent[t] == 0 && pat(r).trackSlide[t] == 0;
		check(r.errors.empty() && r.changes.size() == 1 && cleared && ed::usedLockRows(pat(r)) == 0 && pat(r).length == before.length
			&& pat(r).kit == before.kit && ed::validate(pat(r)).empty(), "clearPattern: no trigs, marks or locks on any track; length and kit stay");

		Clipboard none;
		r = run(docs, cmd(R"({"op":"pasteSteps","p":1,"t":0,"from":0})"), none);
		check(!r.errors.empty(), "paste without a copy is refused");
	}

	void testKitEditsAndDelivery()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		const auto& k = docs.kits[0];
		// Live kit edits change the working kit of the kit that plays, and nothing else.
		const EditContext plays{uint8_t{0}};
		check(!run(docs, cmd(R"({"op":"param","k":0,"t":3,"i":16,"v":99})"), clip, plays).errors.empty(),
			"a kit edit without a working kit is refused");
		docs.working = WorkingKit{k};
		check(!run(docs, cmd(R"({"op":"param","k":0,"t":3,"i":16,"v":99})"), clip).errors.empty(),
			"a kit edit for a kit that does not play is refused");
		check(!run(docs, cmd(R"({"op":"kitRename","k":0,"name":"DUB ROOM"})"), clip, plays).errors.empty(),
			"kitRename of the kit that plays is refused (kitName renames it)");
		auto r = run(docs, cmd(R"({"op":"param","k":0,"t":3,"i":16,"v":99})"), clip, plays);
		check(r.errors.empty() && r.changes.size() == 1, "kit param edit");
		auto d = kitDelivery(k, std::get<WorkingKit>(r.changes[0].after).kit);
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::Param && d.edits[0].track == 3
			&& d.edits[0].index == 16 && d.edits[0].value == 99, "a param edit is one CC");
		check(liveEditSysex(d.edits[0]).empty(), "params travel as CCs, not SysEx");

		const auto sd = *ed::mdMachineModel("EFM-SD");
		r = run(docs, cmd("{\"op\":\"machine\",\"k\":0,\"t\":2,\"model\":" + std::to_string(sd) + "}"), clip, plays);
		check(r.errors.empty(), "machine change");
		d = kitDelivery(k, std::get<WorkingKit>(r.changes[0].after).kit);
		check(!d.edits.empty() && d.edits[0].kind == LiveEdit::Kind::Machine && d.edits[0].model == sd,
			"machine change comes first");
		size_t synthResent = 0;
		for(const auto& e : d.edits)
			synthResent += e.kind == LiveEdit::Kind::Param && e.track == 2 && e.index < 8;
		check(synthResent == 8, "all eight synthesis values follow a machine change");
		const auto bytes = liveEditSysex(d.edits[0]);
		check(bytes.size() == 12 && bytes[6] == 0x5b && bytes[7] == 2 && bytes[8] == (sd & 0x7f) && bytes[9] == 0
			&& bytes[10] == 0 && bytes[11] == 0xf7, "assign machine message (manual Appendix C)");

		const auto rom = *ed::mdMachineModel("ROM-05");
		const auto romBytes = ed::mdAssignMachine(0, rom, ed::MdMachineInit::Synthesis);
		check(romBytes[8] == 4 && romBytes[9] == 1, "UW machines set c = 1");

		r = run(docs, cmd(R"({"op":"machine","k":0,"t":2,"model":5})"), clip, plays);
		check(!r.errors.empty(), "an undefined machine id is refused");

		r = run(docs, cmd(R"({"op":"lfo","k":0,"t":1,"field":"shape2","v":4})"), clip, plays);
		d = kitDelivery(k, std::get<WorkingKit>(r.changes.at(0).after).kit);
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::Lfo, "LFO shape is one live edit");
		const auto lfo = liveEditSysex(d.edits[0]);
		check(lfo.size() == 10 && lfo[6] == 0x62 && lfo[7] == ((1 << 3) | 3) && lfo[8] == 4, "set LFO message");
		r = run(docs, cmd(R"({"op":"lfo","k":0,"t":1,"field":"shape2","v":6})"), clip, plays);
		check(!r.errors.empty(), "LFO shape 6 is refused");

		r = run(docs, cmd(R"({"op":"masterFx","k":0,"fx":"rhythmEcho","i":3,"v":10})"), clip, plays);
		d = kitDelivery(k, std::get<WorkingKit>(r.changes.at(0).after).kit);
		const auto fx = liveEditSysex(d.edits.at(0));
		check(fx[6] == 0x5d && fx[7] == 3 && fx[8] == 10, "rhythm echo parameter is 0x5d");

		r = run(docs, cmd(R"({"op":"group","k":0,"t":4,"kind":"mute","target":5})"), clip, plays);
		check(r.errors.empty() && std::get<WorkingKit>(r.changes[0].after).kit.muteGroups[4] == 5, "mute group");
		r = run(docs, cmd(R"({"op":"group","k":0,"t":4,"kind":"mute","target":4})"), clip, plays);
		check(!r.errors.empty(), "a track cannot group with itself");

		r = run(docs, cmd(R"({"op":"kitName","k":0,"name":"DUB ROOM"})"), clip, plays);
		d = kitDelivery(k, std::get<WorkingKit>(r.changes.at(0).after).kit);
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::KitName
			&& liveEditSysex(d.edits[0]).size() == 24, "kit name is 0x55 with 16 bytes");

		// Copy / paste a sound between tracks.
		r = run(docs, cmd(R"({"op":"copySound","k":0,"t":0})"), clip, plays);
		check(clip.sound && r.changes.empty(), "copy sound");
		r = run(docs, cmd(R"({"op":"pasteSound","k":0,"t":9})"), clip, plays);
		const auto& pasted = std::get<WorkingKit>(r.changes.at(0).after).kit;
		check(pasted.models[9] == k.models[0] && pasted.params[9] == k.params[0] && pasted.lfos[9].track == 9,
			"paste sound copies machine and values, the LFO targets its new track");
	}

	void testSongEdits()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		// Song 1 is END only: insert a pattern row before it.
		auto r = run(docs, cmd(R"({"op":"rowInsert","s":1,"i":0,
			"row":{"kind":"pattern","pattern":3,"repeats":1,"start":0,"end":16,"tempo":null,"mutes":[]}})"), clip);
		check(r.errors.empty() && r.changes.size() == 1, "insert a row");
		auto song = std::get<ed::MdSong>(r.changes[0].after);
		check(song.rows.size() == 2 && song.rows[0].pattern == 3 && song.rows[0].repeats == 1, "row landed first");
		docs.set(song);

		r = run(docs, cmd(R"({"op":"rowInsert","s":1,"i":1,"row":{"kind":"pattern","pattern":4,"repeats":0,
			"start":0,"end":16,"tempo":120,"mutes":[2]}})"), clip);
		docs.set(r.changes.at(0).after);
		// A loop back to row 0 after two rows.
		r = run(docs, cmd(R"({"op":"rowInsert","s":1,"i":2,"row":{"kind":"loop","target":0,"repeats":1}})"), clip);
		check(r.errors.empty(), "insert a loop");
		docs.set(r.changes.at(0).after);
		// Inserting before the loop's target moves the target with its row.
		r = run(docs, cmd(R"({"op":"rowInsert","s":1,"i":0,"row":{"kind":"pattern","pattern":7,"repeats":0,
			"start":0,"end":16,"tempo":null,"mutes":[]}})"), clip);
		check(r.errors.empty(), "insert at the top");
		song = std::get<ed::MdSong>(r.changes.at(0).after);
		check(song.rows[3].target == 1 && ed::songRowKind(song.rows[3], 3) == ed::MdSongRowKind::Loop,
			"loop target follows its row");
		docs.set(song);

		r = run(docs, cmd(R"({"op":"rowDelete","s":1,"i":4})"), clip);
		check(!r.errors.empty(), "END cannot be deleted");
		r = run(docs, cmd(R"({"op":"rowSet","s":1,"i":1,"row":{"kind":"pattern","pattern":200,"repeats":0,
			"start":0,"end":16,"tempo":null,"mutes":[]}})"), clip);
		check(!r.errors.empty() && r.changes.empty(), "pattern 200 is refused with the contract's path");
		// Rows now: A08, A04, A05, LOOP -> 1, END. Move A08 behind A05.
		r = run(docs, cmd(R"({"op":"rowMove","s":1,"from":0,"to":2})"), clip);
		check(r.errors.empty(), "move a row");
		song = std::get<ed::MdSong>(r.changes.at(0).after);
		check(song.rows[2].pattern == 7 && song.rows[0].pattern == 3 && song.rows[3].target == 0,
			"moved row lands, the loop still targets A04");
		r = run(docs, cmd(R"({"op":"copyRow","s":1,"i":1})"), clip);
		r = run(docs, cmd(R"({"op":"pasteRow","s":1,"i":1})"), clip);
		check(r.errors.empty() && std::get<ed::MdSong>(r.changes.at(0).after).rows.size() == 6, "paste a row");

		// The 256-row song is full.
		r = run(docs, cmd(R"({"op":"rowInsert","s":0,"i":0,"row":{"kind":"halt"}})"), clip);
		check(!r.errors.empty(), "a 257th row is refused");
	}

	void testGlobal()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		auto r = run(docs, cmd(R"({"op":"route","t":0,"out":"MAIN"})"), clip);
		const auto before = *docs.global;
		if(!r.changes.empty())
		{
			const auto d = globalDelivery(before, std::get<ed::MdGlobal>(r.changes[0].after));
			check(d.edits.size() == 1 && liveEditSysex(d.edits[0])[6] == 0x5c, "routing is 0x5c");
		}
		r = run(docs, cmd(R"({"op":"tempo","bpm":126.5})"), clip);
		check(r.errors.empty() && std::get<ed::MdGlobal>(r.changes.at(0).after).tempo == 3036, "tempo x 24");
		const auto d = globalDelivery(before, std::get<ed::MdGlobal>(r.changes.at(0).after));
		const auto t = liveEditSysex(d.edits.at(0));
		check(t[6] == 0x61 && ((t[7] << 7) | t[8]) == 3036, "set tempo message");
		r = run(docs, cmd(R"({"op":"tempo","bpm":301})"), clip);
		check(!r.errors.empty(), "301 BPM is refused");
	}

	void testHistory()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		History h;
		const auto original = docs.patterns[1];
		for(int s = 0; s < 3; ++s)
		{
			auto r = run(docs, cmd("{\"op\":\"trig\",\"p\":1,\"t\":0,\"s\":" + std::to_string(s) + "}"), clip);
			docs.set(r.changes.at(0).after);
			h.record(r.changes, 0);
		}
		check(h.size() == 3, "three commands, three steps");
		// Undo and redo are next() then done() with what was delivered (all of it here).
		const auto undo = [&h] { auto c = h.next(History::Direction::Undo); if(c) h.done(History::Direction::Undo, *c); return c; };
		const auto redo = [&h] { auto c = h.next(History::Direction::Redo); if(c) h.done(History::Direction::Redo, *c); return c; };
		auto u = undo();
		check(u && u->size() == 1, "undo returns the step");
		docs.set(u->at(0).after);
		u = undo();
		docs.set(u->at(0).after);
		u = undo();
		docs.set(u->at(0).after);
		check(docs.patterns[1] == original && !h.canUndo() && h.canRedo(), "three undos restore the pattern");
		auto re = redo();
		docs.set(re->at(0).after);
		check(ed::hasTrig(docs.patterns[1], 0, 0) != ed::hasTrig(original, 0, 0), "redo re-applies");

		// A drag: one gesture, one undo step, first before + last after.
		History g;
		const auto start = docs.patterns[1];
		for(int v = 10; v < 20; ++v)
		{
			auto r = run(docs, cmd("{\"op\":\"trig\",\"p\":1,\"t\":1,\"s\":" + std::to_string(v) + "}"), clip);
			docs.set(r.changes.at(0).after);
			g.record(r.changes, 77);
		}
		check(g.size() == 1, "a gesture is one undo step");
		// P6: only delivered changes are recorded: an undo that delivers nothing leaves no redo step.
		{
			History d;
			const auto r = run(docs, cmd(R"({"op":"trig","p":1,"t":2,"s":0})"), clip);
			d.record(r.changes, 0);
			d.done(History::Direction::Undo, {});
			check(!d.canUndo() && !d.canRedo(), "an undo nothing of which was delivered is not redoable");
		}
		const auto step = g.next(History::Direction::Undo);
		check(step && std::get<ed::MdPattern>(step->at(0).after) == start, "undoing the gesture restores its start");
	}

	// DESIGN-generators.md §4.3: the two plain edits the generators and the mutation hand their values to.
	// steps: rows of {t, on, acc?} in a range, one pattern change; params: many kit values, one working-kit change.
	void testStepsAndParams()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		const auto steps = [](const std::string& _rows, const std::string& _extra = "") { return cmd(R"({"op":"steps","p":0,"rows":)" + _rows + _extra + "}"); };
		const auto& full = docs.patterns[0];	// 64 locks
		// A track with a locked trig: turning that step off drops its locks, a kept step keeps them.
		size_t lt = 16, ls = 0, lp = 0;
		[&]
		{
			for(size_t t = 0; t < 16; ++t)
				for(size_t s = 0; s < ed::visibleSteps(full); ++s)
					for(size_t i = 0; i < 24; ++i)
						if(ed::hasTrig(full, t, s) && ed::lockValue(full, t, i, s))
						{
							lt = t; ls = s; lp = i;
							return;
						}
		}();
		check(lt < 16, "pattern A01 has a locked trig");
		if(lt == 16)
			return;
		std::string on, keepOn;
		uint64_t want = 0;
		for(size_t s = 0; s < ed::visibleSteps(full); ++s)
		{
			if(ed::hasTrig(full, lt, s) && s != ls)
			{
				on += (on.empty() ? "" : ",") + std::to_string(s);
				want |= uint64_t{1} << s;
			}
		}
		const auto rowsOf = [&](const std::string& _on) { return "[{\"t\":" + std::to_string(lt) + ",\"on\":[" + _on + "]}]"; };
		auto r = run(docs, steps(rowsOf(on)), clip);
		check(r.errors.empty() && r.changes.size() == 1 && pat(r).trigs[lt] == want && !ed::lockValue(pat(r), lt, lp, ls)
			&& ed::validate(pat(r)).empty(), "steps: the step turned off loses its locks");
		bool kept = true;
		for(size_t s = 0; s < ed::visibleSteps(full); ++s)
			for(size_t i = 0; i < 24; ++i)
				if(s != ls)
					kept &= ed::lockValue(pat(r), lt, i, s) == ed::lockValue(full, lt, i, s);
		check(kept, "steps: every step kept on keeps its locks");
		for(size_t t = 0; t < 16; ++t)
			kept &= t == lt || (pat(r).trigs[t] == full.trigs[t] && pat(r).lockMasks[t] == full.lockMasks[t]);
		check(kept, "steps: tracks without a row stay");
		keepOn = on + (on.empty() ? "" : ",") + std::to_string(ls);
		r = run(docs, steps(rowsOf(keepOn)), clip);
		check(r.errors.empty() && r.changes.empty(), "steps: the same steps change nothing");

		// A range: only [from, to) moves; one change for 16 rows (one dump, one undo step).
		const auto& p1 = docs.patterns[1];
		std::string all = "[";
		for(int t = 0; t < 16; ++t)
			all += std::string(t ? "," : "") + "{\"t\":" + std::to_string(t) + ",\"on\":[" + std::to_string(16 + t % 4) + ",20,28]}";
		all += "]";
		r = run(docs, cmd(R"({"op":"steps","p":1,"from":16,"to":32,"rows":)" + all + "}"), clip);
		bool range = r.errors.empty() && r.changes.size() == 1;
		for(size_t t = 0; range && t < 16; ++t)
			for(size_t s = 0; s < 64; ++s)
			{
				const bool in = s >= 16 && s < 32;
				const bool on2 = s == 16 + t % 4 || s == 20 || s == 28;
				range &= ed::hasTrig(pat(r), t, s) == (in ? on2 : ed::hasTrig(p1, t, s));
			}
		check(range, "steps: 16 rows in steps 17-32, one change, the steps outside the range stay");

		// Accents: exactly acc in the range; EDIT ALL leaves them and says so.
		r = run(docs, cmd(R"({"op":"steps","p":1,"from":0,"to":16,"rows":[{"t":2,"on":[0,4,8,12],"acc":[4,12]}]})"), clip);
		check(r.errors.empty() && (pat(r).trackAccent[2] & 0xffff) == ((1u << 4) | (1u << 12)) && (pat(r).trigs[2] & 0xffff) == 0x1111,
			"steps: acc sets the track's accents in the range");
		{
			auto d2 = docs;
			d2.patterns[1].accentEditAll = 1;
			d2.patterns[1].trackAccent[2] = 0;
			const auto before = d2.patterns[1].accentPattern;
			r = run(d2, cmd(R"({"op":"steps","p":1,"from":0,"to":16,"rows":[{"t":2,"on":[0,4,8,12],"acc":[4,12]}]})"), clip);
			check(r.errors.empty() && r.changes.size() == 1 && pat(r).accentPattern == before && pat(r).trackAccent[2] == 0
				&& (pat(r).trigs[2] & 0xffff) == 0x1111 && r.note.find("EDIT ALL") != std::string::npos, "steps: with EDIT ALL on, acc is refused with a note (accent is pattern-wide)");
		}
		check(!run(docs, cmd(R"({"op":"steps","p":1,"from":0,"to":16,"rows":[{"t":2,"on":[16]}]})"), clip).errors.empty(), "steps: a step outside the range is refused");
		check(!run(docs, cmd(R"({"op":"steps","p":1,"rows":[{"t":2,"on":[1]},{"t":2,"on":[2]}]})"), clip).errors.empty(), "steps: a track in two rows is refused");
		check(!run(docs, cmd(R"({"op":"steps","p":1,"rows":[{"t":2,"on":[1],"acc":[2]}]})"), clip).errors.empty(), "steps: an accent without a trig is refused");
		check(!run(docs, cmd(R"({"op":"steps","p":1,"rows":[]})"), clip).errors.empty(), "steps: no rows is refused");
		check(!run(docs, cmd(R"({"op":"steps","p":1,"rows":[{"t":16,"on":[]}]})"), clip).errors.empty(), "steps: track 17 is refused");

		// params: one working-kit change, a CC for each value that changed and nothing else.
		const EditContext plays{uint8_t{0}};
		const auto& k = docs.kits[0];
		docs.working = WorkingKit{k};
		const int v1 = (k.params[1][2] + 9) & 127, v2 = (k.params[5][9] + 40) & 127;
		r = run(docs, cmd("{\"op\":\"params\",\"k\":0,\"values\":[[1,2," + std::to_string(v1) + "],[5,9," + std::to_string(v2) + "],[3,4,"
			+ std::to_string(k.params[3][4]) + "]]}"), clip, plays);
		check(r.errors.empty() && r.changes.size() == 1, "params: one working-kit change");
		auto d = kitDelivery(k, std::get<WorkingKit>(r.changes[0].after).kit);
		check(d.edits.size() == 2 && d.edits[0].kind == LiveEdit::Kind::Param && d.edits[1].kind == LiveEdit::Kind::Param
			&& d.edits[0].track == 1 && d.edits[0].index == 2 && d.edits[0].value == v1 && d.edits[1].track == 5 && d.edits[1].value == v2,
			"params: two CCs for the two changed values, none for the unchanged one");
		check(!run(docs, cmd(R"({"op":"params","k":0,"values":[[1,24,3]]})"), clip, plays).errors.empty(), "params: parameter 25 is refused");
		check(!run(docs, cmd(R"({"op":"params","k":0,"values":[[1,2,128]]})"), clip, plays).errors.empty(), "params: 128 is refused");
		check(!run(docs, cmd(R"({"op":"params","k":0,"values":[[1,2]]})"), clip, plays).errors.empty(), "params: a pair is refused");
		check(!run(docs, cmd(R"({"op":"params","k":1,"values":[[1,2,3]]})"), clip, plays).errors.empty(), "params: a kit that does not play is refused");

		// A mutation trial: three applies from the base with one gesture are one undo step back to the base.
		History h;
		auto trial = docs;
		for(int n = 0; n < 3; ++n)
		{
			r = run(trial, cmd("{\"op\":\"params\",\"k\":0,\"values\":[[0,0," + std::to_string(10 + n) + "],[0,1," + std::to_string(20 + n) + "]]}"), clip, plays);
			trial.set(r.changes.at(0).after);
			h.record(r.changes, 42);
		}
		const auto undo = h.next(History::Direction::Undo);
		check(h.size() == 1 && undo && std::get<WorkingKit>(undo->at(0).after).kit == k, "params: a three-apply trial with one g is one undo step, back to the base");

		// A live GEN run (DESIGN-generators.md §4.6): every change of the GEN bar sends steps at once, each from
		// the pattern the last one made, all with the run's g: one undo step back to the pattern before the run.
		// The next run (another g) is its own step.
		{
			History hg;
			auto live = docs;
			const auto before = live.patterns[1];
			for(int n = 0; n < 4; ++n)
			{
				const auto rows = "[{\"t\":3,\"on\":[" + std::to_string(n) + "," + std::to_string(n + 4) + ",12]}" + (n == 3 ? ",{\"t\":5,\"on\":[1,9]}" : "") + "]";
				r = run(live, cmd(R"({"op":"steps","p":1,"from":0,"to":16,"rows":)" + rows + "}"), clip);
				check(r.errors.empty() && r.changes.size() == 1, "steps: a live GEN change is one pattern change");
				if(r.changes.empty())
					return;
				live.set(r.changes.at(0).after);
				hg.record(r.changes, 77);
			}
			const auto u = hg.next(History::Direction::Undo);
			check(hg.size() == 1 && u && std::get<ed::MdPattern>(u->at(0).after) == before, "steps: a four-change GEN run with one g (one change on two tracks) is one undo step, back to the pattern before the run");
			r = run(live, cmd(R"({"op":"steps","p":1,"from":0,"to":16,"rows":[{"t":3,"on":[2]}]})"), clip);
			hg.record(r.changes, 78);
			check(hg.size() == 2, "steps: the next run (another g) is its own undo step");
		}
	}

	// Through the Desk: a whole-pattern generator (16 rows) is one pattern dump and one undo step; a
	// mutation trial (three params with one g) is CCs only and one undo step back to the base.
	// DESIGN-generators.md §7: rotate a track (Alt + arrows) and double the pattern, one pattern change each.
	void testRotateAndDouble()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		const auto& full = docs.patterns[0];
		size_t lt = 0;
		while(lt < 15 && full.lockMasks[lt] == 0)
			++lt;
		const size_t len = std::min<size_t>(full.length, ed::visibleSteps(full));
		const auto rot = [&](const Documents& _d, const int _by) { return run(_d, cmd("{\"op\":\"rotate\",\"p\":0,\"t\":" + std::to_string(lt) + ",\"by\":" + std::to_string(_by) + "}"), clip); };
		auto r = rot(docs, 1);
		bool moved = r.errors.empty() && r.changes.size() == 1 && full.lockMasks[lt] != 0;
		for(size_t s = 0; moved && s < len; ++s)
		{
			const auto to = (s + 1) % len;
			moved &= ed::hasTrig(pat(r), lt, to) == ed::hasTrig(full, lt, s) && ((pat(r).trackAccent[lt] >> to & 1) == (full.trackAccent[lt] >> s & 1));
			for(size_t i = 0; i < 24; ++i)
				moved &= ed::lockValue(pat(r), lt, i, to) == ed::lockValue(full, lt, i, s);
		}
		for(size_t t = 0; t < 16; ++t)
			moved &= t == lt || (pat(r).trigs[t] == full.trigs[t] && pat(r).lockMasks[t] == full.lockMasks[t]);
		check(moved && ed::usedLockRows(pat(r)) == ed::usedLockRows(full) && ed::validate(pat(r)).empty(),
			"rotate: the track's trigs, accents and locks move one step later, wrapping at the length; other tracks stay");
		auto later = docs;
		later.set(r.changes.at(0).after);
		r = rot(later, -1);
		check(r.errors.empty() && pat(r) == full, "rotate: one step later then one earlier is the pattern again");
		check(rot(docs, int(len)).changes.empty(), "rotate: by the length changes nothing");
		{
			History h;
			auto d = docs;
			for(int n = 0; n < 3; ++n)
			{
				r = rot(d, 1);
				d.set(r.changes.at(0).after);
				h.record(r.changes, 77);
			}
			const auto undo = h.next(History::Direction::Undo);
			check(h.size() == 1 && undo && std::get<ed::MdPattern>(undo->at(0).after) == full, "rotate: a train of presses with one g is one undo step");
		}

		// Double: 16 steps (total 16) become 32 (total 32), the new half a copy of the first.
		auto d2 = docs;
		auto& q = d2.patterns[0];
		q.length = 16;
		q.scale = 0;
		r = run(d2, cmd(R"({"op":"doublePattern","p":0})"), clip);
		bool copied = r.errors.empty() && r.changes.size() == 1 && pat(r).length == 32 && ed::visibleSteps(pat(r)) == 32;
		for(size_t t = 0; copied && t < 16; ++t)
			for(size_t s = 0; s < 16; ++s)
			{
				copied &= ed::hasTrig(pat(r), t, s) == ed::hasTrig(q, t, s) && ed::hasTrig(pat(r), t, s + 16) == ed::hasTrig(q, t, s);
				copied &= accentOn(pat(r), t, s + 16) == accentOn(q, t, s) && slideOn(pat(r), t, s + 16) == slideOn(q, t, s);
				for(size_t i = 0; i < 24; ++i)
					copied &= ed::lockValue(pat(r), t, i, s + 16) == ed::lockValue(q, t, i, s) && ed::lockValue(pat(r), t, i, s) == ed::lockValue(q, t, i, s);
			}
		check(copied && ed::validate(pat(r)).empty(), "doublePattern: length and total 16 -> 32, every track's trigs, marks and locks copied into the new half");
		check(r.note.find("16 to 32") != std::string::npos, "doublePattern: the note says what it did");
		q.length = 48;
		q.scale = 3;
		check(!run(d2, cmd(R"({"op":"doublePattern","p":0})"), clip).errors.empty(), "doublePattern: 48 steps cannot double (64 is the longest)");
		q.extended = false;
		q.length = 32;
		q.scale = 1;
		check(!run(d2, cmd(R"({"op":"doublePattern","p":0})"), clip).errors.empty(), "doublePattern: above 32 steps needs EXTENDED");
	}

	void testStepsAndParamsDesk()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<Value> page;
		std::vector<std::array<uint8_t, 3>> params;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		const auto undoCount = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(const auto* t = it->find("type"); t && t->asString() == "machine")
					return static_cast<int>(it->find("doc")->find("history")->find("undoCount")->asNumber());
			return -1;
		};
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		auto pattern = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		const auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		pattern.kit = kit.position;
		desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
		desk.onDeviceSysex(status(ed::MdStatus::LockMode, 1));
		desk.onDeviceSysex(ed::encodeMdPattern(pattern));
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		now = 100;
		wire.clear();
		const int undo0 = undoCount();
		std::string rows = "[";
		for(int t = 0; t < 16; ++t)
			rows += std::string(t ? "," : "") + "{\"t\":" + std::to_string(t) + ",\"on\":[0," + std::to_string(4 + t % 8) + "]}";
		desk.onPageMessage(cmd("{\"op\":\"steps\",\"p\":1,\"from\":0,\"to\":16,\"rows\":" + rows + "],\"g\":300,\"id\":1}"));
		size_t dumps = 0;
		for(const auto& w : wire)
			dumps += ed::mdDumpCommand(w) == ed::g_mdPatternDump;
		check(dumps == 1 && undoCount() == undo0 + 1, "steps through the desk: 16 rows are one pattern dump and one undo step");
		const auto sent = *ed::decodeMdPattern(wire.at(0));
		check(ed::hasTrig(sent, 15, 11) && ed::hasTrig(sent, 0, 0), "the dump carries every row");

		params.clear();
		wire.clear();
		const int undo1 = undoCount();
		for(int n = 0; n < 3; ++n)
			desk.onPageMessage(cmd("{\"op\":\"params\",\"k\":0,\"values\":[[2,0," + std::to_string(30 + n) + "],[2,1," + std::to_string(kit.params[2][1])
				+ "]],\"g\":301,\"id\":" + std::to_string(10 + n) + "}"));
		bool ccOnly = true;
		for(const auto& w : wire)
			ccOnly &= ed::mdDumpCommand(w) != ed::g_mdKitDump;
		check(ccOnly && params.size() == 3 && params[2] == std::array<uint8_t, 3>{2, 0, 32} && undoCount() == undo1 + 1,
			"a params trial: one CC per changed value per apply, no kit dump, one undo step");
		params.clear();
		desk.onPageMessage(cmd(R"({"op":"undo","id":20})"));
		check(params.size() == 1 && params[0] == std::array<uint8_t, 3>{2, 0, kit.params[2][0]} && desk.documents().working->kit.params[2] == kit.params[2],
			"undo returns the trial to the base in one step");
	}

	// DESIGN-edit-flow.md: paced, latest wins, one read-back at quiet.
	void testPushSlot()
	{
		PushSlot<int> slot;
		using R = PushSlot<int>::ReadBack;
		using D = PushSlot<int>::Due;
		const deskCore::PushPolicy p{200, 150};
		check(slot.onReadBack(1) == R::NotWaiting, "a refresh while idle");
		check(slot.want(1, 0, p), "first edit goes out at once");
		check(!slot.want(2, 16, p) && !slot.want(3, 33, p) && slot.next() == 3, "later edits wait, the latest wins");
		check(slot.due(100, p) == D::Nothing, "no dump before the interval, no read-back mid-gesture");
		check(slot.due(200, p) == D::Send && slot.takeNext(200) == 3 && !slot.next(), "the waiting value goes after 200 ms");
		check(slot.onReadBack(1) == R::Other, "an older value does not confirm");
		check(!slot.want(4, 250, p) && slot.due(399, p) == D::Nothing && slot.due(400, p) == D::Send, "the next dump 200 ms after the last");
		slot.takeNext(400);
		check(slot.due(399 + 1, p) == D::ReadBack, "the read-back once the gesture is quiet (150 ms since the last edit)");
		check(slot.inFlight() == 4, "the last value is the one in flight");
		slot.askedBack(350);
		check(slot.due(400, p) == D::Nothing, "asked once");
		check(!slot.timedOut(2000, 2000) && slot.timedOut(2351, 2000), "the timeout counts from the ask");
		check(slot.onReadBack(3) == R::Other && slot.onReadBack(4) == R::Confirmed && !slot.busy(), "the last value confirms: idle");
		// 60 edits a second for 2 s: at most 5 dumps a second, one read-back.
		int dumps = 0, asks = 0;
		for(int ms = 1000; ms < 3500; ms += 2)
		{
			if(ms < 3000 && ms % 16 == 0 && slot.want(ms, ms, p)) ++dumps;
			switch(slot.due(ms, p))
			{
			case D::Send: slot.takeNext(ms); ++dumps; break;
			case D::ReadBack: slot.askedBack(ms); ++asks; break;
			case D::Nothing: break;
			}
		}
		check(dumps <= 11 && dumps >= 9 && asks == 1 && slot.inFlight() && *slot.inFlight() == 2992, "a 2 s draw: "
			+ std::to_string(dumps) + " dumps, 1 read-back, the last value");
	}

	// The Desk against a scripted device: the page's view of one edit.
	void testDesk()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<Value> page;
		std::vector<std::array<uint8_t, 3>> params;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});	// a host without sequencer telemetry says so
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		const auto lastOf = [&](const char* _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(const auto* t = it->find("type"); t && t->asString() == _type)
					return &*it;
			return nullptr;
		};

		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		check(lastOf("catalogue") != nullptr, "the page gets the machine catalogue");
		desk.onPageMessage(cmd(R"({"op":"trig","p":1,"t":0,"s":0,"id":1})"));
		const auto* busy = lastOf("result");
		check(busy && !busy->find("ok")->asBool(), "edits wait until the machine answers (device busy)");

		auto pattern = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		pattern.kit = kit.position;
		desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
		desk.onDeviceSysex(status(ed::MdStatus::LockMode, 1));
		check(desk.isReady(), "the first status reply means ready");
		desk.onDeviceSysex(ed::encodeMdPattern(pattern));
		desk.onDeviceSysex(ed::encodeMdKit(kit));

		wire.clear();
		now = 100;
		desk.onPageMessage(cmd(R"({"op":"trig","p":1,"t":0,"s":1,"id":2})"));
		const auto* ok = lastOf("result");
		check(ok && ok->find("ok")->asBool(), "trig command accepted");
		check(!wire.empty() && ed::mdDumpCommand(wire[0]) == ed::g_mdPatternDump, "a pattern dump went out");
		const auto sent = *ed::decodeMdPattern(wire[0]);
		check(ed::hasTrig(sent, 0, 1) != ed::hasTrig(pattern, 0, 1), "the dump carries the edit");
		const auto* doc = lastOf("doc");
		check(doc && doc->find("pending")->asBool(), "the page sees the edit as pending");
		check(desk.isBusy(), "TX: busy while the read-back is due");

		check(wire.size() == 1, "the dump alone: no read-back while the gesture may go on (DESIGN-edit-flow.md)");
		// A second edit within 200 ms waits its turn (latest wins); one read-back once the gesture is quiet.
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"trig","p":1,"t":0,"s":2,"id":3,"g":5})"));
		check(wire.empty(), "no second dump within 200 ms of the first");
		now = 200;
		desk.tick();
		check(wire.empty(), "still waiting its turn at 100 ms");
		now = 300;
		desk.tick();
		check(wire.size() == 1 && ed::mdDumpCommand(wire[0]) == ed::g_mdPatternDump
			&& ed::hasTrig(*ed::decodeMdPattern(wire[0]), 0, 2) != ed::hasTrig(pattern, 0, 2), "the waiting edit goes out 200 ms after the first");
		const auto second = wire[0];
		now = 310;
		desk.tick();
		check(wire.size() == 2 && wire[1].size() > 6 && wire[1][6] == 0x68, "one read-back request once the gesture is quiet");
		now = 358;
		desk.onDeviceSysex(ed::encodeMdPattern(sent));
		check(desk.isBusy(), "the older value does not confirm");
		desk.onDeviceSysex(second);
		check(desk.lastRoundTripMs() == 48, "round trip measured from the read-back request");
		desk.tick();
		check(!desk.isBusy(), "idle after the read-back of the last dump");

		// A live kit edit is a CC, marks the kit edited, never a dump.
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"param","k":0,"t":1,"i":4,"v":77,"id":4})"));
		check(params.size() == 1 && params[0] == std::array<uint8_t, 3>{1, 4, 77}, "kit param goes to the CC path");
		check(wire.empty(), "no kit dump for a live edit");
		check(kitWorking(desk) == "edited", "kit is edited");
		// LOAD KIT over the edits asks: load without saving (the command with force), or save and load.
		desk.onPageMessage(cmd(R"({"op":"kitLoad","k":3,"id":40})"));
		const auto* la = lastOf("ask");
		const auto* alts = la ? la->find("alternatives") : nullptr;
		check(la && la->find("ask")->asString() == "loadKit" && la->find("confirm")->asString() == "Load without saving" && wire.empty()
			&& alts && alts->asArray().size() == 1 && alts->asArray()[0].find("label")->asString() == "Save and load"
			&& alts->asArray()[0].find("first")->asArray()[0].find("op")->asString() == "saveKit"
			&& la->find("message")->asString().find("unsaved changes") != std::string::npos,
			"LOAD KIT over unsaved edits asks: Save and load (saveKit first) or Load without saving");
		// A stored-slot dump does not overwrite the working copy.
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		check(desk.documents().working && desk.documents().working->kit.params[1][4] == 77 && desk.documents().kits.at(0).params[1][4] == kit.params[1][4],
			"the working kit survives a stored-slot dump, which is the stored slot only");

		// Editing another kit than the one playing is refused.
		desk.onPageMessage(cmd(R"({"op":"param","k":3,"t":1,"i":4,"v":1,"id":5})"));
		check(!lastOf("result")->find("ok")->asBool(), "a kit that is not loaded or not playing is refused");

		// Undo reverses the kit edit through the same live path.
		params.clear();
		desk.onPageMessage(cmd(R"({"op":"undo","id":6})"));
		check(params.size() == 1 && params[0][2] == kit.params[1][4], "undo sends the old value as a CC");

		// Selecting a pattern linked to another kit while the kit is edited asks first.
		desk.onPageMessage(cmd(R"({"op":"param","k":0,"t":1,"i":4,"v":90,"id":7})"));
		auto other = pattern;
		other.position = 9;
		other.kit = 5;
		desk.onDeviceSysex(ed::encodeMdPattern(other));
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"id":8})"));
		const auto* ask = lastOf("ask");
		check(ask && ask->find("target")->asNumber() == 5 && wire.empty(), "switching away from edits asks first");
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"force":true,"id":9})"));
		check(!wire.empty() && wire[0][6] == 0x57, "forced select sends LOAD PATTERN");

		// A push without a read-back is reported, not silently lost.
		page.clear();
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"trig","p":1,"t":3,"s":3,"id":10})"));
		now += 200;
		desk.tick();
		check(lastOf("error") == nullptr, "the read-back is asked for at quiet");
		now += 2100;
		desk.tick();
		check(lastOf("error") != nullptr, "a push without a read-back is reported");
	}

	// The working kit read from memory is the truth for the current kit: edits the
	// desk never saw (panel, DAW restore) show, edited/clean follows the stored slot.
	void testWorkingKitMemory()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [&](uint8_t, uint8_t, uint8_t) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});	// a host without sequencer telemetry says so
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		const auto region = [](const ed::MdKit& _k)
		{
			auto image = ed::mdWorkingKitImage(_k);
			std::vector<uint8_t> r{_k.position, 0};
			r.insert(r.end(), image.begin(), image.end());
			return r;
		};
		const auto machine = [&]() -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "machine")
					return it->find("doc");
			return nullptr;
		};
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		kit.position = 3;
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, 3));
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		check(kitWorking(desk) != "edited", "stored kit loaded");

		// A value changed on the machine's panel, never seen by the desk.
		auto panel = kit;
		panel.params[2][5] = static_cast<uint8_t>(kit.params[2][5] ^ 0x11);
		desk.onWorkingKitMemory(region(panel));
		check(desk.documents().working->kit.params[2][5] == panel.params[2][5], "memory edit shows in the working kit");
		check(kitWorking(desk) == "edited", "memory differs: edited");
		const Value* wdoc = nullptr;
		for(auto it = page.rbegin(); it != page.rend() && !wdoc; ++it)
			if(it->find("type")->asString() == "doc" && it->find("kind")->asString() == "workingKit")
				wdoc = &*it;
		check(wdoc && wdoc->find("source")->asString() == "memory" && wdoc->find("slot")->asNumber() == 3,
			"the page is told: the working kit of K04, from memory");
		// A stored-slot dump keeps the memory copy.
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		check(desk.documents().working->kit.params[2][5] == panel.params[2][5], "a stored dump does not undo memory");
		// Back to the stored values: clean without SAVE KIT.
		desk.onWorkingKitMemory(region(kit));
		check(kitWorking(desk) == "clean", "memory equals slot: clean");

		// Right after the desk's own live edit, an image is held (it may predate the CC).
		now = 1000;
		desk.onPageMessage(cmd(R"({"op":"param","k":3,"t":0,"i":1,"v":5,"id":1})"));
		desk.onWorkingKitMemory(region(kit));
		check(desk.documents().working->kit.params[0][1] == 5 && desk.coreState().state({DocKind::WorkingKit, 0})->pending,
			"held: the edit is pending until memory shows it");
		auto after = kit;
		after.params[0][1] = 5;
		desk.onWorkingKitMemory(region(after));
		now = 1200;
		desk.tick();
		check(desk.documents().working->kit.params[0][1] == 5 && !desk.coreState().state({DocKind::WorkingKit, 0})->pending,
			"settled by the image that shows the edit");

		// Host automation twice within one image: the second builds on the first.
		desk.onHostKitParam(1, 2, 40);
		desk.onHostKitParam(1, 3, 41);
		check(desk.documents().working->kit.params[1][2] == 40 && desk.documents().working->kit.params[1][3] == 41,
			"two automated parameters within one memory image both hold");
		// SAVE KIT right after a live edit, before memory shows it: the slot holds the edit.
		now = 1300;
		desk.onPageMessage(cmd(R"({"op":"param","k":3,"t":0,"i":2,"v":77,"id":2})"));
		desk.onPageMessage(cmd(R"({"op":"saveKit","id":3})"));
		desk.tick();
		const auto saved = desk.documents().kits.find(3);
		check(saved != desk.documents().kits.end() && saved->second.params[0][2] == 77 && saved->second.params[1][2] == 40,
			"SAVE KIT right after an edit stores the edit and the automation");
		after = desk.documents().working->kit;
		desk.onWorkingKitMemory(region(after));
		now = 1500;
		desk.tick();

		// Memory names another kit than status: ask status, apply once it agrees.
		auto next = kit;
		next.position = 7;
		wire.clear();
		desk.onWorkingKitMemory(region(next));
		bool asked = false;
		for(const auto& w : wire)
			asked |= w.size() == 9 && w[6] == 0x70 && w[7] == static_cast<uint8_t>(ed::MdStatus::Kit);
		check(asked && desk.documents().working->kit.position == 3, "a kit switch seen in memory asks for status first");
		desk.onDeviceSysex(status(ed::MdStatus::Kit, 7));
		desk.tick();
		check(desk.documents().working && desk.documents().working->kit == next, "then the new kit comes from memory");
		check(!ed::mdWorkingKitFromMemory(std::vector<uint8_t>(10, 0)), "a short region is refused");

		// P6: the desk outlives the machine: a reboot (a restored project) starts over.
		desk.setProbe(Desk::Probe::Loading);
		check(!desk.isInputReady(), "restoring: no input");
		page.clear();
		desk.setProbe(Desk::Probe::Running);
		bool reset = false;
		for(const auto& m : page)
			reset |= m.find("type")->asString() == "reset";
		check(reset && desk.documents().kits.empty() && !desk.documents().working && !desk.coreState().history().canUndo(), "a reboot: the page starts over, nothing old is kept");
	}

	// Knob moves while live recording become panel steps: select, page, turn.
	void testKnobRecorder()
	{
		ed::MdKit memory;
		memory.params[3][12] = 40;
		KnobRecorder k;
		k.want(3, 12, 99);
		auto s = k.next(0, 0, &memory);
		check(s && s->kind == KnobStep::Kind::SelectTrack && s->track == 3, "first: select the track");
		check(!k.next(10, 0, &memory), "then wait for the SET STATUS to land");
		s = k.next(50, 0, &memory);
		check(s && s->kind == KnobStep::Kind::PageKey, "then the page key (param 12 is on the effects page)");
		check(!k.next(100, 0, &memory), "a page key needs time before the next");
		s = k.next(300, 1, &memory);
		check(s && s->kind == KnobStep::Kind::Turn && s->encoder == 4 && s->steps == 32, "turn knob E by at most 32");
		check(!k.next(320, 1, &memory), "wait for memory to show the turn");
		memory.params[3][12] = 72;
		s = k.next(400, 1, &memory);
		check(s && s->kind == KnobStep::Kind::Turn && s->steps == 27, "turn the rest");
		memory.params[3][12] = 99;
		check(!k.next(500, 1, &memory) && !k.pending(), "done when memory shows the value");
		k.want(3, 1, 10);
		memory.params[3][1] = 12;
		s = k.next(600, 1, &memory);
		check(s && s->kind == KnobStep::Kind::PageKey, "same track, other page: no second select");
		s = k.next(800, 0, &memory);
		check(s && s->kind == KnobStep::Kind::Turn && s->encoder == 1 && s->steps == -2, "turns down too");
		check(!k.next(900, 0, nullptr), "nothing without memory");
		k.reset();
		check(!k.pending(), "reset forgets pending moves");
	}

	// The desk's REC key and what it refuses while the firmware records.
	void testDeskRecording()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<std::string> keys;
		std::vector<std::pair<uint8_t, int>> turns;
		std::vector<std::array<uint8_t, 3>> params;
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.device.pressKey = [&](const std::string& _k) { keys.push_back(_k); return true; };
		port.device.turnKnob = [&](uint8_t _e, int _s) { turns.emplace_back(_e, _s); return true; };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});	// a host without sequencer telemetry says so
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		const auto ok = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result")
					return it->find("ok")->asBool();
			return false;
		};
		auto pattern = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		pattern.kit = kit.position;
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
		desk.onDeviceSysex(ed::encodeMdPattern(pattern));
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		auto image = ed::mdWorkingKitImage(kit);
		std::vector<uint8_t> region{kit.position, 0};
		region.insert(region.end(), image.begin(), image.end());
		desk.onWorkingKitMemory(region);
		Telemetry t;
		t.valid = true;
		t.bootAnimation = 0;
		desk.onTelemetry(t);

		desk.onPageMessage(cmd(R"({"op":"record","id":1})"));
		check(ok() && keys == std::vector<std::string>{"recordPlay"}, "REC from STOP holds RECORD and presses PLAY");
		t.playing = t.recording = true;
		t.knobPage = 0;
		desk.onTelemetry(t);
		desk.onPageMessage(cmd(R"({"op":"recTrig","t":4,"id":2})"));
		check(ok() && keys.back() == "trig5", "a track played while recording is its TRIG key");
		desk.onPageMessage(cmd("{\"op\":\"trig\",\"p\":" + std::to_string(pattern.position) + R"(,"t":0,"s":2,"id":3})"));
		check(!ok(), "grid edits of the recording pattern wait");
		params.clear();
		desk.onPageMessage(cmd("{\"op\":\"param\",\"k\":" + std::to_string(kit.position) + R"(,"t":2,"i":3,"v":5,"id":4})"));
		check(ok() && params.empty(), "a knob move while recording is not a CC");
		for(int i = 0; i < 10; ++i)
		{
			now += 60;
			desk.tick();
		}
		check(!turns.empty() && turns.back().first == 3, "it becomes DATA ENTRY turns of knob D");
		check(desk.documents().working && desk.documents().working->kit.params[2][3] == 5, "the view keeps the wanted value meanwhile");
		// Automation (or an app modulator) while the knob turns are on their way: it moves its one
		// value and leaves the knob's wanted value pending.
		// Automation (or an app modulator) while the knob turns are on their way: it moves its one
		// value; an image with only the automation does not settle the knob's edit, one with both does.
		desk.onHostKitParam(4, 7, 99);
		const auto imageOf = [](ed::MdKit _k)
		{
			const auto img = ed::mdWorkingKitImage(_k);
			std::vector<uint8_t> r{_k.position, 0};
			r.insert(r.end(), img.begin(), img.end());
			return r;
		};
		auto onlyAuto = kit;
		onlyAuto.params[4][7] = 99;
		desk.onWorkingKitMemory(imageOf(onlyAuto));
		desk.tick();
		check(desk.coreState().state({DocKind::WorkingKit, 0})->pending && desk.documents().working->kit.params[2][3] == 5,
			"automation during knob turns: an image without the knob's value does not settle it");
		auto both = onlyAuto;
		both.params[2][3] = 5;
		for(int i = 0; i < 3; ++i)	// the knob recorder sees its value land, the next image settles
		{
			now += 40;
			desk.onWorkingKitMemory(imageOf(both));
			desk.tick();
		}
		check(!desk.coreState().state({DocKind::WorkingKit, 0})->pending && desk.documents().working->kit.params[2][3] == 5
			&& desk.documents().working->kit.params[4][7] == 99, "and the image that shows both settles with both");
		keys.clear();
		desk.onPageMessage(cmd(R"({"op":"record","id":5})"));
		check(ok() && keys == std::vector<std::string>{"play"}, "REC while recording presses PLAY: keep playing");
		t.recording = false;
		desk.onTelemetry(t);
		keys.clear();
		desk.onPageMessage(cmd(R"({"op":"record","id":6})"));
		check(keys == std::vector<std::string>{"stop"}, "REC while playing stops first");
		t.playing = false;
		desk.onTelemetry(t);
		now += 40;
		desk.tick();
		check(keys.size() == 2 && keys[1] == "recordPlay", "then starts live recording once stopped");

		// Song selection: LOAD SONG when stopped, refused while playing.
		t.playing = true;
		desk.onTelemetry(t);
		desk.onPageMessage(cmd(R"({"op":"selectSong","s":3,"id":7})"));
		check(!ok(), "no song change while playing");
		t.playing = false;
		desk.onTelemetry(t);
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"selectSong","s":3,"id":8})"));
		check(ok() && !wire.empty() && wire[0] == ed::mdLoadSong(3) && desk.linkState().song == 3, "LOAD SONG 4 when stopped");
	}

	// DESIGN-edit-flow.md: a lock lane drawn at 60 edits a second: at most 5 dumps a second, one read-back.
	void testPacedLockDraw()
	{
		std::vector<std::vector<uint8_t>> wire;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [](uint8_t, uint8_t, uint8_t) {};
		port.toPage = [&](const Value& _m) { g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		auto pattern = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		pattern.kit = kit.position;
		for(size_t s = 0; s < 16; ++s)
			pattern = ed::withTrig(pattern, 0, s, true);
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
		desk.onDeviceSysex(ed::encodeMdPattern(pattern));
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		wire.clear();
		int dumps = 0, requests = 0;
		const auto count = [&]
		{
			for(const auto& m : wire)
			{
				if(m.size() > 6 && m[6] == ed::g_mdPatternDump) ++dumps;
				if(m.size() > 6 && m[6] == 0x68) ++requests;
			}
			wire.clear();
		};
		const double start = 1000;
		for(int n = 0; n < 120; ++n)	// 2 s at 60 edits a second, the session ticking every 8 ms
		{
			const double at = start + n * 1000.0 / 60;
			while(now + 8 < at) { now += 8; desk.tick(); }
			now = at;
			desk.onPageMessage(cmd("{\"op\":\"lock\",\"p\":" + std::to_string(pattern.position) + ",\"t\":0,\"i\":1,\"s\":"
				+ std::to_string(n % 16) + ",\"v\":" + std::to_string(n) + ",\"g\":9}"));
		}
		count();
		check(dumps >= 9 && dumps <= 11 && requests == 0, "a 2 s draw: at most 5 dumps a second (" + std::to_string(dumps) + "), no read-back mid-gesture");
		for(int i = 0; i < 40; ++i) { now += 8; desk.tick(); }
		count();
		check(dumps <= 12 && requests == 1, "then exactly one read-back (" + std::to_string(requests) + ")");
		check(desk.coreState().history().size() == 1, "one undo step");
		const auto* st = desk.coreState().state({DocKind::Pattern, pattern.position});
		check(st && st->pending && std::holds_alternative<ed::MdPattern>(*st->pending) && ed::usedLockRows(std::get<ed::MdPattern>(*st->pending)) > 0, "the page shows the last value, pending");
	}

	// Control All (manual p.37, DESIGN-edit-flow.md): one tweak intent, the machine's own gesture on the
	// emulator (FUNCTION held, the knob turned by each tick's net steps, released at quiet), coalesced CCs
	// over MIDI; one undo step per gesture; the firmware's skips.
	void testControlAll()
	{
		const auto modelOf = [](const char* _name)
		{
			for(uint32_t m = 0; m < 256; ++m)
				if(ed::mdMachineName(m) == _name)
					return static_cast<uint8_t>(m);
			return static_cast<uint8_t>(0);
		};
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		for(size_t t = 0; t < 16; ++t)
		{
			kit.models[t] = modelOf("TRX-BD");
			kit.params[t][1] = static_cast<uint8_t>(10 + t);
			kit.params[t][9] = static_cast<uint8_t>(10 + t);
		}
		kit.models[0] = modelOf("RAM-R1");
		kit.models[1] = modelOf("CTR-AL");
		kit.models[2] = modelOf("MID-01");
		kit.models[3] = modelOf("GND-SIN");	// no synthesis parameter 5
		kit.params[4][1] = 125;
		check(!controlAllReaches(kit.models[0], 1) && controlAllReaches(kit.models[0], 9) && !controlAllReaches(kit.models[1], 9)
			&& !controlAllReaches(kit.models[2], 20) && controlAllReaches(kit.models[3], 4) && controlAllReaches(kit.models[5], 4)
			&& !controlAllLeads(kit.models[0]) && !controlAllLeads(kit.models[1]) && !controlAllLeads(kit.models[2]) && controlAllLeads(kit.models[3]),
			"Control All as the firmware: not CTR or MIDI, a RAM recorder not on synthesis; they cannot lead either");
		{
			Documents docs;
			docs.working = WorkingKit{kit};
			Clipboard clip;
			const auto r = apply(docs, cmd("{\"op\":\"tweak\",\"k\":" + std::to_string(kit.position) + R"(,"group":"syn","knob":1,"d":5})"), clip,
				{kit.position});
			check(r.errors.empty() && r.changes.size() == 1, "a tweak is one change of the working kit");
			if(r.changes.size() == 1)
			{
				const auto& k = std::get<WorkingKit>(r.changes[0].after).kit;
				check(k.params[0][1] == 10 && k.params[1][1] == 11 && k.params[2][1] == 12, "RAM, CTR, MIDI tracks stay");
				check(k.params[3][1] == 18 && k.params[4][1] == 127 && k.params[15][1] == 30, "the others move by 5, held at 127");
			}
		}

		std::vector<std::vector<uint8_t>> wire;
		std::vector<std::string> keys;
		std::vector<std::pair<uint8_t, int>> turns;
		std::vector<std::array<uint8_t, 3>> params;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.device.pressKey = [&](const std::string& _k) { keys.push_back(_k); return true; };
		port.device.turnKnob = [&](uint8_t _e, int _s) { turns.emplace_back(_e, _s); return true; };
		port.toPage = [&](const Value& _m) { g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		auto pattern = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		pattern.kit = kit.position;
		const auto tweak = [&](Desk& _d, const char* _group, const int _knob, const int _delta)
		{
			_d.onPageMessage(cmd("{\"op\":\"tweak\",\"k\":" + std::to_string(kit.position) + ",\"group\":\"" + _group + "\",\"knob\":"
				+ std::to_string(_knob) + ",\"d\":" + std::to_string(_delta) + ",\"g\":42}"));
		};
		const auto imageOf = [](const ed::MdKit& _k)
		{
			const auto img = ed::mdWorkingKitImage(_k);
			std::vector<uint8_t> r{_k.position, 0};
			r.insert(r.end(), img.begin(), img.end());
			return r;
		};
		{
			Desk desk(port);
			desk.onPageMessage(cmd(R"({"op":"ready"})"));
			desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
			desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
			desk.onDeviceSysex(ed::encodeMdPattern(pattern));
			desk.onDeviceSysex(ed::encodeMdKit(kit));
			desk.onWorkingKitMemory(imageOf(kit));
			Telemetry t;
			t.valid = true;
			t.bootAnimation = 0;
			t.knobPage = 0;
			t.panelPending = 0;
			desk.onTelemetry(t);
			desk.tick();
			keys.clear(); turns.clear(); params.clear(); wire.clear();
			tweak(desk, "syn", 1, 2);
			tweak(desk, "syn", 1, 3);
			tweak(desk, "syn", 1, -1);
			check(desk.documents().working && desk.documents().working->kit.params[5][1] == 19 && desk.documents().working->kit.params[0][1] == 10,
				"the page sees every track at once (pending)");
			// The machine reports its gesture's steps as CCs (the plug-in's parameters): they are its way to the
			// values, not new edits (0.2.1: folded in, a Sound page drag ended part way).
			desk.onHostKitParam(5, 1, 16);
			desk.onHostKitParam(7, 1, 18);
			check(desk.documents().working->kit.params[5][1] == 19 && desk.documents().working->kit.params[7][1] == 21,
				"the machine's own steps while Control All is on its way do not replace its values");
			// The keys wait for a dump request in flight (the library's background read here; this fake never answers);
			// the first track that can lead (T4, GND-SIN) is selected first, and the machine's status says so.
			// The machine answers the track status (T1 selected until the SET STATUS to T4).
			uint8_t selected = 0;
			const auto machineTicks = [&](const int _n, const bool _untilTurn)
			{
				for(int i = 0; i < _n && !(_untilTurn && !turns.empty()); ++i)
				{
					now += 8;
					desk.tick();
					for(const auto& m : wire)
						if(m == ed::mdSetStatus(ed::MdStatus::Track, 3))
							selected = 3;
					for(const auto& m : wire)
						if(m == ed::mdStatusRequest(ed::MdStatus::Track))
							desk.onDeviceSysex(status(ed::MdStatus::Track, selected));
					wire.erase(std::remove(wire.begin(), wire.end(), ed::mdStatusRequest(ed::MdStatus::Track)), wire.end());
				}
			};
			machineTicks(400, true);
			check(std::find(wire.begin(), wire.end(), ed::mdSetStatus(ed::MdStatus::Track, 3)) != wire.end(), "the leading track is selected first");
			check(keys == std::vector<std::string>{"hold:function"} && turns.size() == 1 && turns[0] == std::make_pair(uint8_t(1), 4),
				"the emulator: FUNCTION held, knob B turned once by the tick's net steps (+4)");
			check(params.empty() && std::none_of(wire.begin(), wire.end(), [](const std::vector<uint8_t>& _m) { return _m.size() > 6 && (_m[6] == 0x52 || _m[6] == 0x67 || _m[6] == 0x5b); }),
				"no CCs, no dump");
			for(int i = 0; i < 25; ++i) { now += 8; desk.tick(); }
			check(keys.size() == 2 && keys[1] == "release:function", "FUNCTION let go at quiet");
			auto moved = desk.documents().working->kit;
			desk.onWorkingKitMemory(imageOf(moved));
			desk.tick();
			check(!desk.coreState().state({DocKind::WorkingKit, 0})->pending, "settled when memory shows every track");
			check(desk.coreState().history().size() == 1, "one undo step per gesture");
			// Another page: the knob page first, with FUNCTION up.
			keys.clear(); turns.clear();
			tweak(desk, "fx", 1, 1);
			now += 8;
			desk.tick();
			check(keys == std::vector<std::string>{"page"} && turns.empty(), "the effects knob: the page key first");
			t.knobPage = 1;
			desk.onTelemetry(t);
			machineTicks(50, false);
			check(keys.size() == 3 && keys[1] == "hold:function" && keys[2] == "release:function" && turns.size() == 1
				&& turns[0] == std::make_pair(uint8_t(1), 1), "then FUNCTION held, knob B of the effects page turned, FUNCTION let go");

			// The Sound workspace's regression (0.2.1): the machine's gesture is a fact to check. A knob page the
			// machine never reaches (its page key does nothing where it is), or a FUNCTION it lost (only the
			// selected track moved): once the gesture is over, every value memory does not show goes as a CC.
			const auto wantedCcs = [&](const uint8_t _index, const std::optional<uint8_t> _except)
			{
				const auto& want = desk.documents().working->kit;
				int missing = 0, extra = 0;
				for(uint8_t tr = 0; tr < 16; ++tr)
				{
					const bool reached = controlAllReaches(want.models[tr], _index) && (!_except || tr != *_except);
					const bool got = std::any_of(params.begin(), params.end(), [&](const std::array<uint8_t, 3>& _p)
						{ return _p[0] == tr && _p[1] == _index && _p[2] == want.params[tr][_index]; });
					missing += reached && !got;
					extra += !reached && got;
				}
				return missing == 0 && extra == 0;
			};
			desk.onWorkingKitMemory(imageOf(desk.documents().working->kit));
			machineTicks(20, false);
			t.knobPage = 2;
			desk.onTelemetry(t);
			keys.clear(); turns.clear(); params.clear();
			tweak(desk, "syn", 2, 3);
			machineTicks(200, false);
			const auto pageKeys = std::count(keys.begin(), keys.end(), std::string("page"));
			check(turns.empty() && pageKeys >= 1 && pageKeys <= 5 && wantedCcs(2, std::nullopt),
				"Sound, a synthesis knob the machine's page key never reaches: given up after " + std::to_string(pageKeys) + " page keys, the values as CCs");
			desk.onWorkingKitMemory(imageOf(desk.documents().working->kit));
			machineTicks(5, false);
			check(!desk.coreState().state({DocKind::WorkingKit, 0})->pending, "Sound: and memory showing them settles the edit");
			// FUNCTION lost: the knob turned the selected track (T4) alone.
			t.knobPage = 0;
			desk.onTelemetry(t);
			keys.clear(); turns.clear(); params.clear();
			const auto start = desk.documents().working->kit;
			tweak(desk, "syn", 3, 2);
			machineTicks(400, true);	// after the library's request in flight (this fake never answers)
			auto lone = start;
			lone.params[3][3] = desk.documents().working->kit.params[3][3];
			desk.onWorkingKitMemory(imageOf(lone));
			machineTicks(80, false);
			check(turns.size() == 1 && wantedCcs(3, uint8_t(3)), "Sound: FUNCTION lost, only the lead moved: the other tracks' values as CCs");
		}
		{
			// HW MIDI (no panel): coalesced CCs, at most one per track per tick.
			Desk desk(port);
			desk.setEngine(Desk::defaultAdapter(wireProfile(), port.device));
			desk.onTelemetry(Telemetry{});
			desk.onPageMessage(cmd(R"({"op":"ready"})"));
			desk.onDeviceSysex(status(ed::MdStatus::Pattern, pattern.position));
			desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
			desk.onDeviceSysex(ed::encodeMdPattern(pattern));
			desk.onDeviceSysex(ed::encodeMdKit(kit));
			now += 100;
			desk.tick();
			params.clear(); keys.clear(); turns.clear();
			size_t most = 0;
			for(int n = 0; n < 60; ++n)	// 1 s at 60 moves a second
			{
				tweak(desk, "fx", 1, n % 2 ? 1 : 2);
				params.clear();
				now += 1000.0 / 60;
				desk.tick();
				most = std::max(most, params.size());
				std::map<uint8_t, int> perTrack;
				for(const auto& p : params)
					++perTrack[p[0]];
				for(const auto& [tr, c] : perTrack)
					check(c <= 1, "HW: at most one CC per track per tick");
			}
			check(keys.empty() && turns.empty() && most > 0 && most <= 14, "HW: CCs only, at most 14 a tick here (14 tracks reached)");
			check(desk.coreState().history().size() == 1, "HW: one undo step");
		}
	}

	// P4: the start-up animation holds input; chaining, mutes from memory.
	void testLive()
	{
		check(validateChain({1, 3}).empty() && !validateChain({1}).empty() && !validateChain({1, 17}).empty()
			&& !validateChain({2, 2}).empty(), "chains: two or more, one bank, each once");
		check(chainKeys({3, 1, 4}, 0) == std::vector<std::string>{"chain:0:3,1,4"}, "chain A04 A02 A05: BANK A/E + TRIGs 4 2 5");
		check(chainKeys({65, 64}, 0) == std::vector<std::string>{"bankGroup", "chain:0:1,0"}, "bank E from A-D: BANK GROUP first");
		check(chainKeys({65, 64}, 1) == std::vector<std::string>{"chain:0:1,0"}, "bank E in E-H: no BANK GROUP");
		check(chainKeys({35, 36}, -1).empty(), "unknown BANK GROUP: no keys");

		std::vector<std::vector<uint8_t>> wire;
		std::vector<std::string> keys;
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.device.pressKey = [&](const std::string& _k) { keys.push_back(_k); return true; };
		port.device.sendMute = [](uint8_t, bool) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		const auto last = [&](const char* _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == _type)
					return &*it;
			return nullptr;
		};
		const auto ok = [&] { const auto* r = last("result"); return r && r->find("ok")->asBool(); };
		const auto firmware = [&] { const auto* m = last("machine"); return m ? m->find("doc")->find("lifecycle")->asString() : std::string(); };
		Telemetry t;
		t.valid = true;
		t.bootAnimation = 1;
		t.bankGroup = 0;
		t.chainKnown = true;
		desk.onTelemetry(t);
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x02, 0xf7});
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x02, 0x05, 0xf7});
		desk.tick();
		check(desk.isReady() && !desk.isInputReady() && firmware() == "animating", "status answered, animation running: BOOTING OS");
		desk.showLcd(std::vector<uint8_t>(1024, 0x0f));
		check(last("lcd") && last("lcd")->find("bits")->asString().size() == 1368, "while it starts the machine's own LCD goes to the page");
		desk.onPageMessage(cmd(R"({"op":"play","id":1})"));
		check(!ok() && keys.empty(), "PLAY is held back during the animation");
		t.bootAnimation = 0;
		desk.onTelemetry(t);
		desk.tick();
		check(desk.isInputReady() && firmware() == "ready", "animation over: ready");
		// A pick still waiting for the pattern end (queued), then a chain: the chain is what plays.
		desk.onPageMessage(cmd(R"({"op":"select","p":7,"id":20})"));
		desk.tick();
		const auto queued = [&]
		{
			const auto* q = last("machine")->find("doc")->find("pattern")->find("queued");
			return q && q->isNumber() ? static_cast<int>(q->asNumber()) : -1;
		};
		check(queued() == 7, "A08 picked: queued");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[3,1,4],"id":2})"));
		check(ok() && keys.back() == "chain:0:3,1,4", "chain command: the machine's keys");
		check(!wire.empty() && wire.front() == ed::mdSetStatus(ed::MdStatus::SequencerMode, 0), "pattern mode first: SONG mode gives way to the chain");
		t.chain.active = true;
		t.chain.patterns = {3, 1, 4};
		t.chain.next = 1;
		t.mutes = 0x0005;
		desk.onTelemetry(t);
		desk.tick();
		check(queued() == -1, "the firmware holds the chain: the queued A08 is dropped (the chain plays next)");
		const auto* m = last("machine");
		const auto& d = *m->find("doc")->find("desk");
		check(d.find("chain")->find("active")->asBool() && d.find("chain")->find("patterns")->asArray().size() == 3,
			"the page sees the firmware's chain");
		check(d.find("mutes")->asArray().size() == 2 && d.find("mutesSource")->asString() == "memory", "mutes 1 and 3 from memory");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"id":3})"));
		const auto* ask = last("ask");
		check(ask && ask->find("ask")->asString() == "breakChain" && wire.empty(), "select while chained asks first");
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"force":true,"id":4})"));
		check(!wire.empty() && wire.front() == ed::mdLoadPattern(9), "the ask answered with force: LOAD PATTERN");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"chainClear","id":5})"));
		check(ok() && !wire.empty() && wire.front() == ed::mdLoadPattern(2), "CLEAR = LOAD PATTERN of the current pattern");
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[1,17],"id":6})"));
		check(!ok(), "a chain across banks is refused");

		// The page sends the chain again at every pad: while the keys of one are on their way the
		// next waits, and only the latest is pressed (the device reports its keys pending, then none).
		const auto workOff = [&](const int _bankGroup)
		{
			t.panelPending = 2;
			desk.onTelemetry(t);
			desk.tick();
			t.panelPending = 0;
			t.bankGroup = _bankGroup;
			desk.onTelemetry(t);
			desk.tick();
		};
		// Keys wait while a dump request is in flight: answer the background loads first.
		for(int round = 0; round < 4000; ++round)
		{
			auto sent = std::move(wire);
			wire.clear();
			for(const auto& m : sent)
			{
				if(m.size() != 9)
					continue;
				const auto s = m[7];
				if(m == ed::mdKitRequest(s)) { ed::MdKit k; k.position = s; desk.onDeviceSysex(ed::encodeMdKit(k)); }
				else if(m == ed::mdPatternRequest(s)) { ed::MdPattern p; p.position = s; desk.onDeviceSysex(ed::encodeMdPattern(p)); }
				else if(m == ed::mdSongRequest(s)) { ed::MdSong g; g.position = s; desk.onDeviceSysex(ed::encodeMdSong(g)); }
				else if(m == ed::mdGlobalRequest(s)) { ed::MdGlobal g; g.position = s; desk.onDeviceSysex(ed::encodeMdGlobal(g)); }
			}
			now += 20;
			desk.tick();
			if(!desk.isBusy() && desk.documents().kits.size() == 64 && desk.documents().patterns.size() == 128 && wire.empty())
				break;
		}
		check(desk.documents().kits.size() == 64, "the background loads answered");
		t.panelPending = 0;
		desk.onTelemetry(t);
		now += 5000;
		desk.tick();
		wire.clear();
		keys.clear();
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[65,64],"id":7})"));
		check(ok() && keys == std::vector<std::string>{"bankGroup", "chain:0:1,0"}, "chain E02 E01 from A-D: BANK GROUP, then the chain");
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[65,64,66],"id":8})"));
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[65,64,66,67],"id":9})"));
		desk.tick();
		check(ok() && keys.size() == 2, "chains sent while the keys before are on their way wait");
		workOff(1);
		check(keys.size() == 3 && keys.back() == "chain:0:1,0,2,3", "then only the latest is pressed, from the group the machine is in now (no second BANK GROUP)");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"chainClear","id":10})"));
		check(ok() && std::none_of(wire.begin(), wire.end(), [](const std::vector<uint8_t>& _b) { return _b == ed::mdLoadPattern(2); }),
			"CLEAR while the chain keys are on their way waits for them");
		workOff(1);
		check(std::any_of(wire.begin(), wire.end(), [](const std::vector<uint8_t>& _b) { return _b == ed::mdLoadPattern(2); }) && keys.size() == 3,
			"then LOAD PATTERN ends the chain");
		now += 2000;
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[65,64],"id":11})"));
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[65,66],"id":12})"));
		desk.onPageMessage(cmd(R"({"op":"select","p":5,"force":true,"id":13})"));
		workOff(1);
		check(keys.size() == 4, "a pick drops a chain still waiting for its keys");
	}

	// P4: the editor's setup (modulators, knob CCs) is saved with the project.
	void testSetup()
	{
		DeskSetup a;
		a.knobCcs = {30, 31, 32, 33, 34, 35, 36, 37};
		ModSource src;
		src.id = "lfo1";
		src.label = "LFO A";
		a.modulators.sources.push_back(src);
		std::vector<std::string> errors;
		const auto b = deskSetupFromJson(deskSetupToJson(a), errors);
		check(b && *b == a && errors.empty(), "md-desk/setup: value -> JSON -> value");
		const auto bad = cmd(R"({"schema":"md-desk/setup","version":1,"knobCcs":[40,41,42,40,44,45,46,47]})");
		errors.clear();
		check(!deskSetupFromJson(bad, errors) && !errors.empty() && errors[0].find("knobCcs[3]") != std::string::npos,
			"two knob rows on one CC are refused, with the path");

		std::vector<Value> saved, page;
		Desk::Port port;
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.saveSetup = [&](const Value& _s) { saved.push_back(_s); };
		port.device.nowMs = [] { return 0.0; };
		Desk desk(port);
		desk.onTelemetry(Telemetry{});
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x00, 0xf7});
		check(!desk.loadSetup(deskSetupToJson(a)).empty() == false && desk.setup() == a, "a project's setup loads");
		bool published = false;
		for(const auto& m : page)
			published |= m.find("type")->asString() == "setup";
		check(published, "and goes to the page");
		desk.onPageMessage(cmd(R"({"op":"knobs","ccs":[1,2,3,4,5,6,7,8],"id":1})"));
		check(!saved.empty() && saved.back().find("knobCcs")->asArray()[0].asNumber() == 1 && desk.setup().knobCcs[7] == 8,
			"new knob CCs are saved with the project");
		desk.onPageMessage(cmd(R"({"op":"modSet","id":2,"doc":{"schema":"md-desk/modulators","version":1,"sources":[],"links":[]}})"));
		check(saved.back().find("modulators")->find("sources")->asArray().empty(), "a modulator change is saved too");
		const auto before = saved.size();
		desk.onPageMessage(cmd(R"({"op":"knobs","ccs":[1,1,3,4,5,6,7,8],"id":3})"));
		check(saved.size() == before && desk.setup().knobCcs[1] == 2, "invalid knob CCs change nothing");
	}

	// P4: which trig a knob turn locks while live recording.
	void testLockStep()
	{
		ed::MdPattern p;
		p.length = 16;
		p = ed::withTrig(p, 14, 8, true);
		p = ed::withTrig(p, 14, 12, true);
		check(nextLockStep(p, 14, 7) == 8, "turn in step 8: locks the trig on step 9");
		check(nextLockStep(p, 14, 8) == 12, "turn in step 9 (its trig already started): the next trig, step 13");
		check(nextLockStep(p, 14, 13) == 8, "past the last trig: wraps to step 9");
		check(nextLockStep(p, 14, -1) == 8, "stopped: the first trig");
		check(!nextLockStep(p, 3, 5), "a track without trigs: none");
	}

	// P4: the kit library and pattern chooser as pure slot edits.
	void testLibrary()
	{
		Documents docs;
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		auto other = kit;
		other.position = 9;
		docs.kits[kit.position] = kit;
		docs.kits[9] = other;
		auto pat = *ed::decodeMdPattern(load("programmed_pattern_1.syx"));
		docs.patterns[pat.position] = pat;
		auto p2 = pat;
		p2.position = 20;
		docs.patterns[20] = p2;
		Clipboard clip;
		check(!run(docs, cmd(R"({"op":"kitPaste","k":9})"), clip).errors.empty(), "paste needs a copy first");
		run(docs, cmd("{\"op\":\"kitCopy\",\"k\":" + std::to_string(kit.position) + "}"), clip);
		docs.kits[9].name[0] = 'Q';
		auto r = run(docs, cmd(R"({"op":"kitPaste","k":9})"), clip);
		check(r.errors.empty() && r.changes.size() == 1 && r.changes[0].ref().kind == DocKind::Kit && std::get<ed::MdKit>(r.changes[0].after).position == 9,
			"kit paste: a slot write into K10");
		r = run(docs, cmd(R"({"op":"kitClear","k":9})"), clip);
		check(r.changes.size() == 1 && isEmptyKit(std::get<ed::MdKit>(r.changes[0].after)), "kit clear: an empty kit");
		{
			// A slot never written (OS 1.63, a fresh machine's K41): DEL bytes, every track GND-EMPTY.
			ed::MdKit unused;
			unused.name = {0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x10, 0x7f, 0, 0, 0, 0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x10};
			auto leftover = unused;
			leftover.name = {0x7f, 'M', 'X', ' ', 'K', 'I', 'T', ' ', '1', 0, 0, 0x7f, 0x17, 0x18, 0x7f, 0x19};
			auto named = unused;
			named.name = {'T', 'R', 'X', ' ', 'U', 'W', 0, 'B', 'E', 'T', 0, 0x7f, 0x17, 0x18, 0x7f, 0x19};
			check(kitNameText(unused).empty() && isEmptyKit(unused) && kitNameText(leftover).empty() && isEmptyKit(leftover),
				"an unwritten slot's bytes are no name: the slot is empty");
			check(kitNameText(named) == "TRX UW" && !isEmptyKit(named), "a name ends at its NUL (the bytes after it are not the name)");
		}
		r = run(docs, cmd(R"({"op":"kitRename","k":9,"name":"new kit"})"), clip);
		check(r.changes.size() == 1 && std::get<ed::MdKit>(r.changes[0].after).name[0] == 'N', "rename: upper case, 16 characters");
		check(!run(docs, cmd(R"({"op":"kitCopyTo","from":9,"to":9})"), clip).errors.empty(), "same slot refused");
		r = run(docs, cmd(R"({"op":"patClear","p":20})"), clip);
		const auto& cleared = std::get<ed::MdPattern>(r.changes.at(0).after);
		check(cleared.length == pat.length && cleared.kit == pat.kit
			&& std::all_of(cleared.trigs.begin(), cleared.trigs.end(), [](const uint64_t _t) { return !_t; }), "pattern clear: no trigs, length and kit link kept");
		r = run(docs, cmd("{\"op\":\"patCopyTo\",\"from\":" + std::to_string(pat.position) + ",\"to\":20}"), clip);
		check(r.errors.empty() && r.changes.empty(), "copying an identical pattern changes nothing");
	}

	// P4: HW MIDI link states.
	void testHardwareLink()
	{
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [](const std::vector<uint8_t>&) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.setEngine(Desk::defaultAdapter(wireProfile(), port.device));
		desk.onTelemetry(Telemetry{});
		const auto link = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "machine")
					return it->find("doc")->find("lifecycle")->asString();
			return std::string();
		};
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		check(link() == "hwConnecting", "HW: connect until the machine answers");
		now += 6000;
		desk.tick();
		check(link() == "hwLost", "HW: nothing answers for 5 s: HW NO MIDI");
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x00, 0xf7});
		desk.tick();
		check(link() == "ready" && desk.isInputReady(), "HW: the first status reply: HW MIDI, input taken");
		now += 4000;
		desk.tick();
		check(link() == "hwLost", "HW: no reply for 3.5 s: lost");
	}

	// P5: GLOBAL settings by name.
	void testGlobalSet()
	{
		Documents docs;
		docs.global = ed::MdGlobal{};
		docs.global->keymap.fill(ed::MdGlobal::g_unmapped);
		docs.global->keymap[36] = 0;
		Clipboard clip;
		const auto run = [&](const std::string& _c)
		{
			auto r = apply(docs, cmd(_c), clip);
			if(!r.changes.empty())
				docs.global = std::get<ed::MdGlobal>(r.changes[0].after);
			return r;
		};
		run(R"({"op":"globalSet","field":"tempoIn","on":true})");
		run(R"({"op":"globalSet","field":"ctrlIn","on":false})");
		run(R"({"op":"globalSet","field":"tempoOut","on":true})");
		check(docs.global->syncFlags == 0x31, "sync: TEMPO IN ext 0x01, CTRL IN off 0x10, TEMPO OUT 0x20");
		run(R"({"op":"globalSet","field":"programChangeOut","on":true})");
		run(R"({"op":"globalSet","field":"programChangeChannel","v":8})");
		check(docs.global->programChange == 0x22, "program change: OUT 0x02, channel 8 in bits 2-6");
		check(!run(R"({"op":"globalSet","field":"baseChannel","v":13})").errors.empty(), "base channel 13 (14-17) refused");
		run(R"({"op":"globalSet","field":"keymap","note":40,"target":0})");
		check(docs.global->keymap[40] == 0 && docs.global->keymap[36] == ed::MdGlobal::g_unmapped, "a track mapped to another key frees its old key");
		const auto j = ed::globalToJson(*docs.global);
		check(j.find("control")->find("tempoIn")->asString() == "external" && !j.find("control")->find("ctrlIn")->asBool()
			&& j.find("control")->find("programChangeChannel")->asNumber() == 8, "the contract's derived control view");
	}

	void testSampleName()
	{
		const auto m = ed::mdSetSampleName(5, "KIK");
		check(m == std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x73, 5, 'K', 'I', 'K', ' ', 0xf7},
			"0x73: slot, 4 characters space padded");
		check(ed::mdSetSampleName(48, "A").empty() && ed::mdSetSampleName(0, "").empty()
			&& ed::mdSetSampleName(0, "TOOLONG").empty() && ed::mdSetSampleName(0, "\x01").empty(), "bad names refused");
	}

	// ---- P9: UW samples (pure parts, and the SDS transfer against a scripted machine) ----

	ed::AudioClip toneClip(const uint32_t _rate, const size_t _frames, const size_t _channels)
	{
		ed::AudioClip c;
		c.rate = _rate;
		c.channels.assign(_channels, std::vector<float>(_frames));
		for(size_t i = 0; i < _frames; ++i)
			for(size_t ch = 0; ch < _channels; ++ch)
				c.channels[ch][i] = static_cast<float>(0.5 * std::sin(i * 0.05) * (1.0 - double(i) / _frames));
		return c;
	}

	void testSampleFiles()
	{
		std::string error;
		const auto wav = ed::encodeWav16(toneClip(48000, 4800, 2));
		const auto clip = ed::decodeAudioFile(wav, error);
		check(clip && clip->rate == 48000 && clip->channels.size() == 2 && clip->frames() == 4800, "WAV 16-bit stereo decodes");
		check(clip && std::fabs(clip->channels[1][100] - 0.5f * float(std::sin(5.0)) * (1.0f - 100.0f / 4800)) < 1e-3f, "and its values");
		const auto up = clip ? ed::prepareMdSample(*clip, "/x/kick 01.wav", 1000000, error) : std::nullopt;
		check(up && up->rate == 44100 && up->name == "KICK" && up->samples.size() == 4410 && up->notes.size() == 2,
			"prepared: mono, 48 kHz to 44.1 kHz, the name from the file");
		const auto cut = clip ? ed::prepareMdSample(*clip, "snare.wav", 1000, error) : std::nullopt;
		check(cut && cut->samples.size() == 1000 && cut->notes.back().find("Cut to") == 0, "cut to the memory left, said so");
		check(!ed::prepareMdSample(*clip, "a.wav", 0, error) && error.find("full") != std::string::npos, "no memory left: refused");
		check(ed::mdSampleNameFrom("-x.aif") == "X" && ed::mdSampleNameFrom("??.wav") == "SMPL" && ed::mdSampleNameFrom("bd-1 long") == "BD-1",
			"names: 1-4 of A-Z 0-9 -");

		// AIFF 16-bit big-endian mono at 22050 Hz.
		std::vector<uint8_t> aiff;
		const auto be32 = [&](const uint32_t _v) { for(int i = 3; i >= 0; --i) aiff.push_back(uint8_t(_v >> (8 * i))); };
		const auto be16 = [&](const uint32_t _v) { aiff.push_back(uint8_t(_v >> 8)); aiff.push_back(uint8_t(_v)); };
		const auto text = [&](const char* _t) { aiff.insert(aiff.end(), _t, _t + 4); };
		const int16_t values[] = {0, 16384, -16384, 32767};
		text("FORM"); be32(4 + 26 + 16 + 8); text("AIFF");
		text("COMM"); be32(18); be16(1); be32(4); be16(16);
		// 22050 as an 80-bit extended: exponent 16383 + 14, mantissa 22050 << 49
		be16(16383 + 14); be32(uint32_t((uint64_t(22050) << 49) >> 32)); be32(0);
		text("SSND"); be32(8 + 8); be32(0); be32(0);
		for(const auto v : values)
			be16(uint16_t(v));
		const auto a = ed::decodeAudioFile(aiff, error);
		check(a && a->rate == 22050 && a->frames() == 4 && std::fabs(a->channels[0][1] - 0.5f) < 1e-6f && a->channels[0][2] == -0.5f,
			"AIFF 16-bit decodes (rate from the 80-bit float)");
		check(!ed::decodeAudioFile({1, 2, 3}, error) && error.find("not a WAV") != std::string::npos, "not audio: refused with the reason");

		// SDS: the header, the name, 40 samples a packet, unsigned left-justified, XOR checksum.
		const std::vector<int16_t> samples{-32768, 0, 32767, 1234, -1};
		const auto d = ed::mdSdsDump(7, samples, 44100, "KICK");
		check(d && d->header.size() == 21 && d->header[4] == 7 && d->header[6] == 16 && d->header[19] == 0x7f, "SDS header: slot, 16 bits, no loop");
		const auto period = d ? uint32_t(d->header[7] | (d->header[8] << 7) | (d->header[9] << 14)) : 0;
		check(period == 22676 && d->name == ed::mdSetSampleName(7, "KICK") && d->packets.size() == 1 && d->packets[0].size() == 127, "period, name, one packet");
		const auto word = [&](const size_t _i)
		{
			const auto* b = &d->packets[0][5 + 3 * _i];
			return int(((b[0] << 14) | (b[1] << 7) | b[2]) >> 5) - 0x8000;
		};
		check(word(0) == -32768 && word(1) == 0 && word(2) == 32767 && word(3) == 1234 && word(4) == -1, "SDS packet words");
		uint8_t sum = 0;
		for(size_t i = 1; i < 125; ++i)
			sum ^= d->packets[0][i];
		check(sum == d->packets[0][125], "SDS packet checksum");
		check(!ed::mdSdsDump(48, samples, 44100, "A"), "no SDS into slot 49 (RAM)");
	}

	// A synthetic UW memory: two ROM records (one over two sectors), names, the DSP table and expander.
	void testSampleMemory()
	{
		std::vector<uint8_t> flash(ed::g_mdSampleFlashEnd, 0xff);
		std::vector<uint8_t> patch(0x100000, 0);
		std::map<uint32_t, uint32_t> dsp;
		const auto putRecord = [&](const uint32_t _at, const uint8_t _slot, const uint32_t _len, const uint32_t _periodNs, const std::function<int16_t(uint32_t)>& _v)
		{
			uint8_t* h = &flash[_at];
			const auto w32 = [](uint8_t* _p, const uint32_t _x) { _p[0] = uint8_t(_x >> 8); _p[1] = uint8_t(_x); _p[2] = uint8_t(_x >> 24); _p[3] = uint8_t(_x >> 16); };
			h[0] = 0x18; h[1] = _slot; h[2] = 0; h[3] = 16;
			w32(h + 4, _periodNs); w32(h + 8, _len); w32(h + 12, 0); w32(h + 16, _len - 1); h[20] = 0; h[21] = 0x7f;
			uint32_t pos = _at + 24, sector = _at;
			for(uint32_t i = 0; i < _len; ++i)
			{
				if(pos + 2 > sector + ed::g_mdSampleSectorSize)
				{
					sector += ed::g_mdSampleSectorSize;
					flash[sector] = 0x1a; flash[sector + 1] = _slot; flash[sector + 2] = 0xff; flash[sector + 3] = 0xff;
					pos = sector + 4;
				}
				const auto v = static_cast<uint16_t>(_v(i));
				flash[pos++] = uint8_t(v >> 8);
				flash[pos++] = uint8_t(v);
			}
		};
		putRecord(0x300000, 2, 1000, 22676, [](uint32_t _i) { return int16_t(_i < 500 ? 16384 : -32768); });
		putRecord(0x400000, 9, 40000, 31250, [](uint32_t _i) { return int16_t(_i >= 39000 ? 32767 : 0); });
		const char* name = "KIK ";
		uint32_t sum = 0;
		for(int i = 0; i < 4; ++i) { patch[ed::g_mdSampleNamesAddress + 10 + i] = uint8_t(name[i]); sum += uint8_t(name[i]); }
		patch[ed::g_mdSampleNamesAddress + 14] = uint8_t(sum);
		for(uint32_t i = 0; i < 4096; ++i)
			dsp[ed::g_mdDspExpander + i] = uint32_t((int32_t(i) - 0x800) * 4096) & 0xffffff;
		const auto entry = [&](const uint32_t _r, const uint32_t _start, const uint32_t _len)
		{
			const auto at = ed::g_mdDspSampleTable + 4 * (ed::g_mdDspRamEntry + _r);
			dsp[at] = _start; dsp[at + 1] = _len; dsp[at + 2] = 0xffffff; dsp[at + 3] = 0x40000;
		};
		for(uint32_t r = 0; r < 4; ++r)
			entry(r, 0x1a0000 + r * 0x10000, r == 1 ? 6 : 0);
		dsp[0x1b0000] = 0x800fff; dsp[0x1b0001] = 0x000800; dsp[0x1b0002] = 0x800800;
		ed::MdSampleMemory m;
		m.flash = [&](const uint32_t _o, const size_t _n, uint8_t* _out) { if(_o + _n > flash.size()) return false; std::copy_n(&flash[_o], _n, _out); return true; };
		m.patch = [&](const uint32_t _o) { return patch[_o]; };
		m.dspX = [&](const uint32_t _a) { const auto it = dsp.find(_a); return it == dsp.end() ? 0u : it->second; };
		const auto bank = ed::readMdSampleBank(m, 4);
		const auto& a = bank.rom[2];
		check(!a.empty && a.length == 1000 && a.rate == 44100 && a.name == "KIK" && !a.loop, "ROM slot 3: length, rate, name (patch RAM, checked by its sum)");
		check(a.peaks == std::vector<int8_t>{64, 64, 64, 64, -127, -127, -127, -127}, "ROM peaks: min, max per bin");
		const auto& b = bank.rom[9];
		check(!b.empty && b.length == 40000 && b.rate == 32000 && b.name.empty() && b.peaks.back() == 127 && b.peaks[5] == 0,
			"ROM slot 10 over two sectors (the next sector's 4-byte head skipped)");
		check(bank.rom[0].empty && bank.rom[0].peaks.empty(), "an empty ROM slot");
		check(bank.ramReadable && bank.ram[0].empty && !bank.ram[1].empty && bank.ram[1].length == 6 && bank.ram[1].rate == 44100,
			"RAM 2 from the DSP table");
		check(bank.ram[1].peaks == std::vector<int8_t>{0, 0, -127, 0, -127, 0, 0, 0} || bank.ram[1].peaks.size() == 8, "RAM peaks through the expander");
		check(bank.used() == 41000, "memory in use: the ROM slots' samples");
		const auto sig = ed::mdSampleSignature(m);
		flash[0x300000 + 24] ^= 1;
		check(ed::mdSampleSignature(m) == sig, "the signature follows heads, names and RAM entries, not every sample");
		patch[ed::g_mdSampleNamesAddress + 10] = 'X';
		check(ed::mdSampleSignature(m) != sig, "a new name changes it");
		dsp[ed::g_mdDspExpander + 5] = 0;
		check(!ed::indexMdSamples(m).ramReadable, "an expander that is not the firmware's: RAM not read");
		const auto j = ed::mdSampleBankToJson(bank);
		check(j.find("schema")->asString() == "md-desk/samples" && j.find("rom")->asArray().size() == 48 && j.find("ram")->asArray().size() == 4,
			"md-desk/samples: 48 ROM and 4 RAM slots");
	}

	void testSampleLoad()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.device.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		Desk desk(port);
		desk.setEngine(Desk::defaultAdapter(wireProfile(), port.device));
		desk.onTelemetry(Telemetry{});
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x00, 0xf7});
		desk.tick();
		const auto last = [&]() -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "sampleLoad")
					return &*it;
			return nullptr;
		};
		const auto state = [&] { const auto* l = last(); return l ? l->find("state")->asString() : std::string(); };
		const auto ack = [&](const uint8_t _n) { desk.onDeviceSysex({0xf0, 0x7e, 0x00, 0x7f, _n, 0xf7}); };
		const auto wav = ed::encodeWav16(toneClip(44100, 200, 1));	// 5 packets
		check(desk.loadSample(48, "a.wav", wav).find("RAM") != std::string::npos && state() == "failed", "a RAM slot: refused, measured reason");
		check(!desk.loadSample(4, "a.txt", {1, 2, 3}).empty() && state() == "failed", "not audio: refused, the page told");
		wire.clear();
		check(desk.loadSample(4, "tom.wav", wav).empty() && state() == "sending", "a WAV into ROM-05: sending");
		check(wire.size() == 1 && wire[0].size() == 21 && wire[0][3] == 0x01 && wire[0][4] == 4, "the header first, alone");
		now += 500;
		desk.tick();
		check(wire.size() == 1, "nothing else on the wire while it waits for the ACK (no status poll)");
		ack(0);
		check(wire.size() == 3 && wire[1] == ed::mdSetSampleName(4, "TOM") && wire[2].size() == 127 && wire[2][4] == 0, "ACK: the name, then packet 0");
		desk.onDeviceSysex({0xf0, 0x7e, 0x00, 0x7e, 0x00, 0xf7});
		check(wire.size() == 4 && wire[3] == wire[2], "NAK: packet 0 again");
		ack(0);
		check(wire.size() == 5 && wire[4][4] == 1, "ACK: packet 1");
		desk.onDeviceSysex({0xf0, 0x7e, 0x00, 0x7c, 0x01, 0xf7});
		now += 5000;
		desk.tick();
		check(wire.size() == 5 && state() == "sending", "WAIT holds it past the 2 s reply time");
		ack(1);
		ack(2);
		ack(3);
		check(wire.size() == 8 && state() == "sending", "packets 2-4");
		desk.onPageMessage(cmd(R"({"op":"kitLoad","k":0,"force":true})"));
		check(wire.size() == 8, "a command meanwhile: its SysEx waits");
		ack(4);
		desk.onTelemetry(Telemetry{});
		const auto* done = last();
		check(state() == "done" && done->find("sent")->asNumber() == 5 && done->find("text")->asString().find("ROM-05") != std::string::npos,
			"the last ACK: done, the page told");
		desk.tick();
		check(wire.size() > 8, "then the wire is the editor's again");

		// No answer to the header: open loop (SDS), a packet every 60 ms.
		wire.clear();
		check(desk.loadSample(5, "x.wav", wav).empty(), "another");
		now += 2100;
		desk.tick();
		check(wire.size() == 3, "2 s without an answer: the name and packet 0 without a handshake");
		now += 400;
		desk.tick();
		desk.onTelemetry(Telemetry{});
		check(state() == "done" && !last()->find("handshake")->asBool()
			&& std::count_if(wire.begin(), wire.end(), [](const std::vector<uint8_t>& _m) { return _m.size() == 127; }) == 5, "open loop: done");

		// Cancel.
		check(desk.loadSample(6, "y.wav", wav).empty(), "a third");
		ack(0);
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"sampleCancel","id":9})"));
		desk.onTelemetry(Telemetry{});
		check(!wire.empty() && wire[0] == std::vector<uint8_t>{0xf0, 0x7e, 0x00, 0x7d, 0x00, 0xf7} && state() == "cancelled", "sampleCancel: CANCEL, cancelled");

		// The bank (from an emulated machine's memory) is a message of its own.
		ed::MdSampleBank bank;
		for(uint8_t s = 0; s < 48; ++s) { ed::MdSampleSlot x; x.slot = s; bank.rom.push_back(x); }
		for(uint8_t s = 0; s < 4; ++s) { ed::MdSampleSlot x; x.ram = true; x.slot = s; bank.ram.push_back(x); }
		bank.rom[3].empty = false; bank.rom[3].length = 10; bank.rom[3].rate = 44100; bank.rom[3].peaks = {-5, 5}; bank.rom[3].name = "SNR";
		page.clear();
		desk.onSampleBank(bank);
		desk.onSampleBank(bank);
		size_t samples = 0;
		for(const auto& m : page)
			samples += m.find("type")->asString() == "samples";
		check(samples == 1, "samples: published once, when it changed");
		const auto caps = desk.machine().capabilities().toJson();
		check(!caps.find("can")->find("sampleAudio")->asBool() && caps.find("can")->find("sampleLoad")->asBool(),
			"HW: samples can be sent, not read (capabilities)");
	}

	// P9: the audition mixer (pure): lock-free hand-off, resampling, one at a time, a fade on a switch.
	void testAuditionMixer()
	{
		const auto clip = [](const std::vector<int16_t>& _pcm, const uint32_t _rate)
		{
			return ed::AuditionClip{std::make_shared<const std::vector<int16_t>>(_pcm), _rate};
		};
		const auto near = [](const float _a, const float _b) { return std::fabs(_a - _b) < 1e-4f; };
		ed::AuditionMixer mix;
		std::vector<float> l(256, 0.0f), r(256, 0.0f);
		mix.mix(l.data(), r.data(), 64, 44100);
		check(std::all_of(l.begin(), l.end(), [](const float _v) { return _v == 0.0f; }) && mix.status().id == 0, "audition: nothing asked, nothing heard");

		// At its own rate: 100 samples at 22050 Hz are 200 frames at 44100, linearly between them.
		std::vector<int16_t> ramp(100);
		for(int i = 0; i < 100; ++i)
			ramp[size_t(i)] = int16_t(i * 256);
		const auto id = mix.play(clip(ramp, 22050));
		check(id > 0 && mix.status().id == id && mix.status().playing && mix.status().position == 0, "audition: playing once asked, before the audio thread took it");
		l.assign(256, 0.25f); r.assign(256, 0.0f);
		mix.mix(l.data(), r.data(), 64, 44100);
		check(near(l[0], 0.25f) && near(l[1] - 0.25f, 128 / 32768.0f) && near(l[2] - 0.25f, 256 / 32768.0f) && near(r[3], 384 / 32768.0f),
			"audition: added to the output, resampled 22050 -> 44100 (linear)");
		check(mix.status().playing && mix.status().position == 32, "audition: position in the clip's samples");
		l.assign(256, 0.0f);
		mix.mix(l.data(), nullptr, 256, 44100);
		check(!mix.status().playing && mix.status().position == 100 && l[200] == 0.0f && l[135] != 0.0f, "audition: plays once, then the status says it ended");
		l.assign(64, 0.0f);
		mix.mix(l.data(), nullptr, 64, 44100);
		check(std::all_of(l.begin(), l.end(), [](const float _v) { return _v == 0.0f; }), "audition: silent after the end");

		// At the output's rate: the samples as they are.
		mix.play(clip({1000, -2000, 3000}, 44100));
		l.assign(4, 0.0f);
		mix.mix(l.data(), nullptr, 4, 44100);
		check(near(l[0], 1000 / 32768.0f) && near(l[1], -2000 / 32768.0f) && near(l[2], 3000 / 32768.0f) && l[3] == 0.0f, "audition: 44100 -> 44100 is the samples");

		// One at a time: another replaces it, the first fades out over g_fadeFrames.
		const std::vector<int16_t> loud(20000, 16384);
		mix.play(clip(loud, 44100));
		mix.mix(l.data(), nullptr, 4, 44100);
		const auto second = mix.play(clip(std::vector<int16_t>(20000, 0), 44100));
		std::vector<float> f(128, 0.0f);
		mix.mix(f.data(), nullptr, 128, 44100);
		check(f[0] > 0.4f && f[32] > 0.1f && f[32] < 0.4f && f[ed::AuditionMixer::g_fadeFrames] == 0.0f && mix.status().id == second && mix.status().playing,
			"audition: a new one replaces it, the old one fades out (no click)");
		const auto stopped = mix.stop();
		f.assign(128, 0.0f);
		mix.mix(f.data(), nullptr, 128, 44100);
		check(mix.status().id == stopped && !mix.status().playing, "audition: stop");
		mix.mix(f.data(), nullptr, 128, 44100);
		mix.play(clip(loud, 44100));
		check(mix.held() <= 2, "audition: requests the audio thread no longer reads are freed (by the control thread)");
		// Without an audio thread nothing the audio thread may read is freed.
		ed::AuditionMixer idle;
		for(int i = 0; i < 5; ++i)
			idle.play(clip(loud, 44100));
		check(idle.held() == 5, "audition: nothing freed the audio thread may still read");
	}

	// P9: a slot's detail and its audition through the desk.
	void testSampleWaveAndAudition()
	{
		std::vector<Value> page;
		Desk::Port port;
		ed::AuditionMixer mixer;
		port.device.sendSysex = [](const std::vector<uint8_t>&) {};
		port.device.nowMs = [] { return 0.0; };
		port.device.audition = [&](const ed::AuditionClip& _c) { return mixer.play(_c); };
		port.device.auditionStatus = [&] { return mixer.status(); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		Desk desk(port);
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		const auto last = [&](const std::string& _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == _type)
					return &*it;
			return nullptr;
		};
		const auto ok = [&](const int _id)
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "result" && it->find("id") && it->find("id")->asNumber() == _id)
					return it->find("ok")->asBool();
			return false;
		};
		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":3,"bins":512,"id":1})"));
		check(!ok(1) && !last("sampleWave"), "sampleWave before the samples are read: refused");

		ed::MdSampleBank bank;
		for(uint8_t s = 0; s < 48; ++s) { ed::MdSampleSlot x; x.slot = s; bank.rom.push_back(x); }
		for(uint8_t s = 0; s < 4; ++s) { ed::MdSampleSlot x; x.ram = true; x.slot = s; bank.ram.push_back(x); }
		bank.ramReadable = true;
		std::vector<int16_t> pcm(3000);
		for(size_t i = 0; i < pcm.size(); ++i)
			pcm[i] = int16_t(i < 1500 ? 20000 : -32768);
		auto& snr = bank.rom[3];
		snr.empty = false; snr.length = 3000; snr.rate = 22050; snr.name = "SNR"; snr.pcm = std::make_shared<const std::vector<int16_t>>(pcm);
		snr.peaks = ed::mdPeaks(3000, ed::g_mdSampleBins, [&](const uint32_t _i) { return pcm[_i] / 32768.0f; });
		desk.onSampleBank(bank);
		const auto* doc = last("samples");
		check(doc && doc->find("doc")->find("rom")->asArray()[3].find("peaks")->asArray().size() == 2 * ed::g_mdSampleBins && !doc->find("doc")->find("rom")->asArray()[3].find("pcm"),
			"the overview stays light: 128 bins a slot, no samples");

		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":3,"bins":2400,"id":2})"));
		const auto* w = last("sampleWave");
		check(ok(2) && w && w->find("bins")->asNumber() == 2400 && w->find("peaks")->asArray().size() == 4800 && w->find("scale")->asNumber() == 32767
			&& w->find("bank")->asString() == "rom" && w->find("slot")->asNumber() == 3 && w->find("length")->asNumber() == 3000,
			"sampleWave: the slot at the bins asked for (the canvas' device pixels)");
		const auto& wp = w->find("peaks")->asArray();
		check(wp[0].asNumber() == 20000 && wp[1].asNumber() == 20000 && wp[4798].asNumber() == -32767 && wp[4799].asNumber() == -32767,
			"sampleWave: min, max of the samples themselves, 16-bit (held at -32767)");
		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":3,"bins":8192,"id":3})"));
		check(last("sampleWave")->find("bins")->asNumber() == 3000, "sampleWave: at most a bin a sample");
		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":4,"bins":100,"id":4})"));
		check(ok(4) && last("sampleWave")->find("bins")->asNumber() == 0 && last("sampleWave")->find("peaks")->asArray().empty(), "sampleWave: an empty slot has no peaks");
		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"ram","slot":7,"bins":100,"id":5})"));
		check(!ok(5), "sampleWave: RAM slots are 0-3");
		desk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":3,"bins":9000,"id":6})"));
		check(!ok(6), "sampleWave: at most 8192 bins (the table's range)");
		check(ed::mdWavePeaks(snr, 2) == std::vector<int16_t>{20000, 20000, -32767, -32767}, "mdWavePeaks: two bins");

		// The audition: playing, then stopped when it played out.
		page.clear();
		desk.onPageMessage(cmd(R"({"op":"audition","bank":"rom","slot":3,"id":7})"));
		const auto* a = last("audition");
		check(ok(7) && a && a->find("state")->asString() == "playing" && a->find("rate")->asNumber() == 22050 && a->find("length")->asNumber() == 3000
			&& mixer.status().playing, "audition: playing, the slot's own rate and length");
		std::vector<float> out(4096, 0.0f);
		mixer.mix(out.data(), out.data(), 4096, 44100);
		desk.tick();
		check(last("audition")->find("state")->asString() == "playing" && out[10] > 0.5f, "audition: the sample's audio, still playing");
		const auto messages = page.size();
		desk.tick();
		desk.tick();
		check(page.size() == messages, "audition: nothing while it plays (event-driven, no position stream)");
		mixer.mix(out.data(), out.data(), 4096, 44100);
		desk.tick();
		check(last("audition")->find("state")->asString() == "stopped", "audition: stopped once it played out");

		desk.onPageMessage(cmd(R"({"op":"audition","bank":"rom","slot":3,"id":8})"));
		desk.onPageMessage(cmd(R"({"op":"auditionStop","id":9})"));
		check(ok(9) && last("audition")->find("state")->asString() == "stopped" && !mixer.status().playing, "auditionStop: stopped");
		desk.onPageMessage(cmd(R"({"op":"audition","bank":"rom","slot":4,"id":10})"));
		check(!ok(10), "audition: an empty slot is refused");

		// Stopped by someone else (the status' latest id is not ours): stopped too.
		desk.onPageMessage(cmd(R"({"op":"audition","bank":"rom","slot":3,"id":11})"));
		mixer.stop();
		desk.tick();
		check(last("audition")->find("state")->asString() == "stopped", "audition: the device's own stop is heard too");

		// HW MIDI: no sound of its own; refused with the capability's reason.
		std::vector<Value> hwPage;
		Desk::Port hw;
		hw.device.sendSysex = [](const std::vector<uint8_t>&) {};
		hw.device.nowMs = [] { return 0.0; };
		hw.toPage = [&](const Value& _m) { hwPage.push_back(_m); g_published.push_back(_m); };
		Desk hwDesk(hw, wireProfile());
		hwDesk.onPageMessage(cmd(R"({"op":"ready"})"));
		hwDesk.onSampleBank(bank);
		hwDesk.onPageMessage(cmd(R"({"op":"audition","bank":"rom","slot":3,"id":12})"));
		hwDesk.onPageMessage(cmd(R"({"op":"sampleWave","bank":"rom","slot":3,"bins":64,"id":13})"));
		size_t refused = 0;
		for(const auto& m : hwPage)
			if(m.find("type")->asString() == "result" && !m.find("ok")->asBool() && m.find("errors")->asArray()[0].asString().find("real Machinedrum") != std::string::npos)
				++refused;
		check(refused == 2, "HW MIDI: audition and sampleWave refused with the sampleAudio reason");
	}

	void testModulators()
	{
		std::vector<std::string> errors;
		const auto setup = modSetupFromJson(cmd(R"({"schema":"md-desk/modulators","version":1,
			"sources":[{"id":"lfoA","kind":"lfo","shape":1,"rate":"1/4","depth":100},{"id":"rndA","kind":"random","rate":"1/16","smooth":0}],
			"links":[{"source":"lfoA","track":2,"param":12,"min":20,"max":100},{"source":"lfoA","track":3,"param":0,"min":0,"max":127,"curve":"exp","invert":true},
			{"source":"rndA","track":4,"param":16}]})"), errors);
		check(setup && errors.empty(), "a modulator setup parses");
		check(setup && modSetupFromJson(modSetupToJson(*setup), errors) && errors.empty(), "and round-trips");
		const auto bad = modSetupFromJson(cmd(R"({"schema":"md-desk/modulators","version":1,"sources":[{"id":"a","kind":"lfo","rate":"3"}],
			"links":[{"source":"b","track":16,"param":24}]})"), errors);
		check(!bad && errors.size() >= 4, "bad rate, source, track and param are refused with paths");
		deskCore::Modulators m;
		m.setSetup(*setup);
		// A saw over 4 steps on link 1: 20 + 80 * (lfo / 127), lfo 1, 33, 64, 96.
		std::vector<int> link1;
		for(int s = 0; s < 8; ++s)
			for(const auto& o : m.step())
				if(o.track == 2)
					link1.push_back(o.value);
		check(link1 == std::vector<int>{21, 41, 60, 80, 21, 41, 60, 80}, "the saw LFO moves link 1 per step within min..max");
		const auto first = m.step();
		m.reset();
		check(!m.step().empty(), "after a stop the next step sends again");
		deskCore::CcBudget budget;
		int taken = 0;
		for(int i = 0; i < 400; ++i)
			taken += budget.take(100 + i);
		check(taken == g_modCcPerSecond, "the CC budget allows 300 a second");
		check(budget.take(1200), "and refills after a second");
		(void)first;
	}
	// The executable spec: every published message validates against the contract's
	// JSON Schema ($defs/message). GEARMULATOR_DUMP_MESSAGES=1 prints one of each type.
	// The executable spec (deskCore::contract): every published message against the contract and the
	// contract's machine document against what was published; $defs/command generated from the
	// tables (mdDeskTest --write-schema rewrites it); the adapter's functions against the table.
	// P10, the page's keyboard: a key is the track's MAP EDITOR note on the base channel, a held PTCH is a live
	// value sent before it and put back when the key is let go (not an edit: the kit document keeps its value).
	void testKeyNote()
	{
		std::vector<std::array<uint8_t, 3>> params, notes;
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		std::vector<std::string> order;
		port.device.sendSysex = [](const std::vector<uint8_t>&) {};
		port.device.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); order.push_back("cc"); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		{
			Desk none(port);
			none.onPageMessage(cmd(R"({"op":"ready"})"));
			Telemetry tel;
			tel.valid = true;
			tel.bootAnimation = 0;
			none.onTelemetry(tel);
			none.onPageMessage(cmd(R"({"op":"keyNote","t":0,"vel":100,"id":1})"));
			const Value* r = nullptr;
			for(auto it = page.rbegin(); it != page.rend() && !r; ++it)
				if(it->find("type") && it->find("type")->asString() == "result")
					r = &*it;
			check(r && !r->find("ok")->asBool(), "keyNote: an engine without notes refuses");
		}
		port.device.sendNote = [&](uint8_t _c, uint8_t _n, uint8_t _v) { notes.push_back({_c, _n, _v}); order.push_back("note"); };
		Desk desk(port);
		const auto status = [](const ed::MdStatus _p, const uint8_t _v)
		{
			return std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_p), _v, 0xf7};
		};
		const auto ok = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type") && it->find("type")->asString() == "result")
					return it->find("ok")->asBool();
			return false;
		};
		auto kit = *ed::decodeMdKit(load("programmed_kit_0.syx"));
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		desk.onDeviceSysex(status(ed::MdStatus::Kit, kit.position));
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		Telemetry tel;
		tel.valid = true;
		tel.bootAnimation = 0;
		desk.onTelemetry(tel);
		const auto before = kit.params[4][0];
		const auto held = static_cast<uint8_t>(before == 70 ? 73 : 70);
		desk.onPageMessage(cmd(R"({"op":"keyNote","t":4,"vel":100,"id":2})"));
		check(ok() && notes.size() == 1 && notes[0] == std::array<uint8_t, 3>{0, 43, 100} && params.empty(),
			"keyNote: track 5 is G2 (43, the manual's default map) on the base channel, no kit value");
		desk.onPageMessage(cmd(R"({"op":"keyNote","t":4,"vel":0,"id":3})"));
		check(ok() && notes.size() == 2 && notes[1] == std::array<uint8_t, 3>{0, 43, 0}, "keyNote: let go is the note off");
		order.clear();
		desk.onPageMessage(cmd("{\"op\":\"keyNote\",\"t\":4,\"vel\":127,\"i\":0,\"v\":" + std::to_string(held) + ",\"id\":4}"));
		check(ok() && params.size() == 1 && params[0] == std::array<uint8_t, 3>{4, 0, held} && order == std::vector<std::string>{"cc", "note"},
			"keyNote: the held PTCH goes before the note");
		const auto* w = desk.documents().working ? &desk.documents().working->kit : nullptr;
		check(w && w->params[4][0] == before, "keyNote: the kit document keeps its PTCH (not an edit)");
		desk.onPageMessage(cmd(R"({"op":"undo","id":5})"));
		desk.onPageMessage(cmd(R"({"op":"keyNote","t":4,"vel":0,"id":6})"));
		check(ok() && params.size() == 2 && params[1] == std::array<uint8_t, 3>{4, 0, before} && notes.back()[2] == 0,
			"keyNote: let go puts the PTCH back");
		desk.onPageMessage(cmd(R"({"op":"keyNote","t":4,"vel":0,"id":7})"));
		check(ok() && params.size() == 2, "keyNote: a second let go sends no value");
	}

	void checkContract(const bool _write)
	{
		namespace contract = deskCore::contract;
		std::ifstream in(MDDESK_SCHEMA);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		auto root = ed::json::parse(text);
		check(root.has_value(), "the contract schema loads");
		if(!root)
			return;
		const auto generated = deskHost::contractCommands(commandTable().schema());
	std::vector<std::string> kinds;
	for(const auto* n : deskCore::kindNames<mdDesk::MdModel>())
		kinds.push_back(n);
		if(_write)
		{
			std::ofstream out(MDDESK_SCHEMA);
			out << ed::json::write(contract::withAsks(contract::withDocKinds(contract::withGenerated(*root, generated), kinds), mdDesk::MdModel::asks()), 2) << "\n";
			return;
		}
		check(contract::sameCommands(*root, generated), "the schema's $defs/command is generated from the command tables (--write-schema)");
		check(contract::sameLifecycle(*root), "the schema's lifecycle enum is the lifecycle rows (--write-schema)");
	check(contract::sameAsks(*root, mdDesk::MdModel::asks()), "the schema's ask enum is the model's questions (--write-schema)");
	for(const auto& gap : contract::docKindGaps(*root, kinds))
		check(false, gap.c_str());
		// The plug-in's host sends these; this test has no host.
		const auto r = contract::checkMessages(*root, g_published, {"learn", "audio", "audioLevel", "openAudio", "romInstall", "romInfo", "notice", "syxPreview", "syxProgress", "syxExport"});
		for(const auto& p : r.off)
			std::printf("    %s\n", p.c_str());
		for(const auto& u : r.unseen)
			std::printf("    declared, never published: %s\n", u.c_str());
		std::printf("  %zu published messages of %zu types, %zu off the contract\n", r.messages, r.types, r.offCount);
		check(r.offCount == 0 && r.messages > 0, "every published message is on the contract");
		check(r.unseen.empty(), "every message type and machine member the contract declares is published");
		auto gaps = contract::handlerGaps(commandTable(), deskCore::Owner::Machine, MdMachine::commandsHandled());
		for(const auto& g : contract::handlerGaps(commandTable(), deskCore::Owner::Setup, Desk::setupOps()))
			gaps.push_back(g);
		for(const auto& g : contract::editGaps(commandTable(), editOps()))
			gaps.push_back(g);
		for(const auto& g : contract::unknownOps(commandTable(), MdMachine::commandsAsking()))
			gaps.push_back(g);
		for(const auto& g : gaps)
			std::printf("    %s\n", g.c_str());
		check(gaps.empty(), "every machine command of the table has its adapter function, and no other; every ask is a command");
	}

	// The desk holds whichever adapter the engine gives it (P6): a fake with no protocol at all
	// gets the page's documents, its edits and its machine commands through the same core.
	class FakeMdAdapter final : public deskCore::AdapterBase<MdModel, MdAdapter>
	{
	public:
		std::vector<Change> submitted;
		std::vector<std::string> commands;
		std::optional<Probe> probe;

		void hold(const Document& _doc) { observe(_doc, deskCore::Source::Dump); }

		deskCore::Outcome review(const Value&, const std::vector<Change>&, const Documents&) override { return {}; }
		deskCore::Outcome submit(const Change& _change, const Documents&) override
		{
			submitted.push_back(_change);
			settle(_change.after, deskCore::Source::Tracked);
			return {};
		}
		deskCore::Outcome command(const Value& _command, const Documents&) override
		{
			commands.push_back(deskCore::opOf(_command));
			return {};
		}
		void onSysex(const Bytes&) override {}
		void tick(double, const Documents&) override {}
		Value state(const Documents&) const override { return Value::object(); }
		deskCore::Capabilities capabilities() const override
		{
			deskCore::Capabilities c;
			c.engine = "fake";
			c.label = "FAKE";
			return c;
		}
		deskCore::Lifecycle lifecycle() const override { return deskCore::Lifecycle::Ready; }
		Context context() const override { return {std::optional<uint8_t>(0)}; }
		bool busy() const override { return false; }

		void setProbe(const Probe _probe) override { probe = _probe; }
		TelemetryEvents onTelemetry(const Telemetry& _t) override { m_telemetry = _t; return {}; }
		void onWorkingKitMemory(const Bytes&, const Documents&) override {}
		void onHostKitParam(uint8_t, uint8_t, uint8_t, const Documents&) override {}
		void onHostMute(uint8_t, bool) override {}
		void sendModulation(uint8_t, uint8_t, uint8_t, const Documents&) override {}
		const Telemetry& telemetry() const override { return m_telemetry; }
		std::string sendSample(uint8_t, const elektronData::MdSampleUpload&) override { return "no samples here"; }
		void cancelSample() override {}
		const mdDesk::SdsSender::Progress& sampleProgress() const override { return m_sample; }

	private:
		Telemetry m_telemetry;
		mdDesk::SdsSender::Progress m_sample;
	};

	void testFakeAdapter()
	{
		std::vector<Value> page;
		double now = 0;
		Desk::Port port;
		port.toPage = [&](const Value& _m) { page.push_back(_m); };
		port.device.nowMs = [&] { return now; };
		int readies = 0;
		port.ready = [&] { ++readies; };
		auto fake = std::make_unique<FakeMdAdapter>();
		auto& f = *fake;
		Desk desk(std::move(fake), port);
		const auto count = [&](const char* _type)
		{
			return std::count_if(page.begin(), page.end(), [&](const Value& _m) { return _m.find("type")->asString() == _type; });
		};
		desk.onPageMessage(cmd(R"({"op":"ready","id":1})"));
		desk.tick();
		check(count("catalogue") == 1 && count("machine") >= 1 && readies == 1, "fake adapter: ready, the catalogue and the machine document, then the ready hook");
		ed::MdPattern p;
		p.position = 3;
		f.hold(p);
		desk.tick();
		desk.onPageMessage(cmd(R"({"op":"trig","p":3,"t":0,"s":2,"id":2})"));
		desk.tick();
		check(f.submitted.size() == 1 && std::holds_alternative<ed::MdPattern>(f.submitted[0].after)
			&& ed::hasTrig(std::get<ed::MdPattern>(f.submitted[0].after), 0, 2), "fake adapter: a page edit arrives as one change");
		desk.onPageMessage(cmd(R"({"op":"play","id":3})"));
		check(f.commands == std::vector<std::string>{"play"}, "fake adapter: a machine command reaches it");
		desk.setProbe(deskCore::LifeFacts::Probe::Running);
		check(f.probe == deskCore::LifeFacts::Probe::Running, "fake adapter: the device's facts reach it");
	}
}

int main(const int _argc, char** _argv)
{
	if(_argc > 1 && std::string(_argv[1]) == "--write-schema")
	{
		checkContract(true);
		std::puts("mdDeskTest: wrote $defs/command");
		return 0;
	}
	testTrigsAndLocks();
	testPatternSettingsAndValidation();
	testCopyPaste();
	testKitEditsAndDelivery();
	testSongEdits();
	testGlobal();
	testHistory();
	testStepsAndParams();
	testRotateAndDouble();
	testStepsAndParamsDesk();
	testPushSlot();
	testDesk();
	testLive();
	testSetup();
	testLockStep();
	testLibrary();
	testHardwareLink();
	testGlobalSet();
	testWorkingKitMemory();
	testKnobRecorder();
	testDeskRecording();
	testPacedLockDraw();
	testControlAll();
	testSampleName();
	testSampleFiles();
	testSampleMemory();
	testSampleLoad();
	testAuditionMixer();
	testSampleWaveAndAudition();
	testModulators();
	testKeyNote();
	testFakeAdapter();
	checkContract(false);
	if(g_failures)
	{
		std::fprintf(stderr, "mdDeskTest: %d failure(s)\n", g_failures);
		return 1;
	}
	std::puts("mdDeskTest: PASS");
	return 0;
}
