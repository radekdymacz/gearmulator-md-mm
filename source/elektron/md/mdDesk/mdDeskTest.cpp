#include <algorithm>
// MD Desk editing model without firmware: commands -> documents, validation,
// undo/redo, copy/paste, the live edits a kit change needs, and the Desk
// orchestrator against a scripted device. Firmware behaviour is covered by
// mdLibTest/mdDeskFirmwareTest.cpp (manual, needs the ROM).

#include "mdDesk.h"
#include "mdDeskLibrary.h"

#include "elektronData/jsonSchema.h"
#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdValidate.h"
#include "elektronData/mdWorkingKit.h"

#include <cstdio>
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

	// apply is pure: the clipboard a copy leaves comes back in the result.
	EditResult run(const Documents& _docs, const Value& _cmd, Clipboard& _clip)
	{
		auto r = apply(_docs, _cmd, _clip);
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

		Clipboard none;
		r = run(docs, cmd(R"({"op":"pasteSteps","p":1,"t":0,"from":0})"), none);
		check(!r.errors.empty(), "paste without a copy is refused");
	}

	void testKitEditsAndDelivery()
	{
		auto docs = fixtureDocs();
		Clipboard clip;
		const auto& k = docs.kits[0];
		auto r = run(docs, cmd(R"({"op":"param","k":0,"t":3,"i":16,"v":99})"), clip);
		check(r.errors.empty() && r.changes.size() == 1, "kit param edit");
		auto d = kitDelivery(k, std::get<ed::MdKit>(r.changes[0].after));
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::Param && d.edits[0].track == 3
			&& d.edits[0].index == 16 && d.edits[0].value == 99, "a param edit is one CC");
		check(liveEditSysex(d.edits[0]).empty(), "params travel as CCs, not SysEx");

		const auto sd = *ed::mdMachineModel("EFM-SD");
		r = run(docs, cmd("{\"op\":\"machine\",\"k\":0,\"t\":2,\"model\":" + std::to_string(sd) + "}"), clip);
		check(r.errors.empty(), "machine change");
		d = kitDelivery(k, std::get<ed::MdKit>(r.changes[0].after));
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

		r = run(docs, cmd(R"({"op":"machine","k":0,"t":2,"model":5})"), clip);
		check(!r.errors.empty(), "an undefined machine id is refused");

		r = run(docs, cmd(R"({"op":"lfo","k":0,"t":1,"field":"shape2","v":4})"), clip);
		d = kitDelivery(k, std::get<ed::MdKit>(r.changes.at(0).after));
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::Lfo, "LFO shape is one live edit");
		const auto lfo = liveEditSysex(d.edits[0]);
		check(lfo.size() == 10 && lfo[6] == 0x62 && lfo[7] == ((1 << 3) | 3) && lfo[8] == 4, "set LFO message");
		r = run(docs, cmd(R"({"op":"lfo","k":0,"t":1,"field":"shape2","v":6})"), clip);
		check(!r.errors.empty(), "LFO shape 6 is refused");

		r = run(docs, cmd(R"({"op":"masterFx","k":0,"fx":"rhythmEcho","i":3,"v":10})"), clip);
		d = kitDelivery(k, std::get<ed::MdKit>(r.changes.at(0).after));
		const auto fx = liveEditSysex(d.edits.at(0));
		check(fx[6] == 0x5d && fx[7] == 3 && fx[8] == 10, "rhythm echo parameter is 0x5d");

		r = run(docs, cmd(R"({"op":"group","k":0,"t":4,"kind":"mute","target":5})"), clip);
		check(r.errors.empty() && std::get<ed::MdKit>(r.changes[0].after).muteGroups[4] == 5, "mute group");
		r = run(docs, cmd(R"({"op":"group","k":0,"t":4,"kind":"mute","target":4})"), clip);
		check(!r.errors.empty(), "a track cannot group with itself");

		r = run(docs, cmd(R"({"op":"kitName","k":0,"name":"DUB ROOM"})"), clip);
		d = kitDelivery(k, std::get<ed::MdKit>(r.changes.at(0).after));
		check(d.edits.size() == 1 && d.edits[0].kind == LiveEdit::Kind::KitName
			&& liveEditSysex(d.edits[0]).size() == 24, "kit name is 0x55 with 16 bytes");

		// Copy / paste a sound between tracks.
		r = run(docs, cmd(R"({"op":"copySound","k":0,"t":0})"), clip);
		check(clip.sound && r.changes.empty(), "copy sound");
		r = run(docs, cmd(R"({"op":"pasteSound","k":0,"t":9})"), clip);
		const auto& pasted = std::get<ed::MdKit>(r.changes.at(0).after);
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
		auto u = h.undo();
		check(u && u->size() == 1, "undo returns the step");
		docs.set(u->at(0).after);
		u = h.undo();
		docs.set(u->at(0).after);
		u = h.undo();
		docs.set(u->at(0).after);
		check(docs.patterns[1] == original && !h.canUndo() && h.canRedo(), "three undos restore the pattern");
		auto re = h.redo();
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
		const auto step = g.undo();
		check(step && std::get<ed::MdPattern>(step->at(0).after) == start, "undoing the gesture restores its start");
	}

	void testPushSlot()
	{
		PushSlot<int> slot;
		using R = PushSlot<int>::ReadBack;
		check(slot.onReadBack(1) == R::NotWaiting, "a refresh while idle");
		check(slot.want(1), "first edit goes out");
		check(!slot.want(2) && !slot.want(3), "later edits wait");
		check(slot.onReadBack(0) == R::Other, "an older reply does not confirm");
		check(slot.onReadBack(1) == R::ConfirmedSendNext && slot.inFlight() == 3, "latest edit wins, one in flight");
		check(slot.onReadBack(3) == R::Confirmed && !slot.busy(), "confirmed and idle");
	}

	// The Desk against a scripted device: the page's view of one edit.
	void testDesk()
	{
		std::vector<std::vector<uint8_t>> wire;
		std::vector<Value> page;
		std::vector<std::array<uint8_t, 3>> params;
		double now = 0;
		Desk::Port port;
		port.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.nowMs = [&] { return now; };
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

		// A second edit while the first is in flight waits for the read-back.
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"trig","p":1,"t":0,"s":2,"id":3,"g":5})"));
		check(wire.empty(), "no second dump while one is in flight");
		now = 148;
		desk.onDeviceSysex(ed::encodeMdPattern(sent));
		check(!wire.empty() && ed::hasTrig(*ed::decodeMdPattern(wire[0]), 0, 2) != ed::hasTrig(pattern, 0, 2),
			"the waiting edit goes out on the read-back");
		check(desk.lastRoundTripMs() == 48, "round trip measured from the read-back");
		desk.onDeviceSysex(wire[0]);
		check(!desk.isBusy() || now - 100 < 120, "idle after the last read-back");

		// A live kit edit is a CC, marks the kit edited, never a dump.
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"param","k":0,"t":1,"i":4,"v":77,"id":4})"));
		check(params.size() == 1 && params[0] == std::array<uint8_t, 3>{1, 4, 77}, "kit param goes to the CC path");
		check(wire.empty(), "no kit dump for a live edit");
		check(desk.session().state().workingKit == mdDataLink::Session::WorkingKit::Edited, "kit is edited");
		// A stored-slot dump does not overwrite the working copy.
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		check(desk.documents().kits.at(0).params[1][4] == 77, "working copy survives a stored-slot dump");

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
		now += 2500;
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
		port.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.sendKitParam = [&](uint8_t, uint8_t, uint8_t) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.nowMs = [&] { return now; };
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
		check(desk.session().state().workingKit != mdDataLink::Session::WorkingKit::Edited, "stored kit loaded");

		// A value changed on the machine's panel, never seen by the desk.
		auto panel = kit;
		panel.params[2][5] = static_cast<uint8_t>(kit.params[2][5] ^ 0x11);
		desk.onWorkingKitMemory(region(panel));
		check(desk.documents().kits.at(3).params[2][5] == panel.params[2][5], "memory edit shows in the kit document");
		check(desk.session().state().workingKit == mdDataLink::Session::WorkingKit::Edited, "memory differs: edited");
		check(machine() && machine()->find("desk")->find("kitSource")->asString() == "memory", "the page is told: memory");
		// A stored-slot dump keeps the memory copy.
		desk.onDeviceSysex(ed::encodeMdKit(kit));
		check(desk.documents().kits.at(3).params[2][5] == panel.params[2][5], "a stored dump does not undo memory");
		// Back to the stored values: clean without SAVE KIT.
		desk.onWorkingKitMemory(region(kit));
		check(desk.session().state().workingKit == mdDataLink::Session::WorkingKit::Clean, "memory equals slot: clean");

		// Right after the desk's own live edit, an image is held (it may predate the CC).
		now = 1000;
		desk.onPageMessage(cmd(R"({"op":"param","k":3,"t":0,"i":1,"v":5,"id":1})"));
		desk.onWorkingKitMemory(region(kit));
		check(desk.documents().kits.at(3).params[0][1] == 5, "held: the optimistic edit stays");
		auto after = kit;
		after.params[0][1] = 5;
		desk.onWorkingKitMemory(region(after));
		now = 1200;
		desk.tick();
		check(desk.documents().kits.at(3).params[0][1] == 5, "applied after the hold with the edit in memory");

		// Memory names another kit than status: ask status, apply once it agrees.
		auto next = kit;
		next.position = 7;
		wire.clear();
		desk.onWorkingKitMemory(region(next));
		bool asked = false;
		for(const auto& w : wire)
			asked |= w.size() == 9 && w[6] == 0x70 && w[7] == static_cast<uint8_t>(ed::MdStatus::Kit);
		check(asked && !desk.documents().kits.count(7), "a kit switch seen in memory asks for status first");
		desk.onDeviceSysex(status(ed::MdStatus::Kit, 7));
		desk.tick();
		check(desk.documents().kits.count(7) && desk.documents().kits.at(7) == next, "then the new kit comes from memory");
		check(!ed::mdWorkingKitFromMemory(std::vector<uint8_t>(10, 0)), "a short region is refused");
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
		port.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.sendKitParam = [&](uint8_t _t, uint8_t _i, uint8_t _v) { params.push_back({_t, _i, _v}); };
		port.pressKey = [&](const std::string& _k) { keys.push_back(_k); return true; };
		port.turnKnob = [&](uint8_t _e, int _s) { turns.emplace_back(_e, _s); return true; };
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.nowMs = [&] { return now; };
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
		check(desk.documents().kits.at(kit.position).params[2][3] == 5, "the view keeps the wanted value meanwhile");
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
		check(ok() && !wire.empty() && wire[0] == ed::mdLoadSong(3) && desk.session().state().song == 3, "LOAD SONG 4 when stopped");
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
		port.sendSysex = [&](const std::vector<uint8_t>& _b) { wire.push_back(_b); };
		port.pressKey = [&](const std::string& _k) { keys.push_back(_k); return true; };
		port.sendMute = [](uint8_t, bool) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.nowMs = [&] { return now; };
		Desk desk(port);
		const auto last = [&](const char* _type) -> const Value*
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == _type)
					return &*it;
			return nullptr;
		};
		const auto ok = [&] { const auto* r = last("result"); return r && r->find("ok")->asBool(); };
		const auto firmware = [&] { const auto* m = last("machine"); return m ? m->find("doc")->find("desk")->find("firmware")->asString() : std::string(); };
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
		check(desk.isReady() && !desk.isInputReady() && firmware() == "booting", "status answered, animation running: BOOTING OS");
		desk.onPageMessage(cmd(R"({"op":"play","id":1})"));
		check(!ok() && keys.empty(), "PLAY is held back during the animation");
		t.bootAnimation = 0;
		desk.onTelemetry(t);
		desk.tick();
		check(desk.isInputReady() && firmware() == "ready", "animation over: ready");
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[3,1,4],"id":2})"));
		check(ok() && keys.back() == "chain:0:3,1,4", "chain command: the machine's keys");
		t.chain.active = true;
		t.chain.patterns = {3, 1, 4};
		t.chain.next = 1;
		t.mutes = 0x0005;
		desk.onTelemetry(t);
		desk.tick();
		const auto* m = last("machine");
		const auto& d = *m->find("doc")->find("desk");
		check(d.find("chain")->find("active")->asBool() && d.find("chain")->find("patterns")->asArray().size() == 3,
			"the page sees the firmware's chain");
		check(d.find("mutes")->asArray().size() == 2 && d.find("mutesSource")->asString() == "memory", "mutes 1 and 3 from memory");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"id":3})"));
		const auto* ask = last("ask");
		check(ask && ask->find("ask")->asString() == "breakChain" && wire.empty(), "select while chained asks first");
		desk.onPageMessage(cmd(R"({"op":"select","p":9,"chainOk":true,"id":4})"));
		check(!wire.empty() && wire.front() == ed::mdLoadPattern(9), "chainOk: LOAD PATTERN");
		wire.clear();
		desk.onPageMessage(cmd(R"({"op":"chainClear","id":5})"));
		check(ok() && !wire.empty() && wire.front() == ed::mdLoadPattern(2), "CLEAR = LOAD PATTERN of the current pattern");
		desk.onPageMessage(cmd(R"({"op":"chain","patterns":[1,17],"id":6})"));
		check(!ok(), "a chain across banks is refused");
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
		port.nowMs = [] { return 0.0; };
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
		check(r.errors.empty() && r.changes.size() == 1 && r.changes[0].slotWrite && std::get<ed::MdKit>(r.changes[0].after).position == 9,
			"kit paste: a slot write into K10");
		r = run(docs, cmd(R"({"op":"kitClear","k":9})"), clip);
		check(r.changes.size() == 1 && isEmptyKit(std::get<ed::MdKit>(r.changes[0].after)), "kit clear: an empty kit");
		r = run(docs, cmd(R"({"op":"kitRename","k":9,"name":"new kit"})"), clip);
		check(r.changes.size() == 1 && std::get<ed::MdKit>(r.changes[0].after).name[0] == 'N', "rename: upper case, 16 characters");
		check(!run(docs, cmd(R"({"op":"kitCopyTo","from":9,"to":9})"), clip).errors.empty(), "same slot refused");
		r = run(docs, cmd(R"({"op":"patClear","p":20})"), clip);
		const auto& cleared = std::get<ed::MdPattern>(r.changes.at(0).after);
		check(!r.changes[0].slotWrite && cleared.length == pat.length && cleared.kit == pat.kit
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
		port.sendSysex = [](const std::vector<uint8_t>&) {};
		port.toPage = [&](const Value& _m) { page.push_back(_m); g_published.push_back(_m); };
		port.nowMs = [&] { return now; };
		Desk desk(port);
		desk.setHardwareLink(true);
		desk.onTelemetry(Telemetry{});
		const auto link = [&]
		{
			for(auto it = page.rbegin(); it != page.rend(); ++it)
				if(it->find("type")->asString() == "machine")
					return it->find("doc")->find("desk")->find("link")->asString();
			return std::string();
		};
		desk.onPageMessage(cmd(R"({"op":"ready"})"));
		check(link() == "connect", "HW: connect until the machine answers");
		now += 6000;
		desk.tick();
		check(link() == "lost", "HW: nothing answers for 5 s: HW NO MIDI");
		desk.onDeviceSysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x00, 0xf7});
		desk.tick();
		check(link() == "ready" && desk.isInputReady(), "HW: the first status reply: HW MIDI, input taken");
		now += 4000;
		desk.tick();
		check(link() == "lost", "HW: no reply for 3.5 s: lost");
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
		Modulators m;
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
		CcBudget budget;
		int taken = 0;
		for(int i = 0; i < 400; ++i)
			taken += budget.take(100 + i);
		check(taken == g_modCcPerSecond, "the CC budget allows 300 a second");
		check(budget.take(1200), "and refills after a second");
		(void)first;
	}
	// The executable spec: every published message validates against the contract's
	// JSON Schema ($defs/message). GEARMULATOR_DUMP_MESSAGES=1 prints one of each type.
	void checkPublished()
	{
		std::ifstream in(MDDESK_SCHEMA);
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
	// The command table is the vocabulary (P6): the contract's $defs/command must be what the
	// table generates. mdDeskTest --write-schema rewrites it.
	void checkCommandSchema(const bool _write)
	{
		std::ifstream in(MDDESK_SCHEMA);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		auto root = ed::json::parse(text);
		check(root.has_value(), "the contract schema loads");
		if(!root)
			return;
		const auto generated = commandTable().schema();
		auto* defs = root->find("$defs");
		const auto* current = defs ? defs->find("command") : nullptr;
		const bool same = current && ed::json::write(*current) == ed::json::write(generated);
		if(_write && !same && defs)
		{
			defs->put("command", generated);
			std::ofstream out(MDDESK_SCHEMA);
			out << ed::json::write(*root, 2) << "\n";
			std::printf("  wrote $defs/command (%zu commands)\n", commandTable().commands().size());
			return;
		}
		check(same, "the schema's $defs/command is generated from the command table (mdDeskTest --write-schema)");
		// Every command the page sends validates against it; so does a bad one not.
		const ed::json::Schema schema(*root);
		check(schema.validate(*ed::json::parse(R"({"op":"trig","p":1,"t":0,"s":3,"id":4})"), "command").empty()
			&& !schema.validate(*ed::json::parse(R"({"op":"trig","p":200,"t":0,"s":3})"), "command").empty(), "commands validate");
	}
}

int main(const int _argc, char** _argv)
{
	if(_argc > 1 && std::string(_argv[1]) == "--write-schema")
	{
		checkCommandSchema(true);
		return 0;
	}
	testTrigsAndLocks();
	testPatternSettingsAndValidation();
	testCopyPaste();
	testKitEditsAndDelivery();
	testSongEdits();
	testGlobal();
	testHistory();
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
	testSampleName();
	testModulators();
	checkPublished();
	checkCommandSchema(false);
	if(g_failures)
	{
		std::fprintf(stderr, "mdDeskTest: %d failure(s)\n", g_failures);
		return 1;
	}
	std::puts("mdDeskTest: PASS");
	return 0;
}
