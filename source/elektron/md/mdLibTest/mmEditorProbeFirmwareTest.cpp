// MM-P0: Monomachine Editor probes against MM OS 1.32B firmware (manual: needs a
// user-supplied ROM). Discovery harness: it finds and measures. The smoke test
// for the editor is mmDeskFirmwareTest.
//
//   mmEditorProbeFirmwareTest <ROM> <mode> [outdir]
//
// Modes (MM-P0-RESULT.md says what each found):
//   all        gate, recv, commands, kitsong, burst, queue, screens in turn
//   lcd        boot and print the LCD and the status values
//   dumps <d>  a few dumps of each kind into <d>
//   corpus <d> [patch-ram]   every user-data slot into <d>
//   gate       dumps on a normal screen vs on SYSEX RECV (paced, unpaced, chunked)
//   recv       playback, playhead, CCs and dumps while parked on SYSEX RECV
//   commands   which live SysEx commands act outside (and inside) SYSEX RECV
//   kitsong    kit, song and global dumps on SYSEX RECV; kit slot vs working kit
//   burst      back-to-back dumps; the RECV message and error counters
//   macro      how fast the SYSEX RECV panel macro can run
//   recvflag, recvcount, workkit, boot   RAM searches
//   chain [hex] MM-P8: BANK + TRIG, BANK GROUP and pattern chaining in RAM (with an address: its life)
//   queue      a queued pattern switch against status and the playhead
//   screens    the firmware's screen word across screens
//   lab <script>      MM-P1 layout lab: panel/MIDI actions with dump diffs (see labMode)
//   program <dir>     MM-P1 programmed corpus: random valid dumps through SYSEX RECV, read back
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"
#include "sysexPanelDriver.h"

#include "elektronData/mmGlobal.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmMachines.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmSong.h"
#include "elektronData/mmValidate.h"
#include "elektronData/sysex7bit.h"

#include "mdLib/mdautomation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <sstream>

using namespace mdFirmwareSession;

namespace
{
	std::string g_romName;
	int g_failures = 0;
	constexpr auto g_mm = md::MachineModel::Monomachine;

	void check(const bool _condition, const std::string& _what)
	{
		std::printf("  %s %s\n", _condition ? "ok  " : "FAIL", _what.c_str());
		if(!_condition)
			++g_failures;
	}

	double ms(const uint64_t _frames) { return _frames * 1000.0 / g_rate; }

	Bytes mmRequest(const uint8_t _command, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, _command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}

	Bytes mmCommand(const uint8_t _command, const Bytes& _args)
	{
		Bytes m{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, _command};
		m.insert(m.end(), _args.begin(), _args.end());
		m.push_back(0xf7);
		return m;
	}

	// 7-bit unpack of [10, size-5), then the MM run-length decode (0x80|n, v = n x v).
	Bytes rawPayload(const Bytes& _sysex)
	{
		if(_sysex.size() < 15)
			return {};
		const auto packed = elektronData::decode7Bit(_sysex.data() + 10, _sysex.size() - 15);
		Bytes out;
		for(size_t i = 0; i < packed.size(); ++i)
		{
			if(!(packed[i] & 0x80))
				out.push_back(packed[i]);
			else if(i + 1 < packed.size())
			{
				out.insert(out.end(), packed[i] & 0x7f, packed[i + 1]);
				++i;
			}
		}
		return out;
	}

	Bytes packedPayload(const Bytes& _sysex)
	{
		return elektronData::decode7Bit(_sysex.data() + 10, _sysex.size() - 15);
	}

	// Literal (uncompressed, escaped) re-encode of a raw payload into a dump.
	Bytes encodeLiteral(const Bytes& _header10, const Bytes& _raw)
	{
		Bytes packed;
		for(const auto b : _raw)
		{
			if(b & 0x80)
				packed.push_back(0x81);
			packed.push_back(b);
		}
		Bytes m(_header10.begin(), _header10.begin() + 10);
		const auto enc = elektronData::encode7Bit(packed.data(), packed.size());
		m.insert(m.end(), enc.begin(), enc.end());
		m.resize(m.size() + 5);
		elektronData::writeDumpTrailer(m);
		return m;
	}

	// The firmware's own run-length choice (matches every dump it sends): runs of
	// 2-127 equal bytes, and single bytes with bit 7 set, become (0x80|n, v).
	Bytes encodeRle(const Bytes& _header10, const Bytes& _raw)
	{
		Bytes packed;
		for(size_t i = 0; i < _raw.size();)
		{
			size_t j = i;
			while(j < _raw.size() && _raw[j] == _raw[i] && j - i < 127)
				++j;
			const auto n = j - i;
			if(n >= 2 || (_raw[i] & 0x80))
			{
				packed.push_back(static_cast<uint8_t>(0x80 | n));
				packed.push_back(_raw[i]);
			}
			else
				packed.push_back(_raw[i]);
			i = j;
		}
		Bytes m(_header10.begin(), _header10.begin() + 10);
		const auto enc = elektronData::encode7Bit(packed.data(), packed.size());
		m.insert(m.end(), enc.begin(), enc.end());
		// The firmware writes each group's high-bit byte before the group, so a
		// stream that ends on a full group still carries one empty group byte.
		if(packed.size() % 7 == 0)
			m.push_back(0);
		m.resize(m.size() + 5);
		elektronData::writeDumpTrailer(m);
		return m;
	}

	Bytes retarget(Bytes _dump, const uint8_t _slot)
	{
		_dump[9] = _slot;
		elektronData::writeDumpTrailer(_dump);
		return _dump;
	}

	void printLcd(Machine& _m, const char* _title)
	{
		const auto p = _m.hardware().getFrontPanelSnapshot();
		std::printf("---- LCD %s\n", _title);
		for(uint32_t y = 0; y < 64; y += 2)
		{
			std::string line;
			for(uint32_t x = 0; x < 128; ++x)
			{
				const bool a = p.getLcdPixel(x, y), b = p.getLcdPixel(x, y + 1);
				line += a && b ? "\xe2\x96\x88" : a ? "\xe2\x96\x80" : b ? "\xe2\x96\x84" : " ";
			}
			// trim right
			while(!line.empty() && line.back() == ' ')
				line.pop_back();
			std::printf("|%s\n", line.c_str());
		}
	}

	int status(Machine& _m, const uint8_t _param)
	{
		const auto r = _m.request(mmRequest(0x70, _param), 0x72);
		return r.size() == 10 && r[7] == _param ? r[8] : -1;
	}

	Bytes dump(Machine& _m, const uint8_t _command, const uint8_t _slot, uint64_t* _frames = nullptr)
	{
		return _m.request(mmRequest(static_cast<uint8_t>(_command + 1), _slot), _command, _frames);
	}

	Bytes patternDump(Machine& _m, const uint8_t _slot) { return dump(_m, 0x67, _slot); }

	std::unique_ptr<Machine> boot(const Bytes& _rom, const Bytes& _patchRam = {})
	{
		auto m = std::make_unique<Machine>(_rom, g_romName, _patchRam, true, g_mm);
		return m;
	}

	void tap(Machine& _m, const md::PanelControl _c)
	{
		md::test::panelTap(_m.hardware(), _c);
	}

	// The plug-in's own file-transfer path: paced at the DIN rate, gated to one
	// receive kind. Returns the final state; frames = start -> complete.
	md::MidiSysexTransferState sendPaced(Machine& _m, const Bytes& _bytes, uint64_t* _frames = nullptr)
	{
		auto prepared = md::prepareMidiSysexTransfer(_bytes, g_mm);
		require(prepared.has_value(), "transfer did not prepare");
		require(_m.hardware().startMidiSysexTransfer(*prepared), "transfer did not start");
		const auto start = _m.now();
		for(;;)
		{
			_m.step();
			const auto p = _m.hardware().getMidiSysexTransferProgress();
			if(p.state == md::MidiSysexTransferState::Complete || p.state == md::MidiSysexTransferState::Failed
				|| p.state == md::MidiSysexTransferState::WaitingForReceiveMode || _m.now() - start > g_rate * 20)
			{
				if(_frames)
					*_frames = _m.now() - start;
				return p.state;
			}
		}
	}

	// Paced by hand: fragments of _chunk bytes, _gapMs apart, through the plain MIDI input.
	uint64_t sendChunked(Machine& _m, const Bytes& _bytes, const size_t _chunk, const double _gapMs)
	{
		const auto start = _m.now();
		for(size_t i = 0; i < _bytes.size(); i += _chunk)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex.assign(_bytes.begin() + i, _bytes.begin() + std::min(_bytes.size(), i + _chunk));
			_m.hardware().sendMidi(e);
			_m.run(_gapMs);
		}
		while(!_m.hardware().isMidiIngressIdle())
			_m.step();
		return _m.now() - start;
	}

	// ---- modes ---------------------------------------------------------------

	void lcdMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		printLcd(*m, "after boot");
		for(const uint8_t p : {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x21, 0x22, 0x23})
			std::printf("status 0x%02x = %d\n", p, status(*m, p));
	}

	void dumpsMode(const Bytes& _rom, const std::string& _dir)
	{
		auto m = boot(_rom);
		const auto one = [&](const uint8_t _cmd, const uint8_t _slot, const char* _name)
		{
			uint64_t f = 0;
			const auto d = dump(*m, _cmd, _slot, &f);
			const auto raw = rawPayload(d);
			std::printf("%s %u: %zu bytes, packed %zu, raw %zu, reply %.1f ms, header %02x %02x %02x %02x\n", _name, _slot,
				d.size(), d.size() > 15 ? packedPayload(d).size() : 0, raw.size(), ms(f), d.size() > 9 ? d[6] : 0,
				d.size() > 9 ? d[7] : 0, d.size() > 9 ? d[8] : 0, d.size() > 9 ? d[9] : 0);
			if(!_dir.empty() && !d.empty())
				save(_dir + "/" + _name + "-" + std::to_string(_slot) + ".syx", d);
			return d;
		};
		for(uint8_t s = 0; s < 4; ++s) one(0x67, s, "pattern");
		for(uint8_t s = 0; s < 4; ++s) one(0x52, s, "kit");
		for(uint8_t s = 0; s < 2; ++s) one(0x69, s, "song");
		for(uint8_t s = 0; s < 2; ++s) one(0x50, s, "global");
	}

	// Every user-data slot, as the firmware sends it (the factory set without a
	// patch-RAM image, else the image's content).
	void corpusMode(const Bytes& _rom, const std::string& _dir, const Bytes& _patchRam)
	{
		require(!_dir.empty(), "corpus needs an output directory");
		auto m = boot(_rom, _patchRam);
		const auto all = [&](const uint8_t _cmd, const int _count, const char* _name)
		{
			size_t total = 0;
			for(int s = 0; s < _count; ++s)
			{
				const auto d = dump(*m, _cmd, static_cast<uint8_t>(s));
				require(!d.empty(), std::string("no reply for ") + _name + " " + std::to_string(s));
				char n[64];
				std::snprintf(n, sizeof(n), "/%s-%03d.syx", _name, s);
				save(_dir + n, d);
				total += d.size();
			}
			std::printf("%s: %d dumps, %zu bytes\n", _name, _count, total);
		};
		all(0x67, 128, "pattern");
		all(0x52, 128, "kit");
		all(0x69, 24, "song");
		all(0x50, 8, "global");
	}

	Bytes savedKit(Machine& _m, const uint8_t _slot)
	{
		_m.send(mmCommand(0x59, {_slot}));
		_m.run(150);
		return rawPayload(dump(_m, 0x52, _slot));
	}

	std::string diffText(const Bytes& _a, const Bytes& _b, const size_t _max = 24)
	{
		std::string t;
		size_t n = 0;
		for(size_t i = 0; i < std::min(_a.size(), _b.size()); ++i)
		{
			if(_a[i] == _b[i])
				continue;
			if(n++ < _max)
			{
				char b[32];
				std::snprintf(b, sizeof(b), " %03zx:%02x>%02x", i, _a[i], _b[i]);
				t += b;
			}
		}
		return std::to_string(n) + " bytes" + t;
	}

	// Which SysEx commands act outside SYSEX RECV (mm-manual-mapping §13.7)?
	void commandsMode(const Bytes& _rom, const bool _inRecv)
	{
		auto m = boot(_rom);
		if(_inRecv)
			md::test::enterMmReceive(m->hardware(), false);
		std::printf("screen: %s\n", _inRecv ? "SYSEX RECV" : "normal");
		auto kit = savedKit(*m, 0);
		const auto kitStep = [&](const char* _what, const Bytes& _cmd)
		{
			m->send(_cmd);
			m->run(150);
			const auto now = savedKit(*m, 0);
			std::printf("%-28s kit diff: %s\n", _what, diffText(kit, now).c_str());
			const bool changed = now != kit;
			kit = now;
			return changed;
		};
		check(kitStep("0x5b machine T1 GND-SIN", mmCommand(0x5b, {0, 1, 1})), "0x5B assign machine acts");
		check(kitStep("0x5b machine T1 SID", mmCommand(0x5b, {0, 3, 0})), "0x5B assign machine (no init) acts");
		check(kitStep("0x5c route T1 bus CD in B", mmCommand(0x5c, {0, 2, 2})), "0x5C routing acts");
		check(kitStep("0x55 kit name PROBE", mmCommand(0x55, {'P', 'R', 'O', 'B', 'E', 0, 0, 0, 0, 0, 0})), "0x55 kit name acts");
		check(kitStep("CC48 T1 = 99", {0xb0, 48, 99}), "CC 48 (SYN 1) acts");
		check(kitStep("CC56 T2 = 11", {0xb1, 56, 11}), "CC 56 (AMP 1) T2 acts");
		check(kitStep("CC7 T3 = 33", {0xb2, 7, 33}), "CC 7 (level) T3 acts");
		const auto g0 = rawPayload(dump(*m, 0x50, 0));
		m->send(mmCommand(0x61, {static_cast<uint8_t>((100 * 24) >> 7), static_cast<uint8_t>((100 * 24) & 0x7f)}));
		m->run(150);
		const auto g1 = rawPayload(dump(*m, 0x50, 0));
		std::printf("0x61 tempo 100: global diff %s\n", diffText(g0, g1).c_str());
		m->send(mmCommand(0x71, {0x22, 3}));
		m->run(100);
		{
			const int t = status(*m, 0x22);
			std::printf("0x71 0x22 (audio track) = 3 -> status 0x22 reports %d\n", t);
			check(t == 0, "0x71 cannot set the focus track (the manual lists 0x22 for requests only)");
		}
		m->send(mmCommand(0x6c, {2}));
		m->run(100);
		check(status(*m, 0x08) == 2, "0x6C LOAD SONG 2");
		m->send(mmCommand(0x56, {1}));
		m->run(100);
		check(status(*m, 0x01) == 1, "0x56 set active global 1");
		m->send(mmCommand(0x58, {3}));
		m->run(100);
		check(status(*m, 0x02) == 3, "0x58 LOAD KIT 3");
		m->send(mmCommand(0x57, {4}));
		m->run(100);
		check(status(*m, 0x04) == 4, "0x57 LOAD PATTERN 4");
		printLcd(*m, "after commands");
	}

	using Lcd = std::array<uint8_t, 128 * 64 / 8>;
	Lcd lcdBits(Machine& _m)
	{
		const auto p = _m.hardware().getFrontPanelSnapshot();
		Lcd b{};
		for(uint32_t y = 0; y < 64; ++y)
			for(uint32_t x = 0; x < 128; ++x)
				if(p.getLcdPixel(x, y))
					b[(y * 128 + x) >> 3] |= static_cast<uint8_t>(1u << (x & 7));
		return b;
	}

	int lcdDiff(const Lcd& _a, const Lcd& _b)
	{
		int n = 0;
		for(size_t i = 0; i < _a.size(); ++i)
			n += __builtin_popcount(_a[i] ^ _b[i]);
		return n;
	}

	// A panel key through the session (audio rendered), hold and gap in ms.
	void key(Machine& _m, const md::PanelControl _c, const double _holdMs, const double _gapMs)
	{
		const auto p = md::panelPacket(g_mm, _c);
		require(p.has_value(), "no MM panel packet");
		_m.hardware().trySendPanelEvent(p->row, p->mask);
		_m.run(_holdMs);
		_m.hardware().trySendPanelEvent(p->row, 0);
		_m.run(_gapMs);
	}

	void chord(Machine& _m, const md::PanelControl _c, const double _holdMs, const double _gapMs)
	{
		const auto f = *md::panelPacket(g_mm, md::PanelControl::Function);
		const auto t = *md::panelPacket(g_mm, _c);
		md::PanelRowState rows;
		for(const auto pk : {rows.press(f), rows.press(t), rows.release(t), rows.release(f)})
		{
			_m.hardware().trySendPanelEvent(pk.row, pk.mask);
			_m.run(_holdMs);
		}
		_m.run(_gapMs);
	}

	// GLOBAL > FILE > SYSEX RECV (the same path as sysexPanelDriver's enterMmReceive).
	void recvMacro(Machine& _m, const double _hold, const double _gap)
	{
		using C = md::PanelControl;
		chord(_m, C::Kit, _hold, _gap);
		key(_m, C::Enter, _hold, _gap);
		for(int i = 0; i < 4; ++i) key(_m, C::Left, _hold, _gap);
		for(int i = 0; i < 8; ++i) key(_m, C::Up, _hold, _gap);
		key(_m, C::Down, _hold, _gap);
		key(_m, C::Down, _hold, _gap);
		key(_m, C::Right, _hold, _gap);
		for(int i = 0; i < 8; ++i) key(_m, C::Up, _hold, _gap);
		key(_m, C::Down, _hold, _gap);
		key(_m, C::Enter, _hold, _gap);
		key(_m, C::Right, _hold, _gap);
		key(_m, C::Enter, _hold, _gap);
	}

	void macroMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		const auto normal = lcdBits(*m);
		md::test::enterMmReceive(m->hardware(), false);
		const auto recv = lcdBits(*m);
		md::test::exitMenus(m->hardware());
		m->run(300);
		std::printf("reference: normal vs recv %d px, back to normal %d px\n", lcdDiff(normal, recv), lcdDiff(normal, lcdBits(*m)));
		const auto p75 = retarget(patternDump(*m, 75), 20);
		for(const auto& t : std::vector<std::pair<double, double>>{{40, 40}, {20, 30}, {10, 20}, {10, 10}, {5, 10}, {4, 4}})
		{
			const auto t0 = m->now();
			recvMacro(*m, t.first, t.second);
			const auto t1 = m->now();
			m->run(20);
			const int d = lcdDiff(recv, lcdBits(*m));
			// Does it take a dump now?
			const auto before = rawPayload(patternDump(*m, 20));
			m->send(p75);
			m->run(50);
			const bool took = rawPayload(patternDump(*m, 20)) == rawPayload(p75);
			const auto t2 = m->now();
			for(int i = 0; i < 4; ++i) key(*m, md::PanelControl::Exit, t.first, t.second);
			m->run(50);
			const int back = lcdDiff(normal, lcdBits(*m));
			std::printf("hold %.0f gap %.0f ms: macro %.0f ms, lcd diff to RECV %d px, dump taken %d (%.0f ms), 4x EXIT -> normal diff %d px (%.0f ms)\n",
				t.first, t.second, ms(t1 - t0), d, took, ms(t2 - t1), back, ms(m->now() - t2));
			(void)before;
			// put slot 20 back to something else so the next round can detect a store
			md::test::enterMmReceive(m->hardware(), false);
			m->send(retarget(patternDump(*m, 1), 20));
			m->run(50);
			md::test::exitMenus(m->hardware());
			m->run(300);
		}
	}

	// A RAM byte that says "SYSEX RECV is waiting": equal in every RECV entry,
	// different on the normal screen, the GLOBAL menu and the SYSEX SEND screen.
	void recvFlagMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		using C = md::PanelControl;
		std::vector<Bytes> recvSnaps, otherSnaps;
		otherSnaps.push_back(m->snapshotRam());
		tap(*m, C::Play);
		m->run(500);
		otherSnaps.push_back(m->snapshotRam());
		for(int round = 0; round < 3; ++round)
		{
			recvMacro(*m, 10, 10);
			m->run(100 + round * 300);
			recvSnaps.push_back(m->snapshotRam());
			for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
			m->run(200);
			otherSnaps.push_back(m->snapshotRam());
			chord(*m, C::Kit, 10, 10);	// GLOBAL menu
			m->run(200);
			otherSnaps.push_back(m->snapshotRam());
			for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
			m->run(200);
			if(round == 1) { tap(*m, C::Stop); m->run(300); }
		}
		int shown = 0;
		for(uint32_t a = 0; a < 0x100000 && shown < 40; ++a)
		{
			const auto v = recvSnaps[0][a];
			bool same = true;
			for(const auto& r : recvSnaps) same &= r[a] == v;
			if(!same) continue;
			bool distinct = true;
			for(const auto& o : otherSnaps) distinct &= o[a] != v;
			if(!distinct) continue;
			std::printf("  recv flag candidate 0x%06x = 0x%02x (others:", 0x200000 + a, v);
			for(const auto& o : otherSnaps) std::printf(" %02x", o[a]);
			std::printf(")\n");
			++shown;
		}
	}

	constexpr uint32_t g_recvFlag = 0x268033;	// 1 while SYSEX RECV waits for its first message
	constexpr uint32_t g_screenAddress = 0x266ec8;	// u32: the firmware's current screen handler
	constexpr uint32_t g_screenRecv = 0x002c3a98;
	uint32_t read32(Machine& _m, const uint32_t _a)
	{
		return (uint32_t(_m.read8(_a)) << 24) | (uint32_t(_m.read8(_a + 1)) << 16) | (uint32_t(_m.read8(_a + 2)) << 8) | _m.read8(_a + 3);
	}
	bool onRecv(Machine& _m) { return read32(_m, g_screenAddress) == g_screenRecv; }

	void kitSongMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		using C = md::PanelControl;
		recvMacro(*m, 10, 10);
		m->run(50);
		check(m->read8(g_recvFlag) == 1, "RAM 0x268033 = 1 on SYSEX RECV");
		// Kit: stored slot 0 vs the working kit.
		const auto k0 = dump(*m, 0x52, 0);
		auto raw = rawPayload(k0);
		const auto workBefore = raw;
		raw[0x0b] = 5;	// T1 level
		std::memcpy(raw.data(), "KITDUMP\0\0\0\0", 11);
		const auto sent = encodeRle(k0, raw);
		const auto t0 = m->now();
		m->send(sent);
		m->run(100);
		std::printf("kit dump drained+100 ms: %.1f ms, recv flag after %d\n", ms(m->now() - t0), m->read8(g_recvFlag));
		check(onRecv(*m), "still on SYSEX RECV after a kit dump (screen word)");
		check(m->read8(g_recvFlag) == 0, "the waiting flag clears after the first message");
		printLcd(*m, "after kit dump in RECV");
		const auto stored = rawPayload(dump(*m, 0x52, 0));
		check(stored == raw, "kit dump stores slot 0");
		// Working kit: SAVE KIT to a scratch slot (makes it current), compare.
		m->send(mmCommand(0x59, {100}));
		m->run(150);
		const auto working = rawPayload(dump(*m, 0x52, 100));
		std::printf("working kit after the kit dump: %s\n", diffText(workBefore, working).c_str());
		check(working == workBefore, "a kit dump does not change the working kit (stored slot only)");
		m->send(mmCommand(0x58, {0}));
		m->run(150);
		m->send(mmCommand(0x59, {101}));
		m->run(150);
		check(rawPayload(dump(*m, 0x52, 101)) == raw, "LOAD KIT 0 then makes the dumped kit the working kit");

		// Song 0
		const auto s0 = dump(*m, 0x69, 0);
		auto sraw = rawPayload(s0);
		std::memcpy(sraw.data(), "PROBESONG\0\0\0\0\0", 14);
		m->send(encodeRle(s0, sraw));
		m->run(100);
		check(rawPayload(dump(*m, 0x69, 0)) == sraw, "song dump stored in RECV");
		check(onRecv(*m), "still on SYSEX RECV after a song dump");
		// Global 1 (not active)
		const auto g1 = dump(*m, 0x50, 1);
		auto graw = rawPayload(g1);
		std::printf("global raw[0..8]: %02x %02x %02x %02x %02x %02x %02x %02x\n", graw[0], graw[1], graw[2], graw[3], graw[4], graw[5], graw[6], graw[7]);
		graw[0x10] ^= 1;
		m->send(encodeRle(g1, graw));
		m->run(100);
		check(rawPayload(dump(*m, 0x50, 1)) == graw, "global dump stored in RECV");
		check(onRecv(*m), "still on SYSEX RECV after a global dump");
		const auto tx = m->now();
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		m->run(20);
		std::printf("exit: %.0f ms, flag %d\n", ms(m->now() - tx), m->read8(g_recvFlag));
		check(!onRecv(*m), "off SYSEX RECV after EXIT");
		printLcd(*m, "after exit");
	}

	void recvCountMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		using C = md::PanelControl;
		std::vector<Bytes> in, out;
		out.push_back(m->snapshotRam());
		chord(*m, C::Kit, 10, 10);
		m->run(200);
		out.push_back(m->snapshotRam());
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		const auto k5 = dump(*m, 0x52, 5);
		for(int round = 0; round < 2; ++round)
		{
			recvMacro(*m, 10, 10);
			m->run(100);
			in.push_back(m->snapshotRam());
			for(int n = 0; n < 3; ++n)
			{
				m->send(k5);
				m->run(150);
				in.push_back(m->snapshotRam());
			}
			for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
			m->run(200);
			out.push_back(m->snapshotRam());
		}
		for(uint32_t a = 0; a < 0x100000; ++a)
		{
			// count: 0,1,2,3,0,1,2,3
			bool count = true;
			for(size_t i = 0; i < in.size(); ++i) count &= in[i][a] == (i % 4);
			if(count)
				std::printf("  count candidate 0x%06x\n", 0x200000 + a);
			const auto v = in[0][a];
			bool same = true;
			for(const auto& r : in) same &= r[a] == v;
			bool distinct = true;
			for(const auto& o : out) distinct &= o[a] != v;
			if(same && distinct)
			{
				std::printf("  screen candidate 0x%06x = %02x (out:", 0x200000 + a, v);
				for(const auto& o : out) std::printf(" %02x", o[a]);
				std::printf(")\n");
			}
		}
	}

	constexpr uint32_t g_recvCount = 0x26a3c7;	// messages received on this RECV screen

	void burstMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		recvMacro(*m, 10, 10);
		m->run(50);
		std::printf("recv: flag %d count %d, region:", m->read8(g_recvFlag), m->read8(g_recvCount));
		for(uint32_t a = 0x26a3c0; a < 0x26a3d0; ++a) std::printf(" %02x", m->read8(a));
		std::printf("\n");
		std::vector<Bytes> sent;
		for(uint8_t s = 0; s < 4; ++s)
			sent.push_back(retarget(patternDump(*m, static_cast<uint8_t>(1 + s)), static_cast<uint8_t>(40 + s)));
		// back to back, no wait
		std::vector<std::pair<double, int>> flagTrace;
		const auto t0 = m->now();
		m->onBlock = [&] { flagTrace.emplace_back(ms(m->now() - t0), m->read8(g_recvFlag) | (m->read8(g_recvCount) << 1)); };
		for(const auto& d : sent)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = d;
			m->hardware().sendMidi(e);
		}
		m->run(400);
		m->onBlock = nullptr;
		int last = -1;
		for(const auto& [t, v] : flagTrace)
			if(v != last) { std::printf("  %6.1f ms flag %d count %d\n", t, v & 1, v >> 1); last = v; }
		int ok = 0;
		for(uint8_t s = 0; s < 4; ++s)
			ok += rawPayload(patternDump(*m, static_cast<uint8_t>(40 + s))) == rawPayload(sent[s]);
		std::printf("back-to-back: %d of 4 stored\n", ok);
		check(ok == 4, "four pattern dumps back to back are all stored");
		// A bad checksum
		auto bad = sent[0];
		bad[bad.size() - 4] ^= 1;
		m->send(bad);
		m->run(200);
		std::printf("after a bad checksum: count %d, region:", m->read8(g_recvCount));
		for(uint32_t a = 0x26a3c0; a < 0x26a3d0; ++a) std::printf(" %02x", m->read8(a));
		std::printf("\n");
		printLcd(*m, "after bad dump");
	}

	std::vector<size_t> findAll(const Bytes& _hay, const Bytes& _needle)
	{
		std::vector<size_t> at;
		auto it = _hay.begin();
		while((it = std::search(it, _hay.end(), _needle.begin(), _needle.end())) != _hay.end())
		{
			at.push_back(static_cast<size_t>(it - _hay.begin()));
			++it;
		}
		return at;
	}

	// Where does the kit that plays live? (like MD P3: patch RAM 0x70000a)
	void workKitMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		m->send({0xb0, 48, 99});
		m->send({0xb3, 72, 17});
		m->run(100);
		m->send(mmCommand(0x59, {100}));
		m->run(150);
		const auto work = rawPayload(dump(*m, 0x52, 100));
		const auto patch = m->hardware().copyPatchRam();
		const auto ram = m->snapshotRam();
		const Bytes needle(work.begin() + 0x11, work.begin() + 0x11 + 96);
		for(const auto a : findAll(patch, needle))
			std::printf("  params found in patch RAM +0x%06zx (cpu 0x%06zx)\n", a, 0x700000 + a);
		for(const auto a : findAll(ram, needle))
			std::printf("  params found in main RAM 0x%06zx\n", 0x200000 + a);
		const Bytes name(work.begin(), work.begin() + 11);
		for(const auto a : findAll(patch, name))
			std::printf("  name found in patch RAM +0x%06zx\n", a);
		for(const auto a : findAll(ram, name))
			std::printf("  name found in main RAM 0x%06zx\n", 0x200000 + a);
		// Full image?
		for(const auto a : findAll(patch, work))
			std::printf("  WHOLE kit image in patch RAM +0x%06zx\n", a);
		for(const auto a : findAll(ram, work))
			std::printf("  WHOLE kit image in main RAM 0x%06zx\n", 0x200000 + a);
		// Unsaved edit: does a location follow?
		m->send({0xb0, 48, 42});
		m->run(100);
		const auto patch2 = m->hardware().copyPatchRam();
		const auto ram2 = m->snapshotRam();
		std::printf("after CC48=42 (not saved):\n");
		for(size_t i = 0; i < patch.size(); ++i)
			if(patch[i] != patch2[i]) std::printf("  patch +0x%06zx %02x>%02x\n", i, patch[i], patch2[i]);
		int shown = 0;
		for(size_t i = 0; i < ram.size() && shown < 30; ++i)
			if(ram[i] == 99 && ram2[i] == 42) { std::printf("  main 0x%06zx 99>42\n", 0x200000 + i); ++shown; }
	}

	constexpr size_t g_patternLengthOffset = 0x424;	// raw payload, 2-64 (factory: 16/64)

	void queueMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		int shortSlot = -1;
		for(uint8_t s = 2; s < 40 && shortSlot < 0; ++s)
		{
			const auto r = rawPayload(patternDump(*m, s));
			if(r.size() > g_patternLengthOffset && r[g_patternLengthOffset] < 64 && r.size() > 0 && rawPayload(patternDump(*m, s)) != rawPayload(patternDump(*m, 75)))
				shortSlot = s;
		}
		std::printf("short pattern: slot %d\n", shortSlot);
		require(shortSlot > 0, "no short pattern");
		const std::vector<uint32_t> cands{0x257e57, 0x2bc287, 0x2bc28b, 0x2bdf3d};
		m->send(mmCommand(0x57, {1}));
		m->run(100);
		tap(*m, md::PanelControl::Play);
		// run to step ~8 of pattern 1
		while(m->read8(cands[0]) != 8) m->step();
		const auto tSel = m->now();
		m->send(mmCommand(0x57, {static_cast<uint8_t>(shortSlot)}));
		int lastStatus = -2;
		std::vector<int> last(cands.size(), -1);
		for(int i = 0; i < 80 * 4; ++i)
		{
			m->run(31.25);
			if(i % 4 == 0)
			{
				const int st = status(*m, 0x04);
				if(st != lastStatus) { std::printf("  +%6.0f ms status pattern %d\n", ms(m->now() - tSel), st); lastStatus = st; }
			}
			std::string line;
			bool changed = false;
			for(size_t c = 0; c < cands.size(); ++c)
			{
				const int v = m->read8(cands[c]);
				if(v < last[c] && last[c] >= 0) changed = true;
				last[c] = v;
				line += " " + std::to_string(v);
			}
			if(changed)
				std::printf("  +%6.0f ms wrap: candidates%s\n", ms(m->now() - tSel), line.c_str());
		}
		std::printf("final candidates:");
		for(const auto c : cands) std::printf(" 0x%06x=%d", c, m->read8(c));
		std::printf("\n");
	}

	// Boot timeline: MIDI ready, the LCD animation, the first status reply, and
	// when a panel key first takes effect (TEMPO opens its menu).
	void bootMode(const Bytes& _rom)
	{
		md::Hardware hw(_rom, g_romName, g_mm);
		require(hw.isValid(), "invalid MM");
		uint64_t frames = 0;
		uint64_t midiReady = 0;
		uint64_t lastChange = 0;
		std::string prev;
		int changes = 0;
		const auto lcdHash = [&] {
			const auto p = hw.getFrontPanelSnapshot();
			std::string h(128 * 64 / 8, '\0');
			for(uint32_t y = 0; y < 64; ++y)
				for(uint32_t x = 0; x < 128; ++x)
					if(p.getLcdPixel(x, y)) h[(y * 128 + x) >> 3] |= static_cast<char>(1 << (x & 7));
			return h;
		};
		while(frames < g_rate * 40ull)
		{
			hw.advance(g_block);
			frames += g_block;
			if(!midiReady && hw.isFirmwareMidiReady())
			{
				midiReady = frames;
				std::printf("  %6.0f ms firmware MIDI ready\n", ms(frames));
			}
			if((frames / g_block) % 35 == 0)	// ~50 ms
			{
				const auto h = lcdHash();
				if(h != prev)
				{
					++changes;
					if(frames - lastChange > g_rate / 2 || changes < 6)
						std::printf("  %6.0f ms LCD changes (#%d, %zu lit px)\n", ms(frames), changes,
							static_cast<size_t>(hw.getFrontPanelSnapshot().countLitPixels()));
					lastChange = frames;
					prev = h;
				}
			}
		}
		std::printf("  LCD changed %d times; last change at %.0f ms\n", changes, ms(lastChange));

		// A RAM byte that flips when the animation ends (fresh machine).
		md::Hardware h2(_rom, g_romName, g_mm);
		const auto snap = [&] { Bytes r; for(uint32_t a = 0x200000; a < 0x300000; ++a) r.push_back(h2.getUC().read8(a)); return r; };
		std::vector<Bytes> during, after;
		uint64_t f2 = 0;
		for(const double t : {3.0, 5.0, 7.0, 8.5, 10.0, 12.0, 15.0, 20.0})
		{
			while(f2 < static_cast<uint64_t>(t * g_rate)) { h2.advance(g_block); f2 += g_block; }
			(t < 9.0 ? during : after).push_back(snap());
		}
		int shown = 0;
		for(uint32_t a = 0; a < 0x100000 && shown < 30; ++a)
		{
			const auto d = during[0][a], f = after[0][a];
			if(d == f) continue;
			bool ok = true;
			for(const auto& x : during) ok &= x[a] == d;
			for(const auto& x : after) ok &= x[a] == f;
			if(ok) { std::printf("  boot-done candidate 0x%06x %02x -> %02x\n", 0x200000 + a, d, f); ++shown; }
		}
	}

	void screensMode(const Bytes& _rom)
	{
		md::Hardware hw(_rom, g_romName, g_mm);
		uint64_t f = 0;
		uint32_t last = 0;
		while(f < g_rate * 12ull)
		{
			hw.advance(g_block);
			f += g_block;
			auto& uc = hw.getUC();
			const uint32_t v = (uint32_t(uc.read8(g_screenAddress)) << 24) | (uint32_t(uc.read8(g_screenAddress + 1)) << 16)
				| (uint32_t(uc.read8(g_screenAddress + 2)) << 8) | uc.read8(g_screenAddress + 3);
			if(v != last) { std::printf("  boot %6.0f ms screen 0x%08x (midi ready %d)\n", ms(f), v, hw.isFirmwareMidiReady()); last = v; }
		}
		auto m = boot(_rom);
		using C = md::PanelControl;
		std::printf("normal 0x%08x\n", read32(*m, g_screenAddress));
		tap(*m, C::Play); m->run(300);
		std::printf("playing 0x%08x\n", read32(*m, g_screenAddress));
		key(*m, C::Tempo, 10, 10); m->run(200);
		std::printf("tempo 0x%08x\n", read32(*m, g_screenAddress));
		key(*m, C::Exit, 10, 10); m->run(200);
		chord(*m, C::Kit, 10, 10); m->run(200);
		std::printf("global 0x%08x\n", read32(*m, g_screenAddress));
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		recvMacro(*m, 10, 10); m->run(50);
		std::printf("recv 0x%08x\n", read32(*m, g_screenAddress));
		m->send(dump(*m, 0x52, 3)); m->run(100);
		std::printf("recv after a dump 0x%08x\n", read32(*m, g_screenAddress));
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		m->run(50);
		std::printf("after exit 0x%08x\n", read32(*m, g_screenAddress));
		// fewest EXITs back to normal
		recvMacro(*m, 10, 10); m->run(50);
		int n = 0;
		while(read32(*m, g_screenAddress) == 0x3a98 || (read32(*m, g_screenAddress) & 0xffff) != 0x2908)
		{
			key(*m, C::Exit, 10, 10);
			if(++n > 10) break;
		}
		std::printf("EXITs from RECV to normal: %d\n", n);
	}

	// ---- lab: a line script of panel/MIDI actions with dump diffs -------------
	//   key <Name> [hold gap]   chord <Name>    hold <Name>    release <Name>
	//   knob <0-7> <delta>      note <n> [vel] [ch]   noteon <n> [vel] [ch]   noteoff <n> [ch]
	//   cc <ch> <cc> <v>        sx <hex..>       wait <ms>      lcd [title]
	//   empty <slot>            (pattern <slot> := factory empty 75, then LOAD PATTERN)
	//   psnap <label>           (diff of the current pattern slot vs the last psnap)
	//   ksnap <label>           (SAVE KIT 127 and diff vs the last ksnap)
	//   gsnap <slot> <label>    ssnap <slot> <label>   save <file>  (last pattern snap)
	//   # comment
	std::optional<md::PanelControl> controlByName(const std::string& _n)
	{
		for(int i = 0; i <= static_cast<int>(md::PanelControl::ClassicExtended); ++i)
			if(_n == md::panelControlName(static_cast<md::PanelControl>(i)))
				return static_cast<md::PanelControl>(i);
		return std::nullopt;
	}

	void labMode(const Bytes& _rom, const std::string& _script)
	{
		auto m = boot(_rom);
		std::ifstream in(_script);
		require(in.good(), "cannot read script " + _script);
		md::PanelRowState rows;
		int slot = status(*m, 0x04);
		Bytes lastP = rawPayload(patternDump(*m, static_cast<uint8_t>(slot)));
		Bytes lastK, lastG, lastS;
		std::string line;
		while(std::getline(in, line))
		{
			std::istringstream ls(line);
			std::string op;
			if(!(ls >> op) || op[0] == '#')
				continue;
			std::printf("> %s\n", line.c_str());
			if(op == "key" || op == "chord" || op == "hold" || op == "release")
			{
				std::string n;
				double hold = 30, gap = 60;
				ls >> n >> hold >> gap;
				const auto c = controlByName(n);
				require(c.has_value(), "unknown control " + n);
				const auto pk = md::panelPacket(g_mm, *c);
				require(pk.has_value(), "no MM packet for " + n);
				if(op == "key") key(*m, *c, hold, gap);
				else if(op == "chord") chord(*m, *c, hold, gap);
				else if(op == "hold") { const auto s2 = rows.press(*pk); m->hardware().trySendPanelEvent(s2.row, s2.mask); m->run(hold); }
				else { const auto s2 = rows.release(*pk); m->hardware().trySendPanelEvent(s2.row, s2.mask); m->run(hold); }
			}
			else if(op == "knob")
			{
				int e = 0, d = 0;
				ls >> e >> d;
				const auto cmd = md::panelEncoderCommand(g_mm, static_cast<md::PanelEncoder>(e));
				require(cmd.has_value(), "no encoder");
				for(int i = 0; i < std::abs(d); ++i)
				{
					m->hardware().trySendPanelEvent(*cmd, d > 0 ? 0x01 : 0xff);
					m->run(8);
				}
				m->run(50);
			}
			else if(op == "note" || op == "noteon" || op == "noteoff")
			{
				int n = 60, v = 100, ch = 8;
				ls >> n;
				if(op != "noteoff") ls >> v;
				ls >> ch;
				if(op != "noteoff")
					m->send({static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(n), static_cast<uint8_t>(v)});
				if(op == "note")
					m->run(60);
				if(op != "noteon")
					m->send({static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(n), 0});
				m->run(60);
			}
			else if(op == "cc")
			{
				int ch = 0, c = 0, v = 0;
				ls >> ch >> c >> v;
				m->send({static_cast<uint8_t>(0xb0 | ch), static_cast<uint8_t>(c), static_cast<uint8_t>(v)});
				m->run(30);
			}
			else if(op == "sx")
			{
				Bytes b;
				std::string h;
				while(ls >> h) b.push_back(static_cast<uint8_t>(std::stoul(h, nullptr, 16)));
				m->send(b);
				m->run(100);
			}
			else if(op == "wait")
			{
				double t = 0;
				ls >> t;
				m->run(t);
			}
			else if(op == "ramdiff")
			{
				// ramdiff <label>: RAM bytes (0x200000-0x2c0000) that were steady before (two reads 200 ms
				// apart), are steady now, and differ: state, not the machine's moving counters. At most 60.
				std::string label;
				std::getline(ls, label);
				static std::vector<uint8_t> prev;
				static std::vector<bool> prevSteady;
				const auto snap = [&]
				{
					std::vector<uint8_t> r(0xc0000);
					for(uint32_t a2 = 0; a2 < r.size(); ++a2) r[a2] = m->read8(0x200000 + a2);
					return r;
				};
				const auto s1 = snap();
				m->run(200);
				const auto s2 = snap();
				std::vector<bool> steady(s1.size());
				for(size_t i = 0; i < s1.size(); ++i) steady[i] = s1[i] == s2[i];
				if(!prev.empty())
				{
					int n = 0, total = 0;
					std::printf("  ramdiff%s:", label.c_str());
					for(uint32_t i = 0; i < s2.size(); ++i)
						if(prevSteady[i] && steady[i] && prev[i] != s2[i]) { ++total; if(n++ < 4000) std::printf(" %06x:%02x>%02x", 0x200000 + i, prev[i], s2[i]); }
					std::printf(" (%d)\n", total);
				}
				prev = s2;
				prevSteady = steady;
			}
			else if(op == "peek8")
			{
				// peek8 <addr hex> <count>
				std::string h;
				int n = 1;
				ls >> h >> n;
				const auto a = static_cast<uint32_t>(std::stoul(h, nullptr, 16));
				std::printf("  peek8 %06x:", a);
				for(int i = 0; i < n; ++i) std::printf(" %02x", m->read8(a + static_cast<uint32_t>(i)));
				std::printf("\n");
			}
			else if(op == "status")
			{
				// status <param hex>: the status reply (0x70/0x72)
				std::string h;
				ls >> h;
				const auto p = static_cast<uint8_t>(std::stoul(h, nullptr, 16));
				std::printf("  status %02x = %d\n", p, status(*m, p));
			}
			else if(op == "peek16")
			{
				std::string h;
				std::printf("  peek16");
				while(ls >> h)
				{
					const auto a = static_cast<uint32_t>(std::stoul(h, nullptr, 16));
					std::printf(" %06x=%u", a, (static_cast<uint32_t>(m->read8(a)) << 8) | m->read8(a + 1));
				}
				std::printf("\n");
			}
			else if(op == "ramfind")
			{
				// ramfind <u16 hex>: RAM addresses holding that big-endian word (tempo and state hunts)
				std::string h;
				ls >> h;
				const auto v = static_cast<uint32_t>(std::stoul(h, nullptr, 16));
				std::printf("  ramfind %04x:", v);
				int n = 0;
				for(uint32_t a = 0x200000; a < 0x2c0000 && n < 40; ++a)
					if(((static_cast<uint32_t>(m->read8(a)) << 8) | m->read8(a + 1)) == v) { std::printf(" %06x", a); ++n; }
				std::printf("\n");
			}
			else if(op == "lcd")
			{
				std::string t;
				std::getline(ls, t);
				printLcd(*m, t.c_str());
			}
			else if(op == "empty")
			{
				int s2 = 0;
				ls >> s2;
				recvMacro(*m, 10, 10);
				m->send(retarget(patternDump(*m, 75), static_cast<uint8_t>(s2)));
				m->run(50);
				for(int i = 0; i < 6; ++i) key(*m, md::PanelControl::Exit, 10, 10);
				m->send(mmCommand(0x57, {static_cast<uint8_t>(s2)}));
				m->run(200);
				slot = s2;
				lastP = rawPayload(patternDump(*m, static_cast<uint8_t>(slot)));
			}
			else if(op == "psnap")
			{
				std::string label;
				std::getline(ls, label);
				slot = status(*m, 0x04);
				const auto now = rawPayload(patternDump(*m, static_cast<uint8_t>(slot)));
				std::printf("  [P%d%s] %s\n", slot, label.c_str(), diffText(lastP, now, 64).c_str());
				lastP = now;
			}
			else if(op == "ksnap")
			{
				std::string label;
				std::getline(ls, label);
				const auto now = savedKit(*m, 127);
				if(!lastK.empty())
					std::printf("  [K%s] %s\n", label.c_str(), diffText(lastK, now, 64).c_str());
				lastK = now;
			}
			else if(op == "gsnap" || op == "ssnap")
			{
				int s2 = 0;
				std::string label;
				ls >> s2;
				std::getline(ls, label);
				auto& last = op == "gsnap" ? lastG : lastS;
				const auto now = rawPayload(dump(*m, op == "gsnap" ? 0x50 : 0x69, static_cast<uint8_t>(s2)));
				if(!last.empty())
					std::printf("  [%s%d%s] %s\n", op == "gsnap" ? "G" : "S", s2, label.c_str(), diffText(last, now, 64).c_str());
				last = now;
			}
			else if(op == "save")
			{
				std::string f;
				ls >> f;
				save(f, encodeRle(patternDump(*m, static_cast<uint8_t>(slot)), lastP));
			}
			else
				require(false, "unknown lab op " + op);
		}
	}

	// ---- programmed corpus (MM-P1) --------------------------------------------
	namespace ed = elektronData;

	struct Rng
	{
		uint32_t s = 12345;
		uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
		int in(const int _lo, const int _hi) { return _lo + static_cast<int>(next() % static_cast<uint32_t>(_hi - _lo + 1)); }
		bool chance(const int _pct) { return in(0, 99) < _pct; }
	};

	uint64_t randomMask(Rng& _r, const int _pct)
	{
		uint64_t m = 0;
		for(int s = 0; s < 64; ++s)
			if(_r.chance(_pct)) m |= uint64_t{1} << s;
		return m;
	}

	ed::MmPattern randomPattern(Rng& _r, ed::MmPattern _p, const int _variant)
	{
		_p.length = static_cast<uint8_t>(_variant % 4 == 0 ? 64 : _r.in(2, 64));
		_p.multiplier = static_cast<uint8_t>(_r.in(0, 3));
		_p.kit = static_cast<uint8_t>(_r.in(0, 127));
		_p.swingAmount = static_cast<uint8_t>(_r.in(0, 30));
		_p.patternTranspose = static_cast<int8_t>(_r.in(-24, 24));
		for(size_t t = 0; t < 6; ++t)
		{
			_p.pitch[t] = randomMask(_r, 30);
			_p.amp[t] = _p.pitch[t] & randomMask(_r, 90);
			_p.filter[t] = _p.pitch[t] & randomMask(_r, 90);
			_p.lfo[t] = _p.pitch[t] & randomMask(_r, 90);
			_p.noteOff[t] = ~_p.pitch[t] & randomMask(_r, 5);
			_p.slide[t] = randomMask(_r, 10);
			_p.swing[t] = randomMask(_r, 50);
			_p.midiTrig[t] = randomMask(_r, 20);
			_p.midiNote[t] = _p.midiTrig[t];
			_p.midiNoteOff[t] = ~_p.midiTrig[t] & randomMask(_r, 5);
			_p.midiSlide[t] = randomMask(_r, 10);
			_p.midiSwing[t] = randomMask(_r, 50);
			_p.chord[t] = 0;
			for(size_t s = 0; s < 64; ++s)
				_p.notes[t][s] = ed::mmStepSet(_p.pitch[t], s) && _r.chance(90) ? static_cast<uint8_t>(_r.in(24, 100)) : ed::MmPattern::g_noNote;
			_p.transpose.track[t] = static_cast<int8_t>(_r.in(-24, 24));
			_p.transpose.scale[t] = static_cast<uint8_t>(_r.in(0, 3));
			_p.transpose.key[t] = static_cast<uint8_t>(_r.in(0, 11));
			_p.midiTranspose.track[t] = static_cast<int8_t>(_r.in(-24, 24));
			_p.midiTranspose.scale[t] = static_cast<uint8_t>(_r.in(0, 3));
			_p.midiTranspose.key[t] = static_cast<uint8_t>(_r.in(0, 11));
			for(auto* a : {&_p.arp, &_p.midiArp})
			{
				a->playOjmp[t] = static_cast<uint8_t>(_r.in(0, 4) | (_r.in(0, 3) << 4));
				a->mode[t] = static_cast<uint8_t>(_r.in(0, 3));
				a->range[t] = static_cast<uint8_t>(_r.in(0, 3));
				a->speed[t] = static_cast<uint8_t>(_r.in(0, 23));
				a->length[t] = static_cast<uint8_t>(_r.in(1, 16));
				for(auto& st : a->steps[t])
					st = _r.chance(20) ? 0xff : static_cast<uint8_t>(0x40 + _r.in(-24, 24));
			}
			_p.arp.trigs[t] = static_cast<uint8_t>(_r.in(0, 7));
		}
		// Locks: a sorted random subset, up to the full pool.
		for(auto& lm : _p.lockMasks) lm.fill(0);
		const int want = _variant % 4 == 1 ? 62 : _r.in(0, 40);
		int have = 0;
		while(have < want)
		{
			const auto t = _r.in(0, 5), pg = _r.in(0, 7), i = _r.in(0, 7);
			if(_p.lockMasks[t][pg] & (1u << i)) continue;
			_p.lockMasks[t][pg] |= static_cast<uint8_t>(1u << i);
			++have;
		}
		_p.lockRowCount = static_cast<uint8_t>(have);
		const auto params = ed::mmLockParams(_p);
		for(size_t r = 0; r < ed::MmPattern::g_lockRows; ++r)
			for(size_t s = 0; s < 64; ++s)
			{
				const bool used = r < params.size();
				const auto trigs = used ? (params[r].page == 7 ? _p.midiTrig[params[r].track] : _p.pitch[params[r].track]) : 0;
				_p.lockRows[r][s] = used && ed::mmStepSet(trigs, s) && _r.chance(60) ? static_cast<uint8_t>(_r.in(0, 127)) : 0xff;
			}
		// MIDI notes, in (step, track) order, one or more per MIDI trig.
		std::vector<ed::MmNoteEntry> midi;
		for(uint8_t s = 0; s < 64; ++s)
			for(uint8_t t = 0; t < 6; ++t)
				if(ed::mmStepSet(_p.midiTrig[t], s))
					for(int k = _r.in(1, _variant % 4 == 2 ? 4 : 2); k > 0 && midi.size() < 400; --k)
						midi.push_back({t, s, static_cast<uint8_t>(_r.in(24, 100))});
		_p.midiNotes.fill(0xffff);
		for(size_t i = 0; i < midi.size(); ++i) _p.midiNotes[i] = ed::mmNoteEntryWord(midi[i]);
		_p.midiNoteCount = static_cast<uint16_t>(midi.size());
		// Chords: extra notes on some synth trigs with a pitch.
		std::vector<ed::MmNoteEntry> chords;
		for(uint8_t s = 0; s < 64; ++s)
			for(uint8_t t = 0; t < 6; ++t)
				if(_p.notes[t][s] != 0xff && _r.chance(15))
				{
					_p.chord[t] |= ed::mmStepBit(s);
					for(int k = _r.in(1, 3); k > 0 && chords.size() < 192; --k)
						chords.push_back({t, s, static_cast<uint8_t>(_r.in(24, 100))});
				}
		_p.chordNotes.fill(0xffff);
		for(size_t i = 0; i < chords.size(); ++i) _p.chordNotes[i] = ed::mmNoteEntryWord(chords[i]);
		_p.chordNoteCount = static_cast<uint8_t>(chords.size());
		return _p;
	}

	ed::MmKit randomKit(Rng& _r, ed::MmKit _k)
	{
		static const char* names[] = {"PROBE", "RANDOM KIT", "LAB", "MM P1", "SYNTHS", "X"};
		const std::string n = names[_r.in(0, 5)];
		_k.name.fill(0);
		for(size_t i = 0; i < n.size(); ++i) _k.name[i] = static_cast<uint8_t>(n[i]);
		const auto& machines = ed::mmMachines();
		for(size_t t = 0; t < 6; ++t)
		{
			_k.levels[t] = static_cast<uint8_t>(_r.in(0, 127));
			_k.machines[t] = machines[static_cast<size_t>(_r.in(0, static_cast<int>(machines.size()) - 1))].id;
			_k.routing[t] = ed::mmRouting(static_cast<uint8_t>(_r.in(0, 7)), static_cast<uint8_t>(_r.in(0, 6)));
			for(auto& pg : _k.tracks[t].pages) for(auto& v : pg) v = static_cast<uint8_t>(_r.in(0, 127));
			for(auto& v : _k.tracks[t].midi) v = static_cast<uint8_t>(_r.in(0, 127));
			for(auto& v : _k.assignPage[t]) v = static_cast<uint8_t>(_r.in(0, 8));
			for(auto& v : _k.assignDest[t]) v = static_cast<uint8_t>(_r.in(0, 7));
			for(auto& v : _k.assignAdd[t]) v = static_cast<int8_t>(_r.in(-64, 63));
			_k.trigPos[t] = _r.chance(70) ? 0xff : static_cast<uint8_t>(_r.in(0, 5));
		}
		_k.mirrorMask = static_cast<uint8_t>(_r.in(0, 63));
		_k.legatoAmp = static_cast<uint8_t>(0xc0 | _r.in(0, 63));
		return _k;
	}

	ed::MmSong randomSong(Rng& _r, ed::MmSong _s, const int _variant)
	{
		const size_t rows = _variant % 3 == 0 ? 199 : static_cast<size_t>(_r.in(1, 60));
		const std::string n = "SONG P1 " + std::to_string(_variant);
		_s.name.fill(0);
		for(size_t i = 0; i < n.size() && i < 14; ++i) _s.name[i] = static_cast<uint8_t>(n[i]);
		for(size_t i = 0; i < rows; ++i)
		{
			auto& b = _s.rows[i].bytes;
			b.fill(0);
			if(i > 0 && _r.chance(10))
			{
				b[ed::mmSongRow::g_pattern] = ed::MmSong::g_loop;
				b[ed::mmSongRow::g_target] = static_cast<uint8_t>(_r.in(0, static_cast<int>(i)));
				b[ed::mmSongRow::g_repeats] = static_cast<uint8_t>(_r.in(0, 8));
				b[ed::mmSongRow::g_length] = 0x3f;
				b[22] = b[23] = 0xff;
				continue;
			}
			b[ed::mmSongRow::g_pattern] = static_cast<uint8_t>(_r.in(0, 127));
			b[ed::mmSongRow::g_repeats] = static_cast<uint8_t>(_r.in(0, 63));
			b[ed::mmSongRow::g_mutes] = static_cast<uint8_t>(_r.in(0, 63));
			b[ed::mmSongRow::g_midiMutes] = static_cast<uint8_t>(_r.in(0, 63));
			b[ed::mmSongRow::g_length] = static_cast<uint8_t>(_r.in(1, 64));
			b[ed::mmSongRow::g_offset] = static_cast<uint8_t>(_r.in(0, b[ed::mmSongRow::g_length] - 1));
			b[ed::mmSongRow::g_transpose] = static_cast<uint8_t>(static_cast<int8_t>(_r.in(-12, 12)));
			for(size_t t = 0; t < 6; ++t)
			{
				b[ed::mmSongRow::g_trackTranspose + t] = static_cast<uint8_t>(static_cast<int8_t>(_r.in(-12, 12)));
				b[ed::mmSongRow::g_midiTranspose + t] = static_cast<uint8_t>(static_cast<int8_t>(_r.in(-12, 12)));
			}
			const int tempo = _r.chance(50) ? 0xffff : _r.in(30, 300);
			b[22] = static_cast<uint8_t>(tempo >> 8);
			b[23] = static_cast<uint8_t>(tempo);
		}
		auto& end = _s.rows[rows].bytes;
		end.fill(0);
		end[0] = ed::MmSong::g_end;
		end[22] = end[23] = 0xff;
		return _s;
	}

	// Push valid random documents through SYSEX RECV and keep the firmware's raw read-backs.
	void programMode(const Bytes& _rom, const std::string& _dir)
	{
		require(!_dir.empty(), "program needs an output directory");
		auto m = boot(_rom);
		Rng rng;
		const auto base = *ed::decodeMmPattern(patternDump(*m, 75));
		const auto kitBase = *ed::decodeMmKit(dump(*m, 0x52, 0));
		const auto songBase = *ed::decodeMmSong(dump(*m, 0x69, 1));
		recvMacro(*m, 10, 10);
		require(onRecv(*m), "not on SYSEX RECV");
		int stored = 0, total = 0;
		const auto push = [&](const Bytes& _sent, const uint8_t _cmd, const uint8_t _slot, const std::string& _name)
		{
			m->send(_sent);
			m->run(40);
			const auto back = dump(*m, _cmd, _slot);
			++total;
			const bool same = back == _sent;
			stored += same;
			if(!same)
				std::printf("  read-back differs: %s (%zu vs %zu bytes)\n", _name.c_str(), back.size(), _sent.size());
			save(_dir + "/" + _name + ".syx", back);
		};
		for(int i = 0; i < 128; ++i)
		{
			auto p = randomPattern(rng, base, i);
			p.position = static_cast<uint8_t>(i);
			{
				const auto problems = ed::validate(p);
				require(problems.empty(), "generated pattern invalid: " + (problems.empty() ? std::string() : problems.front()));
			}
			char n[32];
			std::snprintf(n, sizeof(n), "pattern-%03d", i);
			push(ed::encodeMmPattern(p), 0x67, static_cast<uint8_t>(i), n);
		}
		for(int i = 0; i < 128; ++i)
		{
			auto k = randomKit(rng, kitBase);
			k.position = static_cast<uint8_t>(i);
			{
				const auto problems = ed::validate(k);
				require(problems.empty(), "generated kit invalid: " + (problems.empty() ? std::string() : problems.front()));
			}
			char n[32];
			std::snprintf(n, sizeof(n), "kit-%03d", i);
			push(ed::encodeMmKit(k), 0x52, static_cast<uint8_t>(i), n);
		}
		for(int i = 0; i < 24; ++i)
		{
			auto s = randomSong(rng, songBase, i);
			s.position = static_cast<uint8_t>(i);
			{
				const auto problems = ed::validate(s);
				require(problems.empty(), "generated song invalid: " + (problems.empty() ? std::string() : problems.front()));
			}
			char n[32];
			std::snprintf(n, sizeof(n), "song-%03d", i);
			push(ed::encodeMmSong(s), 0x69, static_cast<uint8_t>(i), n);
		}
		for(int i = 1; i < 8; ++i)
		{
			auto g = *ed::decodeMmGlobal(dump(*m, 0x50, static_cast<uint8_t>(i)));
			g.routingMode = static_cast<uint8_t>(rng.in(0, 2));
			g.masterTune = static_cast<uint16_t>(rng.in(4300, 4500));
			for(auto& c : g.midiSeqChannels) c = static_cast<uint8_t>(rng.in(0, 15));
			for(auto& cc : g.midiSeqCcs) for(auto& v : cc) v = static_cast<uint8_t>(rng.in(0, 119));
			g.baseChannel = static_cast<uint8_t>(rng.in(0, 9));
			char n[32];
			std::snprintf(n, sizeof(n), "global-%03d", i);
			push(ed::encodeMmGlobal(g), 0x50, static_cast<uint8_t>(i), n);
		}
		std::printf("programmed: %d of %d read back byte for byte\n", stored, total);
		check(stored == total, "the firmware stores every valid programmed dump exactly as sent");

		// Invalid values: stored verbatim, or refused?
		auto bad = base;
		bad.position = 100;
		bad.length = 90;
		bad.multiplier = 7;
		bad.lockMasks[0][0] = 0x01;	// a locked parameter without a row count
		const auto sentBad = ed::encodeMmPattern(bad);
		m->send(sentBad);
		m->run(40);
		const auto backBad = dump(*m, 0x67, 100);
		std::printf("invalid pattern (length 90, multiplier 7, mask without row): stored verbatim %d\n", backBad == sentBad);
		for(int i = 0; i < 6; ++i) key(*m, md::PanelControl::Exit, 10, 10);
	}

	// The current kit number next to the working kit, and a playing/stopped byte.
	void stateBytesMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		const auto around = [&](const char* _t)
		{
			std::printf("%-24s 0x700018..0x700028:", _t);
			for(uint32_t a = 0x700018; a < 0x700029; ++a) std::printf(" %02x", m->read8(a));
			std::printf("\n");
		};
		around("kit 0");
		m->send(mmCommand(0x58, {5}));
		m->run(200);
		around("LOAD KIT 5");
		m->send(mmCommand(0x58, {77}));
		m->run(200);
		around("LOAD KIT 77");
		// playing byte: snapshots stopped / playing / paused / stopped
		std::vector<std::pair<std::string, Bytes>> snaps;
		m->send(mmCommand(0x57, {1}));
		m->run(200);
		snaps.emplace_back("stopped", m->snapshotRam());
		tap(*m, md::PanelControl::Play); m->run(700);
		snaps.emplace_back("playing", m->snapshotRam());
		m->run(900);
		snaps.emplace_back("playing2", m->snapshotRam());
		tap(*m, md::PanelControl::Play); m->run(500);
		snaps.emplace_back("paused", m->snapshotRam());
		tap(*m, md::PanelControl::Play); m->run(700);
		snaps.emplace_back("playing3", m->snapshotRam());
		tap(*m, md::PanelControl::Stop); m->run(500);
		snaps.emplace_back("stopped2", m->snapshotRam());
		tap(*m, md::PanelControl::Stop); m->run(500);
		snaps.emplace_back("stopped3", m->snapshotRam());
		int shown = 0;
		for(uint32_t a = 0; a < 0x100000 && shown < 30; ++a)
		{
			const auto p1 = snaps[1].second[a];
			if(snaps[2].second[a] != p1 || snaps[4].second[a] != p1) continue;
			const auto s0 = snaps[0].second[a];
			if(snaps[5].second[a] != s0 || snaps[6].second[a] != s0 || s0 == p1) continue;
			std::printf("  play-state candidate 0x%06x: stopped %02x playing %02x paused %02x\n", 0x200000 + a, s0, p1, snaps[3].second[a]);
			++shown;
		}
	}

	// Enumerated values: sweep a CC 0..127, group the values that draw the same
	// screen, save one LCD per group (read the names from the images).
	//   enums <outdir> <machine id> <data page 0-6> <param 0-7>...
	void enumsMode(const Bytes& _rom, const std::string& _dir, const int _machine, const int _page, const std::vector<int>& _params)
	{
		auto m = boot(_rom);
		m->send(mmCommand(0x5b, {0, static_cast<uint8_t>(_machine), 1}));
		m->run(200);
		for(int i = 0; i < _page; ++i) key(*m, md::PanelControl::DataPageForward, 20, 60);
		m->run(200);
		if(const char* pre = std::getenv("MM_ENUM_PRESET"))	// "cc=value", e.g. 88=0
		{
			int c = 0, v = 0;
			if(std::sscanf(pre, "%d=%d", &c, &v) == 2)
			{
				m->send({0xb0, static_cast<uint8_t>(c), static_cast<uint8_t>(v)});
				m->run(100);
			}
		}
		for(const int param : _params)
		{
			const auto cc = static_cast<uint8_t>(ed::mmParamCc(static_cast<uint8_t>(_page), static_cast<uint8_t>(param)));
			std::printf("machine %d page %d param %d (CC %d):", _machine, _page, param, cc);
			Lcd last{};
			int groupStart = 0;
			int group = 0;
			for(int v = 0; v <= 128; ++v)
			{
				Lcd now{};
				if(v <= 127)
				{
					m->send({0xb0, cc, static_cast<uint8_t>(v)});
					m->run(60);
					now = lcdBits(*m);
				}
				if(v == 0) { last = now; continue; }
				if(v == 128 || now != last)
				{
					std::printf(" [%d-%d]", groupStart, v - 1);
					// save the screen of this group
					m->send({0xb0, cc, static_cast<uint8_t>(groupStart)});
					m->run(60);
					char n[64];
					std::snprintf(n, sizeof(n), "/enum-m%d-p%d-i%d-g%02d.pgm", _machine, _page, param, group++);
					md::test::panelImage(m->hardware(), _dir + n);
					if(v <= 127) { m->send({0xb0, cc, static_cast<uint8_t>(v)}); m->run(60); }
					groupStart = v;
					last = now;
				}
			}
			std::printf("\n");
		}
	}

	// A RAM byte that is set only on SYSEX RECV (not on other GLOBAL EDIT screens).
	void recvFlag2Mode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		using C = md::PanelControl;
		std::vector<std::pair<std::string, Bytes>> in, out;
		const auto snapOut = [&](const char* _n) { out.emplace_back(_n, m->snapshotRam()); std::printf("  out %-10s screen %08x\n", _n, read32(*m, g_screenAddress)); };
		const auto snapIn = [&](const char* _n) { in.emplace_back(_n, m->snapshotRam()); std::printf("  in  %-10s screen %08x\n", _n, read32(*m, g_screenAddress)); };
		snapOut("main");
		chord(*m, C::Kit, 10, 30); m->run(100); snapOut("slots");
		key(*m, C::Enter, 10, 30); m->run(100); snapOut("edit");
		key(*m, C::Enter, 10, 30); m->run(100); snapOut("audio");
		key(*m, C::Exit, 10, 30); key(*m, C::Down, 10, 30); key(*m, C::Right, 10, 30); key(*m, C::Enter, 10, 30); m->run(100); snapOut("ctrl-item");
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		recvMacro(*m, 10, 10); m->run(100); snapIn("recv");
		key(*m, C::Exit, 10, 30); m->run(100); snapOut("file-list");
		key(*m, C::Up, 10, 30); key(*m, C::Enter, 10, 30); m->run(100); snapOut("sysex-send?");
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		recvMacro(*m, 10, 10); m->run(100);
		m->send(retarget(patternDump(*m, 1), 30)); m->run(200); snapIn("recv+msg");
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		tap(*m, C::Play); m->run(300);
		recvMacro(*m, 10, 10); m->run(100); snapIn("recv-play");
		for(int i = 0; i < 8; ++i) key(*m, C::Exit, 10, 10);
		m->run(100); snapOut("main-play");
		int shown = 0;
		for(uint32_t a = 0; a < 0x100000 && shown < 20; ++a)
		{
			const auto v = in[0].second[a];
			bool ok = true;
			for(const auto& x : in) ok &= x.second[a] == v;
			int differs = 0;
			for(const auto& x : out) differs += x.second[a] != v;
			if(!ok || differs < static_cast<int>(out.size()) - 1) continue;
			std::printf("  recv-only 0x%06x = %02x (out:", 0x200000 + a, v);
			for(const auto& x : out) std::printf(" %02x", x.second[a]);
			std::printf(")\n");
			++shown;
		}
	}

	// Is a pattern dump taken on a normal screen?
	void gateMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		const auto p0 = patternDump(*m, 0);
		require(!p0.empty(), "no pattern 0 dump");
		auto raw = rawPayload(p0);
		std::printf("pattern raw size %zu\n", raw.size());
		// Flip a byte deep inside (a candidate trig byte) and send to slot 5.
		auto edited = raw;
		for(size_t i = 0; i < 8; ++i)
			edited[i] ^= 0xff;
		const auto sent = retarget(encodeLiteral(p0, edited), 5);
		const auto before = rawPayload(patternDump(*m, 5));
		m->send(sent);
		m->run(300);
		const auto after = rawPayload(patternDump(*m, 5));
		std::printf("normal screen: slot5 before==after %d, after==sent %d\n", before == after, after == edited);
		check(before == after, "MM ignores a pattern dump on a normal screen (manual 1-99)");
		printLcd(*m, "after sending on normal screen");

		const auto tryRecv = [&](const char* _how, const std::function<void()>& _send, const uint8_t _slot)
		{
			const auto target = retarget(encodeLiteral(p0, edited), _slot);
			(void)target;
			md::test::enterMmReceive(m->hardware(), false);
			printLcd(*m, (std::string("SYSEX RECV before ") + _how).c_str());
			_send();
			m->run(1500);
			printLcd(*m, (std::string("after ") + _how).c_str());
			md::test::exitMenus(m->hardware());
			m->run(500);
			const auto back = rawPayload(patternDump(*m, _slot));
			std::printf("%s: stored==sent %d\n", _how, back == edited);
			return back == edited;
		};
		check(encodeRle(p0, rawPayload(p0)) == p0, "firmware RLE choice reproduced on pattern 0");
		const auto s6 = retarget(encodeRle(p0, edited), 6);
		const auto s7 = retarget(encodeRle(p0, edited), 7);
		const auto s8 = retarget(encodeRle(p0, edited), 8);
		{
			const auto p1 = patternDump(*m, 1);
			const auto s9 = retarget(p1, 9);
			md::test::enterMmReceive(m->hardware(), false);
			m->send(s9);
			m->run(1500);
			printLcd(*m, "after firmware-bytes dump");
			md::test::exitMenus(m->hardware());
			m->run(500);
			check(rawPayload(patternDump(*m, 9)) == rawPayload(p1), "RECV: firmware's own pattern 1 bytes stored to slot 9");
			const auto lit = retarget(encodeLiteral(p1, rawPayload(p1)), 10);
			md::test::enterMmReceive(m->hardware(), false);
			m->send(lit);
			m->run(1500);
			md::test::exitMenus(m->hardware());
			m->run(500);
			std::printf("literal (uncompressed) pattern dump: %zu bytes\n", lit.size());
			check(rawPayload(patternDump(*m, 10)) != rawPayload(p1), "RECV: an uncompressed pattern dump is dropped (the firmware's run-length form is needed)");
		}
		check(tryRecv("unpaced", [&] { m->send(s6); }, 6), "RECV: unpaced dump stored");
		check(tryRecv("paced transfer", [&] {
			uint64_t f = 0;
			const auto st = sendPaced(*m, s7, &f);
			std::printf("paced transfer state %u after %.1f ms\n", unsigned(st), ms(f));
		}, 7), "RECV: paced transfer stored");
		check(tryRecv("chunked 32B/10ms", [&] {
			std::printf("chunked took %.1f ms\n", ms(sendChunked(*m, s8, 32, 10.0)));
		}, 8), "RECV: chunked dump stored");
	}

	double rms(const std::vector<float>& _l, const std::vector<float>& _r, const uint64_t _from, const uint64_t _to)
	{
		double e = 0;
		const auto to = std::min<uint64_t>(_to, _l.size());
		if(to <= _from)
			return 0;
		for(auto i = _from; i < to; ++i)
			e += double(_l[i]) * _l[i] + double(_r[i]) * _r[i];
		return std::sqrt(e / double(2 * (to - _from)));
	}

	double rmsLast(Machine& _m, const double _ms)
	{
		const auto n = static_cast<uint64_t>(_ms * g_rate / 1000);
		return rms(_m.left(), _m.right(), _m.now() > n ? _m.now() - n : 0, _m.now());
	}

	// Candidate playhead bytes: sampled 4 times per 16th, a byte that steps by one
	// (or wraps to 0) once per step, in main RAM.
	std::vector<uint32_t> scanPlayhead(Machine& _m, const double _stepMs, const int _steps)
	{
		const int perStep = 4;
		std::vector<Bytes> snaps;
		for(int i = 0; i < _steps * perStep; ++i)
		{
			snaps.push_back(_m.snapshotRam());
			_m.run(_stepMs / perStep);
		}
		std::vector<uint32_t> found;
		for(uint32_t a = 0; a < 0x100000; ++a)
		{
			for(int phase = 0; phase < perStep; ++phase)
			{
				bool ok = true;
				int changes = 0;
				for(int k = phase; k + perStep < static_cast<int>(snaps.size()) && ok; k += perStep)
				{
					const int v0 = snaps[k][a], v1 = snaps[k + perStep][a];
					if(v1 == v0 + 1) ++changes;
					else if(v1 == 0 && v0 >= 1) ++changes;
					else ok = false;
				}
				if(ok && changes >= _steps - 2)
				{
					found.push_back(0x200000 + a);
					std::printf("  playhead candidate 0x%06x:", 0x200000 + a);
					for(int k = phase; k < static_cast<int>(snaps.size()); k += perStep)
						std::printf(" %d", snaps[k][a]);
					std::printf("\n");
					break;
				}
			}
		}
		return found;
	}

	void allChannelsCc(Machine& _m, const uint8_t _cc, const uint8_t _v)
	{
		for(uint8_t ch = 0; ch < 6; ++ch)
			_m.send({static_cast<uint8_t>(0xb0 | ch), _cc, _v});
	}

	// ---- chain: BANK held + TRIG keys (MM-PORT-PLAN f, manual 1-46) ----

	// Main RAM, internal SRAM and patch RAM, in one buffer (addrOf maps back).
	Bytes chainSnapshot(Machine& _m)
	{
		Bytes s;
		s.reserve(0x100000 + 0x10000 + 0x100000);
		for(uint32_t a = 0x200000; a < 0x300000; ++a) s.push_back(_m.read8(a));
		for(uint32_t a = 0x1000000; a < 0x1010000; ++a) s.push_back(_m.read8(a));
		for(uint32_t a = 0x700000; a < 0x800000; ++a) s.push_back(_m.read8(a));
		return s;
	}

	uint32_t addrOf(const size_t _i)
	{
		if(_i < 0x100000) return static_cast<uint32_t>(0x200000 + _i);
		if(_i < 0x110000) return static_cast<uint32_t>(0x1000000 + _i - 0x100000);
		return static_cast<uint32_t>(0x700000 + _i - 0x110000);
	}

	void rawKey(Machine& _m, const md::PanelPacket _p, const double _ms)
	{
		_m.hardware().trySendPanelEvent(_p.row, _p.mask);
		_m.run(_ms);
	}

	// Hold BANK (_bank 0-3), the TRIG keys pressed in order and held together (_hold) or each let go
	// after its press but the first (the manual's gesture), then release everything.
	void chainGesture(Machine& _m, const int _bank, const std::vector<int>& _trigs, const bool _holdAll)
	{
		const auto bank = *md::panelPacket(g_mm, static_cast<md::PanelControl>(static_cast<int>(md::PanelControl::BankA) + _bank));
		rawKey(_m, bank, 100);
		uint8_t rows[2] = {0, 0};
		for(size_t i = 0; i < _trigs.size(); ++i)
		{
			const int t = _trigs[i];
			rows[t >> 3] = static_cast<uint8_t>(rows[t >> 3] | (1u << (t & 7)));
			rawKey(_m, {static_cast<uint8_t>(0x20 + (t >> 3)), rows[t >> 3]}, 150);
			if(!_holdAll && i > 0)
			{
				rows[t >> 3] = static_cast<uint8_t>(rows[t >> 3] & ~(1u << (t & 7)));
				rawKey(_m, {static_cast<uint8_t>(0x20 + (t >> 3)), rows[t >> 3]}, 100);
			}
		}
		rawKey(_m, {0x20, 0}, 60);
		rawKey(_m, {0x21, 0}, 60);
		rawKey(_m, {bank.row, 0}, 100);
	}

	std::vector<int> wrapPatterns(Machine& _m, const int _wraps)
	{
		std::vector<int> seen;
		int last = _m.read8(0x257e57);
		for(double t = 0; static_cast<int>(seen.size()) < _wraps && t < 40000; t += 10)
		{
			_m.run(10);
			const int s = _m.read8(0x257e57);
			if(s < last)
			{
				_m.run(40);
				seen.push_back(status(_m, 0x04));
			}
			last = s;
		}
		return seen;
	}

	void printPatterns(const char* _what, const std::vector<int>& _v)
	{
		std::printf("  %s:", _what);
		for(const int p : _v)
			std::printf(" %c%02d", p < 0 ? '?' : 'A' + p / 16, p < 0 ? 0 : p % 16 + 1);
		std::printf("\n");
	}

	void dumpRegion(Machine& _m, const uint32_t _from, const uint32_t _to, const char* _what)
	{
		std::printf("  %-26s", _what);
		for(uint32_t a = _from; a < _to; ++a)
			std::printf("%s%02x", (a & 7) == 0 ? " " : "", _m.read8(a));
		std::printf("\n");
	}

	void chainMode(const Bytes& _rom, const std::string& _region)
	{
		std::puts("== MM chain probe");
		auto m = boot(_rom);
		using C = md::PanelControl;
		std::printf("  pattern %d, song mode %d\n", status(*m, 0x04), status(*m, 0x10));
		// 1. BANK + TRIG selects (stopped): A05.
		{
			const auto bank = *md::panelPacket(g_mm, C::BankA);
			const auto trig = *md::panelPacket(g_mm, C::Trigger5);
			md::PanelRowState rows;
			for(const auto pk : {rows.press(bank), rows.press(trig), rows.release(trig), rows.release(bank)})
				rawKey(*m, pk, 80);
			m->run(200);
			check(status(*m, 0x04) == 4, "BANK A + TRIG 5 selects A05 (stopped)");
		}
		// 2. BANK GROUP: what toggles in RAM.
		{
			const auto s0 = chainSnapshot(*m);
			m->run(300);
			const auto s0b = chainSnapshot(*m);
			key(*m, C::BankGroup, 60, 300);
			const auto s1 = chainSnapshot(*m);
			key(*m, C::BankGroup, 60, 300);
			const auto s2 = chainSnapshot(*m);
			key(*m, C::BankGroup, 60, 300);
			const auto s3 = chainSnapshot(*m);
			std::printf("  BANK GROUP toggles:");
			int n = 0;
			for(size_t i = 0; i < s0.size(); ++i)
				if(s0[i] == s0b[i] && s0[i] == s2[i] && s1[i] == s3[i] && s1[i] != s0[i] && n++ < 40)
					std::printf(" %06x:%02x/%02x", addrOf(i), s0[i], s1[i]);
			std::printf(" (%d)\n", n);
			// Now in E-H: BANK A/E + TRIG 1 selects E01?
			const auto bank = *md::panelPacket(g_mm, C::BankA);
			const auto trig = *md::panelPacket(g_mm, C::Trigger1);
			md::PanelRowState rows;
			for(const auto pk : {rows.press(bank), rows.press(trig), rows.release(trig), rows.release(bank)})
				rawKey(*m, pk, 80);
			m->run(200);
			std::printf("  group toggled 3 times, BANK A/E + TRIG 1: pattern %d\n", status(*m, 0x04));
			const auto t0 = chainSnapshot(*m);
			std::printf("  after selecting E01, the toggling bytes:");
			n = 0;
			for(size_t i = 0; i < s0.size(); ++i)
				if(s0[i] == s0b[i] && s0[i] == s2[i] && s1[i] == s3[i] && s1[i] != s0[i] && n++ < 40)
					std::printf(" %06x:%02x", addrOf(i), t0[i]);
			std::printf("\n");
			// A SysEx LOAD PATTERN A01: does the group follow?
			m->send(mmRequest(0x57, 0));
			m->run(300);
			const auto t1 = chainSnapshot(*m);
			std::printf("  after SysEx LOAD A01 (pattern %d):", status(*m, 0x04));
			n = 0;
			for(size_t i = 0; i < s0.size(); ++i)
				if(s0[i] == s0b[i] && s0[i] == s2[i] && s1[i] == s3[i] && s1[i] != s0[i] && n++ < 40)
					std::printf(" %06x:%02x", addrOf(i), t1[i]);
			std::printf("\n");
			key(*m, C::BankGroup, 60, 300);	// back to A-D
		}
		// 3. Chains while playing: where is the list?
		key(*m, C::Play, 60, 400);
		const std::vector<std::vector<int>> chains = {{2, 4, 1}, {6, 3}, {9, 12, 0, 5}, {14, 2, 7, 1, 11}};
		std::vector<Bytes> snaps;
		const auto before = chainSnapshot(*m);
		for(const auto& c : chains)
		{
			chainGesture(*m, 0, c, true);
			m->run(100);
			snaps.push_back(chainSnapshot(*m));
			std::printf("  chain of %zu: pattern %d\n", c.size(), status(*m, 0x04));
		}
		for(const int k : {0, 1, 0x80})
		{
			for(const size_t stride : {size_t(1), size_t(2), size_t(4)})
			{
				std::map<size_t, int> hits;
				for(size_t i = 0; i < chains.size(); ++i)
				{
					const auto& c = chains[i];
					const auto& s = snaps[i];
					for(size_t b = 0; b + stride * 16 < s.size(); ++b)
					{
						bool ok = true;
						for(size_t j = 0; j < c.size() && ok; ++j)
							ok = s[b + j * stride + (stride - 1)] == static_cast<uint8_t>(c[j] + k);
						if(ok) ++hits[b];
					}
				}
				for(const auto& [a, n] : hits)
					if(n >= 3)
						std::printf("  list (stride %zu, +%d) in %d of 4 chains at 0x%06x\n", stride, k, n, addrOf(a));
			}
		}
		std::printf("  bytes changed with every chain:");
		{
			size_t shown = 0;
			for(size_t a = 0; a < before.size() && shown < 80; ++a)
			{
				bool all = before[a] != snaps[0][a];
				for(size_t i = 1; i < snaps.size() && all; ++i)
					all = snaps[i][a] != snaps[i - 1][a];
				if(!all)
					continue;
				++shown;
				std::printf(" %06x:%02x", addrOf(a), before[a]);
				for(const auto& sn : snaps)
					std::printf(">%02x", sn[a]);
			}
		}
		std::printf("\n");
		if(_region.empty())
			return;
		// 4. A region around the list, through the chain's life (address from step 3, given as hex).
		const uint32_t from = static_cast<uint32_t>(std::stoul(_region, nullptr, 16)) & ~7u;
		const uint32_t to = from + 0x60;
		auto r = boot(_rom);
		dumpRegion(*r, from, to, "idle A01");
		key(*r, C::Play, 60, 400);
		dumpRegion(*r, from, to, "playing A01");
		chainGesture(*r, 0, {2, 4, 1}, true);
		dumpRegion(*r, from, to, "chain A03 A05 A02 (held)");
		printPatterns("wraps", wrapPatterns(*r, 5));
		dumpRegion(*r, from, to, "after 5 wraps");
		chainGesture(*r, 0, {6, 3, 8}, false);
		dumpRegion(*r, from, to, "chain A07 A04 A09 (manual)");
		printPatterns("wraps", wrapPatterns(*r, 4));
		chainGesture(*r, 0, {6, 3}, true);
		dumpRegion(*r, from, to, "chain A07 A04 (again)");
		chainGesture(*r, 0, {5, 9}, true);
		dumpRegion(*r, from, to, "chain A06 A10 (right after)");
		printPatterns("wraps", wrapPatterns(*r, 3));
		r->send(mmRequest(0x57, 11));
		r->run(100);
		dumpRegion(*r, from, to, "SysEx LOAD A12");
		printPatterns("wraps", wrapPatterns(*r, 2));
		chainGesture(*r, 0, {0, 1}, true);
		dumpRegion(*r, from, to, "chain A01 A02");
		key(*r, C::Stop, 60, 300);
		dumpRegion(*r, from, to, "STOP once");
		std::printf("  pattern %d\n", status(*r, 0x04));
		key(*r, C::Stop, 60, 300);
		dumpRegion(*r, from, to, "STOP twice");
		std::printf("  pattern %d\n", status(*r, 0x04));
		key(*r, C::Play, 60, 300);
		printPatterns("PLAY after STOP twice, wraps", wrapPatterns(*r, 3));
		chainGesture(*r, 0, {3, 4}, true);
		dumpRegion(*r, from, to, "chain A04 A05");
		chainGesture(*r, 0, {7}, true);
		dumpRegion(*r, from, to, "one key A08");
		printPatterns("wraps", wrapPatterns(*r, 2));
		key(*r, C::Stop, 60, 300);
		key(*r, C::Stop, 60, 300);
		// From stopped.
		chainGesture(*r, 1, {0, 2}, true);
		dumpRegion(*r, from, to, "stopped: chain B01 B03");
		std::printf("  pattern %d\n", status(*r, 0x04));
		key(*r, C::Play, 60, 300);
		printPatterns("PLAY, wraps", wrapPatterns(*r, 3));
		key(*r, C::Stop, 60, 300);
		key(*r, C::Stop, 60, 300);
		// Song mode: SET STATUS song mode 1, then a chain.
		r->send(mmCommand(0x71, {0x10, 0x01}));
		r->run(200);
		std::printf("  song mode %d\n", status(*r, 0x10));
		key(*r, C::Play, 60, 300);
		chainGesture(*r, 0, {2, 4}, true);
		dumpRegion(*r, from, to, "song mode: chain A03 A05");
		std::printf("  song mode %d\n", status(*r, 0x10));
		printPatterns("wraps", wrapPatterns(*r, 3));
		printLcd(*r, "LCD in song mode after the chain");
	}

	void recvMode(const Bytes& _rom)
	{
		auto m = boot(_rom);
		// Outside RECV: LOAD PATTERN and SET TEMPO.
		m->send(mmCommand(0x57, {1}));
		m->run(200);
		check(status(*m, 0x04) == 1, "0x57 LOAD PATTERN works outside SYSEX RECV (status reports pattern 1)");
		m->send(mmCommand(0x61, {static_cast<uint8_t>((120 * 24) >> 7), static_cast<uint8_t>((120 * 24) & 0x7f)}));
		m->run(100);
		printLcd(*m, "pattern 1 loaded");
		tap(*m, md::PanelControl::Play);
		m->run(1500);
		const double base = rmsLast(*m, 1000);
		std::printf("playing pattern 1: rms %.4f\n", base);
		check(base > 0.001, "pattern 1 plays (audible)");
		const auto heads = scanPlayhead(*m, 125.0, 24);
		check(!heads.empty(), "a RAM playhead byte found");

		// CC live edit on a normal screen: level 0 on all tracks.
		allChannelsCc(*m, 7, 0);
		m->run(600);
		const double muted = rmsLast(*m, 400);
		allChannelsCc(*m, 7, 100);
		m->run(600);
		const double back = rmsLast(*m, 400);
		std::printf("CC7 level 0: rms %.5f, back to 100: %.4f\n", muted, back);
		check(muted < base * 0.1 && back > base * 0.3, "CC level edits are live on a normal screen");

		// Enter SYSEX RECV while playing: does the sequencer keep running?
		const auto head = heads.empty() ? 0u : heads.front();
		const auto t0 = m->now();
		std::vector<std::pair<uint64_t, int>> trace;
		m->onBlock = [&] { if(head && (m->now() % 2048) == 0) trace.emplace_back(m->now(), m->read8(head)); };
		md::test::enterMmReceive(m->hardware(), false);
		// enterMmReceive advances the hardware directly; render some audio through the session.
		m->run(1000);
		const double inRecv = rmsLast(*m, 800);
		std::printf("macro to RECV: %.0f ms machine time (hardware advance, not rendered); rms in RECV %.4f\n", ms(m->now() - t0), inRecv);
		check(inRecv > base * 0.3, "the pattern keeps playing on the SYSEX RECV screen");
		int moves = 0;
		for(size_t i = 1; i < trace.size(); ++i) moves += trace[i].second != trace[i - 1].second;
		std::printf("playhead moves while in RECV: %d\n", moves);
		check(moves > 4, "the playhead moves on the SYSEX RECV screen");

		// CC in RECV
		allChannelsCc(*m, 7, 0);
		m->run(600);
		const double mutedRecv = rmsLast(*m, 400);
		allChannelsCc(*m, 7, 100);
		m->run(600);
		std::printf("CC7 level 0 in RECV: rms %.5f\n", mutedRecv);
		check(mutedRecv < base * 0.1, "CC level edits are live on the SYSEX RECV screen");

		// Pattern dump over the PLAYING pattern (slot 1) with pattern 0's content (empty).
		const auto empty = retarget(patternDump(*m, 75), 1);	// factory slot 75 is empty
		const auto p1 = patternDump(*m, 1);
		std::printf("pattern requests answered in RECV: %d\n", !p1.empty());
		check(!p1.empty(), "dump requests are answered on the SYSEX RECV screen");
		const auto tSend = m->now();
		m->send(empty);
		for(int w = 0; w < 8; ++w)
		{
			m->run(250);
			std::printf("  +%4.0f ms rms %.5f step %d\n", ms(m->now() - tSend), rmsLast(*m, 250), head ? m->read8(head) : -1);
		}
		const double afterEmpty = rmsLast(*m, 800);
		check(rawPayload(patternDump(*m, 1)) == rawPayload(empty), "slot 1 reads back as the empty pattern");
		std::printf("playing pattern replaced by an empty one: rms %.5f (%.0f ms after)\n", afterEmpty, ms(m->now() - tSend));
		check(afterEmpty < base * 0.2, "a dump over the playing pattern is heard at once (no reload)");
		m->send(p1);
		m->run(1500);
		const double restored = rmsLast(*m, 800);
		std::printf("restored: rms %.4f\n", restored);
		check(restored > base * 0.3, "sending the original back restores it, still playing");
		printLcd(*m, "RECV after the dumps");

		// Leave RECV
		md::test::exitMenus(m->hardware());
		m->run(1000);
		const double after = rmsLast(*m, 800);
		check(after > base * 0.3, "still playing after leaving SYSEX RECV");
		printLcd(*m, "after EXIT");
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: mmEditorProbeFirmwareTest <ROM> <all|lcd|dumps|corpus|gate|recv|commands|kitsong|burst|macro|queue|screens|...> [outdir]");
		return 77;
	}
	try
	{
		g_romName = _argv[1];
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MM 1.32B image");
		const std::string mode = _argv[2];
		const std::string dir = _argc > 3 ? _argv[3] : "";
		if(mode == "lcd") lcdMode(rom);
		else if(mode == "dumps") dumpsMode(rom, dir);
		else if(mode == "gate") gateMode(rom);
		else if(mode == "recv") recvMode(rom);
		else if(mode == "macro") macroMode(rom);
		else if(mode == "recvflag") recvFlagMode(rom);
		else if(mode == "kitsong") kitSongMode(rom);
		else if(mode == "recvcount") recvCountMode(rom);
		else if(mode == "burst") burstMode(rom);
		else if(mode == "workkit") workKitMode(rom);
		else if(mode == "queue") queueMode(rom);
		else if(mode == "boot") bootMode(rom);
		else if(mode == "screens") screensMode(rom);
		else if(mode == "lab") labMode(rom, dir);
		else if(mode == "program") programMode(rom, dir);
		else if(mode == "statebytes") stateBytesMode(rom);
		else if(mode == "recvflag2") recvFlag2Mode(rom);
		else if(mode == "chain") chainMode(rom, dir);
		else if(mode == "enums")
		{
			require(_argc >= 7, "enums <outdir> <machine> <page> <param>...");
			std::vector<int> params;
			for(int i = 6; i < _argc; ++i) params.push_back(std::atoi(_argv[i]));
			enumsMode(rom, dir, std::atoi(_argv[4]), std::atoi(_argv[5]), params);
		}
		else if(mode == "all")
		{
			gateMode(rom);
			recvMode(rom);
			commandsMode(rom, false);
			commandsMode(rom, true);
			kitSongMode(rom);
			burstMode(rom);
			queueMode(rom);
			screensMode(rom);
		}
		else if(mode == "commands") { commandsMode(rom, false); commandsMode(rom, true); }
		else if(mode == "corpus") corpusMode(rom, dir, _argc > 4 ? load(_argv[4]) : Bytes{});
		else require(false, "unknown mode " + mode);
		std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
		return g_failures ? 1 : 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mmEditorProbeFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
