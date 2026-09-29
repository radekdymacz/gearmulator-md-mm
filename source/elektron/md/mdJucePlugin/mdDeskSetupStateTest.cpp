// P4: the Machinedrum Editor's setup (md-desk/setup: app modulators, knob-row CCs)
// travels with the plug-in state as the "MDSK" chunk, and a project without it
// starts from the default setup. The controller profile's setup (DESIGN-tr06.md) as the
// "MDCT" chunk, only once changed; its input filter on the processor's MIDI path to a
// wire (simulated TR-06 MIDI). No firmware needed; isolated config.

#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdCtlProfile.h"
#include "mdPageEditor.h"

#include "elektronData/mdGlobal.h"
#include "jucePluginEditorLib/pluginEditorState.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_audio_utils/juce_audio_utils.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig isolated()
	{
		mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig c;
		c.deviceHomePath = std::string{};
		return c;
	}
}

int main()
{
	juce::ScopedJuceInitialiser_GUI juce;
	const std::string setup = R"({"schema":"md-desk/setup","version":1,"modulators":{"schema":"md-desk/modulators","version":1,)"
		R"("sources":[{"id":"lfo1","label":"LFO A","kind":"lfo","shape":2,"rate":"1/4","depth":80,"smooth":30}],)"
		R"("links":[{"source":"lfo1","track":3,"param":12,"min":5,"max":90,"curve":"exp","invert":true}]},"knobCcs":[30,31,32,33,34,35,36,37]})";
	const std::string controller = R"({"schema":"desk/controller","version":1,"machine":"md","profile":"tr06","channel":7,)"
		R"("voices":[{"voice":"BD","t":8}],"knobs":[{"cc":24,"i":16}]})";
	const auto hasChunk = [](const juce::MemoryBlock& _m, const char* _id)
	{
		const auto* d = static_cast<const char*>(_m.getData());
		for(size_t i = 0; i + 4 <= _m.getSize(); ++i)
			if(std::memcmp(d + i, _id, 4) == 0)
				return true;
		return false;
	};
	juce::MemoryBlock withSetup, without;
	{
		mdJucePlugin::AudioPluginAudioProcessor a(md::MachineModel::Machinedrum, isolated(), false);
		juce::AudioProcessor& ja = a;
		ja.getStateInformation(without);
		a.getDeskHost()->setSetup(setup);
		a.getDeskHost()->setController(controller);
		ja.getStateInformation(withSetup);
	}
	check(!hasChunk(without, "MDCT") && hasChunk(withSetup, "MDCT"), "the controller profile's chunk is there only once it was changed");
	mdJucePlugin::AudioPluginAudioProcessor b(md::MachineModel::Machinedrum, isolated(), false);
	juce::AudioProcessor& jb = b;
	const auto g0 = b.getDeskHost()->setupVersion();
	jb.setStateInformation(withSetup.getData(), static_cast<int>(withSetup.getSize()));
	check(b.getDeskHost()->setup() == setup, "the setup comes back with the project, byte for byte");
	check(b.getDeskHost()->setupVersion() != g0, "the session sees a new version");
	check(b.getDeskHost()->controller() == controller, "the controller profile's setup comes back with the project, byte for byte");
	const auto g1 = b.getDeskHost()->setupVersion();
	jb.setStateInformation(without.getData(), static_cast<int>(without.getSize()));
	check(b.getDeskHost()->setup().empty(), "a project without it starts from the default setup");
	check(b.getDeskHost()->setupVersion() != g1, "and says so");
	check(b.getDeskHost()->controller().empty(), "a project without the controller chunk starts with the profile off");
	// The controller profile's input filter on the processor's own MIDI path, to a wire (external MIDI on,
	// as the HW MIDI engine has it): a TR-06 voice leaves as the machine's note, in the same block; the
	// raw note, All Sound Off and Active Sensing do not; another channel and the clock pass as before.
	{
		jb.prepareToPlay(44100.0, 256);
		b.getExternalMidi().set(true);
		auto s = deskController::defaults(deskController::Machine::Md);
		s.on = true;
		elektronData::MdGlobal g;
		g.keymap.fill(elektronData::MdGlobal::g_unmapped);
		g.keymap[40] = 1;	// track 2
		g.baseChannel = 2;
		{
			mdJucePlugin::CtlInputFilter filter(b);
			filter.configure(s, deskController::mdRoute(s, &g));
			juce::AudioBuffer<float> audio(jb.getTotalNumOutputChannels(), 256);
			juce::MidiBuffer midi;
			midi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<uint8_t>(100)), 10);	// SD
			midi.addEvent(juce::MidiMessage::controllerEvent(10, 120, 0), 11);
			midi.addEvent(juce::MidiMessage(0xfe), 12);
			midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 99), 13);	// not the TR-06's channel
			jb.processBlock(audio, midi);
			bool note = false, raw = false, sound = false, sensing = false;
			for(const auto m : midi)
			{
				const auto msg = m.getMessage();
				note |= msg.isNoteOn() && msg.getChannel() == 3 && msg.getNoteNumber() == 40 && msg.getVelocity() == 100;
				raw |= msg.isNoteOn() && msg.getChannel() == 10;
				sound |= msg.isController() && msg.getControllerNumber() == 120;
				sensing |= msg.isActiveSense();
			}
			check(note && !raw, "wire: the TR-06's SD leaves as track 2's keymap note on the base channel, not raw");
			check(!sound && !sensing, "wire: All Sound Off and Active Sensing from the TR-06 go nowhere");
			s.on = false;
			filter.configure(s, deskController::mdRoute(s, &g));
		}
		b.getExternalMidi().set(false);
		jb.releaseResources();
	}
	// The standalone app's MIDI inputs (AUDIO/MIDI, MIDI INPUTS: a TR-06 enabled): JUCE's standalone
	// holder registers its AudioProcessorPlayer as the device manager's callback for every enabled input
	// (addMidiInputDeviceCallback({}, &player)); the player collects what comes in and hands it to
	// processBlock with the next audio block, as host MIDI, where the processor offers it to the input
	// filter (ExternalMidi::takeIn). The same player here, on a stand-in audio device, the message as an
	// enabled input delivers it: with the profile off and the page watching, the filter counts it (what
	// the Controller panel shows) and the machine still gets it; with the profile on, it is the knob's.
	{
		struct Device final : juce::AudioIODevice
		{
			Device() : juce::AudioIODevice("stand-in", "test") {}
			juce::StringArray getOutputChannelNames() override { return {"L", "R"}; }
			juce::StringArray getInputChannelNames() override { return {}; }
			juce::Array<double> getAvailableSampleRates() override { return {44100.0}; }
			juce::Array<int> getAvailableBufferSizes() override { return {256}; }
			int getDefaultBufferSize() override { return 256; }
			juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
			void close() override {}
			bool isOpen() override { return true; }
			void start(juce::AudioIODeviceCallback*) override {}
			void stop() override {}
			bool isPlaying() override { return true; }
			juce::String getLastError() override { return {}; }
			int getCurrentBufferSizeSamples() override { return 256; }
			double getCurrentSampleRate() override { return 44100.0; }
			int getCurrentBitDepth() override { return 32; }
			juce::BigInteger getActiveOutputChannels() const override { return 3; }
			juce::BigInteger getActiveInputChannels() const override { return 0; }
			int getOutputLatencyInSamples() override { return 0; }
			int getInputLatencyInSamples() override { return 0; }
		} device;
		juce::AudioProcessorPlayer player;
		player.setProcessor(&jb);
		player.audioDeviceAboutToStart(&device);
		std::vector<float> l(256), r(256);
		float* outs[] = {l.data(), r.data()};
		const auto fromInput = [&](juce::MidiMessage _m)
		{
			_m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);	// as a MIDI input stamps it
			player.handleIncomingMidiMessage(nullptr, _m);
			player.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, 256, {});
		};
		auto s = deskController::defaults(deskController::Machine::Md);
		elektronData::MdGlobal g;
		g.baseChannel = 0;
		mdJucePlugin::CtlInputFilter filter(b);
		filter.configure(s, deskController::mdRoute(s, &g, true), true);
		check(filter.installed(), "standalone: the profile off, the page watching: the filter is in the path (counting only)");
		fromInput(juce::MidiMessage::controllerEvent(10, 24, 87));
		const auto seen = filter.monitor().last(9);
		check(seen.count == 1 && seen.a == 0xb9 && seen.b == 24 && seen.c == 87, "standalone: a TR-06 CC 24 = 87 from an enabled MIDI input reaches the filter (channel 10)");
		check(filter.input().takeKnob(24) < 0, "standalone: the profile off takes nothing (the machine gets it as before)");
		s.on = true;
		filter.configure(s, deskController::mdRoute(s, &g, true), true);
		fromInput(juce::MidiMessage::controllerEvent(10, 24, 99));
		check(filter.input().takeKnob(24) == 99, "standalone: the profile on, the same input's CC 24 is the controller's knob");
		s.on = false;
		filter.configure(s, deskController::mdRoute(s, &g, true), false);
		check(!filter.installed(), "standalone: off and not watching: out of the path again");
		player.audioDeviceStopped();
		player.setProcessor(nullptr);
	}
	// P4 HW MIDI: the plug-in's MIDI in/out carry the editor's traffic to external hardware.
	{
		jb.prepareToPlay(44100.0, 256);
		b.getExternalMidi().set(true);
		synthLib::SMidiEvent out(synthLib::MidiEventSource::Editor);
		out.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x70, 0x04, 0xf7};
		b.getExternalMidi().send(out);
		juce::AudioBuffer<float> audio(jb.getTotalNumOutputChannels(), 256);
		juce::MidiBuffer midi;
		const uint8_t reply[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 0x05, 0xf7};
		midi.addEvent(juce::MidiMessage::createSysExMessage(reply + 1, sizeof(reply) - 2), 0);
		jb.processBlock(audio, midi);
		bool sent = false;
		for(const auto m : midi)
			sent |= m.numBytes == 9 && m.data[6] == 0x70;
		check(sent, "external MIDI: the editor's SysEx leaves through the plug-in's MIDI out");
		std::vector<synthLib::SMidiEvent> in;
		b.getExternalMidi().drainIn(in);
		check(in.size() == 1 && in[0].sysex.size() == sizeof(reply) && in[0].sysex[6] == 0x72, "and the hardware's SysEx comes to the editor, not the device");
		b.getExternalMidi().set(false);
		jb.releaseResources();
	}
#if JUCE_MAC
	// Teardown with the editor page made (v0.2.0 crashed on quit): the page detaches from the session
	// when it goes, so the processor destroys its editor state before the session. The page outlives
	// its window (the processor's editor state owns it): made through the editor window and the
	// window closed first, as a plug-in host and the standalone do on quit, and made without a
	// window. Freed memory is scribbled (MallocScribble in ctest), so a detach from a freed session
	// crashes here rather than passing by luck. macOS only: it opens a real editor (a web view).
	for(const bool withWindow : {true, false})
	{
		auto c = std::make_unique<mdJucePlugin::AudioPluginAudioProcessor>(md::MachineModel::Machinedrum, isolated(), false);
		std::unique_ptr<juce::AudioProcessorEditor> window;
		if(withWindow)
			window.reset(static_cast<juce::AudioProcessor&>(*c).createEditorIfNeeded());
		const auto& state = c->getOrCreateEditorState();
		check(dynamic_cast<mdJucePlugin::PageEditor*>(state.getEditor()) && c->getDeskHost()->session() && (window || !withWindow),
			withWindow ? "teardown: the editor page is made through its window" : "teardown: the editor page is made without a window");
		window.reset();
		c.reset();
		check(true, "teardown: the processor goes after its editor page, no use of a freed session");
	}
#endif
	std::printf("mdDeskSetupStateTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
