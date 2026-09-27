#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/mdPattern.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <memory>
#include <optional>
#include <string>

namespace mdJucePlugin
{
	class StudioLink;
	class StudioWebView;

	// Second MD editor ("mdStudio" skin): a minimal RML frame hosting an HTML page
	// in a JUCE WebBrowserComponent. P0 plumbing proof only - it shows the
	// current pattern's 16 trig rows and flips a trig on click through the
	// firmware's SysEx pattern dump. The panel editor is untouched.
	class StudioEditor final : public jucePluginEditorLib::Editor, juce::Timer
	{
	public:
		StudioEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~StudioEditor() override;

		StudioEditor(const StudioEditor&) = delete;
		StudioEditor& operator=(const StudioEditor&) = delete;

		void create() override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override { return {}; }

	private:
		void timerCallback() override;
		void onPageCommand(const std::string& _command);
		void onPattern(const elektronData::MdPattern& _pattern);
		void callPage(const std::string& _script) const;
		void layoutWebView() const;

		std::unique_ptr<StudioLink> m_link;
		std::unique_ptr<StudioWebView> m_web;
		std::optional<elektronData::MdPattern> m_pattern;
		double m_editStartedMs = 0;
		int m_lastPlayhead = -1;
		bool m_pageReady = false;
		uint32_t m_retryTicks = 0;
	};
}
