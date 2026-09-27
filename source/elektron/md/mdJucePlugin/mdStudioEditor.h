#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/json.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <memory>
#include <string>
#include <vector>

#include "mdDesk/mdDesk.h"

namespace mdJucePlugin
{
	class StudioLink;
	class StudioWebView;

	// The "mdStudio" skin: the Machinedrum Editor page (skins/mdStudio) in a
	// JUCE WebBrowserComponent under a small RML header. The page speaks only
	// contract JSON and small commands; this class carries them between the
	// page and an mdDesk::Desk, and connects the Desk to the machine through
	// StudioLink. The panel editor is untouched.
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
		void onBridge(const std::string& _url);
		void onPageMessage(const elektronData::json::Value& _message);
		bool handleEditorMessage(const elektronData::json::Value& _message);
		void publishLearn();
		void flushPage();
		std::string bundlePage() const;
		std::string resourceText(const std::string& _name) const;
		void layoutWebView() const;

		std::unique_ptr<StudioLink> m_link;
		std::unique_ptr<mdDesk::Desk> m_desk;
		std::unique_ptr<StudioWebView> m_web;
		std::vector<elektronData::json::Value> m_outbox;
		bool m_pageReady = false;
		uint32_t m_ticks = 0;
		double m_lastCommandMs = 0;
		int m_learnTrack = -1;
		int m_learnIndex = -1;
		mdDesk::Desk::Firmware m_lastFirmware = mdDesk::Desk::Firmware::Missing;
	};
}
