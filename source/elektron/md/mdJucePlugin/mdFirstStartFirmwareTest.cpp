// B-003: the first start in a data folder without the Machinedrum's UW factory cache. The firmware formats
// its sample flash and the processor starts it again (serviceFactoryInitialization). The page sees that as
// one preparation (lifecycle "loading", no LCD) and one start ("booting", "animating", "ready", the machine's
// LCD on the start-up card): one start-up animation, not two. The editor leaves the preparing machine alone,
// so its factory cache is kept and the next start has no preparation at all. B-012: the page ends that first
// start holding what a normal start gives it (the samples, the global, the kits and patterns it reads), as the
// next start does. Needs the MD OS 1.63 ROM:
//   GEARMULATOR_MD_FIRMWARE_BIN=<ROM> mdFirstStartFirmwareTest
// Exits 77 without it. The config is isolated (EphemeralConfig), the device home an empty temp folder.

#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "synthLib/romLoader.h"

#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Value = elektronData::json::Value;

	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s: %s\n", _ok ? "ok" : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	void pump(const int _ms)
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(_ms);
		while(std::chrono::steady_clock::now() < end)
		{
#if JUCE_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.008, false);
#else
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
#endif
		}
	}

	std::string str(const Value& _v, const char* _key)
	{
		const auto* f = _v.find(_key);
		return f && f->isString() ? f->asString() : std::string();
	}

	// What the page saw during one start of the plug-in.
	struct Start
	{
		std::vector<std::string> lifecycles;	// each change of the machine document's lifecycle, in order
		int lcd = 0;							// LCD frames published
		int lcdWhileLoading = 0;				// ... while the lifecycle was "loading"
		int machines = 0;						// the emulated machines that ran (a reboot makes another)
		int resets = 0;							// "reset": the page dropped every document
		std::set<std::string> held;				// what the page holds at the end: "samples", "doc:<kind>"
		int romSamples = 0;						// ROM slots in the samples document the page holds
		int romFilled = 0;						// ... that hold a sample
		bool ready = false;

		std::string heldText() const
		{
			std::string s;
			for(const auto& h : held)
				s += (s.empty() ? "" : ", ") + h;
			return s;
		}

		std::string sequence() const
		{
			std::string s;
			for(const auto& l : lifecycles)
				s += (s.empty() ? "" : " > ") + l;
			return s;
		}
		int count(const std::string& _lifecycle) const
		{
			int n = 0;
			for(const auto& l : lifecycles)
				n += l == _lifecycle;
			return n;
		}
	};

	// One start: a processor on the device home, the page attached, until the machine has taken input for 10 s and the
	// page holds the samples (or 90 s passed without them; at most 420 s in all: a loaded computer runs the emulator
	// slower than real time).
	Start start(const juce::File& _home)
	{
		mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
		config.deviceHomePath = _home.getFullPathName().toStdString() + "/";
		auto processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(md::MachineModel::Machinedrum, config);
		juce::AudioProcessor& ap = *processor;
		ap.prepareToPlay(44100.0, 128);
		std::atomic<bool> run{true};
		std::thread audio([&]
		{
			juce::AudioBuffer<float> buf(ap.getTotalNumOutputChannels(), 128);
			juce::MidiBuffer midi;
			while(run)
			{
				midi.clear();
				buf.clear();
				ap.processBlock(buf, midi);	// as fast as it goes: machine time, not wall time, is what the checks wait for
			}
		});

		Start s;
		std::string lifecycle;
		const void* machine = nullptr;
		auto* session = processor->getDeskHost()->session();
		check(session != nullptr, "the desk session exists");
		if(session)
		{
			session->attach([&](const Value& _m)
			{
				const auto type = str(_m, "type");
				if(type == "lcd")
				{
					++s.lcd;
					s.lcdWhileLoading += lifecycle == "loading";
				}
				if(type == "reset")
				{
					++s.resets;
					s.held.clear();
					s.romSamples = 0;
					s.romFilled = 0;
				}
				if(type == "samples")
				{
					s.held.insert("samples");
					const auto* doc = _m.find("doc");
					const auto* rom = doc ? doc->find("rom") : nullptr;
					s.romSamples = rom && rom->isArray() ? static_cast<int>(rom->asArray().size()) : 0;
					s.romFilled = 0;
					if(s.romSamples)
						for(const auto& slot : rom->asArray())
						{
							const auto* empty = slot.find("empty");
							s.romFilled += empty && empty->isBool() && !empty->asBool();
						}
				}
				if(type == "doc")
					s.held.insert("doc:" + str(_m, "kind"));
				if(type != "machine")
					return;
				const auto* doc = _m.find("doc");
				const auto l = doc ? str(*doc, "lifecycle") : std::string();
				if(l.empty() || l == lifecycle)
					return;
				lifecycle = l;
				s.lifecycles.push_back(l);
			});
			auto ready = elektronData::json::parse(R"({"op":"ready"})");
			session->onPageMessage(*ready);
		}

		const auto began = std::chrono::steady_clock::now();
		const auto end = began + std::chrono::seconds(420);
		auto readySince = std::chrono::steady_clock::time_point::max();
		while(std::chrono::steady_clock::now() < end)
		{
			pump(50);
			const void* now = processor->getPlugin().withDeviceLocked([](synthLib::Device* _d) -> const void*
			{
				auto* d = dynamic_cast<md::DeskDevice*>(_d);
				return d ? &d->getHardware() : nullptr;
			});
			if(now && now != machine)
			{
				machine = now;
				++s.machines;
			}
			if(lifecycle != "ready")
			{
				readySince = std::chrono::steady_clock::time_point::max();
				continue;
			}
			if(readySince == std::chrono::steady_clock::time_point::max())
				readySince = std::chrono::steady_clock::now();
			const auto since = std::chrono::steady_clock::now() - readySince;
			if(since > std::chrono::seconds(10) && (s.held.count("samples") || since > std::chrono::seconds(90)))	// and stays so: no start-up after it
			{
				s.ready = true;
				break;
			}
		}
		run = false;
		audio.join();
		ap.releaseResources();
		processor.reset();
		std::printf("  lifecycle: %s; %d LCD frames (%d while loading); %d machine(s); %d reset(s); %.0f s\n", s.sequence().c_str(), s.lcd,
			s.lcdWhileLoading, s.machines, s.resets, std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
		std::printf("  the page holds: %s; %d ROM sample slot(s), %d filled\n", s.heldText().c_str(), s.romSamples, s.romFilled);
		return s;
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);	// the lines show as they happen (a long run, piped)
	const auto* rom = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!rom || !*rom)
	{
		std::puts("mdFirstStartFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)");
		return 77;
	}
	// An empty data root and device home (no factory cache anywhere): set before anything reads them.
	const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("mdFirstStartFirmwareTest", "");
	home.createDirectory();
	setenv("GEARMULATOR_DATA_ROOT", home.getChildFile("data").getFullPathName().toRawUTF8(), 1);
	juce::ScopedJuceInitialiser_GUI juce;
	synthLib::RomLoader::addSearchPath(juce::File(rom).getParentDirectory().getFullPathName().toStdString());
	const auto cache = home.getChildFile("nvram").getChildFile("md-uw-1.63-factory-v2.cache");

	std::puts("first start (no factory cache):");
	const auto first = start(home);
	check(first.ready, "the machine takes input");
	check(first.machines == 2, "the processor started the machine again after the flash was prepared");
	check(!first.lifecycles.empty() && first.lifecycles.front() == "loading", "the page sees the preparation first");
	check(first.count("loading") == 1 && first.count("booting") == 1 && first.count("animating") == 1,
		"one preparation, then one start with one animation");
	check(first.lcdWhileLoading == 0, "no LCD from the machine that is being prepared");
	check(first.lcd > 0, "the start-up card shows the LCD of the start that stays");
	check(cache.existsAsFile(), "the factory cache is kept (the editor did not disturb the preparation)");
	check(first.held.count("samples") && first.romSamples == 48, "the page holds the samples document, 48 ROM slots (B-012)");

	std::puts("next start (the cache is there):");
	const auto next = start(home);
	check(next.ready, "the machine takes input");
	check(next.machines == 1, "no second start");
	check(next.count("loading") == 0, "no preparation");
	check(next.count("booting") == 1 && next.count("animating") == 1, "one start with one animation");
	check(next.held.count("samples") && next.romSamples == 48, "the page holds the samples document, 48 ROM slots");
	check(first.held == next.held && first.romFilled == next.romFilled,
		"the first start's page holds what the next start's does: " + first.heldText());

	home.deleteRecursively();
	std::printf("mdFirstStartFirmwareTest: %s\n", g_failures == 0 ? "PASS" : "FAIL");
	return g_failures == 0 ? 0 : 1;
}
