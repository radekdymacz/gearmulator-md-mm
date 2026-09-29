// The controller profile on the real firmware (DESIGN-tr06.md), through the whole plug-in path: the
// processor's MIDI in (host MIDI into processBlock, as a DAW or the standalone's port gives it), the
// input filter, the session and the machine. Simulated TR-06 messages only: no TR-06 was connected.
//   GEARMULATOR_MD_FIRMWARE_BIN=<MD OS 1.63 ROM> mdTr06FirmwareTest md
//   GEARMULATOR_MM_FIRMWARE_BIN=<MM OS 1.32B ROM> mdTr06FirmwareTest mm
// Exits 77 without the ROM. The config is isolated (EphemeralConfig). Checks, per machine:
//  - profile off: a TR-06 note on channel 10 reaches the machine raw: the Monomachine does not listen there
//    (silence); the emulated Machinedrum takes any note 36-51 as a TRIG key (upstream's pads), so the raw SD
//    (38) plays track 3;
//  - profile on: every voice and its aliases play the mapped track: MD by the note the machine itself sends
//    for the track it played (its keymap note), MM by sound; a mapping edit moves BD to track 9 (MD);
//  - a TR-06 knob moves the page's selected track's parameter in the machine's working kit (from its
//    memory), and a 60 Hz burst ends on the knob's last value;
//  - the controller documents the session publishes are on the contract;
//  - profile off again: the channel is raw once more (silence).

#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdDeskSession.h"

#include "elektronData/json.h"
#include "elektronData/jsonSchema.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "synthLib/midiRoutingMatrix.h"
#include "synthLib/romLoader.h"

#include <atomic>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Value = elektronData::json::Value;

	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	Value parseJson(const std::string& _json)
	{
		auto v = elektronData::json::parse(_json);
		return v ? *v : Value::object();
	}

	const Value* at(const Value& _v, const std::vector<std::string>& _path)
	{
		const Value* v = &_v;
		for(const auto& k : _path)
		{
			if(!v)
				return nullptr;
			if(v->isArray())
			{
				const auto i = static_cast<size_t>(std::stoi(k));
				v = i < v->asArray().size() ? &v->asArray()[i] : nullptr;
			}
			else
				v = v->find(k);
		}
		return v;
	}

	// The message thread's work (the session's timer) for _ms.
	void pump(const double _ms)
	{
		const auto end = juce::Time::getMillisecondCounterHiRes() + _ms;
		while(juce::Time::getMillisecondCounterHiRes() < end)
		{
#if JUCE_MAC
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.008, false);
#else
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
#endif
		}
	}

	// Until _done or _ms passed; true when done.
	bool waitFor(const std::function<bool()>& _done, const double _ms)
	{
		const auto end = juce::Time::getMillisecondCounterHiRes() + _ms;
		while(juce::Time::getMillisecondCounterHiRes() < end)
		{
			if(_done())
				return true;
			pump(16);
		}
		return _done();
	}
}

int main(const int _argc, char** _argv)
{
	const std::string which = _argc > 1 ? _argv[1] : "md";
	const bool mm = which == "mm";
	const auto* rom = std::getenv(mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
	if(!rom || !*rom)
	{
		std::printf("mdTr06FirmwareTest %s: SKIP (%s not set)\n", which.c_str(), mm ? "GEARMULATOR_MM_FIRMWARE_BIN" : "GEARMULATOR_MD_FIRMWARE_BIN");
		return 77;
	}
	juce::ScopedJuceInitialiser_GUI juce;
	synthLib::RomLoader::addSearchPath(juce::File(rom).getParentDirectory().getFullPathName().toStdString());
	mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig config;
	const auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(mm ? "mdTr06FirmwareTestMm" : "mdTr06FirmwareTestMd");
	home.createDirectory();
	config.deviceHomePath = home.getFullPathName().toStdString() + "/";
	auto processor = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(mm ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum, config, false);
	juce::AudioProcessor& ap = *processor;
	constexpr int g_block = 128;
	ap.prepareToPlay(44100.0, g_block);

	// The host: MIDI in with the audio block (as a DAW gives it), the output's peak measured.
	std::mutex midiMutex;
	std::vector<juce::MidiMessage> midiIn;
	std::atomic<float> peak{0};
	// What the machine itself sends (its MIDI out, routed to the host here): which track played.
	std::vector<juce::MidiMessage> midiOut;
	processor->getMidiRoutingMatrix().setEnabled(synthLib::MidiEventSource::Device, synthLib::MidiEventSource::Host,
		synthLib::MidiRoutingMatrix::EventType::All, true);
	std::atomic<bool> run{true};
	std::thread audio([&]
	{
		juce::AudioBuffer<float> buf(ap.getTotalNumOutputChannels(), g_block);
		juce::MidiBuffer midi;
		while(run)
		{
			midi.clear();
			{
				const std::scoped_lock lock(midiMutex);
				for(const auto& m : midiIn)
					midi.addEvent(m, 0);
				midiIn.clear();
			}
			buf.clear();
			ap.processBlock(buf, midi);
			{
				const std::scoped_lock lock(midiMutex);
				for(const auto m : midi)
				{
					const auto msg = m.getMessage();
					if(msg.isNoteOn() || msg.isController())
						midiOut.push_back(msg);
				}
			}
			float p = 0;
			for(int c = 0; c < buf.getNumChannels(); ++c)
				p = std::max(p, buf.getMagnitude(c, 0, g_block));
			float old = peak.load();
			while(p > old && !peak.compare_exchange_weak(old, p)) {}
			std::this_thread::sleep_for(std::chrono::microseconds(2500));	// about real time (2.9 ms a block)
		}
	});
	const auto send = [&](const juce::MidiMessage& _m)
	{
		const std::scoped_lock lock(midiMutex);
		midiIn.push_back(_m);
	};
	// The loudest block while _play runs and _ms after it.
	const auto loudest = [&](const std::function<void()>& _play, const double _ms)
	{
		pump(300);
		{
			const std::scoped_lock lock(midiMutex);
			midiOut.clear();
		}
		peak = 0;
		_play();
		pump(_ms);
		return peak.load();
	};

	auto* session = processor->getDeskHost()->session();
	if(!session)
	{
		std::puts("mdTr06FirmwareTest: FAIL (no desk session)");
		run = false;
		audio.join();
		return 1;
	}
	std::vector<Value> published;
	session->attach([&](const Value& _m) { published.push_back(_m); });
	const auto last = [&](const std::string& _type, const std::function<bool(const Value&)>& _also = {}) -> const Value*
	{
		for(auto it = published.rbegin(); it != published.rend(); ++it)
		{
			const auto* t = it->find("type");
			if(t && t->isString() && t->asString() == _type && (!_also || _also(*it)))
				return &*it;
		}
		return nullptr;
	};
	const auto kindIs = [](const char* _kind) { return [_kind](const Value& _m) { const auto* k = _m.find("kind"); return k && k->isString() && k->asString() == _kind; }; };
	const auto lifecycle = [&]
	{
		const auto* m = last("machine");
		const auto* l = m ? at(*m, {"doc", "lifecycle"}) : nullptr;
		return l && l->isString() ? l->asString() : std::string();
	};
	std::printf("mdTr06FirmwareTest %s (simulated TR-06 MIDI on channel 10)\n", which.c_str());
	session->onPageMessage(parseJson(R"({"op":"ready"})"));
	const bool ready = waitFor([&] { return lifecycle() == "ready" && last("doc", kindIs("workingKit")) && last("doc", kindIs("global")); }, 120000);
	check(ready, "the machine is ready, the working kit and the global are read (" + lifecycle() + ")");
	// The processor's own services may start the machine again (a factory flash): ready for 3 s in a row.
	{
		double since = juce::Time::getMillisecondCounterHiRes();
		const auto end = since + 120000;
		while(juce::Time::getMillisecondCounterHiRes() < end)
		{
			pump(50);
			if(lifecycle() != "ready")
				since = juce::Time::getMillisecondCounterHiRes();
			else if(juce::Time::getMillisecondCounterHiRes() - since > 3000)
				break;
		}
		check(lifecycle() == "ready", "the machine stays ready (" + lifecycle() + ")");
	}

	const auto ctl = [&] { return last("controller"); };
	const auto voiceOut = [&]() -> const Value*
	{
		const auto* c = ctl();
		const auto* o = c ? at(*c, {"doc", "voices", "0", "out"}) : nullptr;
		return o && o->isObject() ? o : nullptr;
	};
	const auto* warn = ctl() ? at(*ctl(), {"doc", "warning"}) : nullptr;
	std::printf("  the controller document: %s\n", ctl() ? "published" : "missing");

	const auto note = [&](const int _n) { return [&, _n] { send(juce::MidiMessage::noteOn(10, _n, static_cast<uint8_t>(127))); pump(120); send(juce::MidiMessage::noteOff(10, _n)); }; };
	// The Machinedrum says which track played: its MIDI out sends the track's keymap note (a TRIG key press
	// on the emulator does). The note its global's keymap gives a track (the first one).
	const auto keymapNote = [&](const int _t) -> int
	{
		const auto* g = last("doc", kindIs("global"));
		const auto* k = g ? at(*g, {"doc", "keymap"}) : nullptr;
		if(k && k->isArray())
			for(size_t n = 0; n < k->asArray().size(); ++n)
				if(k->asArray()[n].isNumber() && static_cast<int>(k->asArray()[n].asNumber()) == _t)
					return static_cast<int>(n);
		return -1;
	};
	const auto outNotes = [&]
	{
		std::vector<int> n;
		const std::scoped_lock lock(midiMutex);
		for(const auto& m : midiOut)
			if(m.isNoteOn())
				n.push_back(m.getNoteNumber());
		return n;
	};
	const auto played = [&](const std::vector<int>& _notes, const int _want) { return std::find(_notes.begin(), _notes.end(), _want) != _notes.end(); };
	const auto list = [](const std::vector<int>& _n) { std::string s; for(const auto x : _n) s += (s.empty() ? "" : " ") + std::to_string(x); return s.empty() ? std::string("none") : s; };

	// Profile off: channel 10 is not the machine's (its defaults: MD 1-4; MM 1-6 and its extra channels).
	// The Monomachine does not listen there (silence). The emulated Machinedrum takes any note 36-51 as a TRIG
	// key (upstream's pads): the TR-06's SD (38) presses TRIG 3, not track 2's.
	const float offPeak = loudest(note(38), 700);
	const auto offNotes = outNotes();
	std::printf("  profile off, SD (38) on channel 10: peak %.4f, the machine sent notes: %s\n", offPeak, list(offNotes).c_str());
	if(!mm)
		check(played(offNotes, keymapNote(2)), "MD, profile off: the raw SD note presses TRIG 3 (upstream's pads): track 3 played, not track 2");

	session->onPageMessage(parseJson(R"({"op":"ctlSet","id":1,"profile":"tr06"})"));
	const bool routed = waitFor([&] { return voiceOut() != nullptr; }, 5000);
	check(routed, "profile on: BD has a route from the machine's global");
	if(routed)
		std::printf("  BD plays channel %d note %d\n", static_cast<int>(voiceOut()->find("ch")->asNumber()), static_cast<int>(voiceOut()->find("note")->asNumber()));
	warn = ctl() ? at(*ctl(), {"doc", "warning"}) : nullptr;
	const bool overlap = warn && warn->isString() && !warn->asString().empty();
	check(!overlap, "channel 10 does not overlap the machine's channels");
	float onPeak = 0;
	if(mm)
	{
		onPeak = loudest(note(36), 700);
		std::printf("  profile on, BD (36) on channel 10: peak %.4f (off: %.4f)\n", onPeak, offPeak);
		check(onPeak > 0.01f && offPeak < 0.001f, "MM: the TR-06's BD plays track 1 (sound only with the profile)");
		check(loudest(note(35), 700) > 0.01f, "MM: the Rx alias 35 plays it too");
		const float oh = loudest(note(46), 700);
		check(oh > 0.01f, "MM: OH plays track 6");
	}
	else
	{
		const std::pair<int, int> cases[] = {{36, 0}, {38, 1}, {40, 1}, {47, 2}, {50, 3}, {49, 4}, {46, 5}, {42, 6}};
		for(const auto& [n, t] : cases)
		{
			const float p = loudest(note(n), 600);
			const auto got = outNotes();
			onPeak = std::max(onPeak, p);
			check(p > 0.01f && played(got, keymapNote(t)) && (got.size() == 1),
				"MD, profile on: TR-06 note " + std::to_string(n) + " plays track " + std::to_string(t + 1) + " (peak " + std::to_string(p).substr(0, 6) + ", the machine sent " + list(got) + ", track "
				+ std::to_string(t + 1) + "'s note " + std::to_string(keymapNote(t)) + ")");
		}
		session->onPageMessage(parseJson(R"({"op":"ctlVoice","id":4,"voice":"BD","t":8})"));
		pump(400);
		loudest(note(36), 600);
		const auto remapped = outNotes();
		check(played(remapped, keymapNote(8)), "a mapping edit: BD now plays track 9 (the machine sent " + list(remapped) + ")");
		session->onPageMessage(parseJson(R"({"op":"ctlReset","id":5})"));
		pump(400);
	}

	// a knob: the page's selected track 3, BD LEVEL (CC 24) -> MD SYN 1 / MM SYN A
	session->onPageMessage(parseJson(R"({"op":"ctlTrack","id":2,"t":2})"));
	const std::vector<std::string> path = mm ? std::vector<std::string>{"doc", "tracks", "2", "pages", "0", "0"} : std::vector<std::string>{"doc", "tracks", "2", "synth", "0"};
	const auto kitValue = [&]() -> int
	{
		const auto* w = last("doc", [&](const Value& _m)
		{
			const auto* k = _m.find("kind");
			const auto* p = _m.find("pending");
			return k && k->asString() == "workingKit" && !(p && p->isBool() && p->asBool());
		});
		const auto* v = w ? at(*w, path) : nullptr;
		return v && v->isNumber() ? static_cast<int>(v->asNumber()) : -1;
	};
	const int before = kitValue();
	const int want = before == 100 ? 101 : 100;
	send(juce::MidiMessage::controllerEvent(10, 24, want));
	const bool moved = waitFor([&] { return kitValue() == want; }, 4000);
	check(moved, "BD LEVEL (CC 24) moves track 3's " + std::string(mm ? "SYN A" : "SYN 1") + " to " + std::to_string(want) + " in the machine's memory (was " + std::to_string(before) + ", now "
		+ std::to_string(kitValue()) + ")");
	for(int n = 0; n < 60; ++n)
	{
		send(juce::MidiMessage::controllerEvent(10, 24, 20 + n));
		pump(1000.0 / 60);
	}
	const bool burst = waitFor([&] { return kitValue() == 79; }, 4000);
	check(burst, "a 60 Hz knob burst ends on its last value (79; now " + std::to_string(kitValue()) + ")");
	// All Sound Off and Reset All Controllers from the TR-06 are blocked: the machine still plays
	send(juce::MidiMessage::controllerEvent(10, 121, 0));
	send(juce::MidiMessage::controllerEvent(10, 120, 0));
	const float afterOffline = loudest(note(36), 700);
	check(afterOffline > 0.01f, "after the TR-06's MIDI OFFLINE messages (All Sound Off, Reset All Controllers: blocked) the voices still play");

	session->onPageMessage(parseJson(R"({"op":"ctlSet","id":3,"profile":"off"})"));
	pump(300);
	const float offAgain = loudest(note(38), 700);
	const auto rawAgain = outNotes();
	std::printf("  profile off again, SD (38): peak %.4f, the machine sent %s\n", offAgain, list(rawAgain).c_str());
	check(mm ? offAgain < onPeak / 10 : played(rawAgain, keymapNote(2)), "profile off again: channel 10 is raw once more");

	// the controller documents on the contract
	{
		std::ifstream in(mm ? MMDESK_SCHEMA : MDDESK_SCHEMA);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		const auto root = elektronData::json::parse(text);
		size_t n = 0, off = 0;
		if(root)
		{
			const elektronData::json::Schema schema(*root);
			for(const auto& m : published)
				if(const auto* t = m.find("type"); t && t->isString() && t->asString() == "controller")
				{
					++n;
					const auto problems = schema.validate(m, "message");
					if(!problems.empty() && off++ == 0)
						std::printf("    %s\n", problems.front().c_str());
				}
		}
		check(root && n > 0 && off == 0, std::to_string(n) + " controller documents, " + std::to_string(off) + " off the contract");
	}

	session->detach();
	run = false;
	audio.join();
	ap.releaseResources();
	processor.reset();
	std::printf("mdTr06FirmwareTest %s: %s (%d failures)\n", which.c_str(), g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
