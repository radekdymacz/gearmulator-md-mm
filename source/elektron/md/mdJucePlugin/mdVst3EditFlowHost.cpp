// DESIGN-edit-flow.md, the proof in a real host: loads a built VST3 bundle (Gearmulator MD.vst3 or MM.vst3)
// through JUCE's VST3 hosting, as a DAW does, and runs it with a real-time audio thread (a time-constraint
// thread at the block's period) calling processBlock at 48 kHz, while the message loop runs. The bundle's
// edit-flow driver (mdEditFlowDriver.h, a test build) plays the page's messages into the plug-in's session
// in phases and writes their time windows and counts; this host times every audio block and counts the
// host parameter notifications and gestures the VST3 wrapper reports, then prints each phase.
//
//   mdVst3EditFlowHost <bundle.vst3> [block=128] [out-prefix=/tmp/editflow] [rate=48000]
//
// GEARMULATOR_EDITFLOW_DRIVE is set to <out-prefix>.driver.txt unless it is set already. What is real: the
// bundle, the VST3 wrapper, processBlock on a real-time thread, the firmware, the session, the controller.
// What is replayed: the page's messages (the driver sends what the page sends; no web view runs).

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_events/juce_events.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

	struct Listener final : juce::AudioProcessorListener
	{
		std::mutex m;
		std::vector<double> params, begins, ends, changed;
		void add(std::vector<double>& _v) { const std::lock_guard l(m); _v.push_back(nowMs()); }
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { add(params); }
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { add(changed); }
		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { add(begins); }
		void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { add(ends); }
	};

	struct Block { double atMs; float us; };

	class Audio final : public juce::Thread
	{
	public:
		Audio(juce::AudioPluginInstance& _p, const int _block, const double _rate) : Thread("audio"), m_p(_p), m_block(_block), m_rate(_rate)
		{
			m_blocks.reserve(static_cast<size_t>(_rate / _block * 400));
		}
		void run() override
		{
			const int channels = std::max(m_p.getTotalNumInputChannels(), m_p.getTotalNumOutputChannels());
			juce::AudioBuffer<float> buf(channels, m_block);
			juce::MidiBuffer midi;
			const auto period = std::chrono::duration<double>(m_block / m_rate);
			auto next = std::chrono::steady_clock::now();
			while(!threadShouldExit())
			{
				buf.clear();
				midi.clear();
				const auto at = nowMs();
				const auto t = std::chrono::steady_clock::now();
				m_p.processBlock(buf, midi);
				const float us = static_cast<float>(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t).count());
				if(m_blocks.size() < m_blocks.capacity())
					m_blocks.push_back({at, us});
				next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
				const auto n = std::chrono::steady_clock::now();
				if(next > n)
					std::this_thread::sleep_until(next);
				else if(n - next > std::chrono::milliseconds(100))
					next = n;	// far behind: start over, as a device would
			}
		}
		const std::vector<Block>& blocks() const { return m_blocks; }
	private:
		juce::AudioPluginInstance& m_p;
		int m_block;
		double m_rate;
		std::vector<Block> m_blocks;
	};

	std::map<std::string, std::string> fields(const std::string& _line)
	{
		std::map<std::string, std::string> f;
		std::istringstream s(_line);
		std::string w;
		std::string key;
		while(s >> w)
		{
			const auto eq = w.find('=');
			if(eq != std::string::npos)
			{
				key = w.substr(0, eq);
				f[key] = w.substr(eq + 1);
			}
			else if(!key.empty() && !f[key].empty() && f[key].front() == '[' && f[key].back() != ']')
				f[key] += " " + w;
		}
		return f;
	}

	size_t count(const std::vector<double>& _v, const double _a, const double _b)
	{
		return static_cast<size_t>(std::count_if(_v.begin(), _v.end(), [&](const double _t) { return _t >= _a && _t < _b; }));
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdVst3EditFlowHost <bundle.vst3> [block=128] [out-prefix] [rate=48000]");
		return 2;
	}
	const std::string bundle = _argv[1];
	const int block = _argc > 2 ? std::atoi(_argv[2]) : 128;
	const std::string prefix = _argc > 3 ? _argv[3] : "/tmp/editflow";
	const double rate = _argc > 4 ? std::atof(_argv[4]) : 48000.0;
	const std::string driverFile = std::getenv("GEARMULATOR_EDITFLOW_DRIVE") ? std::getenv("GEARMULATOR_EDITFLOW_DRIVE") : prefix + ".driver.txt";
	setenv("GEARMULATOR_EDITFLOW_DRIVE", driverFile.c_str(), 1);
	std::remove(driverFile.c_str());

	juce::ScopedJuceInitialiser_GUI juce;
	juce::AudioPluginFormatManager formats;
	formats.addFormat(new juce::VST3PluginFormat());
	juce::KnownPluginList list;
	juce::OwnedArray<juce::PluginDescription> found;
	list.scanAndAddFile(bundle, true, found, *formats.getFormat(0));
	if(found.isEmpty())
	{
		std::printf("no VST3 plug-in in %s\n", bundle.c_str());
		return 1;
	}
	juce::String error;
	auto plugin = formats.createPluginInstance(*found[0], rate, block, error);
	if(!plugin)
	{
		std::printf("could not load: %s\n", error.toRawUTF8());
		return 1;
	}
	std::printf("%s: %s, %d-frame blocks at %.0f Hz (%.3f ms deadline), %d in / %d out\n", bundle.c_str(), plugin->getName().toRawUTF8(),
		block, rate, 1000.0 * block / rate, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
	Listener listener;
	plugin->addListener(&listener);
	plugin->setRateAndBufferSizeDetails(rate, block);
	plugin->prepareToPlay(rate, block);
	Audio audio(*plugin, block, rate);
	const double periodMs = 1000.0 * block / rate;
	const bool realtime = audio.startRealtimeThread(juce::Thread::RealtimeOptions{}.withPeriodMs(periodMs).withMaximumProcessingTimeMs(periodMs));
	std::printf("audio thread: %s\n", realtime ? "real-time (time constraint)" : "NOT real-time");

	// The message loop, until the driver says it is done.
	const auto t0 = nowMs();
	bool done = false;
	while(!done && nowMs() - t0 < 400000)
	{
		const auto until = nowMs() + 100;
		while(nowMs() < until)
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);
		std::ifstream f(driverFile);
		std::string l;
		while(std::getline(f, l))
			if(l.rfind("done", 0) == 0) done = true;
	}
	audio.stopThread(2000);
	plugin->releaseResources();
	plugin->removeListener(&listener);

	std::ifstream f(driverFile);
	std::string l;
	const auto& blocks = audio.blocks();
	while(std::getline(f, l))
	{
		std::printf("driver: %s\n", l.c_str());
		if(l.rfind("phase ", 0) != 0)
			continue;
		auto p = fields(l);
		const double a = std::atof(p["t0"].c_str()), e = std::atof(p["activeEnd"].c_str()), b = std::atof(p["t1"].c_str());
		// the blocks while the gesture runs (idle phases: the whole phase)
		std::vector<float> us;
		for(const auto& x : blocks)
			if(x.atMs >= a && x.atMs < e)
				us.push_back(x.us);
		std::vector<float> tail;
		for(const auto& x : blocks)
			if(x.atMs >= e && x.atMs < b)
				tail.push_back(x.us);
		const auto stats = [&](std::vector<float> _v, const char* _what)
		{
			if(_v.empty()) return;
			double sum = 0;
			int over = 0;
			for(const auto x : _v) { sum += x; over += x > periodMs * 1000.0; }
			std::sort(_v.begin(), _v.end());
			const auto pct = [&](const double _q) { return _v[std::min(_v.size() - 1, static_cast<size_t>(_q * _v.size()))]; };
			std::printf("  %-6s %-5s blocks %5zu | load %5.1f %% | p50 %6.0f p99 %6.0f max %7.0f us | over deadline %d (%.2f %%)\n",
				p["name"].c_str(), _what, _v.size(), 100.0 * sum / _v.size() / (periodMs * 1000.0), pct(.5), pct(.99), _v.back(), over,
				100.0 * over / _v.size());
		};
		stats(us, "live");
		stats(tail, "tail");
		std::printf("  %-6s host: param notifications %zu, gestures %zu/%zu, updateHostDisplay %zu\n", p["name"].c_str(),
			count(listener.params, a, b), count(listener.begins, a, b), count(listener.ends, a, b), count(listener.changed, a, b));
	}
	plugin.reset();
	return done ? 0 : 1;
}
