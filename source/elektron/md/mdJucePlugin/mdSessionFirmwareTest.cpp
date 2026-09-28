// P5/P6: the app modulators run in the processor's desk session, with no editor: an LFO on
// track 2 DIST moves the machine's working kit while the pattern plays. Needs the MD OS 1.63 ROM:
//   GEARMULATOR_MD_FIRMWARE_BIN=<ROM> mdSessionFirmwareTest
// Exits 77 without it. The config is isolated (EphemeralConfig).

#include "mdPluginProcessor.h"
#include "mdDeskSession.h"
#include "mdAudioMidiLink.h"

#include "mdLib/mddevice.h"

#include "deskCore/deskContract.h"
#include "deskHost/deskHost.h"

#include "elektronData/json.h"
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
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <thread>

namespace
{
	using Value = elektronData::json::Value;

	Value parseJson(const std::string& _json)
	{
		auto v = elektronData::json::parse(_json);
		return v ? *v : Value::object();
	}

	// P6: the host messages a desk session cannot publish itself (they are the plug-in's own,
	// not the desk's: MIDI Learn, AUDIO/MIDI). mdDeskTest's contract check has to name these
	// "elsewhere" for want of a host; here there is one, so they get checked against the same
	// contract instead of staying untested.
	bool checkHostMessages(mdJucePlugin::AudioPluginAudioProcessor& _processor, juce::AudioProcessor& _ap)
	{
		namespace contract = deskCore::contract;
		std::ifstream in(MDDESK_SCHEMA);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		const auto root = elektronData::json::parse(text);
		if(!root)
		{
			std::puts("  contract: the schema did not load");
			return false;
		}
		auto* session = _processor.getDeskSession();
		if(!session)
		{
			std::puts("  contract: no desk session");
			return false;
		}
		std::vector<Value> published;
		session->attach([&](const Value& _m) { published.push_back(_m); });
		// "ready": everything the session publishes once a page attaches, "learn" among it.
		session->onPageMessage(parseJson(R"({"op":"ready"})"));

		// AUDIO/MIDI is the editor window's, not the session's (mdPageEditor.h owns it); built the
		// same way here, on the same processor, so its messages are the real ones.
		mdJucePlugin::AudioMidiLink audio(_ap, [&](Value _m) { published.push_back(std::move(_m)); });
		audio.handle(deskHost::Action::AudioPublish, parseJson(R"({"op":"audio","id":1})"));
		// audioLevel: only sent while a standalone host meters its input (tick(), AudioMidiLink.cpp);
		// this test runs the processor directly, with no standalone holder to meter. Built exactly as
		// AudioMidiLink::tick() does, so the contract still sees the shape it will see live.
		{
			Value m = Value::object();
			m.set("type", "audioLevel");
			m.set("in", 0.0);
			published.push_back(std::move(m));
		}
		// openAudio: the editor window asking the page to open its panel (mdPageEditor.cpp), no doc.
		{
			Value m = Value::object();
			m.set("type", "openAudio");
			published.push_back(std::move(m));
		}
		// romInstall (P7): the session's verdict on a chosen or dropped file; a wrong one, so nothing is copied
		{
			const auto bad = juce::File::createTempFile(".bin");
			bad.replaceWithData("abc", 3);
			session->installRom(bad);
			bad.deleteFile();
		}
		// syxPreview, syxProgress, syxExport (P7): the session's SysEx messages; the export is read back as
		// the import's preview (the documents the session holds, round trip through the file)
		{
			const auto f = juce::File::createTempFile(".syx");
			session->exportSyx(f);
			session->openSyx(f);
			session->onPageMessage(parseJson(R"({"op":"syxImport","id":7,"kinds":["kit"]})"));
			session->onPageMessage(parseJson(R"({"op":"syxCancel","id":8})"));
			f.deleteFile();
		}
		session->detach();

		const auto r = contract::checkMessages(*root, published);
		for(const auto& p : r.off)
			std::printf("    %s\n", p.c_str());
		std::printf("  host messages: %zu published of %zu types, %zu off the contract\n", r.messages, r.types, r.offCount);
		return r.offCount == 0;
	}
}

int main()
{
	const auto* rom = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!rom || !*rom)
	{
		std::puts("mdSessionFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)");
		return 77;
	}
	juce::ScopedJuceInitialiser_GUI juce;
	synthLib::RomLoader::addSearchPath(juce::File(rom).getParentDirectory().getFullPathName().toStdString());
	mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
	// A home for the factory-flash cache (in the temp folder), so the processor prepares it once
	// instead of rebooting the machine again and again without one.
	const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mdModRunnerFirmwareTest");
	home.createDirectory();
	config.deviceHomePath = home.getFullPathName().toStdString() + "/";
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
	bool outside = false;
	int steps = 0, lastStep = -1;
	const void* hw = nullptr;	// the machine the checks run on; the processor may reboot it	// a value outside the link's 10..110 once it runs
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
		const void* current = nullptr;
		telemetry = processor->getPlugin().withDeviceLocked([&](synthLib::Device* _d)
		{
			auto* d = dynamic_cast<md::Device*>(_d);
			current = d ? &d->getHardware() : nullptr;
			return d ? d->getSequencerTelemetry() : nullptr;
		});
		if(current != hw)
		{
			// Rebooted (the processor's factory-flash service): wait for it again.
			std::puts("  the processor rebooted the machine: waiting again");
			phase = 1; ticks = 0; seen.clear(); steps = 0; lastStep = -1; outside = false;
			return true;
		}
		if(telemetry && telemetry->playing.load() != 1 && ticks % 10 == 5)
			pendingStart = 1;	// a replaced device (the processor's own services) starts again
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
		// Machine time, not wall time: until the playhead moved 32 steps (the emulator can run
		// slower than real time on a loaded machine), at most 30 s.
		if(telemetry)
		{
			const int st = telemetry->step.load();
			if(st >= 0 && st != lastStep && telemetry->playing.load() == 1) { ++steps; lastStep = st; }
		}
		return steps < 32 && ticks < 300;
	};
	// This thread is the message thread: the checks run here, and the session's timer through the
	// run loop.
	if(!processor->getDeskSession())
	{
		std::puts("mdSessionFirmwareTest: FAIL (no desk session)");
		return 1;
	}
	while(tick())
		for(int i = 0; i < 12; ++i)
		{
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
	std::printf("  with no editor, track 2 DIST took %zu values in %d steps (%d..%d)\n", seen.size(), steps, seen.empty() ? -1 : *seen.begin(),
		seen.empty() ? -1 : *seen.rbegin());
	const bool hostOk = checkHostMessages(*processor, ap);
	std::printf("mdSessionFirmwareTest: %s\n", ok && hostOk ? "PASS" : "FAIL");
	processor.reset();
	return ok && hostOk ? 0 : 1;
}
