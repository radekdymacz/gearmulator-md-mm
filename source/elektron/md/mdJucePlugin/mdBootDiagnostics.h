#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "juce_core/juce_core.h"

namespace mdJucePlugin
{
	// B-035: whether the host runs the plug-in's audio, and whether the machine keeps up. The host's calls are
	// counted on the audio thread (processBlock and processBlockBypassed apart); once a second, for the first 30 s
	// of the processor, the message thread writes a line into the editor's start-up log (editor-*.log, B-022, the
	// one Open Log Folder shows), standalone and plug-in alike, and keeps the rates the page's start-up card shows
	// (the session's "audioRun" message).

	// The audio thread's counters (lock-free).
	struct AudioRunCounters
	{
		std::atomic<uint64_t> blocks{0};		// processBlock calls
		std::atomic<uint64_t> bypassed{0};		// processBlockBypassed calls (the host bypasses or deactivated it)
		std::atomic<uint64_t> nonRealtime{0};	// blocks while the host renders offline
		std::atomic<int> blockSize{0};			// the last block's frames

		void onBlock(const int _frames, const bool _offline)
		{
			blocks.fetch_add(1, std::memory_order_relaxed);
			if(_offline)
				nonRealtime.fetch_add(1, std::memory_order_relaxed);
			blockSize.store(_frames, std::memory_order_relaxed);
		}
		void onBypassed(const int _frames)
		{
			bypassed.fetch_add(1, std::memory_order_relaxed);
			blockSize.store(_frames, std::memory_order_relaxed);
		}
	};

	// What the message thread reads once a second (a value).
	struct BootSample
	{
		double wallMs = 0;			// since the processor started
		double sampleRate = 0;
		int blockSize = 0;
		uint64_t blocks = 0, bypassed = 0, nonRealtime = 0;
		uint64_t cycles = 0;		// the machine's MCU cycles (machine time; md::g_ucClockHz a second)
		int dspBooted = 0, dsps = 2;
		bool firmwareMidiReady = false;
		std::string lifecycle;		// the desk's (deskCore::lifecycleName)
		std::string rom;			// the ROM file the machine runs, or "none"
	};

	// The rates between two samples. Pure.
	struct BootRate
	{
		double blocksPerSecond = 0;
		double realtime = 0;		// machine seconds per wall second; 0 when the machine did not run
		bool known = false;			// false for the first sample

		static BootRate between(const BootSample& _before, const BootSample& _after);
	};

	// The log line: boot t=12s rate=44100 block=256 blocks=0 (0/s) bypassed=0 nonRealtime=0 realtime=0.00x
	// dspBooted=0/2 firmwareMidiReady=0 lifecycle=booting rom=<file> cycles=<n>. Pure.
	std::string bootLine(const BootSample& _s, const BootRate& _r);

	// The message thread's side: the lines (kept until the editor names its log), the last sample and rate.
	class BootDiagnostics
	{
	public:
		static constexpr double g_logSeconds = 30;

		AudioRunCounters counters;

		// Once a second (the processor's timer); false after the first 30 s, when nothing is written any more.
		void record(const BootSample& _s);
		// The editor's start-up log (B-022): the lines so far are written into it, the next ones as they come.
		void setLog(const juce::File& _file);

		const BootSample& last() const { return m_last; }
		const BootRate& rate() const { return m_rate; }
		bool sampled() const { return m_sampled; }

	private:
		void write(const std::string& _line);

		BootSample m_last;
		BootRate m_rate;
		bool m_sampled = false;
		juce::File m_log;
		std::vector<std::string> m_pending;
	};
}
