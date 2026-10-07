// The MD and MM standalones use the shared native-window application
// (jucePluginEditorLib/standaloneApp.h): native title bar, menu bar menus. On macOS they add
// the Record menu (mdRecordMenu.h), which only the _Standalone targets build and link: this
// file's object is pulled into the app alone (nothing in a plug-in names juce_CreateApplication).
#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP
#include "jucePluginEditorLib/standaloneApp.h"

#include "mdPluginProcessor.h"

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
			return isMonomachine(_processor) ? "Monomachine Editor" : "Machinedrum Editor";
		}
	}

	// The window says which editor it is.
	class StandaloneApp : public jucePluginEditorLib::StandaloneApp
	{
	public:
		juce::String getWindowTitle(juce::AudioProcessor& _processor) const override
		{
			if(!dynamic_cast<const AudioPluginAudioProcessor*>(&_processor))
				return {};
			return editorName(_processor);
		}

#if JUCE_MAC
		void windowOpened(jucePluginEditorLib::StandaloneWindow& _window) override
		{
			auto* p = _window.getAudioProcessor();
			if(!p)
				return;
			// the notices' owner is the processor as PageEditor names it (mdPageEditor.cpp)
			const void* owner = dynamic_cast<const AudioPluginAudioProcessor*>(p);
			m_record = std::make_unique<RecordMenu>(_window, editorName(*p), isMonomachine(*p) ? "Monomachine" : "Machinedrum", owner);
			_window.addMenu({"Record", [this] { return m_record ? m_record->menu() : juce::PopupMenu(); }});
		}

		void windowClosing(jucePluginEditorLib::StandaloneWindow&) override
		{
			if(m_record)
				m_record->close();
			m_record.reset();
		}

	private:
		std::unique_ptr<RecordMenu> m_record;
#endif
	};
}

JUCE_CREATE_APPLICATION_DEFINE(mdJucePlugin::StandaloneApp)
#endif
