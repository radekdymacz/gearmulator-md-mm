// P4: the Machinedrum Editor's setup (md-desk/setup: app modulators, knob-row CCs)
// travels with the plug-in state as the "MDSK" chunk, and a project without it
// starts from the default setup. No firmware needed; isolated config.

#include "mdPluginProcessor.h"

#include "juce_audio_processors/juce_audio_processors.h"

#include <cstdio>
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
	juce::MemoryBlock withSetup, without;
	{
		mdJucePlugin::AudioPluginAudioProcessor a(md::MachineModel::Machinedrum, isolated(), false);
		juce::AudioProcessor& ja = a;
		ja.getStateInformation(without);
		a.setDeskSetup(setup);
		ja.getStateInformation(withSetup);
	}
	mdJucePlugin::AudioPluginAudioProcessor b(md::MachineModel::Machinedrum, isolated(), false);
	juce::AudioProcessor& jb = b;
	const auto g0 = b.getDeskSetupGeneration();
	jb.setStateInformation(withSetup.getData(), static_cast<int>(withSetup.getSize()));
	check(b.getDeskSetup() == setup, "the setup comes back with the project, byte for byte");
	check(b.getDeskSetupGeneration() != g0, "an open editor sees a new generation");
	const auto g1 = b.getDeskSetupGeneration();
	jb.setStateInformation(without.getData(), static_cast<int>(without.getSize()));
	check(b.getDeskSetup().empty(), "a project without it starts from the default setup");
	check(b.getDeskSetupGeneration() != g1, "and says so");
	std::printf("mdDeskSetupStateTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
