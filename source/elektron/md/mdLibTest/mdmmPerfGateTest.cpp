// The gate for the emulation CPU plan (doc/modern-ux/RESEARCH-emulation-cpu.md, section 5) and of the local release
// gate (doc/release/LOCAL-GATE.md): one tool that says whether a build kept every sample, every byte of memory and
// every MIDI byte the same, and what it did to the host cost of one audio frame. A lever of the plan lands only if the
// hashes of the old and the new binary are identical and the instructions and cycles per frame drop by the amount the
// lever promises; a release build passes only if its hashes equal the recorded goldens.
//   mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8] [--scenario <name>] [--outputs stereo|all]
//                    [--golden <goldens.json> [--record]] [--wav <file.wav>]
// Boots the machine (MD OS 1.63 or MM OS 1.32B) headless, sets up the scenario, then renders two phases in 64-frame
// blocks at 44.1 kHz: "stopped" and, after a press of PLAY, "playing". The scenarios
// (doc/md_mm_performance_diagnostics.md):
//   md-busy     the MD's current pattern with every track on 16ths (as mdCpuBenchTest makes it): the md default
//   md-factory  the pattern the MD boots with, untouched
//   md-song     md-busy's pattern and a second one (every track on every 4th 16th) in the current song slot, in song
//               mode: the busy pattern twice, then the other with tracks 5-8 muted at 150 BPM, then a loop to the start
//   mm-a01      the pattern the MM boots with (the factory A01): the mm default
//   mm-busy     that pattern with a trig in ALL on every other 16th of the six synth tracks (sent on SYSEX RECV, then
//               LOAD PATTERN)
//   mm-song     mm-busy's pattern and a second one (every 4th 16th) in the current song slot, in song mode: the busy
//               pattern twice, then the other with tracks 1-3 muted, then a loop to the start
// At the start of the stopped phase a short, fixed burst of host notes goes in (the MD's sixteen pads on its base
// channel, the MM's note 60 on channel 1, each twice, within the first 1.5 s), so that the stopped phase renders a few
// hits and not only silence. One line per phase, fixed field names so that a script can compare runs:
//   cpu_pct              the thread's CPU time per second of audio, as a share of one core (it is CPU time, not
//                        wall-clock, but it still moves with the computer's load: caches, clocks, other work)
//   instr_per_frame      retired host instructions per audio frame, from thread_selfcounts (macOS): independent of the
//                        load of the computer, but two runs still differ by about 0.5 %, so judge a change by medians
//   cycles_per_frame     host CPU cycles per audio frame, the same counter: steadier than time, but the core type
//                        matters
//   uc_cycles_per_frame  guest ColdFire cycles per audio frame (what the firmware was given, not what it cost the host)
//   audio_hash           FNV-1a 64 of the phase's left then right samples, as raw floats
// then, added after those:
//   frames               frames the phase rendered
//   nonsilent_frames     frames whose left or right sample is not 0 (a silent scenario shows here)
//   playhead_moves       how often the sequencer's step changed, read once per block (MD 0x261aa7, MM
//                        MmTelemetry::g_stepAddress): 0 stopped; the playing phase must move or the run fails
//   instr_median_per_frame  the median block's instructions per frame (the same counter, robust to outliers)
//   outputs_hash, out0_hash .. out5_hash  (--outputs all) FNV-1a 64 of each of the six outputs over the phase, and of
//                        those six hashes in order; the stereo pair is still audio_hash
// The last line gives the audio hash of both phases chained, then the FNV-1a 64 of
//   ram_hash             the 1 MiB of main RAM at $200000
//   sram_hash            the ColdFire internal SRAM at $01000000 (64 KiB)
//   loader_hash          the upper loader RAM, $310000 to $3fffff
//   patch_hash           patch RAM, as Hardware::copyPatchRam() returns it
//   midi_out_hash        everything the machine sent out on MIDI from the start of the stopped phase to the end of the
//                        playing phase (the PLAY press included): each message with the frame of the block that
//                        returned it and its offset in the block
// and midi_out_events, the number of messages behind that hash, then the PLAY press and the whole stream:
//   press_frame          the frame, counted from the start of the stopped phase, at which PLAY goes down
//   press_frames         the frames from the press to the start of the playing phase (PLAY held 40 ms, 40 ms after)
//   press_hash           left then right samples of those frames
//   stream_frames, stream_hash  every frame from the start of the stopped phase to the end of the playing phase, no
//                        gap (stopped, the press, playing), left then right
//   stream_outputs_hash  (--outputs all) the same over the six outputs, as outputs_hash
// The phases are the same frames as before the press was hashed, so the older fields keep their values. The memory
// windows are read through the ColdFire's public read16 (main RAM through read8, in Machine::snapshotRam); the SIM and
// HI08 windows are never read, reads there have side effects.
// Compare those hashes across builds; compare the per-frame costs as medians of a few paired runs (old, new, old, new).
// --golden compares the hashes and counts with the entry of the goldens file for (ROM fingerprint, scenario, outputs,
// GEARMULATOR_MDMM_SPEEDUPS position, seconds per phase) and exits 1 on any difference, field by field; with --record
// it writes or replaces that entry instead (the file keeps the others). The goldens hold hashes, counts and, as
// information only, the instruction figures and guest cycles of the recording run: nothing from the firmware.
// --wav writes the hashed stream (stopped, the press, playing; left and right) as a 32-bit float WAV at 44.1 kHz, for
// listening to two builds or two switch positions side by side; it changes nothing that is hashed.
// The tool uses only what main has, so one source builds against both an old and a new emulator.
// Manual: needs a user-supplied ROM (no firmware is bundled). Exits 77 without one. The counters need macOS; on
// other systems they print as 0 and only the hashes and cpu_pct mean something.

#include "mdFirmwareSession.h"
#include "mmSysexRecv.h"

#include "mdLib/mdmemorymap.h"
#include "mdLib/mdromcheck.h"
#include "mdLib/mmtelemetry.h"

#include "elektronData/json.h"
#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdSong.h"
#include "elektronData/mmCommands.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmSong.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

using namespace mdFirmwareSession;
namespace ed = elektronData;
namespace json = elektronData::json;

namespace
{
	constexpr uint64_t g_fnvOffset = 1469598103934665603ull;
	constexpr size_t g_outputs = 6;

	uint64_t fnv1a(uint64_t _hash, const void* _data, const size_t _size)
	{
		const auto* bytes = static_cast<const uint8_t*>(_data);
		for(size_t i = 0; i < _size; ++i)
		{
			_hash ^= bytes[i];
			_hash *= 1099511628211ull;
		}
		return _hash;
	}

	double threadCpuSeconds()
	{
		timespec t{};
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
		return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
	}

	double sum(const std::vector<float>& _values)
	{
		double total = 0;
		for(const auto v : _values)
			total += v;
		return total;
	}

	double median(std::vector<float> _values)
	{
		if(_values.empty())
			return 0;
		const auto mid = _values.begin() + static_cast<std::ptrdiff_t>(_values.size() / 2);
		std::nth_element(_values.begin(), mid, _values.end());
		return *mid;
	}

	std::string hex(const uint64_t _value)
	{
		char text[24];
		std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(_value));
		return text;
	}

	std::string format(const char* _format, const double _value)
	{
		char text[64];
		std::snprintf(text, sizeof(text), _format, _value);
		return text;
	}

	// FNV-1a 64 of [_begin, _end) as the ColdFire reads it, 16 bits at a time and most significant byte first. Only
	// plain RAM windows go in here: reads of the SIM and HI08 windows have side effects.
	uint64_t hashWindow(md::Microcontroller& _uc, uint64_t _hash, const md::memorymap::Range& _window)
	{
		for(uint32_t a = _window.begin; a < _window.end; a += 2)
		{
			const uint16_t word = _uc.read16(a);
			const uint8_t bytes[2] = {static_cast<uint8_t>(word >> 8), static_cast<uint8_t>(word & 0xff)};
			_hash = fnv1a(_hash, bytes, sizeof(bytes));
		}
		return _hash;
	}

	// Everything the machine sends out on MIDI, with the frame of the block that returned it and its offset in the
	// block.
	struct MidiOut
	{
		uint64_t hash = g_fnvOffset;
		uint64_t events = 0;

		void add(const uint64_t _frame, const uint32_t _offset, const void* _bytes, const size_t _size)
		{
			const uint64_t size = _size;
			hash = fnv1a(hash, &_frame, sizeof(_frame));
			hash = fnv1a(hash, &_offset, sizeof(_offset));
			hash = fnv1a(hash, &size, sizeof(size));
			hash = fnv1a(hash, _bytes, _size);
			++events;
		}

		void listen(Machine& _m)
		{
			_m.onMidi = [this, &_m](const synthLib::SMidiEvent& _e)
			{
				const uint8_t bytes[3] = {_e.a, _e.b, _e.c};
				add(_m.now(), _e.offset, bytes, sizeof(bytes));
			};
			_m.onSysex = [this, &_m](const Bytes& _sysex)
			{
				add(_m.now(), 0, _sysex.data(), _sysex.size());
			};
		}
	};

	// ---- scenarios

	uint64_t stepMask(const uint8_t _length, const uint64_t _steps)
	{
		return _steps & ((_length >= 64) ? ~0ull : ((1ull << _length) - 1));
	}

	uint8_t mdCurrent(Machine& _m, const ed::MdStatus _param)
	{
		const auto status = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(_param), 0x72));
		require(status.has_value(), "no reply to an MD status request");
		return status->value;
	}

	uint8_t mmCurrent(Machine& _m, const ed::MmStatus _param)
	{
		const auto status = ed::parseMmStatusResponse(_m.request(ed::mmStatusRequest(_param), 0x72));
		require(status.has_value(), "no reply to an MM status request");
		return status->value;
	}

	// The MD's pattern with a trig on every 16th of every track, so the sequencer and the voices have work.
	ed::MdPattern mdMakeBusy(Machine& _m)
	{
		const auto slot = mdCurrent(_m, ed::MdStatus::Pattern);
		auto pattern = ed::decodeMdPattern(_m.request(ed::mdPatternRequest(slot), ed::g_mdPatternDump));
		require(pattern.has_value(), "no pattern dump");
		for(auto& trigs : pattern->trigs)
			trigs = stepMask(pattern->length, 0x5555555555555555ull);
		_m.send(ed::encodeMdPattern(*pattern));
		return *pattern;
	}

	// md-busy's pattern, a sparser one in the next slot, and the current song slot as: the busy pattern twice, the
	// other with tracks 5-8 muted at 150 BPM, a loop to the start. Then LOAD SONG and song mode, by SysEx.
	void mdSong(Machine& _m)
	{
		const auto busy = mdMakeBusy(_m);
		auto sparse = busy;
		sparse.position = static_cast<uint8_t>((busy.position + 1) & 0x7f);
		for(auto& trigs : sparse.trigs)
			trigs = stepMask(sparse.length, 0x1111111111111111ull);
		_m.send(ed::encodeMdPattern(sparse));

		ed::MdSong song;
		song.position = mdCurrent(_m, ed::MdStatus::Song);
		song.rows.clear();
		ed::MdSongRow row;
		row.pattern = busy.position;
		row.repeats = 1;
		song.rows.push_back(row);
		row.pattern = sparse.position;
		row.repeats = 0;
		row.mutes = 0x00f0;
		row.tempo = 150 * 24;
		song.rows.push_back(row);
		ed::MdSongRow loop;
		loop.pattern = ed::MdSongRow::g_loopRow;
		loop.target = 0;
		loop.repeats = 0;	// infinite
		song.rows.push_back(loop);
		song.rows.push_back(ed::MdSongRow{});
		_m.send(ed::encodeMdSong(song));
		_m.send(ed::mdLoadSong(song.position));
		_m.send(ed::mdSetStatus(ed::MdStatus::SequencerMode, 1));
		_m.run(250);
		require(mdCurrent(_m, ed::MdStatus::SequencerMode) == 1, "the MD did not go to song mode");
	}

	// The MM's current pattern with a trig in ALL (amp, filter, LFO) on _steps of the six synth tracks.
	ed::MmPattern mmPatternWith(Machine& _m, const uint64_t _steps)
	{
		const auto slot = mmCurrent(_m, ed::MmStatus::Pattern);
		auto pattern = ed::decodeMmPattern(_m.request(ed::mmPatternRequest(slot), 0x67));
		require(pattern.has_value(), "no MM pattern dump");
		const auto mask = stepMask(pattern->length, _steps);
		for(size_t t = 0; t < ed::MmPattern::g_tracks; ++t)
			pattern->amp[t] = pattern->filter[t] = pattern->lfo[t] = mask;
		return *pattern;
	}

	void mmSend(Machine& _m, const std::vector<Bytes>& _dumps)
	{
		const auto r = mmSysexRecv::send(_m, _dumps);
		require(r.taken == _dumps.size() && r.errors == 0, "the Monomachine did not take every dump on SYSEX RECV");
	}

	void mmBusy(Machine& _m)
	{
		const auto busy = mmPatternWith(_m, 0x5555555555555555ull);
		mmSend(_m, {ed::encodeMmPattern(busy)});
		// what plays is what LOAD PATTERN reads from the slot
		_m.send(ed::mmLoadPattern(busy.position));
		_m.run(250);
	}

	// mm-busy's pattern, a sparser one in the next slot, and the current song slot as: the busy pattern twice, the
	// other with tracks 1-3 muted, a loop to the start (each row 16 steps, the tempo kept). Then LOAD SONG and song
	// mode, by SysEx.
	void mmSong(Machine& _m)
	{
		const auto busy = mmPatternWith(_m, 0x5555555555555555ull);
		auto sparse = mmPatternWith(_m, 0x1111111111111111ull);
		sparse.position = static_cast<uint8_t>((busy.position + 1) & 0x7f);

		ed::MmSong song;
		song.position = mmCurrent(_m, ed::MmStatus::Song);
		const auto row = [&](const size_t _index, const uint8_t _pattern, const uint8_t _repeats, const uint8_t _mutes)
		{
			auto& r = song.rows[_index].bytes;
			r.fill(0);
			r[ed::mmSongRow::g_pattern] = _pattern;
			r[ed::mmSongRow::g_repeats] = _repeats;
			r[ed::mmSongRow::g_target] = 0;
			r[ed::mmSongRow::g_mutes] = _mutes;
			r[ed::mmSongRow::g_length] = 16;
			r[ed::mmSongRow::g_tempo] = 0xff;
			r[ed::mmSongRow::g_tempo + 1] = 0xff;
		};
		for(size_t i = 0; i < song.rows.size(); ++i)
			row(i, ed::MmSong::g_end, 0, 0);
		row(0, busy.position, 1, 0);
		row(1, sparse.position, 0, 0x07);
		row(2, ed::MmSong::g_loop, 0, 0);	// to row 0, infinite
		mmSend(_m, {ed::encodeMmPattern(busy), ed::encodeMmPattern(sparse), ed::encodeMmSong(song)});
		_m.send(ed::mmLoadSong(song.position));
		_m.send(ed::mmSetStatus(ed::MmStatus::SongMode, 1));
		_m.run(300);
		require(mmCurrent(_m, ed::MmStatus::SongMode) == 1, "the MM did not go to song mode");
	}

	struct Scenario
	{
		const char* name;
		bool mm;
		void (*setup)(Machine&);
	};

	const Scenario g_scenarios[] = {
		{"md-busy", false, [](Machine& _m) { mdMakeBusy(_m); }},
		{"md-factory", false, [](Machine&) {}},
		{"md-song", false, mdSong},
		{"mm-a01", true, [](Machine&) {}},
		{"mm-busy", true, mmBusy},
		{"mm-song", true, mmSong},
	};

	const Scenario* findScenario(const std::string& _name)
	{
		for(const auto& s : g_scenarios)
			if(_name == s.name)
				return &s;
		return nullptr;
	}

	// A short, fixed burst of host notes for the stopped phase to play, so that the phase renders hits and not only
	// silence. The MD gets its pads 36 to 51 on the base channel of global slot 0, the MM note 60 on channel 1. Each
	// note is held for a while (a released MM note is silent at once), and the burst is played twice, the second time
	// softer. The notes go out at fixed frames after start(), from the machine's block callback (post()).
	class HitBurst
	{
	public:
		HitBurst(Machine& _m, const bool _mm)
			: m_machine(_m)
		{
			if(_mm)
			{
				add(0.0, 0x90, 60, 100);
				add(0.5, 0x80, 60, 0);
				add(1.0, 0x90, 60, 64);
				add(1.5, 0x80, 60, 0);
				return;
			}
			const auto global = ed::decodeMdGlobal(_m.request(ed::mdGlobalRequest(0), ed::g_mdGlobalDump));
			require(global.has_value(), "no global settings dump");
			const std::pair<double, uint8_t> rounds[] = {{0.0, 100}, {1.0, 64}};
			for(const auto& round : rounds)
			{
				for(uint8_t pad = 36; pad < 52; ++pad)
					add(round.first, static_cast<uint8_t>(0x90 | global->baseChannel), pad, round.second);
				for(uint8_t pad = 36; pad < 52; ++pad)
					add(round.first + 0.25, static_cast<uint8_t>(0x80 | global->baseChannel), pad, 0);
			}
		}

		// The first notes are queued at once, the rest follow from post() in the block callback.
		void start()
		{
			m_origin = m_machine.now();
			post();
		}

		void post()
		{
			while(m_next < m_notes.size() && m_origin + m_notes[m_next].frame <= m_machine.now())
				m_machine.post(m_notes[m_next++].message);
		}

	private:
		struct Note
		{
			uint64_t frame;
			Bytes message;
		};

		void add(const double _seconds, const uint8_t _status, const uint8_t _note, const uint8_t _velocity)
		{
			m_notes.push_back({static_cast<uint64_t>(_seconds * g_rate), {_status, _note, _velocity}});
		}

		Machine& m_machine;
		std::vector<Note> m_notes;
		size_t m_next = 0;
		uint64_t m_origin = 0;
	};

	// Every block from start() on, as the block callback sees it: all six outputs (with --outputs all; their frame i is
	// frame base + i of Machine::left()) and the sequencer's step, read once per block. Reading main RAM has no side
	// effects, so the run renders the same with or without it.
	class Capture
	{
	public:
		using Hashes = std::array<uint64_t, g_outputs>;

		Capture(Machine& _m, const bool _allOutputs, const uint32_t _stepAddress)
			: m_machine(_m), m_all(_allOutputs), m_stepAddress(_stepAddress)
		{
		}

		void start()
		{
			m_base = m_machine.left().size();
			m_lastStep = m_machine.read8(m_stepAddress);
		}

		void onBlock()
		{
			if(m_all)
			{
				const auto& block = m_machine.blockOutputs();
				for(size_t c = 0; c < g_outputs; ++c)
					m_outputs[c].insert(m_outputs[c].end(), block[c].begin(), block[c].end());
			}
			const auto step = m_machine.read8(m_stepAddress);
			m_moves += step != m_lastStep;
			m_lastStep = step;
		}

		uint64_t moves() const { return m_moves; }

		// Each output's FNV-1a 64 over [_first, _first + _count) in Machine::left() frames, and the hash of those six
		// in order.
		uint64_t hashes(const size_t _first, const size_t _count, Hashes& _each) const
		{
			uint64_t all = g_fnvOffset;
			for(size_t c = 0; c < g_outputs; ++c)
			{
				_each[c] = fnv1a(g_fnvOffset, m_outputs[c].data() + (_first - m_base), _count * sizeof(float));
				all = fnv1a(all, &_each[c], sizeof(_each[c]));
			}
			return all;
		}

	private:
		Machine& m_machine;
		const bool m_all;
		const uint32_t m_stepAddress;
		std::array<std::vector<float>, g_outputs> m_outputs;
		size_t m_base = 0;
		uint8_t m_lastStep = 0;
		uint64_t m_moves = 0;
	};

	// ---- the report: the lines as printed, and which of their fields the goldens hold

	struct Field
	{
		enum class Kind
		{
			Print,		// printed only (host load: cpu_pct, cycles_per_frame)
			Compare,	// a hash or a count: a golden must match it
			Info		// kept in a golden as information (the instruction figures), never compared
		};

		std::string key;
		std::string value;
		Kind kind = Kind::Print;
	};

	struct Line
	{
		std::string section;	// the goldens' prefix: stopped, playing, combined
		std::string head;		// what the printed line starts with
		std::vector<Field> fields;

		void add(const std::string& _key, const std::string& _value, const Field::Kind _kind)
		{
			fields.push_back({_key, _value, _kind});
		}
		void compare(const std::string& _key, const std::string& _value) { add(_key, _value, Field::Kind::Compare); }
		void count(const std::string& _key, const uint64_t _count)
		{
			add(_key, std::to_string(_count), Field::Kind::Compare);
		}
		void info(const std::string& _key, const char* _format, const double _value)
		{
			add(_key, format(_format, _value), Field::Kind::Info);
		}

		void write() const
		{
			std::string text = head;
			for(const auto& f : fields)
				text += " " + f.key + "=" + f.value;
			std::puts(text.c_str());
		}
	};

	// ---- goldens

	std::optional<json::Value> readJson(const std::string& _path, std::string& _error)
	{
		std::ifstream s(_path, std::ios::binary);
		if(!s.good())
		{
			_error = "cannot read " + _path;
			return std::nullopt;
		}
		const std::string text((std::istreambuf_iterator<char>(s)), std::istreambuf_iterator<char>());
		auto value = json::parse(text, &_error);
		if(value && !value->isObject())
		{
			_error = _path + " is not a JSON object";
			return std::nullopt;
		}
		if(!value)
			_error = _path + ": " + _error;
		return value;
	}

	json::Value entryOf(const std::vector<Line>& _lines, const std::string& _firmware)
	{
		json::Value compare = json::Value::object();
		json::Value info = json::Value::object();
		for(const auto& line : _lines)
		{
			for(const auto& f : line.fields)
			{
				const auto key = line.section + "." + f.key;
				if(f.kind == Field::Kind::Compare)
					compare.set(key, f.value);
				else if(f.kind == Field::Kind::Info)
					info.set(key, std::stod(f.value));	// as printed: the run's noise below that is no information
			}
		}
		json::Value entry = json::Value::object();
		entry.set("firmware", _firmware);
		entry.set("compare", compare);
		entry.set("info", info);
		return entry;
	}

	// --record: the entry for _key goes into the file, the other entries are kept, all sorted by key.
	int record(const std::string& _path, const std::string& _key, const json::Value& _entry)
	{
		json::Value root = json::Value::object();
		if(std::ifstream(_path).good())
		{
			std::string error;
			auto existing = readJson(_path, error);
			if(!existing)
			{
				std::printf("mdmmPerfGateTest golden: FAIL (%s; the file is left as it is)\n", error.c_str());
				return 1;
			}
			root = std::move(*existing);
		}
		root.put("format", 1);
		root.put("about", "mdmmPerfGateTest goldens (doc/md_mm_performance_diagnostics.md). "
			"Key: ROM fingerprint/scenario/outputs/GEARMULATOR_MDMM_SPEEDUPS position/seconds per phase. "
			"compare: the hashes and counts a run must reproduce; "
			"info: the recording run's instruction figures and guest cycles, never compared.");
		if(const auto* existing = root.find("entries"); !existing || !existing->isObject())
			root.put("entries", json::Value::object());
		auto* entries = root.find("entries");	// put() returns the object it was called on, not the member
		entries->put(_key, _entry);
		auto& members = entries->asObject();
		std::sort(members.begin(), members.end(),
			[](const json::Value::Member& _a, const json::Value::Member& _b) { return _a.first < _b.first; });
		std::ofstream out(_path, std::ios::binary | std::ios::trunc);
		out << json::write(root, 1) << "\n";
		out.close();
		if(!out.good())
		{
			std::printf("mdmmPerfGateTest golden: FAIL (cannot write %s)\n", _path.c_str());
			return 1;
		}
		std::printf("mdmmPerfGateTest golden: RECORDED %s (%zu fields) in %s\n", _key.c_str(),
			_entry.find("compare")->asObject().size(), _path.c_str());
		return 0;
	}

	// The run against the entry for _key: every compared field equal, none missing on either side. The info fields are
	// shown beside the recording's, never judged.
	int compareGolden(const std::string& _path, const std::string& _key, const json::Value& _run)
	{
		std::string error;
		const auto root = readJson(_path, error);
		if(!root)
		{
			std::printf("mdmmPerfGateTest golden: FAIL (%s)\n", error.c_str());
			return 1;
		}
		const auto* entries = root->find("entries");
		const auto* entry = entries ? entries->find(_key) : nullptr;
		const auto* want = entry ? entry->find("compare") : nullptr;
		if(!want || !want->isObject())
		{
			std::printf("mdmmPerfGateTest golden: FAIL (no entry %s in %s; record one with --record)\n", _key.c_str(),
				_path.c_str());
			return 1;
		}
		const auto& got = *_run.find("compare");
		size_t differ = 0;
		std::string first;
		const auto differs = [&](const std::string& _field, const std::string& _golden, const std::string& _run)
		{
			std::printf("mdmmPerfGateTest golden: differs %s golden=%s run=%s\n", _field.c_str(), _golden.c_str(),
				_run.c_str());
			if(!differ++)
				first = _field;
		};
		for(const auto& [field, value] : want->asObject())
		{
			const auto* mine = got.find(field);
			const auto golden = value.isString() ? value.asString() : json::write(value);
			if(!mine)
				differs(field, golden, "(missing)");
			else if(mine->asString() != golden)
				differs(field, golden, mine->asString());
		}
		for(const auto& [field, value] : got.asObject())
			if(!want->find(field))
				differs(field, "(missing)", value.asString());
		if(const auto* info = entry->find("info"))
		{
			for(const auto& [field, value] : _run.find("info")->asObject())
			{
				const auto* recorded = info->find(field);
				if(!recorded || !recorded->isNumber() || recorded->asNumber() == 0)
					continue;
				std::printf("mdmmPerfGateTest golden: info %s golden=%.0f run=%.0f (%+.1f %%)\n", field.c_str(),
					recorded->asNumber(), value.asNumber(), (value.asNumber() / recorded->asNumber() - 1.0) * 100.0);
			}
		}
		const auto total = want->asObject().size();
		if(differ)
		{
			std::printf("mdmmPerfGateTest golden: FAIL (%zu of %zu fields differ from %s, first %s)\n", differ, total,
				_key.c_str(), first.c_str());
			return 1;
		}
		std::printf("mdmmPerfGateTest golden: PASS (%zu fields equal %s)\n", total, _key.c_str());
		return 0;
	}

	void usage()
	{
		std::puts("usage: mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8] [--scenario <name>] "
			"[--outputs stereo|all] [--golden <goldens.json> [--record]] [--wav <file.wav>]");
		std::string names;
		for(const auto& s : g_scenarios)
			names += std::string(" ") + s.name;
		std::printf("  scenarios:%s\n", names.c_str());
	}
}

// 32-bit float, two channels, interleaved, little-endian (the hosts this runs on).
static bool writeStereoWav(const std::string& _path, const std::vector<float>& _left, const std::vector<float>& _right,
	const size_t _first, const size_t _count)
{
	std::ofstream out(_path, std::ios::binary | std::ios::trunc);
	if(!out)
		return false;
	const auto u32 = [&out](const uint32_t _v) { out.write(reinterpret_cast<const char*>(&_v), 4); };
	const auto u16 = [&out](const uint16_t _v) { out.write(reinterpret_cast<const char*>(&_v), 2); };
	const auto dataBytes = static_cast<uint32_t>(_count * 2 * sizeof(float));
	out.write("RIFF", 4); u32(36 + dataBytes); out.write("WAVE", 4);
	out.write("fmt ", 4); u32(16); u16(3); u16(2); u32(g_rate); u32(g_rate * 2 * sizeof(float)); u16(2 * sizeof(float));
	u16(32);
	out.write("data", 4); u32(dataBytes);
	for(size_t i = _first; i < _first + _count; ++i)
	{
		const float frame[2] = {_left[i], _right[i]};
		out.write(reinterpret_cast<const char*>(frame), sizeof(frame));
	}
	return static_cast<bool>(out);
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		usage();
		return 77;
	}
	const std::string model = _argv[2];
	if(model != "md" && model != "mm")
	{
		std::puts("mdmmPerfGateTest: the machine is md or mm");
		return 2;
	}
	const bool mm = model == "mm";
	double seconds = 8.0;
	std::string scenarioName = mm ? "mm-a01" : "md-busy";
	bool allOutputs = false;
	std::string goldenPath;
	bool recordGolden = false;
	std::string wavPath;
	for(int i = 3; i < _argc; ++i)
	{
		const std::string arg = _argv[i];
		const bool hasValue = i + 1 < _argc;
		if(arg == "--scenario" && hasValue)
			scenarioName = _argv[++i];
		else if(arg == "--outputs" && hasValue)
		{
			const std::string outputs = _argv[++i];
			if(outputs != "stereo" && outputs != "all")
			{
				std::puts("mdmmPerfGateTest: --outputs is stereo or all");
				return 2;
			}
			allOutputs = outputs == "all";
		}
		else if(arg == "--golden" && hasValue)
			goldenPath = _argv[++i];
		else if(arg == "--record")
			recordGolden = true;
		else if(arg == "--wav" && hasValue)
			wavPath = _argv[++i];
		else if(i == 3 && arg.rfind("--", 0) != 0)
			seconds = std::atof(arg.c_str());
		else
		{
			std::printf("mdmmPerfGateTest: unknown argument %s\n", arg.c_str());
			usage();
			return 2;
		}
	}
	if(!(seconds > 0))
	{
		std::puts("mdmmPerfGateTest: seconds-per-phase must be above 0");
		return 2;
	}
	const auto* scenario = findScenario(scenarioName);
	if(!scenario || scenario->mm != mm)
	{
		std::printf("mdmmPerfGateTest: no scenario %s for %s\n", scenarioName.c_str(), model.c_str());
		usage();
		return 2;
	}
	if(recordGolden && goldenPath.empty())
	{
		std::puts("mdmmPerfGateTest: --record needs --golden <goldens.json>");
		return 2;
	}

#ifdef __APPLE__
	// The performance cores, as the plug-in's audio thread gets them: steadier cycle counts.
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

	try
	{
		const auto rom = load(_argv[1]);
		// The machine is large: on the heap.
		auto machine = std::make_unique<Machine>(rom, _argv[1], Bytes{}, true,
			mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum);
		auto& m = *machine;
		scenario->setup(m);

		const char* const name = mm ? "MM" : "MD";
		const bool speedUps = m.hardware().speedUps();
		const auto fingerprint = m.hardware().firmwareFingerprint();
		uint64_t combined = g_fnvOffset;

		// Start the burst of notes and listen: the stopped phase renders the hits, the hash sees what they send out.
		HitBurst hits(m, mm);
		MidiOut midiOut;
		midiOut.listen(m);
		Capture capture(m, allOutputs, mm ? md::MmTelemetry::g_stepAddress : g_playheadAddress);
		m.onBlock = [&]
		{
			hits.post();
			capture.onBlock();
		};
		capture.start();
		hits.start();

		const auto stereoHash = [&](const size_t _first, const size_t _count)
		{
			const size_t bytes = _count * sizeof(float);
			return fnv1a(fnv1a(g_fnvOffset, m.left().data() + _first, bytes), m.right().data() + _first, bytes);
		};

		std::vector<Line> lines;
		const auto phase = [&](const char* _phase)
		{
			m.resetAudioTime();
			const size_t first = m.left().size();
			const uint64_t ucFirst = m.hardware().hostCurrentCycle();
			const uint64_t movesFirst = capture.moves();
			const double cpuFirst = threadCpuSeconds();
			m.run(seconds * 1000.0);
			const double cpu = threadCpuSeconds() - cpuFirst;

			const size_t count = m.left().size() - first;
			// a phase shorter than one block renders nothing: report zeros, not a division by zero
			const double perFrame = count ? 1.0 / static_cast<double>(count) : 0.0;
			const size_t bytes = count * sizeof(float);
			combined = fnv1a(combined, m.left().data() + first, bytes);
			combined = fnv1a(combined, m.right().data() + first, bytes);
			size_t nonsilent = 0;
			for(size_t i = first; i < first + count; ++i)
				nonsilent += m.left()[i] != 0.0f || m.right()[i] != 0.0f;

			Line line;
			line.section = _phase;
			line.head = std::string(name) + " " + _phase + ":";
			line.add("cpu_pct", format("%.2f", cpu * perFrame * g_rate * 100), Field::Kind::Print);
			line.info("instr_per_frame", "%.0f", sum(m.blockMInstr) * 1e6 * perFrame);
			line.add("cycles_per_frame", format("%.0f", sum(m.blockMCycles) * 1e6 * perFrame), Field::Kind::Print);
			const auto ucCycles = static_cast<double>(m.hardware().hostCurrentCycle() - ucFirst);
			line.info("uc_cycles_per_frame", "%.3f", ucCycles * perFrame);
			line.compare("audio_hash", hex(stereoHash(first, count)));
			line.count("frames", count);
			line.count("nonsilent_frames", nonsilent);
			line.count("playhead_moves", capture.moves() - movesFirst);
			line.info("instr_median_per_frame", "%.0f", median(m.blockMInstr) * 1e6 / g_block);
			if(allOutputs)
			{
				Capture::Hashes each{};
				line.compare("outputs_hash", hex(capture.hashes(first, count, each)));
				for(size_t c = 0; c < g_outputs; ++c)
					line.compare("out" + std::to_string(c) + "_hash", hex(each[c]));
			}
			line.write();
			lines.push_back(std::move(line));
			return capture.moves() - movesFirst;
		};

		std::printf("mdmmPerfGateTest: %s, %.1f s per phase, %u-frame blocks at %u Hz, scenario=%s outputs=%s "
			"speedups=%s fingerprint=%s\n", name, seconds, g_block, g_rate, scenario->name,
			allOutputs ? "all" : "stereo", speedUps ? "on" : "off", hex(fingerprint).c_str());
		if(const char* pacing = std::getenv("GEARMULATOR_MDMM_MIDI_PACING"))
			std::printf("mdmmPerfGateTest: GEARMULATOR_MDMM_MIDI_PACING=%s is set: MIDI timing, and so the hashes, "
				"follow it\n", pacing);
		const size_t streamFirst = m.left().size();
		phase("stopped");
		// The press is in the hashed stream: the frames between the phases are press_hash, and stream_hash covers all.
		const size_t pressFirst = m.left().size();
		m.panel(md::PanelControl::Play);
		const size_t pressCount = m.left().size() - pressFirst;
		const auto playingMoves = phase("playing");
		const size_t streamCount = m.left().size() - streamFirst;

		const auto ram = m.snapshotRam();
		auto& uc = m.hardware().getUC();
		const auto patchRam = m.hardware().copyPatchRam();
		Line last;
		last.section = "combined";
		last.head = "combined";
		last.compare("audio_hash", hex(combined));
		last.compare("ram_hash", hex(fnv1a(g_fnvOffset, ram.data(), ram.size())));
		last.compare("sram_hash", hex(hashWindow(uc, g_fnvOffset, md::memorymap::g_internalSram)));
		last.compare("loader_hash", hex(hashWindow(uc, g_fnvOffset, md::memorymap::g_loaderRam)));
		last.compare("patch_hash", hex(fnv1a(g_fnvOffset, patchRam.data(), patchRam.size())));
		last.compare("midi_out_hash", hex(midiOut.hash));
		last.count("midi_out_events", midiOut.events);
		last.count("press_frame", pressFirst - streamFirst);
		last.count("press_frames", pressCount);
		last.compare("press_hash", hex(stereoHash(pressFirst, pressCount)));
		last.count("stream_frames", streamCount);
		last.compare("stream_hash", hex(stereoHash(streamFirst, streamCount)));
		if(allOutputs)
		{
			Capture::Hashes each{};
			last.compare("stream_outputs_hash", hex(capture.hashes(streamFirst, streamCount, each)));
		}
		last.write();
		lines.push_back(std::move(last));
		if(!wavPath.empty() && !writeStereoWav(wavPath, m.left(), m.right(), streamFirst, streamCount))
		{
			std::printf("mdmmPerfGateTest: could not write %s\n", wavPath.c_str());
			return 2;
		}

		if(playingMoves == 0)
		{
			std::printf("mdmmPerfGateTest: FAIL (%s did not play: its step never moved in the playing phase)\n", name);
			return 1;
		}
		if(goldenPath.empty())
			return 0;
		char secondsText[32];
		std::snprintf(secondsText, sizeof(secondsText), "%gs", seconds);
		const auto key = hex(fingerprint) + "/" + scenario->name + "/" + (allOutputs ? "all" : "stereo") + "/speedups-"
			+ (speedUps ? "on" : "off") + "/" + secondsText;
		const auto firmware = md::firmwareName(mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum);
		const auto entry = entryOf(lines, firmware);
		return recordGolden ? record(goldenPath, key, entry) : compareGolden(goldenPath, key, entry);
	}
	catch(const std::exception& e)
	{
		std::printf("mdmmPerfGateTest: %s\n", e.what());
		return 1;
	}
}
