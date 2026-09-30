// A machine without its ROM is not an error: the processor makes a silent stand-in (no alert, no
// exception), the desk session publishes lifecycle "missing" (not "loading" for ever) so the page
// opens its ROM card, and installing the user's ROM starts the machine without reopening the
// plug-in. The data root is an empty temp folder; the ROM (the user's own, from the environment) is
// only read, and its copy lives in that folder until the test ends.
//   mdSessionNoRomTest <md|mm>            no ROM: the state the page sees
//   mdSessionNoRomTest <md|mm> install    then the ROM (GEARMULATOR_MD_FIRMWARE_BIN / _MM_): exits 77 without it
#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "synthLib/plugin.h"

#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

	// The lifecycle in the last "machine" message, empty if none came.
	std::string lifecycleOf(const std::vector<Value>& _published)
	{
		for(auto it = _published.rbegin(); it != _published.rend(); ++it)
			if(str(*it, "type") == "machine")
				if(const auto* doc = it->find("doc"))
					return str(*doc, "lifecycle");
		return {};
	}

	int count(const std::vector<Value>& _published, const char* _type)
	{
		int n = 0;
		for(const auto& m : _published)
			n += str(m, "type") == _type;
		return n;
	}
}

int main(const int _argc, char** const _argv)
{
	const bool mm = _argc > 1 && std::strcmp(_argv[1], "mm") == 0;
	const bool install = _argc > 2 && std::strcmp(_argv[2], "install") == 0;
	const auto model = mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum;
	const char* rom = std::getenv(mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
	if(install && (!rom || !juce::File(rom).existsAsFile()))
	{
		std::puts("mdSessionNoRomTest: SKIP (the firmware variable is not set)");
		return 77;
	}

	// An empty data root: no ROM anywhere the processor looks. Set before anything reads it.
	const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile(mm ? "mdNoRomMm" : "mdNoRomMd", "");
	root.createDirectory();
	setenv("GEARMULATOR_DATA_ROOT", root.getFullPathName().toRawUTF8(), 1);

	juce::ScopedJuceInitialiser_GUI juce;
	mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
	const auto home = root.getChildFile("home");
	home.createDirectory();
	config.deviceHomePath = home.getFullPathName().toStdString() + "/";
	auto processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(model, config, false);
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
			ap.processBlock(buf, midi);
			std::this_thread::sleep_for(std::chrono::microseconds(1500));
		}
	});

	auto* session = processor->getDeskHost()->session();
	check(session != nullptr, "the desk session exists");
	std::vector<Value> published;
	if(session)
	{
		session->attach([&](const Value& _m) { published.push_back(_m); });
		auto ready = elektronData::json::parse(R"({"op":"ready"})");
		session->onPageMessage(*ready);
	}
	const auto stand = [&] { return processor->getPlugin().withDeviceLocked([](synthLib::Device* _d) { return mdJucePlugin::isNoRomDevice(_d); }); };
	const auto running = [&]
	{
		return processor->getPlugin().withDeviceLocked([](synthLib::Device* _d)
		{
			auto* d = dynamic_cast<md::DeskDevice*>(_d);
			return d && d->isValid() && d->getHardware().isFirmwareMidiReady();
		});
	};

	// No ROM: the stand-in runs, and within a couple of seconds the page is told "missing".
	for(int i = 0; i < 40 && lifecycleOf(published) != "missing"; ++i)
		pump(100);
	check(stand(), "no ROM: the processor runs the silent stand-in (no exception, so no alert)");
	check(lifecycleOf(published) == "missing", "the page is told the ROM is missing (lifecycle '" + lifecycleOf(published) + "'), not left loading");

	if(install && session)
	{
		published.clear();
		session->installRom(juce::File(rom));
		check(count(published, "romInstall") >= 1, "the session answers the ROM with a romInstall message");
		bool okText = false;
		for(const auto& m : published)
			if(str(m, "type") == "romInstall")
			{
				const auto* ok = m.find("ok");
				okText = ok && ok->isBool() && ok->asBool();
			}
		check(okText, "the ROM is accepted");
		// The machine starts in place, without reopening the plug-in.
		for(int i = 0; i < 1500 && !running(); ++i)
			pump(40);
		check(!stand() && running(), "the machine boots with the installed ROM and takes MIDI");
		pump(500);
		const auto life = lifecycleOf(published);
		check(life != "missing" && life != "loading" && !life.empty(), "the page's lifecycle moved on ('" + life + "')");
	}
	else
	{
		// A wrong file: refused inside the page's card (a romInstall message with ok false), nothing installed.
		published.clear();
		const auto bad = juce::File::createTempFile(".bin");
		bad.replaceWithData("abc", 3);
		if(session)
			session->installRom(bad);
		bad.deleteFile();
		bool refused = false;
		for(const auto& m : published)
			if(str(m, "type") == "romInstall")
			{
				const auto* ok = m.find("ok");
				refused = ok && ok->isBool() && !ok->asBool() && !str(m, "text").empty();
			}
		check(refused, "a wrong file is refused with a text for the card");
		check(stand(), "and the machine is still missing");
	}

	run = false;
	audio.join();
	if(session)
		session->detach();
	ap.releaseResources();
	processor.reset();
	root.deleteRecursively();
	std::printf("mdSessionNoRomTest %s%s: %s\n", mm ? "mm" : "md", install ? " install" : "", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
