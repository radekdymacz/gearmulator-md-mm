// The fork's hooks in jucePluginLib's Processor (doc/modern-ux/UPSTREAM.md): the host latency published after a
// resampler mode change (codex review 2026-10, item 7), and no MIDI learn translator while the editors' MIDI mapping
// is off (item 8). No firmware: a synthetic device, and the editors' processors without a ROM.
#include "mdPluginProcessor.h"

#include "deskHost/deskHost.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_events/juce_events.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"
#include "synthLib/plugin.h"
#include "synthLib/resampler.h"
#include "synthLib/syntheticAudioTestDevice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	class SyntheticController final : public pluginLib::Controller
	{
	public:
		explicit SyntheticController(pluginLib::Processor& _processor) : Controller(_processor) {}

		void sendParameterChange(const pluginLib::Parameter&, pluginLib::ParamValue, pluginLib::Parameter::Origin) override {}
		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override { return false; }
		void onStateLoaded() override {}
	};

	// A device at 44.1 kHz behind upstream's Processor: at another host rate each resampler mode has its own delay
	class SyntheticProcessor final : public pluginLib::Processor
	{
	public:
		SyntheticProcessor()
			: Processor(BusesProperties()
				.withInput("Input A/B", juce::AudioChannelSet::stereo(), true)
				.withOutput("Main A/B", juce::AudioChannelSet::stereo(), true)
				.withOutput("Out C/D", juce::AudioChannelSet::stereo(), false)
				.withOutput("Out E/F", juce::AudioChannelSet::stereo(), false),
				{"Synthetic audio I/O", "Test", true, true, false, false,
					"Tsio", "urn:gearmulator:test:audio-io", {}, {},
					{0}, {0, 2, 4}})
		{
		}

		synthLib::Device* createDevice() override
		{
			return new synthLib::test::SyntheticAudioDevice(2, 6, 19, false, 0.125f);
		}
		pluginLib::Controller* createController() override { return new SyntheticController(*this); }
		juce::AudioProcessorEditor* createEditor() override { return nullptr; }
		bool hasEditor() const override { return false; }
		bool isBusesLayoutSupported(const BusesLayout& _layout) const override
		{
			if(_layout.inputBuses.size() != 1 || _layout.outputBuses.size() != 3)
				return false;
			const auto input = _layout.getMainInputChannelSet();
			if(input != juce::AudioChannelSet::disabled() && input != juce::AudioChannelSet::stereo())
				return false;
			for(int bus = 0; bus < _layout.outputBuses.size(); ++bus)
			{
				const auto channels = _layout.outputBuses[bus];
				if(channels != juce::AudioChannelSet::disabled() && channels != juce::AudioChannelSet::stereo())
					return false;
			}
			return _layout.outputBuses[0] == juce::AudioChannelSet::stereo();
		}
		void serviceAsyncForTest() { handleAsyncUpdate(); }
	};

	class LatencyListener final : public juce::AudioProcessorListener
	{
	public:
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _details) override
		{
			if(_details.latencyChanged)
				++latencyChanges;
		}

		std::atomic<uint32_t> latencyChanges{0};
	};

	// Each resampler has its own group delay. A mode change (settings page, project restore) must publish the new
	// latency to the host at once on the message thread, and through the async update from any other thread.
	void verifyResamplerModeLatencyIsPublished()
	{
		constexpr std::array<synthLib::Resampler::Mode, 4> modes{
			synthLib::Resampler::Mode::MameHq, synthLib::Resampler::Mode::Legacy,
			synthLib::Resampler::Mode::MameLofi, synthLib::Resampler::Mode::Legacy};

		SyntheticProcessor processor;
		juce::AudioProcessor& audioProcessor = processor;
		audioProcessor.prepareToPlay(48000.0, 256);
		LatencyListener listener;
		processor.addListener(&listener);

		const auto expectedLatency = [&]
		{
			return static_cast<int>(std::max(processor.getPlugin().getLatencyMidiToOutput(),
				processor.getPlugin().getLatencyInputToOutput()));
		};

		bool anyModeChangedLatency = false;
		for(const auto mode : modes)
		{
			const auto before = processor.getLatencySamples();
			const auto notificationsBefore = listener.latencyChanges.load();
			processor.setResamplerMode(mode);
			const auto expected = expectedLatency();
			require(processor.getLatencySamples() == expected,
				"resampler mode " + std::to_string(static_cast<int>(mode))
				+ " left the host latency stale: reported " + std::to_string(processor.getLatencySamples())
				+ ", plug-in latency " + std::to_string(expected));
			if(expected == before)
				continue;
			anyModeChangedLatency = true;
			require(listener.latencyChanges.load() > notificationsBefore,
				"resampler mode latency change was not published to the host");
		}
		require(anyModeChangedLatency, "no resampler mode changed the latency at 48 kHz; the test proves nothing");

		// From a worker thread the host is told on the message thread, never synchronously
		processor.setResamplerMode(synthLib::Resampler::Mode::Legacy);
		const auto legacyLatency = processor.getLatencySamples();
		const auto notificationsBefore = listener.latencyChanges.load();
		std::thread worker([&]
		{
			processor.setResamplerMode(synthLib::Resampler::Mode::MameHq);
		});
		worker.join();
		const auto expected = expectedLatency();
		require(expected != legacyLatency, "MAME HQ and legacy resampling have the same latency");
		require(listener.latencyChanges.load() == notificationsBefore && processor.getLatencySamples() == legacyLatency,
			"resampler mode change on a worker thread notified the host synchronously");
		processor.serviceAsyncForTest();
		require(listener.latencyChanges.load() > notificationsBefore && processor.getLatencySamples() == expected,
			"resampler mode change on a worker thread never published its latency");
		processor.removeListener(&listener);
	}

	// MIDI mapping off: incoming MIDI must not run through the (not thread-safe) learn translator at all
	void verifyNoMidiLearnTranslatorWhileMappingIsOff(const md::MachineModel _model)
	{
		mdJucePlugin::AudioPluginAudioProcessor processor(_model,
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{}, false);
		require((processor.getMidiLearnTranslator() != nullptr) == deskHost::midiMappingEnabled,
			"the MIDI learn translator does not follow the MIDI mapping switch");
	}
}

int main()
{
	const juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		verifyResamplerModeLatencyIsPublished();
		verifyNoMidiLearnTranslatorWhileMappingIsOff(md::MachineModel::Machinedrum);
		verifyNoMidiLearnTranslatorWhileMappingIsOff(md::MachineModel::Monomachine);
		std::cout << "mdProcessorHooksTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mdProcessorHooksTest: FAIL: " << _e.what() << '\n';
		return 1;
	}
}
