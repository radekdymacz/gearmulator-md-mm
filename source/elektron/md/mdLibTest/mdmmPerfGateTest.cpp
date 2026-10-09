// The gate for the emulation CPU plan (doc/modern-ux/RESEARCH-emulation-cpu.md, section 5): one tool that says
// whether a change to the emulator kept every sample, every byte of memory and every MIDI byte the same, and what it
// did to the host cost of one audio frame. A lever of the plan lands only if the hashes of the old and the new binary
// are identical and the instructions and cycles per frame drop by the amount the lever promises.
//   mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8]
// Boots the machine (MD OS 1.63 or MM OS 1.32B) headless, then renders two phases in 64-frame blocks at 44.1 kHz:
// "stopped" and, after a press of PLAY, "playing". The MD's pattern is made busy first (every track on 16ths,
// as mdCpuBenchTest does it); the MM plays the pattern it boots with. At the start of the stopped phase a short, fixed
// burst of host notes goes in (the MD's sixteen pads on its base channel, the MM's note 60 on channel 1, each twice,
// within the first 1.5 s), so that the stopped phase renders a few hits and not only silence. One line per phase, fixed
// field names so that a script can compare runs:
//   cpu_pct              the thread's CPU time per second of audio, as a share of one core (it is CPU time, not
//                        wall-clock, but it still moves with the computer's load: caches, clocks, other work)
//   instr_per_frame      retired host instructions per audio frame, from thread_selfcounts (macOS): independent of the
//                        load of the computer, but two runs still differ by about 0.5 %, so judge a change by medians
//   cycles_per_frame     host CPU cycles per audio frame, the same counter: steadier than time, but the core type
//                        matters
//   uc_cycles_per_frame  guest ColdFire cycles per audio frame (what the firmware was given, not what it cost the host)
//   audio_hash           FNV-1a 64 of the phase's left then right samples, as raw floats
// The last line gives the audio hash of both phases chained, then the FNV-1a 64 of
//   ram_hash             the 1 MiB of main RAM at $200000
//   sram_hash            the ColdFire internal SRAM at $01000000 (64 KiB)
//   loader_hash          the upper loader RAM, $310000 to $3fffff
//   patch_hash           patch RAM, as Hardware::copyPatchRam() returns it
//   midi_out_hash        everything the machine sent out on MIDI from the start of the stopped phase to the end of the
//                        playing phase (the PLAY press included): each message with the frame of the block that
//                        returned it and its offset in the block
// and midi_out_events, the number of messages behind that hash. The memory windows are read through the ColdFire's
// public read16 (main RAM through read8, in Machine::snapshotRam); the SIM and HI08 windows are never read, reads
// there have side effects.
// Compare those hashes across builds; compare the per-frame costs as medians of a few paired runs (old, new, old, new).
// The tool uses only what main has, so one source builds against both an old and a new emulator.
// Manual: needs a user-supplied ROM (no firmware is bundled). Exits 77 without one. The counters need macOS; on
// other systems they print as 0 and only the hashes and cpu_pct mean something.

#include "mdFirmwareSession.h"

#include "mdLib/mdmemorymap.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdPattern.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	constexpr uint64_t g_fnvOffset = 1469598103934665603ull;

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

	// The MD's pattern with a trig on every 16th of every track, so the sequencer and the voices have work.
	void makeBusy(Machine& _m)
	{
		const auto status = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72));
		require(status.has_value(), "no reply to the pattern status request");
		auto pattern = ed::decodeMdPattern(
			_m.request(ed::mdPatternRequest(static_cast<uint8_t>(status->value)), ed::g_mdPatternDump));
		require(pattern.has_value(), "no pattern dump");
		for(auto& trigs : pattern->trigs)
			trigs = 0x5555555555555555ull & ((pattern->length >= 64) ? ~0ull : ((1ull << pattern->length) - 1));
		_m.send(ed::encodeMdPattern(*pattern));
	}

	// A short, fixed burst of host notes for the stopped phase to play, so that the phase renders hits and not only
	// silence. The MD gets its pads 36 to 51 on the base channel of global slot 0, the MM note 60 on channel 1. Each
	// note is held for a while (a released MM note is silent at once), and the burst is played twice, the second time
	// softer. The notes go out at fixed frames after start(), from the machine's block callback.
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

		// The first notes are queued at once, the rest follow from the block callback.
		void start()
		{
			m_origin = m_machine.now();
			m_machine.onBlock = [this] { post(); };
			post();
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

		void post()
		{
			while(m_next < m_notes.size() && m_origin + m_notes[m_next].frame <= m_machine.now())
				m_machine.post(m_notes[m_next++].message);
		}

		Machine& m_machine;
		std::vector<Note> m_notes;
		size_t m_next = 0;
		uint64_t m_origin = 0;
	};
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8]");
		return 77;
	}
	const std::string model = _argv[2];
	if(model != "md" && model != "mm")
	{
		std::puts("mdmmPerfGateTest: the machine is md or mm");
		return 2;
	}
	const bool mm = model == "mm";
	const double seconds = _argc > 3 ? std::atof(_argv[3]) : 8.0;
	if(!(seconds > 0))
	{
		std::puts("mdmmPerfGateTest: seconds-per-phase must be above 0");
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
		if(!mm)
			makeBusy(m);

		const char* const name = mm ? "MM" : "MD";
		uint64_t combined = g_fnvOffset;

		// Start the burst of notes and listen: the stopped phase renders the hits, the hash sees what they send out.
		HitBurst hits(m, mm);
		MidiOut midiOut;
		midiOut.listen(m);
		hits.start();

		const auto phase = [&](const char* _phase)
		{
			m.resetAudioTime();
			const size_t first = m.left().size();
			const uint64_t ucFirst = m.hardware().hostCurrentCycle();
			const double cpuFirst = threadCpuSeconds();
			m.run(seconds * 1000.0);
			const double cpu = threadCpuSeconds() - cpuFirst;

			const size_t count = m.left().size() - first;
			// a phase shorter than one block renders nothing: report zeros, not a division by zero
			const double perFrame = count ? 1.0 / static_cast<double>(count) : 0.0;
			const size_t bytes = count * sizeof(float);
			uint64_t hash = fnv1a(g_fnvOffset, m.left().data() + first, bytes);
			hash = fnv1a(hash, m.right().data() + first, bytes);
			combined = fnv1a(combined, m.left().data() + first, bytes);
			combined = fnv1a(combined, m.right().data() + first, bytes);

			const double ucCycles = static_cast<double>(m.hardware().hostCurrentCycle() - ucFirst);
			std::printf("%s %s: cpu_pct=%.2f instr_per_frame=%.0f cycles_per_frame=%.0f uc_cycles_per_frame=%.3f "
				"audio_hash=%016llx\n", name, _phase, cpu * perFrame * g_rate * 100,
				sum(m.blockMInstr) * 1e6 * perFrame, sum(m.blockMCycles) * 1e6 * perFrame, ucCycles * perFrame,
				static_cast<unsigned long long>(hash));
		};

		std::printf("mdmmPerfGateTest: %s, %.1f s per phase, %u-frame blocks at %u Hz\n", name, seconds, g_block,
			g_rate);
		phase("stopped");
		m.panel(md::PanelControl::Play);
		phase("playing");

		const auto ram = m.snapshotRam();
		auto& uc = m.hardware().getUC();
		const auto patchRam = m.hardware().copyPatchRam();
		std::printf("combined audio_hash=%016llx ram_hash=%016llx sram_hash=%016llx loader_hash=%016llx "
			"patch_hash=%016llx midi_out_hash=%016llx midi_out_events=%llu\n",
			static_cast<unsigned long long>(combined),
			static_cast<unsigned long long>(fnv1a(g_fnvOffset, ram.data(), ram.size())),
			static_cast<unsigned long long>(hashWindow(uc, g_fnvOffset, md::memorymap::g_internalSram)),
			static_cast<unsigned long long>(hashWindow(uc, g_fnvOffset, md::memorymap::g_loaderRam)),
			static_cast<unsigned long long>(fnv1a(g_fnvOffset, patchRam.data(), patchRam.size())),
			static_cast<unsigned long long>(midiOut.hash), static_cast<unsigned long long>(midiOut.events));
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("mdmmPerfGateTest: %s\n", e.what());
		return 1;
	}
}
