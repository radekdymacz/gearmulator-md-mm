// P1 data layer against MD OS 1.63 firmware (manual: needs a user-supplied ROM).
//
//   mdDataLayerFirmwareTest corpus <ROM> <out-dir> [device-state.mdst]
//       Dump every global, kit, pattern and song; then program a varied corpus
//       through the codec (64-step patterns with full lock pools, random kits,
//       songs with loops/jumps/halts up to 256 rows), read each back, report
//       what the firmware normalised, and write all dumps to <out-dir>.
//   mdDataLayerFirmwareTest probe <ROM>
//       Behaviour while playing: pattern select/queue, kit-per-pattern link and
//       unsaved kit edits, kit/song push timing and audio continuity, song
//       mutes, tempo multipliers, firmware normalisation of edge values.
//
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"

#include "mdDataLink/mdDataLink.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdKit.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdSong.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	std::string g_romName;

	// ---- small helpers ----

	uint32_t nextRandom(uint32_t& _s)
	{
		_s ^= _s << 13;
		_s ^= _s >> 17;
		_s ^= _s << 5;
		return _s;
	}

	std::optional<ed::MdPattern> readPattern(Machine& _m, const uint8_t _slot, uint64_t* _frames = nullptr)
	{
		return ed::decodeMdPattern(_m.request(ed::mdPatternRequest(_slot), ed::g_mdPatternDump, _frames));
	}

	std::optional<ed::MdKit> readKit(Machine& _m, const uint8_t _slot, uint64_t* _frames = nullptr)
	{
		return ed::decodeMdKit(_m.request(ed::mdKitRequest(_slot), ed::g_mdKitDump, _frames));
	}

	std::optional<ed::MdSong> readSong(Machine& _m, const uint8_t _slot, uint64_t* _frames = nullptr)
	{
		return ed::decodeMdSong(_m.request(ed::mdSongRequest(_slot), ed::g_mdSongDump, _frames));
	}

	std::optional<ed::MdGlobal> readGlobal(Machine& _m, const uint8_t _slot)
	{
		return ed::decodeMdGlobal(_m.request(ed::mdGlobalRequest(_slot), ed::g_mdGlobalDump));
	}

	int status(Machine& _m, const ed::MdStatus _param)
	{
		const auto r = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(_param), 0x72));
		return r ? r->value : -1;
	}

	double ms(const uint64_t _frames) { return _frames * 1000.0 / g_rate; }

	float maxJump(const std::vector<float>& _x, const size_t _begin, const size_t _end)
	{
		float j = 0;
		for(size_t i = std::max<size_t>(_begin, 1); i < std::min(_end, _x.size()); ++i)
			j = std::max(j, std::abs(_x[i] - _x[i - 1]));
		return j;
	}

	float rms(const std::vector<float>& _x, const size_t _begin, const size_t _end)
	{
		double s = 0;
		size_t n = 0;
		for(size_t i = _begin; i < std::min(_end, _x.size()); ++i, ++n)
			s += _x[i] * _x[i];
		return n ? static_cast<float>(std::sqrt(s / n)) : 0.f;
	}

	// Longest run of exact digital silence, a dropout detector.
	size_t longestSilence(const std::vector<float>& _x, const size_t _begin, const size_t _end)
	{
		size_t best = 0, run = 0;
		for(size_t i = _begin; i < std::min(_end, _x.size()); ++i)
		{
			run = _x[i] == 0.f ? run + 1 : 0;
			best = std::max(best, run);
		}
		return best;
	}

	size_t firstDifference(const std::vector<float>& _a, const std::vector<float>& _b, const size_t _from = 0)
	{
		for(size_t i = _from; i < std::min(_a.size(), _b.size()); ++i)
			if(_a[i] != _b[i])
				return i;
		return SIZE_MAX;
	}

	// ---- random but valid content ----

	std::vector<uint32_t> knownMachines()
	{
		std::vector<uint32_t> list;
		for(uint32_t m = 0; m < 256; ++m)
			if(ed::isKnownMdMachine(m))
				list.push_back(m);
		return list;
	}

	ed::MdPattern randomPattern(uint32_t& _seed, const uint8_t _slot, const size_t _lockCount)
	{
		ed::MdPattern p;
		p.position = _slot;
		p.scale = static_cast<uint8_t>(nextRandom(_seed) % 4);
		const auto total = 16u * (p.scale + 1u);
		p.length = static_cast<uint8_t>(2 + nextRandom(_seed) % (total - 1));
		p.tempoMultiplier = static_cast<uint8_t>(nextRandom(_seed) % 4);
		p.kit = static_cast<uint8_t>(nextRandom(_seed) % 64);
		p.accentAmount = ed::accentAmountFromDisplay(static_cast<int>(nextRandom(_seed) % 16));
		p.swingAmount = ed::swingAmountFromPercent(50 + static_cast<int>(nextRandom(_seed) % 31));
		const auto word = [&] { return (uint64_t(nextRandom(_seed)) << 32) | nextRandom(_seed); };
		const uint64_t mask = total == 64 ? ~uint64_t{0} : (uint64_t{1} << total) - 1;
		for(size_t t = 0; t < ed::MdPattern::g_tracks; ++t)
		{
			p.trigs[t] = word() & word() & mask;
			p.trackAccent[t] = word() & word() & mask;
			p.trackSlide[t] = word() & word() & word() & mask;
			p.trackSwing[t] = word() & mask;
		}
		p.accentPattern = word() & mask;
		p.slidePattern = word() & word() & mask;
		p.swingPattern = 0xaaaaaaaaaaaaaaaaull & mask;
		p.accentEditAll = nextRandom(_seed) & 1;
		p.slideEditAll = nextRandom(_seed) & 1;
		p.swingEditAll = nextRandom(_seed) & 1;
		while(ed::usedLockRows(p) < _lockCount)
		{
			const auto track = nextRandom(_seed) % 16;
			const auto param = nextRandom(_seed) % 24;
			for(int k = 0; k < 6; ++k)
			{
				const auto step = nextRandom(_seed) % total;
				if(auto q = ed::withLock(p, track, param, step, static_cast<uint8_t>(nextRandom(_seed) & 0x7f)))
					p = *q;
			}
		}
		return p;
	}

	ed::MdKit randomKit(uint32_t& _seed, const uint8_t _slot, const ed::MdKit& _base)
	{
		static const auto machines = knownMachines();
		auto k = _base;
		k.position = _slot;
		k.name.fill(0);
		const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
		const auto len = 1 + nextRandom(_seed) % 16;
		for(size_t i = 0; i < len; ++i)
			k.name[i] = static_cast<uint8_t>(letters[nextRandom(_seed) % 38]);
		for(size_t t = 0; t < ed::MdKit::g_tracks; ++t)
		{
			k.models[t] = machines[nextRandom(_seed) % machines.size()];
			for(auto& p : k.params[t])
				p = static_cast<uint8_t>(nextRandom(_seed) & 0x7f);
			k.levels[t] = static_cast<uint8_t>(nextRandom(_seed) & 0x7f);
			auto& l = k.lfos[t];
			l.track = static_cast<uint8_t>(nextRandom(_seed) % 16);
			l.param = static_cast<uint8_t>(nextRandom(_seed) % 24);
			l.shape1 = static_cast<uint8_t>(nextRandom(_seed) % 6);
			l.shape2 = static_cast<uint8_t>(nextRandom(_seed) % 6);
			l.update = static_cast<uint8_t>(nextRandom(_seed) % 3);
			const auto group = [&] { return static_cast<uint8_t>(nextRandom(_seed) % 16); };
			k.trigGroups[t] = nextRandom(_seed) % 3 ? ed::MdKit::g_noGroup : group();
			k.muteGroups[t] = nextRandom(_seed) % 3 ? ed::MdKit::g_noGroup : group();
		}
		for(auto& fx : k.masterFx)
			for(auto& p : fx)
				p = static_cast<uint8_t>(nextRandom(_seed) & 0x7f);
		return k;
	}

	ed::MdSong randomSong(uint32_t& _seed, const uint8_t _slot, const size_t _rows)
	{
		ed::MdSong s;
		s.position = _slot;
		const char* name = "P1 CORPUS";
		for(size_t i = 0; name[i]; ++i)
			s.name[i] = static_cast<uint8_t>(name[i]);
		s.rows.clear();
		for(size_t i = 0; i + 1 < _rows; ++i)
		{
			ed::MdSongRow r;
			const auto kind = nextRandom(_seed) % 10;
			if(kind == 0 && i > 0)
			{
				r.pattern = ed::MdSongRow::g_loopRow;
				r.target = static_cast<uint8_t>(nextRandom(_seed) % i);
				r.repeats = static_cast<uint8_t>(nextRandom(_seed) % 8);
				r.end = 0;
			}
			else if(kind == 1 && i + 2 < _rows)
			{
				r.pattern = ed::MdSongRow::g_loopRow;
				r.target = static_cast<uint8_t>(i + 1 + nextRandom(_seed) % (_rows - 2 - i));
				r.end = 0;
			}
			else if(kind == 2)
			{
				r.pattern = ed::MdSongRow::g_loopRow;
				r.target = static_cast<uint8_t>(i);
				r.end = 0;
			}
			else
			{
				r.pattern = static_cast<uint8_t>(nextRandom(_seed) % 128);
				r.repeats = static_cast<uint8_t>(nextRandom(_seed) % 64);
				r.end = static_cast<uint8_t>(2 + nextRandom(_seed) % 63);
				r.start = static_cast<uint8_t>(nextRandom(_seed) % r.end);
				r.tempo = nextRandom(_seed) % 3 ? ed::MdSongRow::g_noTempo
					: static_cast<uint16_t>(30 * 24 + nextRandom(_seed) % (270 * 24));
				r.mutes = static_cast<uint16_t>(nextRandom(_seed) & nextRandom(_seed));
			}
			s.rows.push_back(r);
		}
		s.rows.push_back(ed::MdSongRow{});
		return s;
	}

	// ---- corpus ----

	template<typename T>
	void noteNormalisation(std::map<std::string, int>& _report, const char* _type, const T& _sent,
		const std::optional<T>& _back)
	{
		if(!_back)
			++_report[std::string(_type) + ": no read-back"];
		else if(!(*_back == _sent))
			++_report[std::string(_type) + ": read-back differs from sent"];
		else
			++_report[std::string(_type) + ": read-back identical"];
	}

	int corpus(const Bytes& _rom, const std::string& _outDir, const Bytes& _patchRam)
	{
		const auto t0 = std::chrono::steady_clock::now();
		Machine m(_rom, g_romName, _patchRam);
		// Raw firmware replies are what the corpus stores; nothing is re-encoded.
		const auto dump = [&](const char* _type, const Bytes& _request, const uint8_t _reply, const unsigned _slot)
		{
			const auto raw = m.request(_request, _reply);
			if(!raw.empty())
				save(_outDir + "/" + _type + "_" + std::to_string(_slot) + ".syx", raw);
			return raw;
		};
		size_t dumped = 0;
		std::vector<ed::MdKit> kits;
		for(uint8_t i = 0; i < 8; ++i)
			dumped += !dump("state/global", ed::mdGlobalRequest(i), ed::g_mdGlobalDump, i).empty();
		for(uint8_t i = 0; i < 64; ++i)
			if(auto k = ed::decodeMdKit(dump("state/kit", ed::mdKitRequest(i), ed::g_mdKitDump, i)))
				kits.push_back(*k), ++dumped;
		for(uint8_t i = 0; i < 128; ++i)
			dumped += !dump("state/pattern", ed::mdPatternRequest(i), ed::g_mdPatternDump, i).empty();
		for(uint8_t i = 0; i < 32; ++i)
			dumped += !dump("state/song", ed::mdSongRequest(i), ed::g_mdSongDump, i).empty();
		std::printf("state: %zu of 232 dumps\n", dumped);
		require(!kits.empty(), "no kits");

		// Programmed corpus: our own content, pushed through the codec.
		std::map<std::string, int> report;
		uint32_t seed = 0x5eed1234u;
		for(uint8_t slot = 0; slot < 128; ++slot)
		{
			const size_t locks = slot % 4 == 0 ? 64 : slot % 4 == 1 ? 0 : 1 + nextRandom(seed) % 40;
			const auto p = randomPattern(seed, slot, locks);
			require(ed::validate(p).empty(), "generated pattern does not validate");
			m.send(ed::encodeMdPattern(p));
			const auto back = ed::decodeMdPattern(dump("programmed/pattern", ed::mdPatternRequest(slot),
				ed::g_mdPatternDump, slot));
			noteNormalisation(report, "pattern", p, back);
		}
		for(uint8_t slot = 0; slot < 64; ++slot)
		{
			const auto k = randomKit(seed, slot, kits[slot % kits.size()]);
			require(ed::validate(k).empty(), "generated kit does not validate");
			m.send(ed::encodeMdKit(k));
			const auto back = ed::decodeMdKit(dump("programmed/kit", ed::mdKitRequest(slot), ed::g_mdKitDump, slot));
			noteNormalisation(report, "kit", k, back);
		}
		for(uint8_t slot = 0; slot < 32; ++slot)
		{
			const size_t rows = slot == 0 ? 256 : slot == 1 ? 1 : 2 + nextRandom(seed) % 80;
			const auto s = randomSong(seed, slot, rows);
			require(ed::validate(s).empty(), "generated song does not validate");
			m.send(ed::encodeMdSong(s));
			const auto back = ed::decodeMdSong(dump("programmed/song", ed::mdSongRequest(slot), ed::g_mdSongDump,
				slot));
			noteNormalisation(report, "song", s, back);
		}
		for(uint8_t slot = 0; slot < 8; ++slot)
		{
			auto g = *readGlobal(m, slot);
			for(auto& r : g.routing)
				r = static_cast<uint8_t>(nextRandom(seed) % 7);
			g.tempo = static_cast<uint16_t>(60 * 24 + nextRandom(seed) % (180 * 24));
			m.send(ed::encodeMdGlobal(g));
			const auto back = ed::decodeMdGlobal(dump("programmed/global", ed::mdGlobalRequest(slot),
				ed::g_mdGlobalDump, slot));
			noteNormalisation(report, "global", g, back);
		}
		for(const auto& [what, n] : report)
			std::printf("programmed %s: %d\n", what.c_str(), n);
		std::printf("corpus wall time %.1f s\n",
			std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		return 0;
	}
}

namespace probes
{
	void kitLink(const Bytes& _rom);
	void session(const Bytes& _rom);
	void persistence(const Bytes& _rom);
	void queueRam(const Bytes& _rom);
	void patternQueue(const Bytes& _rom);
	void kitPush(const Bytes& _rom);
	void songPush(const Bytes& _rom);
	void songMutesAndTempo(const Bytes& _rom);
	void normalisation(const Bytes& _rom);
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: mdDataLayerFirmwareTest corpus <ROM> <out-dir> [state.mdst] | probe <ROM> [name]");
		return 77;
	}
	try
	{
		const std::string mode = _argv[1];
		g_romName = _argv[2];
		const auto rom = load(_argv[2]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		if(mode == "corpus" && _argc >= 4)
			return corpus(rom, _argv[3], _argc > 4 ? patchRamFromState(_argv[4], rom) : Bytes{});
		if(mode == "probe")
		{
			const std::string only = _argc > 3 ? _argv[3] : "";
			if(only.empty() || only == "persist")
				probes::persistence(rom);
			if(only.empty() || only == "session")
				probes::session(rom);
			if(only.empty() || only == "link")
				probes::kitLink(rom);
			if(only.empty() || only == "queueram")
				probes::queueRam(rom);
			if(only.empty() || only == "queue")
				probes::patternQueue(rom);
			if(only.empty() || only == "kit")
				probes::kitPush(rom);
			if(only.empty() || only == "song")
				probes::songPush(rom);
			if(only.empty() || only == "mutes")
				probes::songMutesAndTempo(rom);
			if(only.empty() || only == "normalise")
				probes::normalisation(rom);
			return 0;
		}
		std::puts("unknown mode");
		return 2;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdDataLayerFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}

namespace probes
{
	// Select another pattern while playing: when does it take over, what does
	// status report meanwhile, and does the linked kit load (EXTENDED) and
	// discard unsaved working-kit edits?
	void patternQueue(const Bytes& _rom)
	{
		std::puts("== probe: pattern select while playing, kit-per-pattern link");
		Machine m(_rom, g_romName);
		// Pattern 1 (A02) linked to kit 5, 16 steps, one kick per beat; A01 is linked to kit 0.
		auto a01 = *readPattern(m, 0);
		auto a02 = a01;
		a02.position = 1;
		a02.kit = 5;
		a02.length = 16;
		a02.scale = 0;
		m.send(ed::encodeMdPattern(a02));
		std::printf("A01: length %u kit %u; A02: length %u kit %u\n", a01.length, a01.kit, a02.length, a02.kit);

		m.panel(md::PanelControl::Play);
		m.run(1000);
		const auto selectAt = m.now();
		const auto stepAtSelect = m.playhead();
		m.send(ed::mdLoadPattern(1));
		// Watch status and the playhead until A02 is really playing.
		int lastStatus = -2, lastKit = -2;
		uint8_t lastStep = m.playhead();
		uint64_t wrapAt = 0;
		const auto ramBefore = m.snapshotRam();
		std::vector<uint32_t> candidates;
		bool scanned = false;
		for(int i = 0; i < 300 && !wrapAt; ++i)
		{
			const int st = status(m, ed::MdStatus::Pattern);
			const int kit = status(m, ed::MdStatus::Kit);
			const auto step = m.playhead();
			if(st != lastStatus || kit != lastKit)
				std::printf("  +%6.1f ms step %2u: status pattern=%d kit=%d\n", ms(m.now() - selectAt), step, st, kit);
			if(!scanned && step > stepAtSelect + 2)
			{
				// Queued but not yet playing: which RAM bytes now hold 1?
				const auto ram = m.snapshotRam();
				for(uint32_t a = 0; a < ram.size(); ++a)
					if(ram[a] == 1 && ramBefore[a] != 1)
						candidates.push_back(0x200000 + a);
				scanned = true;
			}
			if(step < lastStep)
				wrapAt = m.now();
			lastStatus = st;
			lastKit = kit;
			lastStep = step;
			m.run(20);
		}
		std::printf("select sent at step %u of A01 (length %u); pattern wrapped %.1f ms later\n", stepAtSelect,
			a01.length, ms(wrapAt - selectAt));
		std::printf("RAM bytes that became 1 while A02 was queued: %zu", candidates.size());
		for(size_t i = 0; i < candidates.size() && i < 12; ++i)
			std::printf(" 0x%06x", candidates[i]);
		std::printf("\n");
		// Which of them still read 1 now that A02 plays (current) vs back to 0?
		m.run(200);
		for(size_t i = 0; i < candidates.size() && i < 12; ++i)
			std::printf("  0x%06x now %u\n", candidates[i], m.read8(candidates[i]));

		m.run(500);
		std::printf("after the switch: status pattern=%d kit=%d (unsaved-edit loss: see the link probe)\n",
			status(m, ed::MdStatus::Pattern), status(m, ed::MdStatus::Kit));
		m.send(ed::mdLoadPattern(0));
		m.run(2500);

		// CLASSIC mode: no kit link.
		m.send(ed::mdSetStatus(ed::MdStatus::LockMode, 0));
		m.send(ed::mdLoadPattern(1));
		m.run(4500);
		std::printf("CLASSIC: after selecting A02 status pattern=%d kit=%d\n", status(m, ed::MdStatus::Pattern),
			status(m, ed::MdStatus::Kit));
		m.send(ed::mdSetStatus(ed::MdStatus::LockMode, 1));

		// Stopped: selection is immediate?
		m.panel(md::PanelControl::Stop);
		m.run(200);
		const auto t = m.now();
		m.send(ed::mdLoadPattern(0));
		std::printf("stopped: LOAD PATTERN 0 -> status pattern=%d after %.1f ms\n", status(m, ed::MdStatus::Pattern),
			ms(m.now() - t));
	}

	struct KitRun
	{
		std::vector<float> left;
		std::vector<std::pair<uint64_t, uint8_t>> steps;
		uint64_t pushAt = 0, drained = 0, loadAt = 0, loadDrained = 0, replyFrames = 0;
		bool readBackMatches = false;
		bool workingMatches = false;
	};

	enum class KitMode { Base, Store, StoreAndLoad, LoadOnly };

	KitRun kitRun(const Bytes& _rom, const KitMode _mode)
	{
		Machine m(_rom, g_romName);
		KitRun r;
		auto kit = *readKit(m, 0);
		// Audible change: track 1 (kick) pitch and decay.
		auto edited = kit;
		edited.params[0][0] = 0;
		edited.params[0][1] = 20;
		m.onBlock = [&]
		{
			const auto s = m.playhead();
			if(r.steps.empty() || r.steps.back().second != s)
				r.steps.emplace_back(m.now(), s);
		};
		const auto start = m.now();
		m.panel(md::PanelControl::Play);
		m.runUntil(start + g_rate * 3);
		if(_mode == KitMode::Store || _mode == KitMode::StoreAndLoad)
		{
			r.pushAt = m.now() - start;
			r.drained = m.send(ed::encodeMdKit(edited));
			const auto back = readKit(m, 0, &r.replyFrames);
			r.readBackMatches = back && *back == edited;
		}
		if(_mode == KitMode::StoreAndLoad || _mode == KitMode::LoadOnly)
		{
			r.loadAt = m.now() - start;
			r.loadDrained = m.send(ed::mdLoadKit(0));
		}
		m.runUntil(start + g_rate * 8);
		m.send(ed::mdSaveKit(63));
		const auto working = readKit(m, 63);
		r.workingMatches = working && working->params[0] == edited.params[0];
		r.left.assign(m.left().begin() + static_cast<std::ptrdiff_t>(start),
			m.left().begin() + static_cast<std::ptrdiff_t>(start + g_rate * 8));
		for(auto& s : r.steps)
			s.first -= start;
		return r;
	}

	void compareSteps(const char* _label, const KitRun& _a, const KitRun& _b)
	{
		int64_t worst = 0;
		size_t moved = 0;
		const auto n = std::min(_a.steps.size(), _b.steps.size());
		for(size_t i = 0; i < n; ++i)
		{
			const auto d = static_cast<int64_t>(_b.steps[i].first) - static_cast<int64_t>(_a.steps[i].first);
			moved += d != 0;
			if(std::abs(d) > std::abs(worst))
				worst = d;
		}
		std::printf("  %s: step clock vs base over %zu steps: %zu moved, worst %lld frames (%.2f ms)\n", _label, n,
			moved, static_cast<long long>(worst), worst * 1000.0 / g_rate);
	}

	void kitPush(const Bytes& _rom)
	{
		std::puts("== probe: kit push while playing (A01 at 125 BPM, kit 0)");
		const auto base = kitRun(_rom, KitMode::Base);
		const auto store = kitRun(_rom, KitMode::Store);
		const auto load = kitRun(_rom, KitMode::StoreAndLoad);
		const auto reload = kitRun(_rom, KitMode::LoadOnly);
		const auto report = [&](const char* _label, const KitRun& _r)
		{
			const auto d = firstDifference(base.left, _r.left);
			std::printf("  %s: push at %.1f ms, UART drained %.1f ms, request->reply %.1f ms, read-back %s;"
				" LOAD KIT drained %.1f ms; audio first differs %s; working kit = pushed: %s\n", _label, ms(_r.pushAt),
				ms(_r.drained), ms(_r.replyFrames), _r.readBackMatches ? "matches" : "n/a",
				ms(_r.loadDrained), d == SIZE_MAX ? "never" : (std::to_string(ms(d) - ms(_r.loadAt ? _r.loadAt
				: _r.pushAt)) + " ms after the command").c_str(), _r.workingMatches ? "yes" : "no");
			std::printf("    max sample jump %.3f (base %.3f), longest digital silence %zu frames (base %zu),"
				" RMS 3-8 s %.4f (base %.4f)\n", maxJump(_r.left, 0, _r.left.size()), maxJump(base.left, 0,
				base.left.size()), longestSilence(_r.left, g_rate * 3, _r.left.size()),
				longestSilence(base.left, g_rate * 3, base.left.size()), rms(_r.left, g_rate * 3, _r.left.size()),
				rms(base.left, g_rate * 3, base.left.size()));
			compareSteps(_label, base, _r);
		};
		report("store only (kit dump to the playing kit's slot)", store);
		report("store + LOAD KIT", load);
		report("LOAD KIT only (reload stored kit)", reload);
	}

	// Song pushed while the song plays: does the playing song change?
	void songPush(const Bytes& _rom)
	{
		std::puts("== probe: song push while playing in song mode");
		Machine m(_rom, g_romName);
		ed::MdSong s;
		s.position = 3;
		s.rows.clear();
		for(uint8_t p : {10, 11, 12, 13, 14, 15})
		{
			ed::MdSongRow r;
			r.pattern = p;
			r.end = 8;
			s.rows.push_back(r);
		}
		s.rows.push_back(ed::MdSongRow{});
		m.send(ed::encodeMdSong(s));
		m.send(ed::mdLoadSong(3));
		m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 1));
		m.panel(md::PanelControl::Play);
		std::vector<int> order;
		const auto track = [&](const double _ms)
		{
			const auto end = m.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(m.now() < end)
			{
				const int p = status(m, ed::MdStatus::Pattern);
				if(order.empty() || order.back() != p)
					order.push_back(p);
				m.run(20);
			}
		};
		track(1500);
		// Edit rows 4-5 (not yet played) and push to the playing song's slot.
		auto edited = s;
		edited.rows[4].pattern = 40;
		edited.rows[5].pattern = 41;
		const auto drained = m.send(ed::encodeMdSong(edited));
		track(5000);
		std::printf("  song 10 11 12 13 14 15, rows 5-6 pushed as 40 41 mid-play (drained %.1f ms): played",
			ms(drained));
		for(const auto p : order)
			std::printf(" %d", p);
		std::printf("\n");
		// Now push + LOAD SONG mid-play.
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Stop);
		m.send(ed::encodeMdSong(s));
		m.send(ed::mdLoadSong(3));
		order.clear();
		m.panel(md::PanelControl::Play);
		track(1500);
		m.send(ed::encodeMdSong(edited));
		m.send(ed::mdLoadSong(3));
		track(5000);
		std::printf("  same, pushed + LOAD SONG mid-play: played");
		for(const auto p : order)
			std::printf(" %d", p);
		std::printf("\n");
		const auto stored = readSong(m, 3);
		std::printf("  slot 3 holds the edited song: %s\n", stored && *stored == edited ? "yes" : "no");
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Stop);
		m.send(ed::mdLoadSong(3));
		order.clear();
		m.panel(md::PanelControl::Play);
		track(6500);
		std::printf("  after STOP, LOAD SONG, PLAY: played");
		for(const auto p : order)
			std::printf(" %d", p);
		std::printf("\n");
		m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 0));
	}

	// Song row mutes (bit order) and pattern tempo multipliers, measured.
	void songMutesAndTempo(const Bytes& _rom)
	{
		std::puts("== probe: song row mutes and tempo multipliers");
		Machine m(_rom, g_romName);
		// Pattern 100: only track 1 (kick) on every 4th step, kit 0, 16 steps.
		auto p = *readPattern(m, 0);
		p.position = 100;
		p.length = 16;
		p.scale = 0;
		p.kit = 0;
		for(auto& t : p.trigs)
			t = 0;
		p.trigs[0] = 0x1111;
		p.lockMasks.fill(0);
		for(auto& row : p.lockRows)
			row.fill(0);
		m.send(ed::encodeMdPattern(p));

		ed::MdSong s;
		s.position = 4;
		s.rows.clear();
		for(const uint16_t mutes : {0x0000, 0x0001, 0x8000, 0x0002})
		{
			ed::MdSongRow r;
			r.pattern = 100;
			r.end = 16;
			r.mutes = mutes;
			s.rows.push_back(r);
		}
		s.rows.push_back(ed::MdSongRow{});
		m.send(ed::encodeMdSong(s));
		m.send(ed::mdLoadSong(4));
		m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 1));
		const auto start = m.now();
		m.panel(md::PanelControl::Play);
		m.run(9000);
		// Row boundaries from the audio: 16 steps at 125 BPM = 1.92 s per row.
		const auto row = static_cast<size_t>(g_rate * 60.0 / 125.0 * 4);
		for(size_t i = 0; i < 4; ++i)
		{
			const auto b = start + i * row + g_rate / 10;
			std::printf("  row %zu mutes 0x%04x: RMS %.4f\n", i + 1, s.rows[i].mutes,
				rms(m.left(), b, b + row - g_rate / 5));
		}
		m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 0));
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Stop);

		// Swing: onset delay of swung steps, track 1 on every step, swing on odd steps.
		// A short kick decay keeps the onsets apart.
		{
			auto k = *readKit(m, 0);
			k.params[0][1] = 8;
			m.send(ed::encodeMdKit(k));
			m.send(ed::mdLoadKit(0));
		}
		for(const uint32_t amount : {0u, ed::swingAmountFromPercent(65), ed::swingAmountFromPercent(75)})
		{
			auto q = p;
			q.trigs[0] = 0xffff;
			q.swingAmount = amount;
			q.swingEditAll = 1;
			q.swingPattern = 0xaaaa;
			q.tempoMultiplier = 0;
			m.send(ed::encodeMdPattern(q));
			m.send(ed::mdLoadPattern(100));
			const auto begin = m.now();
			m.panel(md::PanelControl::Play);
			m.run(2200);
			m.panel(md::PanelControl::Stop);
			m.panel(md::PanelControl::Stop);
			// Onsets: 10 ms energy rising 4x over the 10 ms ending just before, 40 ms apart.
			std::vector<size_t> onsets;
			const size_t win = g_rate / 100;
			const auto energy = [&](const size_t _at)
			{
				double e = 0;
				for(size_t i = _at; i < _at + win && i < m.left().size(); ++i)
					e += m.left()[i] * m.left()[i];
				return e;
			};
			for(size_t i = begin + win; i + win < m.left().size(); i += g_rate / 2000)
			{
				if(!onsets.empty() && i - onsets.back() < g_rate / 25)
					continue;
				const auto now = energy(i);
				if(now > 1e-3 && now > 4 * energy(i - win))
					onsets.push_back(i);
			}
			double even = 0, odd = 0;
			size_t ne = 0, no = 0;
			for(size_t k = 1; k + 1 < onsets.size() && k < 15; ++k)
			{
				const double gap = ms(onsets[k] - onsets[k - 1]);
				(k % 2 ? even : odd) += gap;
				(k % 2 ? ne : no)++;
			}
			const double step = 60000.0 / 125.0 / 4.0;
			const double first = ne ? even / ne : 0;
			std::printf("  swingAmount %5u (%d %%): onset gaps %.1f / %.1f ms -> swing %.1f %% (expected %d %%)\n",
				amount, ed::swingPercent(amount), first, no ? odd / no : 0, 50.0 * first / step,
				ed::swingPercent(amount));
		}

		// Tempo multiplier: step period from the playhead.
		for(uint8_t mult = 0; mult < 4; ++mult)
		{
			p.tempoMultiplier = mult;
			m.send(ed::encodeMdPattern(p));
			m.send(ed::mdLoadPattern(100));
			std::vector<uint64_t> edges;
			uint8_t last = m.playhead();
			m.onBlock = [&]
			{
				const auto s2 = m.playhead();
				if(s2 != last)
					edges.push_back(m.now());
				last = s2;
			};
			m.panel(md::PanelControl::Play);
			m.run(2500);
			m.onBlock = nullptr;
			m.panel(md::PanelControl::Stop);
			m.panel(md::PanelControl::Stop);
			double period = 0;
			if(edges.size() > 6)
				period = ms(edges.back() - edges[3]) / static_cast<double>(edges.size() - 4);
			std::printf("  tempoMultiplier %u: step period %.1f ms (1X at 125 BPM = 120.0 ms)\n", mult, period);
		}
	}

	// Edge values: what does the firmware keep, clamp or reject?
	void normalisation(const Bytes& _rom)
	{
		std::puts("== probe: firmware normalisation of edge values");
		Machine m(_rom, g_romName);
		const auto base = *readPattern(m, 0);
		const auto tryPattern = [&](const char* _what, ed::MdPattern _p)
		{
			_p.position = 120;
			m.send(ed::encodeMdPattern(_p));
			const auto back = readPattern(m, 120);
			std::string diff;
			if(!back)
				diff = "no read-back (rejected?)";
			else if(*back == _p)
				diff = "kept as sent";
			else
			{
				if(back->length != _p.length)
					diff += " length " + std::to_string(_p.length) + "->" + std::to_string(back->length);
				if(back->scale != _p.scale)
					diff += " scale " + std::to_string(_p.scale) + "->" + std::to_string(back->scale);
				if(back->swingAmount != _p.swingAmount)
					diff += " swing " + std::to_string(_p.swingAmount) + "->" + std::to_string(back->swingAmount);
				if(back->accentAmount != _p.accentAmount)
					diff += " accent " + std::to_string(_p.accentAmount) + "->" + std::to_string(back->accentAmount);
				if(back->tempoMultiplier != _p.tempoMultiplier)
					diff += " mult " + std::to_string(_p.tempoMultiplier) + "->"
						+ std::to_string(back->tempoMultiplier);
				if(back->lockRows != _p.lockRows || back->lockMasks != _p.lockMasks)
					diff += " locks changed";
				if(back->trigs != _p.trigs)
					diff += " trigs changed";
				if(back->kit != _p.kit)
					diff += " kit " + std::to_string(_p.kit) + "->" + std::to_string(back->kit);
				if(back->lockedRows != _p.lockedRows)
					diff += " lockedRows " + std::to_string(_p.lockedRows) + "->" + std::to_string(back->lockedRows);
				if(diff.empty())
					diff = "other fields changed";
			}
			std::printf("  pattern %s: %s\n", _what, diff.c_str());
		};
		auto p = base;
		p.length = 40;
		p.scale = 1;
		tryPattern("length 40 > total 32", p);
		p = base;
		p.swingAmount = 12000;
		tryPattern("swing 12000 (>80 %)", p);
		p = base;
		p.accentAmount = 127;
		tryPattern("accent 127", p);
		p = base;
		p.kit = 70;
		tryPattern("kit 70", p);
		p = base;
		p.tempoMultiplier = 5;
		tryPattern("multiplier 5", p);
		p = base;
		for(auto& t : p.trigs)
			t = 0;
		p = *ed::withLock(p, 0, 0, 3, 50);
		tryPattern("lock on a step without a trig", p);
		p = base;
		p.lockedRows = 9;
		tryPattern("lockedRows byte 9", p);
		p = base;
		p.lockMasks[3] |= 1u << 28;	// parameter 28 does not exist
		tryPattern("lock mask bit 28", p);

		const auto kit = *readKit(m, 0);
		const auto tryKit = [&](const char* _what, ed::MdKit _k)
		{
			_k.position = 50;
			m.send(ed::encodeMdKit(_k));
			const auto back = readKit(m, 50);
			std::printf("  kit %s: %s\n", _what, !back ? "no read-back" : *back == _k ? "kept as sent"
				: back->models != _k.models ? "model changed" : back->lfos != _k.lfos ? "lfo changed"
				: back->trigGroups != _k.trigGroups ? "trig group changed" : "other fields changed");
		};
		auto k = kit;
		k.models[0] = 90;
		tryKit("model 90 (undefined)", k);
		k = kit;
		k.lfos[0].shape1 = 9;
		tryKit("lfo shape 9", k);
		k = kit;
		k.trigGroups[2] = 2;
		tryKit("trig group to itself", k);

		const auto trySong = [&](const char* _what, const ed::MdSong& _s)
		{
			auto s = _s;
			s.position = 20;
			m.send(ed::encodeMdSong(s));
			const auto back = readSong(m, 20);
			std::printf("  song %s: %s\n", _what, !back ? "no read-back" : *back == s ? "kept as sent"
				: ("read back with " + std::to_string(back->rows.size()) + " rows").c_str());
		};
		ed::MdSong s;
		s.rows.assign(256, ed::MdSongRow{0, 0, 0, 0, 0, ed::MdSongRow::g_noTempo, 0, 16});
		s.rows.back() = ed::MdSongRow{};
		trySong("256 rows incl. END", s);
		s.rows.insert(s.rows.begin(), ed::MdSongRow{0, 0, 0, 0, 0, ed::MdSongRow::g_noTempo, 0, 16});
		trySong("257 rows incl. END", s);
		s.rows.assign(3, ed::MdSongRow{0, 0, 70, 0, 0, ed::MdSongRow::g_noTempo, 0, 16});
		s.rows.back() = ed::MdSongRow{};
		trySong("repeats 70", s);
		s.rows.assign(2, ed::MdSongRow{0, 0, 0, 0, 0, ed::MdSongRow::g_noTempo, 0, 16});
		trySong("no END row", s);
	}

	// Unsaved working-kit edits across a pattern change (EXTENDED: linked kits).
	// The working kit is read by saving it over its own slot, which keeps the
	// current kit number (SAVE KIT to another slot would make that slot current).
	void kitLink(const Bytes& _rom)
	{
		std::puts("== probe: unsaved kit edits across kit-per-pattern switches (stopped)");
		for(const bool viaSameKit : {false, true})
		{
			Machine m(_rom, g_romName);
			auto a02 = *readPattern(m, 0);
			a02.position = 1;
			a02.kit = viaSameKit ? 0 : 5;
			m.send(ed::encodeMdPattern(a02));
			const auto stored = *readKit(m, 0);
			m.send({0xb0, 8, 5});	// track 1 level, working kit only
			m.send(ed::mdLoadPattern(1));
			m.run(100);
			const int kitOnA02 = status(m, ed::MdStatus::Kit);
			m.send(ed::mdLoadPattern(0));
			m.run(100);
			const int kitOnA01 = status(m, ed::MdStatus::Kit);
			m.send(ed::mdSaveKit(0));
			const auto working = *readKit(m, 0);
			std::printf("  A01(kit 0) -> A02(kit %u) -> A01: kit on A02 %d, back %d; level[0] stored %u, edited 5,"
				" now %u -> unsaved edit %s\n", a02.kit, kitOnA02, kitOnA01, stored.levels[0], working.levels[0],
				working.levels[0] == 5 ? "KEPT" : "LOST");
		}
		{
			Machine m(_rom, g_romName);
			m.send({0xb0, 8, 5});
			m.send(ed::mdLoadKit(0));
			m.send(ed::mdSaveKit(0));
			std::printf("  LOAD KIT 0 after an edit: level[0] now %u -> edit %s\n", readKit(m, 0)->levels[0],
				readKit(m, 0)->levels[0] == 5 ? "KEPT" : "LOST");
			m.send(ed::mdSaveKit(9));
			std::printf("  SAVE KIT 9 makes the current kit %d\n", status(m, ed::MdStatus::Kit));
		}
	}

	// Look for the queued-next and current pattern in RAM: queue A03 while A01
	// plays, then A02 while A03 plays.
	void queueRam(const Bytes& _rom)
	{
		std::puts("== probe: RAM for current / queued pattern");
		Machine m(_rom, g_romName);
		for(const uint8_t slot : {1, 2})
		{
			auto p = *readPattern(m, 0);
			p.position = slot;
			p.length = 16;
			p.scale = 0;
			m.send(ed::encodeMdPattern(p));
		}
		m.panel(md::PanelControl::Play);
		m.run(500);
		const auto idle = m.snapshotRam();			// A01 playing, nothing queued
		m.send(ed::mdLoadPattern(2));
		m.run(300);
		const auto queued2 = m.snapshotRam();		// A01 playing, A03 queued
		m.run(4000);
		const auto playing2 = m.snapshotRam();		// A03 playing, nothing queued
		m.send(ed::mdLoadPattern(1));
		m.run(300);
		const auto queued1 = m.snapshotRam();		// A03 playing, A02 queued
		m.run(2500);
		const auto playing1 = m.snapshotRam();		// A02 playing
		std::printf("  status now %d\n", status(m, ed::MdStatus::Pattern));
		for(uint32_t a = 0; a < idle.size(); ++a)
		{
			const bool next = idle[a] == 0 && queued2[a] == 2 && queued1[a] == 1;
			const bool current = idle[a] == 0 && queued2[a] == 0 && playing2[a] == 2 && queued1[a] == 2
				&& playing1[a] == 1;
			if(next || current)
				std::printf("  0x%06x: idle %u, A03 queued %u, A03 playing %u, A02 queued %u, A02 playing %u -> %s\n",
					0x200000 + a, idle[a], queued2[a], playing2[a], queued1[a], playing1[a],
					next ? "next?" : "current");
		}
	}

	// The transport glue (mdDataLink::Session) driving the firmware: the numbers
	// a UI would see, end to end.
	void session(const Bytes& _rom)
	{
		std::puts("== probe: mdDataLink::Session against the firmware");
		Machine m(_rom, g_romName);
		std::vector<Bytes> wire;
		mdDataLink::Session link([&](const Bytes& _b) { m.send(_b); });
		m.onSysex = [&](const Bytes& _b) { link.onSysex(_b); };
		std::optional<ed::MdPattern> pattern;
		std::optional<ed::MdKit> kit;
		std::optional<ed::MdSong> song;
		uint64_t patternAt = 0, kitAt = 0, songAt = 0;
		link.onPattern = [&](const ed::MdPattern& _p) { pattern = _p; patternAt = m.now(); };
		link.onKit = [&](const ed::MdKit& _k) { kit = _k; kitAt = m.now(); };
		link.onSong = [&](const ed::MdSong& _s) { song = _s; songAt = m.now(); };
		const auto waitFor = [&](const auto& _done, const double _ms)
		{
			const auto end = m.now() + static_cast<uint64_t>(_ms * g_rate / 1000);
			while(!_done() && m.now() < end)
				m.step();
		};

		link.requestStatus();
		link.requestPattern(0);
		waitFor([&] { return pattern.has_value() && link.state().extendedMode.has_value(); }, 500);
		const auto& st = link.state();
		std::printf("  status: pattern %d kit %d song %d songMode %d extended %d\n", st.pattern ? *st.pattern : -1,
			st.kit ? *st.kit : -1, st.song ? *st.song : -1, st.songMode ? *st.songMode : -1,
			st.extendedMode ? *st.extendedMode : -1);

		// A02 linked to kit 5; the working kit gets a live edit.
		auto a02 = *pattern;
		a02.position = 1;
		a02.kit = 5;
		a02.length = 16;
		a02.scale = 0;
		pattern.reset();
		auto t = m.now();
		link.pushPattern(a02);
		waitFor([&] { return pattern.has_value(); }, 500);
		std::printf("  pushPattern: read-back value %s after %.1f ms\n",
			pattern && *pattern == a02 ? "equal" : "DIFFERS",
			ms(patternAt - t));
		m.send({0xb0, 8, 5});
		link.noteWorkingKitEdited();
		std::printf("  working kit edited; selecting A02 would discard it: %s\n",
			link.selectWouldDiscardKitEdits(1) ? "yes (UI must warn)" : "no");

		m.panel(md::PanelControl::Play);
		m.run(1000);
		t = m.now();
		link.selectPattern(1);
		double clearedAt = -1;
		std::string kitState;
		for(int i = 0; i < 250 && clearedAt < 0; ++i)
		{
			link.requestStatus();
			m.run(20);
			if(!st.queuedPattern)
				clearedAt = ms(m.now() - t);
		}
		// The kit status follows about one step after the pattern status.
		for(int i = 0; i < 20; ++i)
		{
			link.requestStatus();
			m.run(20);
		}
		std::printf("  selectPattern(A02) while playing: queued until %.0f ms, then kit %d, working kit %s\n",
			clearedAt,
			st.kit ? *st.kit : -1, st.workingKit == mdDataLink::Session::WorkingKit::Clean ? "clean (edit lost)"
			: "edited");

		// Kit push, stored and loaded, while playing.
		link.requestKit(5);
		waitFor([&] { return kit.has_value(); }, 500);
		auto k = *kit;
		k.params[0][0] = 10;
		kit.reset();
		t = m.now();
		link.pushKit(k, mdDataLink::Session::KitApply::StoreAndLoad);
		const auto sentMs = ms(m.now() - t);
		waitFor([&] { return kit.has_value(); }, 500);
		std::printf("  pushKit(StoreAndLoad): on the wire %.1f ms, read-back %s after %.1f ms\n", sentMs,
			kit && *kit == k ? "equal" : "DIFFERS", ms(kitAt - t));

		// A 256-row song into the current song's slot.
		ed::MdSong s;
		s.position = st.song ? *st.song : 0;
		s.rows.assign(255, ed::MdSongRow{0, 0, 0, 0, 0, ed::MdSongRow::g_noTempo, 0, 16});
		s.rows.push_back(ed::MdSongRow{});
		song.reset();
		t = m.now();
		link.pushSong(s);
		const auto songSent = ms(m.now() - t);
		waitFor([&] { return song.has_value(); }, 1000);
		std::printf("  pushSong(256 rows, %zu bytes): on the wire %.1f ms, read-back %s after %.1f ms,"
			" reload needed: %s\n", ed::encodeMdSong(s).size(), songSent, song && *song == s ? "equal" : "DIFFERS",
			ms(songAt - t), st.songReloadNeeded ? "yes" : "no");
	}

	// What a DAW project keeps: the plug-in state is the 1 MiB patch RAM. Edit
	// the machine, snapshot patch RAM, boot a second machine from it, compare.
	void persistence(const Bytes& _rom)
	{
		std::puts("== probe: which edits survive a project save/restore (patch-RAM snapshot)");
		Bytes ram;
		ed::MdPattern pattern;
		ed::MdSong song;
		{
			Machine m(_rom, g_romName);
			pattern = *readPattern(m, 0);
			pattern = ed::withTrig(pattern, 5, 3, !ed::hasTrig(pattern, 5, 3));
			m.send(ed::encodeMdPattern(pattern));
			song = *readSong(m, 0);
			song.rows.front().repeats = 7;
			m.send(ed::encodeMdSong(song));
			m.send({0xb0, 8, 5});	// working kit 0, track 1 level: not saved
			m.send(ed::mdSetStatus(ed::MdStatus::Track, 3));
			m.run(200);
			ram = m.hardware().copyPatchRam();
		}
		Machine m(_rom, g_romName, ram);
		const auto p = readPattern(m, 0);
		const auto s = readSong(m, 0);
		const auto stored = readKit(m, 0);
		m.send(ed::mdSaveKit(0));
		const auto working = readKit(m, 0);
		std::printf("  pattern dump: %s; song dump: %s; unsaved working-kit edit: %s (level %u, stored %u)\n",
			p && *p == pattern ? "kept" : "LOST", s && *s == song ? "kept" : "LOST",
			working && working->levels[0] == 5 ? "kept" : "LOST", working ? working->levels[0] : 0,
			stored ? stored->levels[0] : 0);
	}
}
