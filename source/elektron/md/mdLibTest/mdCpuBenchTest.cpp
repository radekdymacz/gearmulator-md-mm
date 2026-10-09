// P4: the CPU one emulated MD OS 1.63 costs, headless (no editor, no host), repeatable:
//   mdCpuBenchTest <ROM> [instances=1] [seconds=30]
// Each instance boots, then renders audio as fast as it can, first stopped, then playing
// its current pattern (made busy: every track on 16ths), in 64-frame blocks at 44.1 kHz.
// Reported per instance: thread CPU time per second of audio = the share of one core it
// needs in real time. Instances run in parallel threads (a host with several instances).
// A playing result counts only if the machine played: its playhead (RAM 0x261aa7) must
// move while it renders the playing part, read every 50 ms; else the run exits 1.
// Exits 77 without a ROM.

#include "mdFirmwareSession.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdPattern.h"

#include <atomic>
#include <memory>
#include <ctime>
#include <sys/resource.h>
#include <thread>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	double processCpuSeconds()
	{
		rusage u{};
		getrusage(RUSAGE_SELF, &u);
		return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) * 1e-6;
	}

	std::atomic<double> g_procStart{0};

	double threadCpuSeconds()
	{
		timespec t{};
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
		return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
	}

	// A machine that plays moves its playhead many times a second (16ths at 120 BPM: 8): two
	// moves are the least that is not a single jump.
	constexpr int g_minPlayheadMoves = 2;

	struct Result { double stopped = 0, playing = 0; int moves = 0; };

	Result bench(const Bytes& _rom, const std::string& _name, const double _seconds, std::atomic<int>& _ready, const int _instances)
	{
		// The machine is large: on the heap (thread stacks are small).
		auto mp = std::make_unique<Machine>(_rom, _name);
		auto& m = *mp;
		const auto slot = static_cast<uint8_t>(ed::parseMdStatusResponse(m.request(ed::mdStatusRequest(ed::MdStatus::Pattern), 0x72))->value);
		auto p = *ed::decodeMdPattern(m.request(ed::mdPatternRequest(slot), ed::g_mdPatternDump));
		for(auto& t : p.trigs)
			t = 0x5555555555555555ull & ((p.length >= 64) ? ~0ull : ((1ull << p.length) - 1));
		m.send(ed::encodeMdPattern(p));
		if(++_ready == _instances)
			g_procStart = processCpuSeconds();
		while(_ready.load() < _instances)
			std::this_thread::yield();
		const auto run = [&](const double _s)
		{
			const double c0 = threadCpuSeconds();
			m.run(_s * 1000.0);
			return (threadCpuSeconds() - c0) / _s;
		};
		Result r;
		r.stopped = run(_seconds / 3);
		m.panel(md::PanelControl::Play);
		// Count the playhead's moves, so a result is known to be a playing machine.
		int last = m.playhead();
		const double c0 = threadCpuSeconds();
		for(double t = 0; t < _seconds * 2 / 3; t += 0.05)
		{
			m.run(50.0);
			const int step = m.playhead();
			r.moves += step != last;
			last = step;
		}
		r.playing = (threadCpuSeconds() - c0) / (_seconds * 2 / 3);
		return r;
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdCpuBenchTest <ROM> [instances] [seconds]");
		return 77;
	}
	const auto rom = load(_argv[1]);
	const int n = _argc > 2 ? std::max(1, std::atoi(_argv[2])) : 1;
	const double seconds = _argc > 3 ? std::atof(_argv[3]) : 30.0;
	std::vector<Result> results(static_cast<size_t>(n));
	std::vector<std::thread> threads;
	std::atomic<int> ready{0};
	const auto w0 = std::chrono::steady_clock::now();
	for(int i = 0; i < n; ++i)
		threads.emplace_back([&, i] { results[static_cast<size_t>(i)] = bench(rom, _argv[1], seconds, ready, n); });
	for(auto& t : threads)
		t.join();
	const double proc = processCpuSeconds() - g_procStart;
	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
	std::printf("mdCpuBenchTest: %d instance(s), %.0f s of audio each (64-frame blocks, 44.1 kHz), wall %.1f s incl. boot\n", n, seconds, wall);
	for(int i = 0; i < n; ++i)
		std::printf("  instance %d: stopped %.1f %% of one core, playing a busy pattern %.1f %% of one core\n", i + 1,
			results[static_cast<size_t>(i)].stopped * 100, results[static_cast<size_t>(i)].playing * 100);
	std::printf("  whole process (every thread, e.g. DSP threads too): %.1f %% of one core per instance, averaged over stopped + playing\n",
		proc / (seconds * n) * 100);
	int failures = 0;
	for(int i = 0; i < n; ++i)
	{
		if(results[static_cast<size_t>(i)].moves >= g_minPlayheadMoves)
			continue;
		std::printf("  FAIL instance %d did not play: its playhead moved %d time(s) after PLAY\n", i + 1,
			results[static_cast<size_t>(i)].moves);
		++failures;
	}
	return failures ? 1 : 0;
}
