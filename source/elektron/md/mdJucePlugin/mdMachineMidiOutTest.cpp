// B-037: the emulated machine's own MIDI out reaches the host's MIDI out (the standalone's MIDI output port, a DAW's
// plug-in MIDI out): its notes, CCs, program changes, pitch bend, pressure, clock and transport; never its SysEx
// (the answers to the editor's own requests). mdMachineMidiOut.h.
//   mdMachineMidiOutTest          the route as plain values: which kinds go, which do not, a loaded project's matrix
//   mdMachineMidiOutTest <md|mm>  the processor with the machine's firmware (GEARMULATOR_MD_FIRMWARE_BIN /
//                                 GEARMULATOR_MM_FIRMWARE_BIN; exits 77 without it): while the machine plays, its
//                                 notes (MD) or clock and transport (MM) come out of processBlock's MIDI buffer, and
//                                 none of the SysEx it sends the editor does; without the route nothing comes out; a
//                                 project saved without the route gets it back when loaded
#include "mdMachineMidiOut.h"

#include "mdController.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"

#include "mdLib/mddeskdevice.h"

#include "elektronData/json.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "synthLib/plugin.h"
#include "synthLib/romLoader.h"

#include <array>
#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !JUCE_MAC
namespace juce::detail
{
	// JUCE's own step of the message loop (juce_Messaging_linux.cpp, _windows.cpp), as mdSessionNoRomTest uses it.
	bool dispatchNextMessageOnSystemQueue(bool _returnIfNoPendingMessages);
}
#endif

namespace
{
	namespace route = mdJucePlugin::machineMidiOut;
	using Source = synthLib::MidiEventSource;
	using Value = elektronData::json::Value;

	int g_failures = 0;

	void setDataRoot(const juce::File& _folder)
	{
#if defined(_WIN32)
		_putenv_s("GEARMULATOR_DATA_ROOT", _folder.getFullPathName().toRawUTF8());
#else
		setenv("GEARMULATOR_DATA_ROOT", _folder.getFullPathName().toRawUTF8(), 1);
#endif
	}

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s: %s\n", _ok ? "ok" : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	synthLib::SMidiEvent event(const Source _source, const uint8_t _a, const uint8_t _b = 0, const uint8_t _c = 0)
	{
		return synthLib::SMidiEvent(_source, _a, _b, _c);
	}

	synthLib::SMidiEvent sysex(const Source _source, const std::vector<uint8_t>& _bytes)
	{
		synthLib::SMidiEvent e(_source);
		e.sysex.assign(_bytes.begin(), _bytes.end());
		return e;
	}

	// ---- the route as plain values ----

	int values()
	{
		std::puts("the route (mdMachineMidiOut.h)");
		synthLib::MidiRoutingMatrix upstream;
		check(!route::toHost(upstream, event(Source::Device, 0x90, 36, 100)),
			"upstream's default matrix: the machine's notes never reach the host (B-037)");

		synthLib::MidiRoutingMatrix m;
		route::route(m);
		struct Kind { synthLib::SMidiEvent e; const char* name; };
		const std::array<Kind, 12> go{{
			{event(Source::Device, 0x90, 36, 100), "note on (channel 1)"},
			{event(Source::Device, 0x8f, 60, 0), "note off (channel 16)"},
			{event(Source::Device, 0xb3, 74, 64), "CC"},
			{event(Source::Device, 0xc0, 9), "program change"},
			{event(Source::Device, 0xe5, 0, 64), "pitch bend"},
			{event(Source::Device, 0xd0, 80), "channel pressure"},
			{event(Source::Device, 0xa0, 60, 80), "poly pressure"},
			{event(Source::Device, 0xf8), "clock"},
			{event(Source::Device, 0xfa), "start"},
			{event(Source::Device, 0xfb), "continue"},
			{event(Source::Device, 0xfc), "stop"},
			{event(Source::Device, 0xf2, 0, 0),
				"song position (the matrix's bucket for system messages; neither firmware sends one)"},
		}};
		for(const auto& k : go)
			check(route::toHost(m, k.e), std::string("the machine's ") + k.name + " goes to the host");

		// The machine's SysEx: what it answers the desk (a Monomachine status reply, a Machinedrum kit dump's head, the
		// TurboMIDI speed answer); none of it may reach a real Elektron on the port
		const std::array<Kind, 3> stay{{
			{sysex(Source::Device, {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x72, 0x01, 0x05, 0xf7}), "status reply"},
			{sysex(Source::Device, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x52, 0x04, 0x01, 0x00, 0xf7}), "kit dump"},
			{sysex(Source::Device, {0xf0, 0x00, 0x20, 0x3c, 0x00, 0x00, 0x11, 0x02, 0x02, 0xf7}), "TurboMIDI answer"},
		}};
		for(const auto& k : stay)
			check(!route::toHost(m, k.e), std::string("the machine's SysEx (") + k.name + ") never goes to the host");
		// The desk's own traffic is the editor's (the requests, its edits): not the device's output
		check(!route::toHost(m, sysex(Source::Editor, {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x70, 0x01, 0xf7})),
			"the desk's own SysEx request never goes to the host");
		check(!route::toHost(m, event(Source::Editor, 0xb0, 16, 64)), "the desk's own CC edit never goes to the host");
		check(!route::toHost(m, event(Source::Internal, 0xf8)),
			"the plug-in's internal clock to the machine never goes to the host");
		// The editor still gets all of the machine's MIDI, its SysEx too
		check(m.enabled(sysex(Source::Device, {0xf0, 0x7e, 0xf7}), Source::Editor),
			"the editor still gets the machine's SysEx");

		// A project's state carries the whole matrix: one saved before 0.4.0 has no route, a hand-edited
		// one may send SysEx
		{
			const route::RouteAfterLoad r(m);
			m = synthLib::MidiRoutingMatrix();	// what loading an old project's "MiRM" chunk does
		}
		check(route::toHost(m, event(Source::Device, 0xf8)),
			"after loading a project saved without the route, it is there again");
		m.setEnabled(Source::Device, Source::Host, synthLib::MidiRoutingMatrix::EventType::SysEx, true);
		try
		{
			const route::RouteAfterLoad r(m);
			throw std::range_error("a malformed chunk after MiRM");
		}
		catch(const std::range_error&)
		{
		}
		check(!route::toHost(m, stay[1].e) && route::toHost(m, go[0].e),
			"a load that throws still ends with the route, SysEx off");
		return g_failures;
	}

	// ---- the processor with the machine's firmware ----

	void pump(const int _ms)
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(_ms);
		while(std::chrono::steady_clock::now() < end)
		{
#if JUCE_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.008, false);
#else
			if(!juce::detail::dispatchNextMessageOnSystemQueue(true))
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
		}
	}

	std::string str(const Value& _v, const char* _key)
	{
		const auto* f = _v.find(_key);
		return f && f->isString() ? f->asString() : std::string();
	}

	// What came out of processBlock's MIDI buffer: the host's MIDI out
	struct HostOut
	{
		std::atomic<int> notes{0}, ccs{0}, clocks{0}, starts{0}, stops{0}, sysex{0}, others{0};
		void reset() { notes = ccs = clocks = starts = stops = sysex = others = 0; }
		void add(const juce::MidiMessage& _m)
		{
			const auto* d = _m.getRawData();
			if(_m.isSysEx() || (_m.getRawDataSize() > 0 && d[0] == 0xf0)) ++sysex;
			else if(_m.isNoteOn()) ++notes;
			else if(_m.isController()) ++ccs;
			else if(d[0] == 0xf8) ++clocks;
			else if(d[0] == 0xfa || d[0] == 0xfb) ++starts;
			else if(d[0] == 0xfc) ++stops;
			else if(d[0] < 0x80 || d[0] >= 0xf0) ++others;	// not a channel message, not clock or transport
		}
	};

	int firmware(const bool _mm)
	{
		const char* rom = std::getenv(_mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
		if(!rom || !juce::File(rom).existsAsFile())
		{
			std::puts("mdMachineMidiOutTest: SKIP (the firmware variable is not set)");
			return 77;
		}
		std::printf("the %s's MIDI out at the host\n", _mm ? "Monomachine" : "Machinedrum");
		// A data root of its own (nothing of the user's is touched) and a home kept between runs (the Machinedrum's
		// factory flash is prepared once, B-003)
		const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
		const auto root = tmp.getChildFile("mdMachineMidiOut-" + juce::Uuid().toDashedString());
		root.createDirectory();
		setDataRoot(root);
		juce::ScopedJuceInitialiser_GUI juce;
		synthLib::RomLoader::addSearchPath(juce::File(rom).getParentDirectory().getFullPathName().toStdString());
		mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
		const auto home = tmp.getChildFile(_mm ? "mdMachineMidiOutTest-mm" : "mdMachineMidiOutTest-md");
		home.createDirectory();
		config.deviceHomePath = home.getFullPathName().toStdString() + "/";
		auto processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(
			_mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum, config);
		juce::AudioProcessor& ap = *processor;
		ap.prepareToPlay(44100.0, 128);

		HostOut out;
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
				for(const auto m : midi)
					out.add(m.getMessage());
				std::this_thread::sleep_for(std::chrono::microseconds(1500));	// about real time
			}
		});

		// Every SysEx the machine sends (the desk's answers), counted where the editor gets it
		std::atomic<int> deviceSysex{0};
		auto& controller = dynamic_cast<mdJucePlugin::Controller&>(processor->getController());
		auto sysexListener = std::make_unique<baseLib::EventListener<pluginLib::SysEx>>(controller.evDeviceSysex,
			[&](const pluginLib::SysEx&) { ++deviceSysex; });

		auto* session = processor->getDeskHost()->session();
		check(session != nullptr, "the desk session exists");
		if(!session)
		{
			run = false;
			audio.join();
			return 1;
		}
		std::vector<Value> published;
		std::mutex publishedLock;
		session->attach([&](const Value& _m) { const std::lock_guard l(publishedLock); published.push_back(_m); });
		session->onPageMessage(*elektronData::json::parse(R"({"op":"ready"})"));
		const auto lifecycle = [&]
		{
			const std::lock_guard l(publishedLock);
			for(auto it = published.rbegin(); it != published.rend(); ++it)
				if(str(*it, "type") == "machine")
					if(const auto* doc = it->find("doc"))
						return str(*doc, "lifecycle");
			return std::string();
		};
		int id = 100;
		const auto op = [&](const char* _op)
		{
			session->onPageMessage(*elektronData::json::parse(
				std::string(R"({"op":")") + _op + R"(","id":)" + std::to_string(++id) + "}"));
		};

		// The machine up (its start-up animation over: the desk takes commands), up to 180 s
		for(int i = 0; i < 1800 && lifecycle() != "ready"; ++i)
			pump(100);
		check(lifecycle() == "ready", "the machine is up (lifecycle " + lifecycle() + ")");

		// PLAY until the machine's MIDI comes out (a machine the processor started again asks for PLAY
		// again), up to 30 s
		const auto played = [&] { return _mm ? out.starts > 0 && out.clocks > 10 : out.notes > 5; };
		out.reset();
		for(int i = 0; i < 15 && !played(); ++i)
		{
			op("play");
			pump(2000);
		}
		pump(3000);
		op("stop");
		pump(1000);
		std::printf("  at the host: %d notes, %d CCs, %d clocks, %d starts, %d stops, %d SysEx, %d others; the machine "
			"sent the editor %d SysEx\n",
			out.notes.load(), out.ccs.load(), out.clocks.load(), out.starts.load(), out.stops.load(), out.sysex.load(),
			out.others.load(),
			deviceSysex.load());
		if(_mm)
			check(out.clocks > 50 && out.starts > 0 && out.stops > 0,
				"the Monomachine's clock, start and stop come out at the host while it plays");
		else
			check(out.notes > 5, "the Machinedrum's notes come out at the host while it plays");
		check(deviceSysex > 0, "the machine answered the desk in SysEx meanwhile");
		check(out.sysex == 0, "none of its SysEx reached the host");
		check(out.others == 0, "nothing but channel messages, clock and transport reached the host");

		// Without the route (upstream's matrix) the same playing machine sends the host nothing
		processor->getMidiRoutingMatrix() = synthLib::MidiRoutingMatrix();
		op("play");
		pump(1500);
		out.reset();
		pump(1500);
		const int unrouted = out.notes + out.ccs + out.clocks + out.starts + out.stops;
		op("stop");
		pump(500);
		check(unrouted == 0, "without the route nothing reaches the host (" + std::to_string(unrouted)
			+ " messages): processBlock reads it");

		// A project saved without the route (before 0.4.0) gets it back when loaded
		juce::MemoryBlock saved;
		ap.getStateInformation(saved);
		ap.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
		check(route::toHost(processor->getMidiRoutingMatrix(), event(Source::Device, 0xf8)),
			"a project saved without the route has it again once loaded");

		run = false;
		audio.join();
		session->detach();
		sysexListener.reset();	// before the controller it listens to
		ap.releaseResources();
		processor.reset();
		root.deleteRecursively();
		return g_failures;
	}
}

int main(const int _argc, char** const _argv)
{
	// Every line in a CI log, also when the process dies. Unbuffered, not line-buffered: the MSVC CRT has no line
	// buffering and takes _IOLBF with a size of 0 as an invalid parameter, a fail-fast (0xc0000409) before main's
	// next line (B-047)
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const bool md = _argc > 1 && std::strcmp(_argv[1], "md") == 0;
	const bool mm = _argc > 1 && std::strcmp(_argv[1], "mm") == 0;
	const int result = md || mm ? firmware(mm) : values();
	if(result == 77)
		return 77;
	std::printf("mdMachineMidiOutTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
