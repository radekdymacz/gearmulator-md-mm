// A machine without its ROM is not an error: the processor makes a silent stand-in (no alert, no
// exception), the desk session publishes lifecycle "missing" (not "loading" for ever) so the page
// opens its ROM card, and installing the user's ROM starts the machine without reopening the
// plug-in. The data root is an empty temp folder; the ROM (the user's own, from the environment) is
// only read, and its copy lives in that folder until the test ends.
//   mdSessionNoRomTest <md|mm>            no ROM: the state the page sees
//   mdSessionNoRomTest <md|mm> manage     the same, then LOAD ROM: romInfo, REPLACE with another copy, REMOVE (the
//                                         machine stops, the folder is empty, the page is told "missing" again)
//   mdSessionNoRomTest <md|mm> bytes      a file dropped on the page arrives in pieces (romBytes): out of order and
//                                         wrong sizes are refused, the real ROM in pieces installs and boots
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
	const bool bytes = _argc > 2 && std::strcmp(_argv[2], "bytes") == 0;
	const bool manage = _argc > 2 && std::strcmp(_argv[2], "manage") == 0;
	const bool install = bytes || manage || (_argc > 2 && std::strcmp(_argv[2], "install") == 0);
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
		ap.setStateInformation(kept, sizeof(kept));
		juce::MemoryBlock back;
		ap.getStateInformation(back);
		check(back.getSize() == sizeof(kept) && std::memcmp(back.getData(), kept, sizeof(kept)) == 0, "no ROM: saving hands back the project it was given");
	}

	// One piece as the page sends it (romBytes); the acknowledgement it gets: 1 taken, 0 refused, -1 none.
	const auto piece = [&](double _tid, const char* _name, size_t _size, int _index, int _count, size_t _offset, const juce::String& _b64)
	{
		auto m = elektronData::json::parse(R"({"op":"romBytes","id":5})");
		m->set("tid", _tid);
		m->set("name", std::string(_name));
		m->set("size", static_cast<double>(_size));
		m->set("index", _index);
		m->set("count", _count);
		m->set("offset", static_cast<double>(_offset));
		m->set("data", _b64.toStdString());
		published.clear();
		session->onPageMessage(*m);
		int ack = -1;
		for(const auto& r : published)
			if(str(r, "type") == "romBytesAck" && r.find("tid") && r.find("tid")->asNumber() == _tid)
				ack = r.find("ok") && r.find("ok")->asBool() ? 1 : 0;
		return ack;
	};
	if(bytes && session)
	{
		// refused: a file of the wrong size (the bytes are not a ROM), damaged data, a piece outside the file
		piece(1, "small.bin", 3, 0, 1, 0, juce::Base64::toBase64("abc", 3));
		bool refusedText = false;
		for(const auto& m : published)
			if(str(m, "type") == "romInstall")
				refusedText = m.find("ok") && !m.find("ok")->asBool() && !str(m, "text").empty();
		check(refusedText && stand(), "a small file dropped as bytes is refused with a text for the card, and nothing starts");
		check(piece(2, "bad.bin", 4, 0, 1, 0, "!!!not base64") == 0, "damaged data is refused, and acknowledged as refused");
		check(piece(3, "wild.bin", 10, 0, 2, 9999, "AAAA") == 0, "a piece outside the file is refused");
		juce::MemoryBlock img;
		juce::File(rom).loadFileAsData(img);
		const auto sz = img.getSize();
		const size_t part = 1048576;
		const int n = static_cast<int>((sz + part - 1) / part);
		const auto data = [&](int i) { const auto at = i * part; return juce::Base64::toBase64(static_cast<const char*>(img.getData()) + at, std::min(part, sz - at)); };
		// the page's loop, four in flight, pieces arriving in any order; a second drop starts half way
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		bool all = true;
		const int order[] = {2, 0, 3, 1, 5, 4, 7, 6};
		for(int k = 0; k < n / 2; ++k)
			all = piece(10, "cancelled.bin", sz, order[k], n, order[k] * part, data(order[k])) == 1 && all;
		// the second drop, a newer transfer id: the first is over
		check(piece(11, "dropped-rom.bin", sz, 3, n, 3 * part, data(3)) == 1, "a newer transfer starts at once");
		check(piece(10, "cancelled.bin", sz, order[n / 2], n, order[n / 2] * part, data(order[n / 2])) == 0, "a piece of the cancelled transfer is refused quietly");
		check(count(published, "romInstall") == 0, "and it does not disturb the card");
		for(int k = 0; k < n; ++k)
		{
			const int i = order[k];
			if(i == 3)
				continue;
			all = piece(11, "dropped-rom.bin", sz, i, n, i * part, data(i)) == 1 && all;
		}
		const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
		check(all, "every piece is acknowledged, in any order");
		bool installed = false;
		for(const auto& m : published)
			if(str(m, "type") == "romInstall")
				installed = m.find("ok") && m.find("ok")->asBool();
		check(installed, "the last piece installs it (romInstall ok)");
		std::printf("  session side: %d pieces, %zu bytes, %d ms (incl. install)\n", n, sz, static_cast<int>(ms));
		const juce::File romFolder(juce::String::fromUTF8(processor->getPublicRomFolder().c_str()));
		check(romFolder.getChildFile("dropped-rom.bin").existsAsFile() && !romFolder.getChildFile("cancelled.bin").existsAsFile(), "it is in the ROM folder under the dropped name");
		check(piece(11, "dropped-rom.bin", sz, 0, n, 0, data(0)) == 0, "a repeat of a finished transfer is refused");
		for(int i = 0; i < 1500 && !running(); ++i)
			pump(40);
		check(!stand() && running(), "the machine boots with the dropped ROM");
	}
	else if(install && session)
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
	std::printf("mdSessionNoRomTest %s%s: %s\n", mm ? "mm" : "md", bytes ? " bytes" : manage ? " manage" : install ? " install" : "", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
