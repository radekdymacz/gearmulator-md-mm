// The gate for the emulation CPU plan (doc/modern-ux/RESEARCH-emulation-cpu.md, section 5): one tool that says
// whether a change to the emulator kept every sample and every byte of RAM the same, and what it did to the
// host cost of one audio frame. A lever of the plan lands only if the hashes of the old and the new binary are
// identical and the instructions and cycles per frame drop by the amount the lever promises.
//   mdmmPerfGateTest <ROM> md|mm [seconds-per-phase=8]
// Boots the machine (MD OS 1.63 or MM OS 1.32B) headless, then renders two phases in 64-frame blocks at 44.1 kHz:
// "stopped" and, after a press of PLAY, "playing". The MD's pattern is made busy first (every track on 16ths,
// as mdCpuBenchTest does it); the MM plays the pattern it boots with. One line per phase, fixed field names so
// that a script can compare runs:
//   cpu_pct              the thread's CPU time as a share of one core (wall-clock, so it moves with the computer's load)
//   instr_per_frame      retired host instructions per audio frame, from thread_selfcounts (macOS): independent of the load
//                        of the computer, but two runs still differ by about 0.5 %, so judge a change by medians
//   cycles_per_frame     host CPU cycles per audio frame, the same counter: steadier than time, but the core type matters
//   uc_cycles_per_frame  guest ColdFire cycles per audio frame (what the firmware was given, not what it cost the host)
//   audio_hash           FNV-1a 64 of the phase's left then right samples, as raw floats
// The last line gives the audio hash of both phases chained, and the FNV-1a 64 of the 1 MiB of RAM at $200000.
// Compare those two across builds; compare the per-frame costs as medians of a few paired runs (old, new, old, new).
// Manual: needs a user-supplied ROM (no firmware is bundled). Exits 77 without one. The counters need macOS; on
// other systems they print as 0 and only the hashes and cpu_pct mean something.

#include "mdFirmwareSession.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdPattern.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>
#include <string>

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

	// The MD's pattern with a trig on every 16th of every track, so the sequencer and the voices have work.
	void makeBusy(Machine& _m)
	{
		const auto slot = static_cast<uint8_t>(ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72))->value);
		auto pattern = *ed::decodeMdPattern(_m.request(ed::mdPatternRequest(slot), ed::g_mdPatternDump));
		for(auto& trigs : pattern.trigs)
			trigs = 0x5555555555555555ull & ((pattern.length >= 64) ? ~0ull : ((1ull << pattern.length) - 1));
		_m.send(ed::encodeMdPattern(pattern));
	}
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

		const auto phase = [&](const char* _phase)
		{
			m.resetAudioTime();
			const size_t first = m.left().size();
			const uint64_t ucFirst = m.hardware().hostCurrentCycle();
			const double cpuFirst = threadCpuSeconds();
			m.run(seconds * 1000.0);
			const double cpu = threadCpuSeconds() - cpuFirst;

			const size_t count = m.left().size() - first;
			const double frames = static_cast<double>(count);
			const size_t bytes = count * sizeof(float);
			uint64_t hash = fnv1a(g_fnvOffset, m.left().data() + first, bytes);
			hash = fnv1a(hash, m.right().data() + first, bytes);
			combined = fnv1a(combined, m.left().data() + first, bytes);
			combined = fnv1a(combined, m.right().data() + first, bytes);

			std::printf("%s %s: cpu_pct=%.2f instr_per_frame=%.0f cycles_per_frame=%.0f uc_cycles_per_frame=%.3f audio_hash=%016llx\n",
				name, _phase, cpu / (frames / g_rate) * 100, sum(m.blockMInstr) * 1e6 / frames, sum(m.blockMCycles) * 1e6 / frames,
				static_cast<double>(m.hardware().hostCurrentCycle() - ucFirst) / frames, static_cast<unsigned long long>(hash));
		};

		std::printf("mdmmPerfGateTest: %s, %.1f s per phase, %u-frame blocks at %u Hz\n", name, seconds, g_block, g_rate);
		phase("stopped");
		m.panel(md::PanelControl::Play);
		phase("playing");

		const auto ram = m.snapshotRam();
		std::printf("combined audio_hash=%016llx ram_hash=%016llx\n", static_cast<unsigned long long>(combined),
			static_cast<unsigned long long>(fnv1a(g_fnvOffset, ram.data(), ram.size())));
		return 0;
	}
	catch(const std::exception& e)
	{
		std::printf("mdmmPerfGateTest: %s\n", e.what());
		return 1;
	}
}
