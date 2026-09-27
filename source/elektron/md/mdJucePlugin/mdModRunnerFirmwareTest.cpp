// P5: the app modulators run in the processor, with no editor: an LFO on track 2 DIST moves
// the machine's working kit while the pattern plays. Needs the MD OS 1.63 ROM:
//   GEARMULATOR_MD_FIRMWARE_BIN=<ROM> mdModRunnerFirmwareTest
// Exits 77 without it. The config is isolated (EphemeralConfig).

#include "mdPluginProcessor.h"
#include "mdModRunner.h"

#include "mdLib/mddevice.h"

#include "elektronData/mdWorkingKit.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "synthLib/romLoader.h"

#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <cstdio>
#include <functional>
#include <set>
#include <thread>

int main()
{
	const auto* rom = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!rom || !*rom)
	{
		std::puts("mdModRunnerFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)");
		return 77;
	}
	juce::ScopedJuceInitialiser_GUI juce;
	synthLib::RomLoader::addSearchPath(juce::File(rom).getParentDirectory().getFullPathName().toStdString());
	mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
	config.deviceHomePath = std::string{};
	auto processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(md::MachineModel::Machinedrum, config, false);
	juce::AudioProcessor& ap = *processor;
	ap.prepareToPlay(44100.0, 128);
	std::atomic<bool> run{true};
	std::atomic<int> pendingStart{0};
	std::thread audio([&]
	{
		juce::AudioBuffer<float> buf(ap.getTotalNumOutputChannels(), 128);
		juce::MidiBuffer midi;
		while(run)
		{
			midi.clear();
			if(pendingStart.exchange(0))
				midi.addEvent(juce::MidiMessage::midiStart(), 0);
			buf.clear();
			ap.processBlock(buf, midi);
			std::this_thread::sleep_for(std::chrono::microseconds(1500));	// about real time
		}
	});
	std::set<int> seen;
	std::shared_ptr<const md::Device::SequencerTelemetry> telemetry;
	std::function<bool()> tick;
	int phase = 0, ticks = 0;
	bool outside = false;	// a value outside the link's 10..110 once it runs
	tick = [&]
	{
		++ticks;
		if(phase == 0)
		{
			const bool ready = processor->getPlugin().withDeviceLocked([](synthLib::Device* _d)
			{
				auto* d = dynamic_cast<md::Device*>(_d);
				return d && d->getHardware().isFirmwareMidiReady();
			});
			if(ready) { phase = 1; ticks = 0; std::puts("  firmware takes MIDI"); }
			if(ticks == 599) std::puts("  FAIL: no firmware");
			return ticks < 600;
		}
		if(phase == 1)
		{
			// The start-up animation; the processor may replace the device meanwhile (its
			// factory services), which boots again: count from the last boot.
			static const void* hw = nullptr;
			const void* now = nullptr;
			const bool ready = processor->getPlugin().withDeviceLocked([&](synthLib::Device* _d)
			{
				auto* d = dynamic_cast<md::Device*>(_d);
				now = d ? &d->getHardware() : nullptr;
				return d && d->getHardware().isFirmwareMidiReady();
			});
			if(!ready || now != hw)
			{
				if(now != hw)
					std::printf("  device %p (tick %d)\n", now, ticks);
				hw = now;
				ticks = 0;
			}
			if(ticks < 170)
				return true;
			processor->setDeskSetup(R"({"schema":"md-desk/setup","version":1,"modulators":{"schema":"md-desk/modulators","version":1,)"
				R"("sources":[{"id":"lfo1","label":"LFO A","kind":"lfo","shape":0,"rate":"1/2","depth":100}],)"
				R"("links":[{"source":"lfo1","track":1,"param":16,"min":10,"max":110,"curve":"lin"}]}})");
			telemetry = processor->getPlugin().withDeviceLocked([](synthLib::Device* _d)
			{
				auto* d = dynamic_cast<md::Device*>(_d);
				return d ? d->getSequencerTelemetry() : nullptr;
			});
			pendingStart = 1;
			phase = 2;
			ticks = 0;
			return true;
		}
		telemetry = processor->getPlugin().withDeviceLocked([](synthLib::Device* _d)
		{
			auto* d = dynamic_cast<md::Device*>(_d);
			return d ? d->getSequencerTelemetry() : nullptr;
		});
		if(telemetry && telemetry->playing.load() != 1 && ticks % 10 == 5)
			pendingStart = 1;	// a replaced device (the processor's own services) starts again
		if(false)
		{
			const auto r = processor->getModRunner()->report();
			auto* prm = processor->getController().getParameter("Distortion", 1);
			std::printf("  [step %d playing %d, lfo %d, cc/s %d, param %d]\n", telemetry->step.load(), telemetry->playing.load(), r.values.empty() ? -1 : r.values[0], r.ccPerSecond, prm ? prm->getUnnormalizedValue() : -1);
		}
		std::vector<uint8_t> region;
		uint32_t seq = 0;
		if(telemetry && telemetry->readWorkingKit(region, seq))
		{
			const int v = region[2 + elektronData::g_mdWorkingKitParamsOffset + 1 * 24 + 16];
			if(v >= 10 && v <= 110)
				seen.insert(v);
			else if(ticks > 5)
				outside = true;
		}
		return ticks < 40;
	};
	// This thread is the message thread: run the checks and the ModRunner's polls here.
	auto* runner = processor->getModRunner();
	while(tick())
		for(int i = 0; i < 12; ++i)
		{
			if(runner)
				runner->poll();
			// Deliver the message thread's queued work (JUCE on macOS posts to the CFRunLoop).
#if JUCE_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.008, false);
#else
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
#endif
		}
	run = false;
	audio.join();
	ap.releaseResources();
	const bool ok = seen.size() >= 4 && !outside;
	std::printf("  with no editor, track 2 DIST took %zu values in 4 s (%d..%d)\n", seen.size(), seen.empty() ? -1 : *seen.begin(),
		seen.empty() ? -1 : *seen.rbegin());
	std::printf("mdModRunnerFirmwareTest: %s\n", ok ? "PASS" : "FAIL");
	processor.reset();
	return ok ? 0 : 1;
}
