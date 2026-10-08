// The MD and MM standalones use the shared native-window application
// (jucePluginEditorLib/standaloneApp.h): native title bar, menu bar menus. On macOS they add
// the Record menu (mdRecordMenu.h), which only the _Standalone targets build and link: this
// file's object is pulled into the app alone (nothing in a plug-in names juce_CreateApplication).
#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#ifdef __APPLE__
#include "mdBackgroundRun.h"	// first: the JUCE plug-in headers define Component as a macro
#endif
#include "jucePluginEditorLib/standaloneApp.h"

#include "mdAbout.h"
#include "mdPluginProcessor.h"
#include "mdSettingsMigration.h"
#include "mdUpdater.h"

#if JUCE_MAC
#include "mdRecordMenu.h"
#endif

namespace mdJucePlugin
{
	namespace
	{
		bool isMonomachine(const juce::AudioProcessor& _processor)
		{
			const auto* p = dynamic_cast<const AudioPluginAudioProcessor*>(&_processor);
			return p && p->getModel() == md::MachineModel::Monomachine;
		}

		juce::String editorName(const juce::AudioProcessor& _processor)
		{
			return isMonomachine(_processor) ? MDMM_PRODUCT_NAME_MM : MDMM_PRODUCT_NAME_MD;
		}
	}

	// The window says which editor it is.
	class StandaloneApp : public jucePluginEditorLib::StandaloneApp
	{
	public:
		StandaloneApp() : jucePluginEditorLib::StandaloneApp(ownSettingsName())
		{
#ifdef __APPLE__
			backgroundRun::enterBeforeLaunch();	// the journeys on a person's Mac (mdBackgroundRun.h)
#endif
		}

		juce::String getWindowTitle(juce::AudioProcessor& _processor) const override
		{
			if(!dynamic_cast<const AudioPluginAudioProcessor*>(&_processor))
				return {};
			return editorName(_processor);
		}

		// I-005: an update staged for after exit (Windows, Linux) starts its helper as the app quits
		// (doc/modern-ux/DESIGN-updates.md); "Restart now" has started it already.
		void shutdown() override
		{
			jucePluginEditorLib::StandaloneApp::shutdown();
			m_updater->launchPendingSwap(false);
		}

		void windowOpened(jucePluginEditorLib::StandaloneWindow& _window) override
		{
			auto* p = _window.getAudioProcessor();
			// 0.3.4: About <product> (mdAbout.h): the app menu on macOS, Help elsewhere
			if(p)
			{
				const bool mm = isMonomachine(*p);
				_window.setAbout(editorName(*p), [mm]
				{
					juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon,
						juce::String::fromUTF8(about::title(mm).c_str()), juce::String::fromUTF8(about::text().c_str()));
				});
			}
#if JUCE_MAC
			// the journeys on a person's Mac: out of their way (mdBackgroundRun.h)
			backgroundRun::sendWindowBack(_window);
			if(!p)
				return;
			// the notices' owner is the processor as PageEditor names it (mdPageEditor.cpp)
			const void* owner = dynamic_cast<const AudioPluginAudioProcessor*>(p);
			m_record = std::make_unique<RecordMenu>(_window, editorName(*p), isMonomachine(*p) ? "Monomachine" : "Machinedrum", owner);
			_window.addMenu({"Record", [this] { return m_record ? m_record->menu() : juce::PopupMenu(); }});
#endif
		}

#if JUCE_MAC
		void windowClosing(jucePluginEditorLib::StandaloneWindow&) override
		{
			if(m_record)
				m_record->close();
			m_record.reset();
		}
#endif

	private:
#if JUCE_MAC
		std::unique_ptr<RecordMenu> m_record;
#endif
		// the process's Updater lives as long as the app, so a staged update outlives the window
		juce::SharedResourcePointer<updates::Updater> m_updater;
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
