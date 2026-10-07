// The MD and MM standalones use the shared native-window application
// (jucePluginEditorLib/standaloneApp.h): native title bar, menu bar menus.
#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include "jucePluginEditorLib/standaloneApp.h"

#include "mdPluginProcessor.h"
#include "mdSettingsMigration.h"

namespace mdJucePlugin
{
	// The window says which editor it is.
	class StandaloneApp : public jucePluginEditorLib::StandaloneApp
	{
	public:
		StandaloneApp() : jucePluginEditorLib::StandaloneApp(ownSettingsName())
		{
		}

		juce::String getWindowTitle(juce::AudioProcessor& _processor) const override
		{
			const auto* p = dynamic_cast<const AudioPluginAudioProcessor*>(&_processor);
			if(!p)
				return {};
			return p->getModel() == md::MachineModel::Monomachine ? MDMM_PRODUCT_NAME_MM : MDMM_PRODUCT_NAME_MD;
		}

	private:
		// The app's own settings file, not upstream's "Gearmulator MD.settings" (mdSettingsMigration.h), copied
		// from that one the first time. Before the base class is made: it opens the file.
		static juce::String ownSettingsName()
		{
			const auto model = AudioPluginAudioProcessor::getCompiledProductModel();
			const juce::String own(editorStandaloneSettingsName(model));
			copySettingsOnce(settingsOptions(legacyStandaloneSettingsName(model)).getDefaultFile(),
				settingsOptions(own).getDefaultFile());
			return own;
		}
	};
}

JUCE_CREATE_APPLICATION_DEFINE(mdJucePlugin::StandaloneApp)
#endif
