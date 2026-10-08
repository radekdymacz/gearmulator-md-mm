// Pure unit tests for .syx import and export (P7, elektronData/syxImport.h) on synthetic dumps
// built with the codecs' own encoders: parse, problems, summary, import plan, and a byte-exact
// export round trip; B-019's as-is import: what can be sent and why not, the compared form, the
// outcomes, and the session's import job (mdSyxSession.h) against a fake machine. No firmware, no files.

#include "elektronData/syxImport.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mmDump.h"

#include "../mdJucePlugin/mdSyxSession.h"	// the session's import job, header only (B-019)

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
	namespace ed = elektronData;
	using Bytes = std::vector<uint8_t>;
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	void append(Bytes& _out, const Bytes& _m)
	{
		_out.insert(_out.end(), _m.begin(), _m.end());
	}

	template<size_t N>
	void setName(std::array<uint8_t, N>& _name, const std::string& _s)
	{
		_name.fill(0);
		for(size_t i = 0; i < _s.size() && i < N; ++i)
			_name[i] = static_cast<uint8_t>(_s[i]);
	}

	ed::MdKit mdKit(const uint8_t _slot)
	{
		ed::MdKit k;
		k.position = _slot;
		setName(k.name, "KIT" + std::to_string(_slot));
		k.models[0] = 1;
		k.trigGroups.fill(ed::MdKit::g_noGroup);
		k.muteGroups.fill(ed::MdKit::g_noGroup);
		return k;
	}

	ed::MdPattern mdPattern(const uint8_t _slot)
	{
		ed::MdPattern p;
		p.position = _slot;
		p.kit = static_cast<uint8_t>(_slot % 64);
		p.trigs[_slot % 16] = 1;
		for(auto& row : p.lockRows)
			row.fill(ed::MdPattern::g_noLock);
		return p;
	}

	ed::MdSong mdSong(const uint8_t _slot)
	{
		ed::MdSong s;
		s.position = _slot;
		setName(s.name, "SONG" + std::to_string(_slot));
		ed::MdSongRow row;
		row.pattern = _slot;
		s.rows = {row, ed::MdSongRow{}};
		return s;
	}

	ed::MdGlobal mdGlobal(const uint8_t _slot)
	{
		ed::MdGlobal g;
		g.position = _slot;
		g.keymap.fill(ed::MdGlobal::g_unmapped);
		return g;
	}

	// A complete MD backup: 8 globals, 64 kits, 128 patterns, 32 songs.
	ed::MdDocuments mdBackup()
	{
		ed::MdDocuments d;
		for(uint8_t i = 0; i < 8; ++i)
			d.globals[i] = mdGlobal(i);
		for(uint8_t i = 0; i < 64; ++i)
			d.kits[i] = mdKit(i);
		for(uint8_t i = 0; i < 128; ++i)
			d.patterns[i] = mdPattern(i);
		for(uint8_t i = 0; i < 32; ++i)
			d.songs[i] = mdSong(i);
		return d;
	}

	ed::MmDocuments mmBackup()
	{
		ed::MmDocuments d;
		for(uint8_t i = 0; i < 8; ++i)
		{
			ed::MmGlobal g;
			g.position = i;
			d.globals[i] = g;
		}
		for(uint8_t i = 0; i < 128; ++i)
		{
			ed::MmKit k;
			k.position = i;
			setName(k.name, "MK" + std::to_string(i));
			k.machines[0] = 1;
			d.kits[i] = k;
		}
		for(uint8_t i = 0; i < 128; ++i)
		{
			ed::MmPattern p;
			p.position = i;
			p.kit = static_cast<uint8_t>(127 - i);
			for(auto& t : p.notes)
				t.fill(ed::MmPattern::g_noNote);
			for(auto& r : p.lockRows)
				r.fill(ed::MmPattern::g_noLock);
			p.amp[0] = ed::mmStepBit(i % 64);
			d.patterns[i] = p;
		}
		for(uint8_t i = 0; i < 24; ++i)
		{
			ed::MmSong s;
			s.position = i;
			setName(s.name, "MS" + std::to_string(i));
			s.rows[0].bytes[0] = i;
			for(size_t r = 1; r < s.rows.size(); ++r)
				s.rows[r].bytes[0] = ed::MmSong::g_end;
			d.songs[i] = s;
		}
		return d;
	}

	void parseOk()
	{
		std::printf("parse\n");
		const auto bytes = ed::writeSyx(mdBackup());
		const auto file = ed::parseSyx(bytes);
		check(file.model == ed::SyxModel::Md, "the model is the Machinedrum");
		check(file.messages.size() == 232, "232 messages");
		check(file.problems.empty(), "no problems");
		check(file.md.kits.size() == 64 && file.md.patterns.size() == 128, "64 kits, 128 patterns");
		check(file.mm.kits.empty(), "no MM documents");
		check(file.md.kits.at(7) == mdKit(7), "kit 7 decodes to its value");
		check(file.messages[8].kind == ed::SyxKind::Kit && file.messages[8].slot == 0, "message 8 is kit 0");

		const auto mm = ed::parseSyx(ed::writeSyx(mmBackup()));
		check(mm.model == ed::SyxModel::Mm, "the model is the Monomachine");
		check(mm.messages.size() == 288 && mm.problems.empty(), "288 MM messages, no problems");
		check(ed::parseSyx({}).model == ed::SyxModel::Unknown, "an empty file has no model");
	}

	void problems()
	{
		std::printf("problems\n");
		Bytes bytes;
		append(bytes, ed::encodeMdKit(mdKit(1)));
		auto bad = ed::encodeMdKit(mdKit(2));
		bad[bad.size() - 4] ^= 0x01;	// checksum low byte
		append(bytes, bad);
		ed::MmKit mmKit;
		append(bytes, ed::encodeMmKit(mmKit));		// another model
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x01, 0xf7});	// MD, not a dump
		auto shortKit = ed::encodeMdKit(mdKit(3));
		shortKit.erase(shortKit.begin() + 20);	// one byte less
		append(bytes, shortKit);
		append(bytes, ed::encodeMdKit(mdKit(1)));	// kit 1 again
		auto outOfRange = mdKit(0);
		outOfRange.position = 64;
		append(bytes, ed::encodeMdKit(outOfRange));
		const auto cut = ed::encodeMdKit(mdKit(4));
		append(bytes, Bytes(cut.begin(), cut.begin() + 100));	// no F7

		const auto file = ed::parseSyx(bytes);
		check(file.model == ed::SyxModel::Mixed, "an MD file with an MM message is mixed");
		check(ed::documentsModel(file) == ed::SyxModel::Md, "its documents are the MD's");
		check(file.messages.size() == 8, "8 messages");
		auto status = [&](const size_t _i) { return file.messages[_i].status; };
		check(status(0) == ed::SyxStatus::Ok, "kit 1 ok");
		check(status(1) == ed::SyxStatus::BadChecksum && file.messages[1].slot == 2, "kit 2: bad checksum");
		check(status(2) == ed::SyxStatus::OtherModel, "the MM kit: other model");
		check(status(3) == ed::SyxStatus::UnknownId, "0x72: unknown id");
		check(status(4) == ed::SyxStatus::WrongLength, "a short kit: wrong length");
		check(status(5) == ed::SyxStatus::DuplicateSlot, "kit 1 again: duplicate slot");
		check(status(6) == ed::SyxStatus::SlotOutOfRange, "kit 64: slot out of range");
		check(status(7) == ed::SyxStatus::Truncated, "a cut message: truncated");
		check(file.problems.size() == 7, "7 problems");
		check(file.md.kits.size() == 1 && file.md.kits.count(1), "only kit 1 is kept");
	}

	void summary()
	{
		std::printf("summary\n");
		const auto full = ed::summarizeSyx(ed::parseSyx(ed::writeSyx(mdBackup())));
		check(full.globals == 8 && full.kits == 64 && full.patterns == 128 && full.songs == 32, "MD counts 8/64/128/32");
		check(full.fullBackup, "a full MD backup");
		check(full.kitNames.size() == 64 && full.kitNames[5].name == "KIT5", "kit names");
		check(full.patternList[17].label == "B02" && full.patternList[17].kit == 17, "pattern B02 plays kit 17");
		check(full.patternList[127].label == "H16", "the last pattern is H16");
		check(full.songNames[3].name == "SONG3", "song names");

		auto partial = mdBackup();
		partial.songs.erase(31);
		const auto p = ed::summarizeSyx(ed::parseSyx(ed::writeSyx(partial)));
		check(!p.fullBackup && p.songs == 31, "one song missing: not a full backup");

		const auto mm = ed::summarizeSyx(ed::parseSyx(ed::writeSyx(mmBackup())));
		check(mm.globals == 8 && mm.kits == 128 && mm.patterns == 128 && mm.songs == 24, "MM counts 8/128/128/24");
		check(mm.fullBackup, "a full MM backup");
		check(mm.kitNames[9].name == "MK9" && mm.songNames[2].name == "MS2", "MM names");
		check(mm.patternList[0].kit == 127, "MM pattern A01 plays kit 127");
	}

	void plan()
	{
		std::printf("plan\n");
		ed::MdDocuments source;
		for(uint8_t i = 3; i < 6; ++i)
			source.kits[i] = mdKit(i);
		source.patterns[5] = mdPattern(5);
		source.globals[0] = mdGlobal(0);
		const auto file = ed::parseSyx(ed::writeSyx(source));

		ed::MdDocuments machine;
		machine.kits[3] = mdKit(3);					// holds data
		machine.kits[4] = ed::MdKit{};				// empty
		auto emptyPattern = mdPattern(5);
		emptyPattern.trigs.fill(0);
		machine.patterns[5] = emptyPattern;			// empty
		machine.globals[0] = mdGlobal(0);			// globals are never empty

		const auto all = ed::planSyxImport(file, ed::syxItems(file), machine);
		check(all.size() == 5, "all: 5 items");
		check(all[0].kind == ed::SyxKind::Global && all[0].overwrites, "global 0 overwrites");
		check(all[1].kind == ed::SyxKind::Kit && all[1].slot == 3 && all[1].overwrites, "kit 3 overwrites");
		check(all[2].slot == 4 && !all[2].overwrites, "kit 4 (empty on the machine) does not");
		check(all[3].slot == 5 && !all[3].overwrites, "kit 5 (not on the machine) does not");
		check(all[4].kind == ed::SyxKind::Pattern && !all[4].overwrites, "pattern 5 (empty) does not");

		const auto kits = ed::planSyxImport(file, ed::syxItems(file, {ed::SyxKind::Kit}), machine);
		check(kits.size() == 3, "kits only: 3 items");

		const auto chosen = ed::planSyxImport(file,
			{{ed::SyxKind::Kit, 5}, {ed::SyxKind::Kit, 3}, {ed::SyxKind::Song, 0}, {ed::SyxKind::Kit, 5}}, machine);
		check(chosen.size() == 2 && chosen[0].slot == 3 && chosen[1].slot == 5,
			"chosen slots: sorted, duplicates and slots the file lacks left out");

		check(ed::syxMessage(file, {ed::SyxKind::Kit, 4}) == ed::encodeMdKit(mdKit(4)), "one item as a message");
		check(ed::syxMessage(file, {ed::SyxKind::Kit, 9}).empty(), "no message for a slot the file lacks");
	}

	void roundTrip()
	{
		std::printf("round trip\n");
		const auto md = ed::writeSyx(mdBackup());
		const auto mdFile = ed::parseSyx(md);
		check(ed::writeSyx(mdFile) == md, "MD: export -> parse -> export is byte-exact");
		check(md[6] == ed::g_mdGlobalDump, "MD: globals first");

		const auto mm = ed::writeSyx(mmBackup());
		const auto mmFile = ed::parseSyx(mm);
		check(ed::writeSyx(mmFile) == mm, "MM: export -> parse -> export is byte-exact");
		check(mmFile.messages.back().kind == ed::SyxKind::Song, "MM: songs last");

		// Every message re-encodes to its own bytes.
		bool each = true;
		for(const auto& r : mdFile.messages)
		{
			const Bytes in(md.begin() + static_cast<std::ptrdiff_t>(r.offset),
				md.begin() + static_cast<std::ptrdiff_t>(r.offset + r.size));
			each = each && ed::syxMessage(mdFile, {r.kind, static_cast<uint8_t>(r.slot)}) == in;
		}
		check(each, "MD: each message re-encodes to its input");
	}
}

namespace
{
	// ---- B-019: the as-is import ----

	// What the import may send and what it leaves out, and why.
	void sendable()
	{
		std::printf("as is: what can be sent\n");
		Bytes bytes;
		auto oldKit = mdKit(2);
		oldKit.version = 3;						// an older OS's kit format
		oldKit.revision = 0;
		append(bytes, ed::encodeMdKit(mdKit(1)));							// 0
		append(bytes, ed::encodeMdKit(oldKit));								// 1
		append(bytes, ed::encodeMmKit(ed::MmKit{}));						// 2: the other model
		append(bytes, {0xf0, 0x7e, 0x00, 0x06, 0x01, 0xf7});				// 3: universal (device inquiry)
		append(bytes, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7});	// 4: another maker
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x7e, 0x01, 0x02, 0xf7});	// 5: an OS update packet
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x57, 0x05, 0xf7});	// 6: an MD command (LOAD PATTERN)
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x52, 0x04, 0x01, 0x05, 0x90, 0x01, 0xf7});	// 7: a byte above 7F
		const auto cut = ed::encodeMdKit(mdKit(4));
		append(bytes, Bytes(cut.begin(), cut.begin() + 100));				// 8: no F7

		const auto file = ed::parseSyx(bytes);
		const auto why = [&](const size_t _i) { return ed::syxUnsendable(file.messages[_i], bytes, ed::SyxModel::Md); };
		check(file.messages.size() == 9, "9 messages");
		check(why(0).empty() && why(1).empty(), "a kit of OS 1.63's format and an older one both go (the firmware decides)");
		check(file.messages[1].version == 3 && file.messages[1].revision == 0 && file.messages[1].command == 0x52, "the older kit's format bytes are read");
		check(why(2).find("Monomachine") != std::string::npos, "the Monomachine's kit: not this machine's (" + why(2) + ")");
		check(why(3).find("universal") != std::string::npos, "a universal message is left out (" + why(3) + ")");
		check(why(4).find("another maker") != std::string::npos, "another maker's SysEx is left out (" + why(4) + ")");
		check(why(5).find("OS update") != std::string::npos, "an OS update packet is never sent (" + why(5) + ")");
		check(why(6).empty() && file.messages[6].kind == ed::SyxKind::Other, "a command of this machine can go");
		check(why(7).find("above 7F") != std::string::npos, "a byte above 7F: broken (" + why(7) + ")");
		check(why(8).find("no F7") != std::string::npos, "no F7: broken (" + why(8) + ")");
		check(ed::syxBytes(bytes, file.messages[1]) == ed::encodeMdKit(oldKit), "a message's own bytes, unchanged");
		check(ed::syxRequest(ed::SyxModel::Md, ed::SyxKind::Pattern, 5) == ed::mdPatternRequest(5)
			&& ed::syxRequest(ed::SyxModel::Mm, ed::SyxKind::Kit, 9) == ed::mmKitRequest(9), "read-back requests per model");
	}

	void canonical()
	{
		std::printf("as is: the compared form, the outcome\n");
		auto older = mdKit(5);
		older.version = 3;
		const auto a = ed::syxCanonical(ed::SyxModel::Md, ed::encodeMdKit(mdKit(5)));
		const auto b = ed::syxCanonical(ed::SyxModel::Md, ed::encodeMdKit(older));
		check(a && b && a->kind == ed::SyxKind::Kit && a->slot == 5, "a dump's compared form: its kind and slot");
		check(a && b && a->bytes == b->bytes, "two dumps that differ only in their format bytes compare equal");
		auto renamed = mdKit(5);
		setName(renamed.name, "OTHER");
		check(a && a->bytes != ed::syxCanonical(ed::SyxModel::Md, ed::encodeMdKit(renamed))->bytes, "a different name compares different");
		ed::MdDocuments docs;
		docs.kits[5] = mdKit(5);
		check(a && ed::syxCanonical(docs, ed::SyxKind::Kit, 5) == a->bytes, "a document the editor holds, in the same form");
		check(ed::syxCanonical(docs, ed::SyxKind::Kit, 6).empty(), "a slot it does not hold: empty");
		check(!ed::syxCanonical(ed::SyxModel::Mm, ed::encodeMdKit(mdKit(5))), "an MD dump is not an MM document");

		const Bytes f{1}, x{2}, y{3};
		using O = ed::SyxOutcome;
		check(ed::syxOutcome(f, x, f) == O::Taken, "after = file: taken");
		check(ed::syxOutcome(f, x, x) == O::Ignored, "after = before: ignored");
		check(ed::syxOutcome(f, x, y) == O::Converted, "something new, not the file's: converted");
		check(ed::syxOutcome(f, std::nullopt, y) == O::Differs, "not the file's, before not known: differs");
		check(ed::syxOutcome({}, x, y) == O::Changed, "the file's dump not readable, the slot changed: changed");
		check(ed::syxOutcome({}, x, x) == O::Ignored, "the file's dump not readable, the slot did not change: ignored");
		check(ed::syxOutcome({}, std::nullopt, y) == O::Unknown, "neither comparable: unknown");
		check(ed::syxOutcome(f, x, std::nullopt) == O::NoReply, "no read-back: no reply");
		check(ed::syxOutcome(f, f, f) == O::Taken, "the machine already held the file's: taken");
	}

	struct MdTraits
	{
		using Docs = ed::MdDocuments;
		static constexpr ed::SyxModel model = ed::SyxModel::Md;
		static constexpr const char* name = "Machinedrum";
		static const Docs& docs(const ed::SyxFile& _f) { return _f.md; }
	};
	using Job = mdJucePlugin::SyxJob<MdTraits>;

	// A Machinedrum as the job sees it through the adapter: it stores the kit dumps of format 4 it receives and
	// ignores older ones (the test's firmware rule), answers kit requests with what it holds, never answers a
	// pattern request, and with _manual holds every dump until the person's SEND (the Monomachine over HW MIDI).
	struct FakeMachine
	{
		struct AsIs
		{
			size_t queued = 0;
			bool busy = false;
			std::string waitsFor;
		};
		std::map<uint8_t, ed::MdKit> kits;
		std::vector<Bytes> wire;		// what reached the machine, in order
		std::vector<Bytes> held;		// manual: waiting for SEND
		std::vector<Bytes> replies;		// what the machine answers, for the job's tap
		bool manual = false;
		size_t maxQueued = 0;

		std::string sendAsIs(const Bytes& _m, const bool _dump)
		{
			if(manual && _dump)
			{
				held.push_back(_m);
				maxQueued = std::max(maxQueued, held.size());
				return {};
			}
			take(_m);
			return {};
		}
		AsIs asIs() const
		{
			AsIs a;
			a.queued = held.size();
			a.busy = !held.empty();
			if(!held.empty())
				a.waitsFor = "open SYSEX RECV, then press SEND";
			return a;
		}
		void send()
		{
			for(const auto& m : held)
				take(m);
			held.clear();
		}
		void take(const Bytes& _m)
		{
			wire.push_back(_m);
			if(_m.size() > 9 && _m[6] == ed::g_mdKitDump)
			{
				if(const auto k = ed::decodeMdKit(_m); k && k->version == 4)
					kits[k->position] = *k;
			}
			else if(_m.size() == 9 && _m[6] == 0x53)	// kit request
			{
				if(const auto it = kits.find(_m[7]); it != kits.end())
					replies.push_back(ed::encodeMdKit(it->second));
			}
		}
	};

	// One session loop: a step every 10 ms, the machine's replies to the job's tap.
	template<typename Fake>
	std::vector<ed::json::Value> drive(Job& _job, Fake& _machine, double& _now, const std::function<void(const ed::json::Value&)>& _onProgress = {})
	{
		std::vector<ed::json::Value> progress;
		for(int i = 0; i < 20000 && _job.running(); ++i)
		{
			_now += 10;
			if(auto p = _job.step(_machine, _now, false))
			{
				progress.push_back(*p);
				if(_onProgress)
					_onProgress(*p);
			}
			auto r = std::move(_machine.replies);
			_machine.replies.clear();
			for(const auto& m : r)
				_job.onMachineSysex(m);
		}
		return progress;
	}

	void job()
	{
		std::printf("as is: the session's import job\n");
		// The file: kit 1 new, kit 2 in an older format, kit 3 new (the editor has not read slot 3), pattern 5 (no reply),
		// a command, the Monomachine's kit (left out), kit 9 (the person leaves it out).
		auto k1 = mdKit(1); setName(k1.name, "NEW1");
		auto k2 = mdKit(2); setName(k2.name, "OLD2"); k2.version = 3;
		auto k3 = mdKit(3); setName(k3.name, "NEW3");
		auto k9 = mdKit(9); setName(k9.name, "NEW9");
		Bytes file;
		append(file, ed::encodeMdKit(k1));
		append(file, ed::encodeMdKit(k2));
		append(file, ed::encodeMdKit(k3));
		append(file, ed::encodeMdPattern(mdPattern(5)));
		append(file, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x57, 0x05, 0xf7});
		append(file, ed::encodeMmKit(ed::MmKit{}));
		append(file, ed::encodeMdKit(k9));

		FakeMachine machine;
		for(uint8_t s : {1, 2, 3, 9})
			machine.kits[s] = mdKit(s);
		ed::MdDocuments known;				// what the editor has read: not kit 3
		known.kits[1] = mdKit(1);
		known.kits[2] = mdKit(2);
		known.kits[9] = mdKit(9);

		Job job;
		const auto preview = job.open(file, "test.syx", known, {5, 1, -1, 0});
		check(preview.find("ok")->asBool(), "the preview opens");
		const auto& items = *preview.find("items");
		check(items.find("kit")->asArray().size() == 4 && items.find("pattern")->asArray().size() == 1 && items.find("other")->asArray().size() == 1,
			"4 kits, 1 pattern, 1 other message");
		const auto& kit2 = items.find("kit")->asArray()[1];
		check(kit2.find("format")->asString() == "3.1" && kit2.find("name")->asString() == "OLD2", "the older kit: its format and name are shown, it is not refused ("
			+ ed::json::write(kit2) + ")");
		check(items.find("kit")->asArray()[0].find("plays")->asBool() && items.find("pattern")->asArray()[0].find("plays")->asBool(),
			"kit 1 and pattern 5 are marked: they play");
		check(items.find("kit")->asArray()[0].find("overwrites")->asBool(), "kit 1 overwrites a slot that holds data");
		check(preview.find("skippedCount")->asNumber() == 1 && preview.find("skipped")->asArray()[0].asString().find("Monomachine") != std::string::npos,
			"the Monomachine's kit is left out, with the reason");

		check(job.start({"kit", "pattern", "other"}, {"kit:9"}, known).empty(), "the import starts");
		check(job.phase() == Job::Phase::Before, "slot 3, not known to the editor, is read first");
		double now = 0;
		const auto progress = drive(job, machine, now);
		check(!job.running() && job.phase() == Job::Phase::Done, "the import ends");

		// the wire: kit 3 asked for before anything was sent, then the file's messages as they are, in file order
		const auto& w = machine.wire;
		const auto at = [&](const Bytes& _m) { return std::find(w.begin(), w.end(), _m) - w.begin(); };
		check(at(ed::mdKitRequest(3)) < at(ed::encodeMdKit(k1)), "kit 3 is read before the import");
		const Bytes command{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x57, 0x05, 0xf7};
		check(at(ed::encodeMdKit(k1)) < at(ed::encodeMdKit(k2)) && at(ed::encodeMdKit(k2)) < at(ed::encodeMdKit(k3))
			&& at(ed::encodeMdKit(k3)) < at(ed::encodeMdPattern(mdPattern(5))) && at(ed::encodeMdPattern(mdPattern(5))) < at(command),
			"the file's messages go unchanged, in file order");
		check(at(ed::encodeMdKit(k9)) == static_cast<std::ptrdiff_t>(w.size()), "kit 9, left out by the person, is not sent");
		check(at(ed::encodeMmKit(ed::MmKit{})) == static_cast<std::ptrdiff_t>(w.size()), "the Monomachine's kit is not sent");
		check(std::count(w.begin(), w.end(), ed::mdPatternRequest(5)) == 6, "pattern 5 (not known before) is asked for three times before and three times after, then given up ("
			+ std::to_string(std::count(w.begin(), w.end(), ed::mdPatternRequest(5))) + ")");

		std::map<std::string, ed::SyxOutcome> out;
		for(const auto& [i, o] : job.outcomes())
			out[std::string(ed::syxKindName(i.kind)) + std::to_string(i.slot)] = o;
		check(out["kit1"] == ed::SyxOutcome::Taken, "kit 1: taken");
		check(out["kit2"] == ed::SyxOutcome::Ignored, "kit 2 (an older format the machine ignores): ignored");
		check(out["kit3"] == ed::SyxOutcome::Taken, "kit 3: taken");
		check(out["pattern5"] == ed::SyxOutcome::NoReply, "pattern 5: no reply");
		check(!out.count("kit9"), "kit 9 has no outcome");

		const auto& last = progress.back();
		const auto& report = *last.find("report");
		check(last.find("phase")->asString() == "done" && report.find("taken")->asNumber() == 2 && report.find("ignored")->asNumber() == 1
			&& report.find("no reply")->asNumber() == 1 && report.find("commands")->asNumber() == 1, "the report counts");
		bool says = false;
		for(const auto& i : report.find("items")->asArray())
			says = says || (i.find("slot")->asNumber() == 2 && i.find("text")->asString().find("ignored") != std::string::npos
				&& i.find("text")->asString().find("format 3.1") != std::string::npos);
		check(says, "kit 2's line says the machine ignored it, with the dump's format");
		check(last.find("text")->asString().find("2 imported") == 0, "the summary: " + last.find("text")->asString());

		// HW MIDI on the Monomachine's way: the dumps wait for the person's SEND together
		FakeMachine manual;
		manual.manual = true;
		manual.kits = machine.kits;
		Job j2;
		j2.open(file, "test.syx", known, {});
		known.kits[3] = mdKit(3);
		j2.start({"kit"}, {}, known);
		double t = 0;
		bool waited = false;
		drive(j2, manual, t, [&](const ed::json::Value& _p)
		{
			if(_p.find("text")->asString().find("SEND") != std::string::npos && !manual.held.empty())
			{
				waited = true;
				check(manual.held.size() == 4, "every kit waits for SEND together (" + std::to_string(manual.held.size()) + ")");
				manual.send();
			}
		});
		check(waited, "the progress says what the import waits for (SYSEX RECV, SEND)");
		check(!j2.running(), "after SEND the kits are read back and the import ends");

		// stopping while sending: what went is read back
		FakeMachine m3;
		m3.manual = true;
		Job j3;
		j3.open(file, "test.syx", known, {});
		j3.start({"kit"}, {}, known);
		double t3 = 0;
		j3.step(m3, t3 += 10, false);
		j3.cancel();
		check(j3.phase() == Job::Phase::Reading, "Stop while sending: what went is read back");
	}

	void notData()
	{
		std::printf("as is: files with nothing to import\n");
		Job job;
		Bytes os;
		for(int i = 0; i < 3; ++i)
			append(os, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x7e, static_cast<uint8_t>(i), 0x01, 0xf7});
		append(os, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x7f, 0x00, 0xf7});
		const auto p = job.open(os, "os.syx", {}, {});
		check(!p.find("ok")->asBool() && p.find("text")->asString().find("OS update") != std::string::npos, "an OS update file: " + p.find("text")->asString());
		const auto mm = job.open(ed::writeSyx(mmBackup()), "mm.syx", {}, {});
		check(!mm.find("ok")->asBool() && mm.find("text")->asString().find("Monomachine file") != std::string::npos, "a Monomachine file in the Machinedrum Editor: " + mm.find("text")->asString());
		const auto none = job.open({0x01, 0x02}, "x.syx", {}, {});
		check(!none.find("ok")->asBool() && none.find("text")->asString() == "No SysEx in this file.", "no SysEx at all");
		check(!job.start({}, {}, {}).empty(), "nothing to start after a file that cannot be imported");
	}
}

int main()
{
	parseOk();
	problems();
	summary();
	plan();
	roundTrip();
	sendable();
	canonical();
	job();
	notData();
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
