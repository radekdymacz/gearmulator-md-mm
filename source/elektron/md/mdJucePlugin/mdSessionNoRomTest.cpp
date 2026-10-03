// A machine without its ROM is not an error: the processor makes a silent stand-in (no alert, no
// exception), the desk session publishes lifecycle "missing" (not "loading" for ever) so the page
// opens its ROM card, and installing the user's ROM starts the machine without reopening the
// plug-in. The data root is an empty temp folder; the ROM (the user's own, from the environment) is
// only read, and its copy lives in that folder until the test ends.
//   mdSessionNoRomTest <md|mm>            no ROM: the state the page sees
//   mdSessionNoRomTest <md|mm> manage     the same, then LOAD ROM: romInfo, REPLACE with another copy, REMOVE (the
//                                         machine stops, the folder is empty, the page is told "missing" again)
//   mdSessionNoRomTest <md|mm> install    then the ROM (GEARMULATOR_MD_FIRMWARE_BIN / _MM_): exits 77 without it
#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"
#include "mdRomInstall.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "synthLib/plugin.h"

#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <chrono>
#include <cmath>
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

	// kit.working ("clean", "edited", "unknown") in the last machine document, empty if none came.
	std::string kitWorkingOf(const std::vector<Value>& _published)
	{
		for(auto it = _published.rbegin(); it != _published.rend(); ++it)
			if(str(*it, "type") == "machine")
				if(const auto* doc = it->find("doc"))
					if(const auto* kit = doc->find("kit"))
						return str(*kit, "working");
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
	const bool manage = _argc > 2 && std::strcmp(_argv[2], "manage") == 0;
	const bool install = manage || (_argc > 2 && std::strcmp(_argv[2], "install") == 0);
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

	// The project the app was opened with is kept while there is no ROM (saving must not overwrite it
	// with the stand-in's empty state) and nothing is saved when there was none.
	const char kept[] = "the saved project, as the host gave it";
	{
		juce::MemoryBlock none;
		ap.getStateInformation(none);
		check(none.getSize() == 0, "no ROM and no project given: nothing to save");

		// The DAW's automation parameters are saved and restored without a ROM too (the pluginTester's
		// -automation-smoke in CI, which has no ROM): moved parameters make a project, a project given back
		// restores them, an unmoved session hands the same bytes back, and a later move rewrites only them.
		// Only in the plain run: moved parameters are edits the machine gets when its ROM arrives, and the
		// install run checks that a first boot leaves the kit clean.
		if(!install)
		{
			std::vector<juce::AudioProcessorParameter*> params;
			for(auto* p : ap.getParameters())
				if(p->isAutomatable() && p->getNumSteps() >= 3 && params.size() < 8)
					params.push_back(p);
			check(params.size() == 8, "the plug-in exposes automatable parameters");
			const auto step = [](const juce::AudioProcessorParameter* _p, const int _n) { return static_cast<float>(_n) / static_cast<float>(_p->getNumSteps() - 1); };
			std::vector<float> expected;
			for(size_t i = 0; i < params.size(); ++i)
			{
				params[i]->setValue(step(params[i], static_cast<int>(i) + 1));
				expected.push_back(params[i]->getValue());
			}
			juce::MemoryBlock project;
			ap.getStateInformation(project);
			check(project.getSize() > 0, "no ROM, moved parameters: the plug-in's own state is saved");
			const auto restores = [&](const juce::MemoryBlock& _state, const std::vector<float>& _values)
			{
				for(auto* p : params)
					p->setValue(0.0f);
				ap.setStateInformation(_state.getData(), static_cast<int>(_state.getSize()));
				for(size_t i = 0; i < params.size(); ++i)
					if(std::abs(params[i]->getValue() - _values[i]) > 0.0001f)
						return false;
				return true;
			};
			check(restores(project, expected), "no ROM: the project restores the automation parameters");
			{
				juce::MemoryBlock again;
				ap.getStateInformation(again);
				check(again == project, "no ROM, nothing moved: the held project is handed back byte for byte");
			}
			params[0]->setValue(step(params[0], 2));
			expected[0] = params[0]->getValue();
			juce::MemoryBlock moved;
			ap.getStateInformation(moved);
			check(moved.getSize() == project.getSize() && moved != project, "no ROM, a parameter moved: the held project with its automation rewritten");
			check(restores(moved, expected), "and that project restores the moved parameter");
			// The editor's setup ("MDSK") lives without a machine too.
			const std::string setupText = R"({"schema":"md-desk/setup","version":1,"knobCcs":[30,31,32,33,34,35,36,37]})";
			processor->getDeskHost()->setSetup(setupText);
			juce::MemoryBlock withSetup;
			ap.getStateInformation(withSetup);
			processor->getDeskHost()->setSetup({});
			ap.setStateInformation(withSetup.getData(), static_cast<int>(withSetup.getSize()));
			check(processor->getDeskHost()->setup() == setupText, "no ROM: the editor's setup is saved and restored with the held project");
			ap.setStateInformation(moved.getData(), static_cast<int>(moved.getSize()));
			check(processor->getDeskHost()->setup().empty(), "and a project without one starts from the default setup");
		}

		ap.setStateInformation(kept, sizeof(kept));
		juce::MemoryBlock back;
		ap.getStateInformation(back);
		check(back.getSize() == sizeof(kept) && std::memcmp(back.getData(), kept, sizeof(kept)) == 0, "no ROM: saving hands back the project it was given");
	}

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
		{
			juce::MemoryBlock now;
			ap.getStateInformation(now);
			check(now.getSize() > sizeof(kept) && std::memcmp(now.getData(), kept, sizeof(kept)) != 0, "with the ROM the machine's own state is saved");
		}
		// The stand-in had no state: the new machine must not try to restore one (the "state restore"
		// alert on a first run came from the stand-in's bare header being applied to the real device).
		std::string restoreError;
		bool restoreFailed = true;
		processor->getPlugin().withDeviceLocked([&](synthLib::Device* _d)
		{
			if(auto* d = dynamic_cast<md::Device*>(_d))
			{
				restoreError = d->projectStateRestoreError();
				restoreFailed = d->projectStateRestoreStatus() == md::Device::ProjectStateRestoreStatus::Failed;
			}
		});
		check(!restoreFailed && restoreError.empty(), "no state restore failed after the install ('" + restoreError + "')");
		// Nothing was edited: after a first boot the playing kit matches its stored slot. Give the
		// session time to read both (it asks the machine once it is up), then it must say "clean".
		std::string working;
		for(int i = 0; i < 400 && (working = kitWorkingOf(published)) != "clean"; ++i)
			pump(100);
		pump(15000);
		working = kitWorkingOf(published);
		{
			std::string seq, last;
			for(const auto& m : published)
				if(str(m, "type") == "machine")
					if(const auto* doc = m.find("doc"))
						if(const auto* kit = doc->find("kit"))
						{
							const auto w = str(*kit, "working");
							if(w != last)
								seq += (seq.empty() ? "" : " > ") + w;
							last = w;
						}
			std::printf("  kit.working over time: %s\n", seq.c_str());
		}
		check(working == "clean", "the playing kit is not edited after the first boot (kit.working '" + working + "')");
		const auto life = lifecycleOf(published);
		check(life != "missing" && life != "loading" && !life.empty(), "the page's lifecycle moved on ('" + life + "')");
	}
	if(manage && session)
	{
		const juce::File romFolder(juce::String::fromUTF8(processor->getPublicRomFolder().c_str()));
		const auto model2 = model;
		const auto lastOf = [&](const char* _type) -> const Value*
		{
			for(auto it = published.rbegin(); it != published.rend(); ++it)
				if(str(*it, "type") == _type)
					return &*it;
			return nullptr;
		};
		// LOAD ROM: which firmware runs
		published.clear();
		session->onPageMessage(*elektronData::json::parse(R"({"op":"romInfo","id":21})"));
		const auto* info = lastOf("romInfo");
		check(info && info->find("installed") && info->find("installed")->asBool() && info->find("inFolder") && info->find("inFolder")->asBool()
			&& !str(*info, "name").empty(), "romInfo names the installed image, in the editor's ROM folder");
		// REPLACE with another copy of the image under a new name
		const auto other = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("replacement-rom", ".bin");
		juce::File(rom).copyFileTo(other);
		published.clear();
		session->installRom(other);
		pump(300);
		other.deleteFile();
		const auto after = mdJucePlugin::romsInFolder(model2, romFolder);
		check(after.size() == 1 && after[0].getFileName().startsWith("replacement-rom"), "REPLACE leaves one image, the new one");
		for(int i = 0; i < 1500 && !running(); ++i)
			pump(40);
		check(!stand() && running(), "the machine runs on the replaced image");
		// REMOVE
		published.clear();
		session->onPageMessage(*elektronData::json::parse(R"({"op":"removeRom","id":22})"));
		check(mdJucePlugin::romsInFolder(model2, romFolder).empty(), "REMOVE deletes the image from the ROM folder");
		check(stand(), "and the machine stops (the silent stand-in runs)");
		bool okReply = false;
		for(const auto& m : published)
			if(str(m, "type") == "result" && str(m, "op") == "removeRom")
			{
				const auto* ok = m.find("ok");
				okReply = ok && ok->isBool() && ok->asBool();
			}
		check(okReply, "the page gets an ok result");
		for(int i = 0; i < 60 && lifecycleOf(published) != "missing"; ++i)
			pump(100);
		check(lifecycleOf(published) == "missing", "the page is told the ROM is missing again (the start-up card opens)");
		published.clear();
		session->onPageMessage(*elektronData::json::parse(R"({"op":"romInfo","id":23})"));
		const auto* none = lastOf("romInfo");
		check(none && none->find("installed") && !none->find("installed")->asBool(), "romInfo says nothing is installed");
		published.clear();
		session->onPageMessage(*elektronData::json::parse(R"({"op":"removeRom","id":24})"));
		bool refused = false;
		for(const auto& m : published)
			if(str(m, "type") == "result" && str(m, "op") == "removeRom")
			{
				const auto* ok = m.find("ok");
				refused = ok && ok->isBool() && !ok->asBool();
			}
		check(refused, "REMOVE with nothing installed is refused with a text");
	}
	else if(!install)
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
	std::printf("mdSessionNoRomTest %s%s: %s\n", mm ? "mm" : "md", manage ? " manage" : install ? " install" : "", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
