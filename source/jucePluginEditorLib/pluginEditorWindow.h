#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "baseLib/event.h"
#include "editorWindowScaleRestore.h"

namespace jucePluginEditorLib
{
	class PluginEditorState;

	//==============================================================================
	class EditorWindow : public juce::AudioProcessorEditor, juce::Timer
	{
	public:
	    explicit EditorWindow (juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config);
	    ~EditorWindow() override;

		void paint(juce::Graphics& g) override {}

		void resized() override;

		int getControlParameterIndex(Component&) override;

		// A combined product may host this editor inside a larger editor. In that
		// case the parent owns sizing and the normal standalone parent-size repair
		// must not expand the entire composite to this panel's saved scale.
		void setEmbedded(bool _embedded);

	private:
		void setGuiScale(float _percent);
		void setUiRoot(juce::Component* _component);
		// The configured size: the GUI scale, or a free editor's own width and height.
		void restoreSize(float _percent);
		bool fluid() const;

		void timerCallback() override;
		void fixParentWindowSize() const;
		// The standalone's window no larger than its screen's visible area (menu bar and Dock
		// excluded), and inside it (P7).
		void fitToScreen();
		bool m_fitting = false;

		PluginEditorState& m_state;
		juce::PropertiesFile& m_config;
		baseLib::EventListener<juce::Component*> m_skinLoadedListener;
		baseLib::EventListener<int> m_guiScaleListener;

	    juce::ComponentBoundsConstrainer m_sizeConstrainer;
		EditorWindowScaleRestore m_scaleRestore;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
	};
}
