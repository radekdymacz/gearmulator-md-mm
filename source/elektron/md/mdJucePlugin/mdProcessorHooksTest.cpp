// The fork's hooks in jucePluginLib's Processor (doc/modern-ux/UPSTREAM.md): the host latency published after a
// resampler mode change (codex review 2026-10, item 7), and no MIDI learn translator while the editors' MIDI mapping
// is off (item 8), and a project saved with upstream's DSP bridge (a remote device, deleted in 0.5) loading on the
// local device. No firmware: a synthetic device, and the editors' processors without a ROM.
#include "mdPluginProcessor.h"

#include "deskHost/deskHost.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_events/juce_events.h"
#include "baseLib/binarystream.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"
#include "jucePluginLib/types.h"
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
#include <vector>

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

		void sendParameterChange(const pluginLib::Parameter&, pluginLib::ParamValue,
			pluginLib::Parameter::Origin) override {}
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

	std::vector<uint8_t> stateOf(juce::AudioProcessor& _processor)
	{
		juce::MemoryBlock block;
		_processor.getStateInformation(block);
		const auto* bytes = static_cast<const uint8_t*>(block.getData());
		return {bytes, bytes + block.getSize()};
	}

	// A state as upstream's processor wrote it with the DSP bridge on: the REMO chunk (device type, host, port) in
	// front of the other chunks (jucePluginLib/processor.cpp, saveChunkData before 0.5). _remo is its payload.
	std::vector<uint8_t> withRemoChunk(const std::vector<uint8_t>& _state, const uint32_t _version,
		const std::vector<uint8_t>& _remo)
	{
		baseLib::BinaryStream in(_state);
		const auto magic = in.readString();
		const auto version = in.read<uint32_t>();
		std::vector<uint8_t> chunks;
		in.read(chunks);

		baseLib::BinaryStream remo;
		{
			baseLib::ChunkWriter cw(remo, "REMO", _version);
			if(!_remo.empty())
				remo.write(_remo.data(), _remo.size());
		}
		std::vector<uint8_t> custom;
		remo.toVector(custom);
		custom.insert(custom.end(), chunks.begin(), chunks.end());

		baseLib::BinaryStream out;
		out.write(magic);
		out.write(version);
		out.write(custom);
		std::vector<uint8_t> result;
		out.toVector(result);
		return result;
	}

	std::vector<uint8_t> remoteDevicePayload()
	{
		baseLib::BinaryStream s;
		s.write<int32_t>(1);	// DeviceType::Remote, as upstream stored it
		s.write(std::string("192.168.1.20"));
		s.write<uint32_t>(58000);
		std::vector<uint8_t> bytes;
		s.toVector(bytes);
		return bytes;
	}

	// An old project that says "remote device" loads on the local device: no connection, no failure, no crash, and
	// the chunks after REMO are read (the output gain). Also with a damaged REMO payload and a newer chunk version.
	void verifyOldRemoteDeviceStateLoadsLocal()
	{
		SyntheticProcessor saved;
		juce::AudioProcessor& savedAudio = saved;
		savedAudio.prepareToPlay(48000.0, 256);
		saved.setOutputGain(0.25f);
		const auto state = stateOf(savedAudio);

		struct Case { const char* name; uint32_t version; std::vector<uint8_t> payload; };
		const std::vector<Case> cases{
			{"remote device, as upstream saved it", 1, remoteDevicePayload()},
			{"damaged payload", 1, {0x01, 0x00, 0x00}},
			{"empty payload", 1, {}},
			{"newer chunk version", 7, remoteDevicePayload()},
		};

		for(const auto& c : cases)
		{
			SyntheticProcessor loaded;
			juce::AudioProcessor& loadedAudio = loaded;
			loadedAudio.prepareToPlay(48000.0, 256);
			auto* const deviceBefore = loaded.getPlugin().getDevice();
			require(deviceBefore != nullptr && loaded.getOutputGain() == 1.0f, std::string(c.name) + ": bad start");

			const auto crafted = withRemoChunk(state, c.version, c.payload);
			loadedAudio.setStateInformation(crafted.data(), static_cast<int>(crafted.size()));

			require(loaded.getDeviceType() == pluginLib::DeviceType::Local,
				std::string(c.name) + ": the device type is not local");
			require(loaded.getPlugin().getDevice() == deviceBefore && loaded.isPluginValid(),
				std::string(c.name) + ": the local device was replaced or is invalid");
			require(loaded.getOutputGain() == 0.25f,
				std::string(c.name) + ": the chunks after REMO were not read (output gain "
				+ std::to_string(loaded.getOutputGain()) + ")");

			// saved again, the project has no REMO chunk
			const auto again = stateOf(loadedAudio);
			const std::string tag("REMO");
			require(std::search(again.begin(), again.end(), tag.begin(), tag.end()) == again.end(),
				std::string(c.name) + ": a REMO chunk was written");
		}

		// The editors' processors: their own project with a REMO chunk in front loads on the local device (without
		// a ROM the project is held and handed back, DeskHost::holdState; with one it is read)
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
		{
			mdJucePlugin::AudioPluginAudioProcessor processor(model,
				mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{});
			juce::AudioProcessor& audio = processor;
			// without a ROM the stand-in has no project of its own to save: the synthetic one stands in for it
			const auto own = stateOf(audio);
			const bool held = own.empty();
			const auto crafted = withRemoChunk(held ? state : own, 1, remoteDevicePayload());
			audio.setStateInformation(crafted.data(), static_cast<int>(crafted.size()));
			require(processor.getDeviceType() == pluginLib::DeviceType::Local,
				"the editor's processor left the local device");
			if(held)
				require(stateOf(audio) == crafted, "the editor's processor without a ROM did not hand the project back");
		}
	}

	// MIDI mapping off: incoming MIDI must not run through the (not thread-safe) learn translator at all
	void verifyNoMidiLearnTranslatorWhileMappingIsOff(const md::MachineModel _model)
	{
		mdJucePlugin::AudioPluginAudioProcessor processor(_model,
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{});
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
		verifyOldRemoteDeviceStateLoadsLocal();
		std::cout << "mdProcessorHooksTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cerr << "mdProcessorHooksTest: FAIL: " << _e.what() << '\n';
		return 1;
	}
}
