#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/json.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "mmDesk/mmDesk.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	class MmStudioLink;
	class MmStudioWebView;

	// The "mmStudio" skin: the Monomachine Editor page (skins/mmStudio) in a JUCE
	// WebBrowserComponent under a small RML header. The page speaks contract JSON
	// and small commands; this class carries them between the page and an
	// mmDesk::Desk and connects the desk to the machine through MmStudioLink.
	// While the firmware boots, the page shows the machine's own LCD.
	class MmStudioEditor final : public jucePluginEditorLib::Editor, juce::Timer
	{
	public:
		MmStudioEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~MmStudioEditor() override;

		MmStudioEditor(const MmStudioEditor&) = delete;
		MmStudioEditor& operator=(const MmStudioEditor&) = delete;

		void create() override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override { return {}; }

	private:
		void timerCallback() override;
		void onBridge(const std::string& _url);
		void onPageMessage(const elektronData::json::Value& _message);
		bool handleEditorMessage(const elektronData::json::Value& _message);
		void publishLearn();
		void publishLcd(bool _force);
		void flushPage();
		std::string bundlePage() const;
		std::string resourceText(const std::string& _name) const;
		void layoutWebView() const;

		std::unique_ptr<MmStudioLink> m_link;
		std::unique_ptr<mmDesk::Desk> m_desk;
		std::unique_ptr<MmStudioWebView> m_web;
		std::vector<elektronData::json::Value> m_outbox;
		bool m_pageReady = false;
		uint32_t m_ticks = 0;
		std::array<uint8_t, 1024> m_lastLcd{};
		bool m_lcdSent = false;
		int m_learnTrack = -1;
		int m_learnPage = -1;
		int m_learnIndex = -1;
		mmDesk::Desk::Engine m_lastEngine = mmDesk::Desk::Engine::Missing;
	};
}
