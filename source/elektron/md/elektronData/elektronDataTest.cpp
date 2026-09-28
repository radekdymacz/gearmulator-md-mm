// Pure codec tests: no firmware, no device. Byte offsets asserted here were
// observed on dumps from the emulated MD OS 1.63 after panel and SysEx edits
// (doc/modern-ux/P0-RESULT.md, P1-RESULT.md); firmware round trips live in
// mdLibTest/mdDataLayerFirmwareTest.cpp and the corpus in elektronDataCorpusTest.

#include "jsonFirmware.h"
#include "jsonSchema.h"
#include "mdGlobal.h"
#include "mdJson.h"
#include "mdKit.h"
#include "mdMachines.h"
#include "mdPattern.h"
#include "mdSong.h"
#include "mdValidate.h"
#include "sysex7bit.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace
{
	int g_failures = 0;

	void check(const bool _condition, const char* _what)
	{
		if(_condition)
			return;
		std::fprintf(stderr, "FAIL: %s\n", _what);
		++g_failures;
	}

	uint32_t nextRandom(uint32_t& _state)
	{
		_state ^= _state << 13;
		_state ^= _state >> 17;
		_state ^= _state << 5;
		return _state;
	}

	void test7Bit()
	{
		uint32_t seed = 0x1234567u;
		for(size_t size = 0; size < 64; ++size)
		{
			std::vector<uint8_t> raw(size);
			for(auto& b : raw)
				b = static_cast<uint8_t>(nextRandom(seed));
			const auto encoded = elektronData::encode7Bit(raw.data(), raw.size());
			check(encoded.size() == elektronData::encoded7BitSize(size), "7-bit encoded size");
			bool clean = true;
			for(const auto b : encoded)
				clean &= b < 0x80;
			check(clean, "7-bit encoding produces data bytes only");
			check(elektronData::decode7Bit(encoded.data(), encoded.size()) == raw, "7-bit round trip");
		}
	}

	elektronData::MdPattern randomPattern(uint32_t& _seed, const bool _extended)
	{
		elektronData::MdPattern p;
		p.extended = _extended;
		p.position = static_cast<uint8_t>(nextRandom(_seed) & 0x7f);
		const auto word64 = [&] { return (uint64_t(nextRandom(_seed)) << 32) | nextRandom(_seed); };
		const auto limit = [&](const uint64_t _v) { return _extended ? _v : (_v & 0xffffffffull); };
		for(auto& t : p.trigs)
			t = limit(word64());
		for(auto& m : p.lockMasks)
			m = nextRandom(_seed) & 0x00ffffffu;
		p.accentPattern = limit(word64());
		p.slidePattern = limit(word64());
		p.swingPattern = limit(word64());
		p.swingAmount = nextRandom(_seed);
		p.accentAmount = static_cast<uint8_t>(nextRandom(_seed) & 0x0f);
		p.length = 64;
		p.scale = 3;
		p.kit = static_cast<uint8_t>(nextRandom(_seed) & 0x3f);
		for(auto& row : p.lockRows)
			for(size_t s = 0; s < (_extended ? 64u : 32u); ++s)
				row[s] = static_cast<uint8_t>(nextRandom(_seed));
		for(auto& v : p.trackAccent)
			v = limit(word64());
		for(auto& v : p.trackSlide)
			v = limit(word64());
		for(auto& v : p.trackSwing)
			v = limit(word64());
		return p;
	}

	void testRoundTrip()
	{
		uint32_t seed = 0xfeedbeefu;
		for(int i = 0; i < 50; ++i)
		{
			const bool extended = (i & 1) != 0;
			const auto p = randomPattern(seed, extended);
			const auto bytes = elektronData::encodeMdPattern(p);
			check(bytes.size() == (extended ? elektronData::MdPattern::g_extendedDumpSize
				: elektronData::MdPattern::g_shortDumpSize), "encoded dump size");
			check(elektronData::isDumpTrailerValid(bytes), "encoded dump checksum");
			const auto decoded = elektronData::decodeMdPattern(bytes);
			check(decoded.has_value() && *decoded == p, "decode(encode(p)) == p");
			check(decoded && elektronData::encodeMdPattern(*decoded) == bytes, "encode(decode(x)) == x");

			auto corrupt = bytes;
			corrupt[0x40] ^= 0x01;
			check(!elektronData::decodeMdPattern(corrupt), "checksum mismatch is rejected");
		}
	}

	// Offsets and values observed on the emulated firmware (MD OS 1.63, pattern A01).
	void testObservedLayout()
	{
		elektronData::MdPattern p;
		p.length = 32;
		p.scale = 1;
		p = elektronData::withTrig(p, 0, 4, true);
		auto bytes = elektronData::encodeMdPattern(p);
		check(bytes[0x0e] == 0x10, "track 1 step 5 trig is bit 4 of byte 0x0e");
		check(bytes[0xb2] == 32, "length byte at 0xb2");

		auto locked = elektronData::withLock(p, 0, 0, 4, 0x47);
		check(locked.has_value(), "first lock fits");
		bytes = elektronData::encodeMdPattern(*locked);
		check(bytes[0x58] == 0x01, "lock mask bit for track 1 parameter 1 at 0x58");
		check(bytes[0xb7] == 0x7b && bytes[0xbc] == 0x47, "lock row 0 step 5 value encoding");
		check(elektronData::lockValue(*locked, 0, 0, 4) == uint8_t{0x47}, "lock value read back");
		check(!elektronData::lockValue(*locked, 0, 0, 5), "other steps are unlocked");
	}

	void testLockRowOrder()
	{
		elektronData::MdPattern p;
		// Panel order on the firmware was parameter 1, 4, then 2; dump rows came
		// back sorted by parameter. The codec must produce the same order.
		auto q = *elektronData::withLock(p, 0, 0, 4, 0x47);
		q = *elektronData::withLock(q, 0, 3, 4, 0x19);
		q = *elektronData::withLock(q, 0, 1, 4, 0x4f);
		check(q.lockMasks[0] == 0x0b, "lock mask after three locks");
		check(q.lockRows[0][4] == 0x47 && q.lockRows[1][4] == 0x4f && q.lockRows[2][4] == 0x19,
			"rows sorted by parameter");
		check(q.lockRows[3][4] == 0x00, "unused rows stay zero");
		q = *elektronData::withLock(q, 2, 5, 0, 0x10);
		q = *elektronData::withLock(q, 1, 7, 9, 0x20);
		check(elektronData::lockRowIndex(q, 1, 7) == size_t{3} && elektronData::lockRowIndex(q, 2, 5) == size_t{4},
			"rows sorted by track");
		check(elektronData::lockValue(q, 2, 5, 0) == uint8_t{0x10}, "moved row keeps its value");
	}

	void testLockBudget()
	{
		elektronData::MdPattern p;
		for(size_t i = 0; i < elektronData::MdPattern::g_lockRows; ++i)
		{
			const auto next = elektronData::withLock(p, i / 24, i % 24, 0, 1);
			check(next.has_value(), "lock within budget");
			p = *next;
		}
		check(elektronData::usedLockRows(p) == 64, "64 rows used");
		check(!elektronData::withLock(p, 15, 23, 0, 1), "65th locked parameter is refused");
		check(elektronData::withLock(p, 0, 0, 7, 9).has_value(), "existing row accepts more steps");
	}

	// Kit offsets observed by diffing firmware dumps around single SysEx edits.
	void testKitLayout()
	{
		elektronData::MdKit k;
		k.trigGroups.fill(elektronData::MdKit::g_noGroup);
		k.muteGroups.fill(elektronData::MdKit::g_noGroup);
		k.name = {'A', 'B', 'C'};
		k.trigGroups[0] = 5;	// SysEx 0x65 00 05
		k.muteGroups[1] = 6;	// SysEx 0x66 01 06
		k.models[2] = 0x84;		// SysEx 0x5b 02 04 01: ROM-05
		k.masterFx[elektronData::MdKit::GateBox][0] = 0x21;		// SysEx 0x5e 00 21
		k.masterFx[elektronData::MdKit::RhythmEcho][0] = 0x11;	// SysEx 0x5d 00 11
		k.params[0][0] = 5;		// CC 16 on channel 1
		k.params[1][21] = 0x55;	// LFO 1 speed via SysEx 0x62 0d 55
		const auto b = elektronData::encodeMdKit(k);
		check(b.size() == 1233, "kit dump size");
		check(b[0x0a] == 'A' && b[0x0c] == 'C', "kit name at 0x0a");
		check(b[0x4a7] == 0x3f && b[0x4a8] == 0x05, "trig group of track 1 at 0x4a8");
		check(b[0x4b7] == 0x77 && b[0x4bb] == 0x06, "mute group of track 2 at 0x4bb");
		check(b[0x1b2] == 0x04 && b[0x1b7] == 0x04, "model of track 3 with the UW bit");
		check(b[0x487] == 0x21 && b[0x48f] == 0x11, "gate box then rhythm echo");
		check(b[0x1a] == 5 && b[0x1a + 24 + 21] == 0x55, "track parameters from 0x1a");
		const auto d = elektronData::decodeMdKit(b);
		check(d && *d == k && elektronData::encodeMdKit(*d) == b, "kit round trip");
		check(elektronData::mdMachineName(0x84) == "ROM-05" && elektronData::mdMachineName(17) == "TRX-SD"
			&& elektronData::mdMachineName(0xa2) == "RAM-P1" && elektronData::mdMachineName(99) == "MID-04"
			&& elektronData::mdMachineName(90).empty(), "machine names");
		check(elektronData::mdMachineModel("P-I-SD") == uint32_t{65}, "machine by name");
		auto bad = k;
		bad.lfos[4].shape1 = 7;
		check(!elektronData::validate(bad).empty(), "LFO shape beyond 5 is refused");
	}

	void testSong()
	{
		using elektronData::MdSongRow;
		elektronData::MdSong s;
		s.rows = {MdSongRow{10, 0, 2, 0, 0x0001, 125 * 24, 4, 12}, MdSongRow{0xfe, 0, 1, 0, 0, 0xffff, 0, 0},
			MdSongRow{0xfe, 0, 0, 4, 0, 0xffff, 0, 0}, MdSongRow{0xfe, 0, 0, 3, 0, 0xffff, 0, 0}, MdSongRow{}};
		check(elektronData::songRowKind(s.rows[1], 1) == elektronData::MdSongRowKind::Loop, "target before = LOOP");
		check(elektronData::songRowKind(s.rows[2], 2) == elektronData::MdSongRowKind::Jump, "target after = JUMP");
		check(elektronData::songRowKind(s.rows[3], 3) == elektronData::MdSongRowKind::Halt, "target self = HALT");
		const auto b = elektronData::encodeMdSong(s);
		check(b.size() == 26 + 12 * 5 + 5, "song dump size");
		const auto d = elektronData::decodeMdSong(b);
		check(d && *d == s && elektronData::encodeMdSong(*d) == b, "song round trip");
		check(elektronData::validate(s).empty(), "valid song");
		auto noEnd = s;
		noEnd.rows.pop_back();
		check(!elektronData::validate(noEnd).empty(), "a song must end with END");
		auto tooLong = s;
		tooLong.rows.insert(tooLong.rows.begin(), 252, s.rows[0]);
		check(!elektronData::validate(tooLong).empty(), "257 rows are refused (firmware keeps 256)");

		std::vector<std::string> errors;
		const auto j = elektronData::songToJson(s);
		const auto back = elektronData::songFromJson(*elektronData::json::parse(elektronData::json::write(j)), errors);
		check(back && *back == s, "song JSON round trip");
		const auto text = elektronData::json::write(j);
		check(text.find("\"kind\":\"halt\"") != std::string::npos && text.find("\"mutes\":[0]") != std::string::npos
			&& text.find("\"tempo\":125") != std::string::npos, "song JSON uses kinds, track lists and BPM");
	}

	void testGlobal()
	{
		elektronData::MdGlobal g;
		g.routing.fill(elektronData::MdGlobal::g_mainOutput);
		g.keymap.fill(elektronData::MdGlobal::g_unmapped);
		g.routing[3] = 2;		// SysEx 0x5c 03 02
		g.tempo = 0x17 << 7 | 0x70;	// SysEx 0x61 17 70
		const auto b = elektronData::encodeMdGlobal(g);
		check(b.size() == 197 && b[0x0d] == 2, "routing at 0x0a + track");
		check(b[0xaf] == 0x17 && b[0xb0] == 0x70 && b[0xb1] == 1, "tempo and extended mode");
		const auto text = elektronData::json::write(elektronData::globalToJson(g));
		check(text.find("\"routing\":[\"MAIN\",\"MAIN\",\"MAIN\",\"C\"") != std::string::npos, "routing names");
	}

	void testPatternContract()
	{
		elektronData::MdPattern p;
		p.length = 32;
		p.scale = 1;
		p.tempoMultiplier = 2;
		p.accentAmount = elektronData::accentAmountFromDisplay(4);
		p.swingAmount = elektronData::swingAmountFromPercent(65);
		p = elektronData::withTrig(p, 0, 4, true);
		p = *elektronData::withLock(p, 0, 16, 4, 20);
		check(p.accentAmount == 34 && p.swingAmount == 4915, "display units match the factory encoding");
		check(elektronData::swingPercent(8192) == 75 && elektronData::accentDisplay(68) == 8, "and back");
		const auto j = elektronData::patternToJson(p);
		const auto text = elektronData::json::write(j);
		check(text.find("\"tempoMultiplier\":\"3/4X\"") != std::string::npos, "multiplier name");
		check(text.find("\"locks\":[{\"track\":0,\"param\":16,\"steps\":[[4,20]]}]") != std::string::npos,
			"locks as [step, value]");
		std::vector<std::string> errors;
		const auto back = elektronData::patternFromJson(*elektronData::json::parse(text), errors);
		check(back && *back == p, "pattern JSON round trip");
		// P6, contract version 2: the firmware's pass-through fields sit under "firmware"; a version
		// 1 document (the old layout, fields at the top) still reads.
		check(j.find("version")->asNumber() == 2 && j.find("firmware") && j.find("firmware")->find("format") && !j.find("format"),
			"version 2 groups the firmware fields");
		elektronData::MdKit kit;
		kit.lfos[3].state[5] = 0x42;
		const auto kj = elektronData::kitToJson(kit);
		check(kj.find("firmware")->find("lfoState") && !kj.find("tracks")->asArray()[3].find("lfo")->find("state"), "the LFO state is firmware");
		const elektronData::json::FirmwareLayout kitLayout{{"format", "nameTail"}, {{"lfo.state", "lfoState"}}, {}};
		const auto v1 = elektronData::json::ungroupFirmware(kj, kitLayout, 2, 1);
		check(v1.find("version")->asNumber() == 1 && v1.find("format") && v1.find("tracks")->asArray()[3].find("lfo")->find("state"), "as version 1");
		const auto kback = elektronData::kitFromJson(v1, errors);
		check(kback && *kback == kit, "a version 1 kit document still reads");
		check(elektronData::kitFromJson(kj, errors) == std::optional<elektronData::MdKit>(kit), "and version 2");

		// Hardware limits reported with a path.
		auto doc = text;
		doc.replace(doc.find("\"length\":32"), 11, "\"length\":40");
		errors.clear();
		check(!elektronData::patternFromJson(*elektronData::json::parse(doc), errors) && !errors.empty()
			&& errors[0].find("total length") != std::string::npos, "length beyond the total length");
		auto big = p;
		for(size_t i = 0; big.lockMasks[15] != 0xffffff || elektronData::usedLockRows(big) < 64; ++i)
		{
			const auto next = elektronData::withLock(big, i / 24, i % 24, 0, 1);
			if(!next)
				break;
			big = *next;
		}
		auto over = elektronData::patternToJson(big);
		auto lockText = elektronData::json::write(over);
		lockText.replace(lockText.find("\"locks\":["), 9, "\"locks\":[{\"track\":15,\"param\":23,\"steps\":[]},");
		errors.clear();
		check(!elektronData::patternFromJson(*elektronData::json::parse(lockText), errors), "65 locks are refused");
		errors.clear();
		check(!elektronData::patternFromJson(*elektronData::json::parse("{\"schema\":\"md-desk/kit\"}"), errors)
			&& errors.size() > 3, "wrong document: every problem is listed");
	}

	void testJson()
	{
		const auto v = elektronData::json::parse(" {\"a\": [1, 2.5, -3e2, true, null, \"x\\u0041\\n\"], \"b\": {}} ");
		check(v && v->find("a") && v->find("a")->asArray().size() == 6, "JSON parse");
		check(v && v->find("a")->asArray()[5].asString() == "xA\n", "JSON escapes");
		check(elektronData::json::write(*v) == "{\"a\":[1,2.5,-300,true,null,\"xA\\n\"],\"b\":{}}", "JSON write");
		check(!elektronData::json::parse("[1,]") && !elektronData::json::parse("{\"a\":1} x"), "JSON errors");
	}

	// Optional: byte-exact round trip of dumps captured from firmware.

	// The schema validator refuses what the contract refuses (the executable spec, P6).
	void testSchema()
	{
		namespace j = elektronData::json;
		const auto schema = j::parse(R"({"$defs":{"u7":{"type":"integer","minimum":0,"maximum":127}},
			"type":"object","required":["schema","v"],"properties":{"schema":{"const":"x"},
			"v":{"$ref":"#/$defs/u7"},"list":{"type":"array","items":{"enum":["a","b"]},"maxItems":2,"uniqueItems":true},
			"name":{"type":"string","pattern":"^[A-H][0-9]{2}$"},"n":{"oneOf":[{"type":"null"},{"type":"integer"}]}}})");
		check(schema.has_value(), "schema parses");
		const j::Schema s(*schema);
		const auto errors = [&](const char* _json) { return s.validate(*j::parse(_json)); };
		check(errors(R"({"schema":"x","v":5,"list":["a"],"name":"A01","n":null})").empty(), "a valid instance passes");
		check(errors(R"({"schema":"x","v":128})").size() == 1, "maximum");
		check(errors(R"({"schema":"x","v":1.5})").size() == 1, "integer");
		check(errors(R"({"schema":"y","v":1})").size() == 1, "const");
		check(errors(R"({"schema":"x"})").size() == 1, "required");
		check(errors(R"({"schema":"x","v":1,"list":["a","a","c"]})").size() == 3, "items, maxItems, uniqueItems");
		check(errors(R"({"schema":"x","v":1,"name":"Z99"})").size() == 1, "pattern");
		check(!errors(R"({"schema":"x","v":1,"n":"no"})").empty(), "oneOf");
		const auto e = errors(R"({"schema":"x","v":200})");
		check(!e.empty() && e[0].find("$.v") == 0, "errors carry the JSON path");
	}

	void testCapturedDumps(const int _argc, char** _argv)
	{
		for(int i = 1; i < _argc; ++i)
		{
			std::ifstream in(_argv[i], std::ios::binary);
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			const auto decoded = elektronData::decodeMdPattern(bytes);
			check(decoded.has_value(), "captured dump decodes");
			check(decoded && elektronData::encodeMdPattern(*decoded) == bytes, "captured dump round trips");
			std::printf("captured %s: %s\n", _argv[i], decoded ? "round trip ok" : "not a pattern");
		}
	}
}

int main(const int _argc, char** _argv)
{
	test7Bit();
	testRoundTrip();
	testObservedLayout();
	testLockRowOrder();
	testLockBudget();
	testKitLayout();
	testSong();
	testGlobal();
	testPatternContract();
	testJson();
	testSchema();
	testCapturedDumps(_argc, _argv);
	std::printf("elektronDataTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
