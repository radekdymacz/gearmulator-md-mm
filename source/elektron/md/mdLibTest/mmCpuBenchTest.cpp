// MM-P3: the CPU one emulated Monomachine OS 1.32B costs, headless (no editor, no host), the
// same method as mdCpuBenchTest (P4) so the two machines compare:
//   mmCpuBenchTest <ROM> [instances=1] [seconds=30]
// Each instance boots, then renders audio as fast as it can, first stopped, then playing its
// current pattern (the factory A01 "SUPERWAVES": six synth tracks, 116 trigs over 64 steps), in
// 64-frame blocks. Reported per instance: thread CPU time per second of audio = the share of one
// core it needs in real time. Instances run in parallel threads (a host with several instances).
// A playing result counts only if the machine played: its step counter (MmTelemetry::g_stepAddress)
// must move while it renders the playing part, read every 50 ms; else the run exits 1.
// Exits 77 without a ROM.

#include "mdFirmwareSession.h"

#include "mdLib/mmtelemetry.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <memory>
#include <sys/resource.h>
#include <thread>

using namespace mdFirmwareSession;

namespace
{
	double processCpuSeconds()
	{
		rusage u{};
		getrusage(RUSAGE_SELF, &u);
		return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) * 1e-6;
	}

	double threadCpuSeconds()
	{
		timespec t{};
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
		return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
	}

	std::atomic<double> g_procStart{0};

	// A machine that plays moves its step counter many times a second: two moves are the
	// least that is not a single jump.
	constexpr int g_minSteps = 2;

	struct Result { double stopped = 0, playing = 0; int steps = 0; };

	Result bench(const Bytes& _rom, const std::string& _name, const double _seconds, std::atomic<int>& _ready, const int _instances)
	{
		// The machine is large: on the heap (thread stacks are small).
		auto mp = std::make_unique<Machine>(_rom, _name, Bytes{}, true, md::MachineModel::Monomachine);
		auto& m = *mp;
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
		// Count the steps it plays, so a result is known to be a playing machine.
		int last = m.read8(md::MmTelemetry::g_stepAddress), steps = 0;
		const double c0 = threadCpuSeconds();
		for(double t = 0; t < _seconds * 2 / 3; t += 0.05)
		{
			m.run(50.0);
			const int s = m.read8(md::MmTelemetry::g_stepAddress);
			if(s != last) { ++steps; last = s; }
		}
		r.playing = (threadCpuSeconds() - c0) / (_seconds * 2 / 3);
		r.steps = steps;
		return r;
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mmCpuBenchTest <ROM> [instances] [seconds]");
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
	std::printf("mmCpuBenchTest: %d instance(s), %.0f s of audio each (64-frame blocks, %u Hz), wall %.1f s incl. boot\n", n, seconds,
		static_cast<unsigned>(g_rate), wall);
	for(int i = 0; i < n; ++i)
		std::printf("  instance %d: stopped %.1f %% of one core, playing A01 %.1f %% of one core (%d steps played)\n", i + 1,
			results[static_cast<size_t>(i)].stopped * 100, results[static_cast<size_t>(i)].playing * 100, results[static_cast<size_t>(i)].steps);
	std::printf("  whole process (every thread, e.g. DSP threads too): %.1f %% of one core per instance, averaged over stopped + playing\n",
		proc / (seconds * n) * 100);
	int failures = 0;
	for(int i = 0; i < n; ++i)
	{
		if(results[static_cast<size_t>(i)].steps >= g_minSteps)
			continue;
		std::printf("  FAIL instance %d did not play: its step counter moved %d time(s) after PLAY\n", i + 1,
			results[static_cast<size_t>(i)].steps);
		++failures;
	}
	return failures ? 1 : 0;
}
