// Pure unit tests for .syx import and export (P7, elektronData/syxImport.h) on synthetic dumps
// built with the codecs' own encoders: parse, problems, summary, import plan, and a byte-exact
// export round trip. No firmware, no files.

#include "elektronData/syxImport.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mmDump.h"

#include <cstdio>
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

int main()
{
	parseOk();
	problems();
	summary();
	plan();
	roundTrip();
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
