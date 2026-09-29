// The MD and MM standalones use the shared native-window application
// (jucePluginEditorLib/standaloneApp.h): native title bar, menu bar menus.
#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include "jucePluginEditorLib/standaloneApp.h"

#include "mdPluginProcessor.h"

namespace mdJucePlugin
{
	// The window says which editor it is.
	class StandaloneApp : public jucePluginEditorLib::StandaloneApp
	{
	public:
		juce::String getWindowTitle(juce::AudioProcessor& _processor) const override
		{
			const auto* p = dynamic_cast<const AudioPluginAudioProcessor*>(&_processor);
			if(!p)
				return {};
			return p->getModel() == md::MachineModel::Monomachine ? "Monomachine Editor" : "Machinedrum Editor";
		}
	};
}

JUCE_CREATE_APPLICATION_DEFINE(mdJucePlugin::StandaloneApp)
#endif
