#include "mdDiagnostics.h"

#include "mdDeskSession.h"
#include "mdWebPageHost.h"

#include "juce_audio_processors/juce_audio_processors.h"

namespace mdJucePlugin
{
	Diagnostics::Diagnostics(WebPageHost& _page, const DeskSession& _session, juce::Component& _root, juce::AudioProcessor& _processor)
		: m_page(_page), m_session(_session), m_root(_root), m_processor(_processor)
	{
	}

	void Diagnostics::tick()
	{
		const auto t = ++m_ticks;
		if(t == 60 || t == 300)
		{
			auto& web = m_page.component();
			m_page.log("web view: " + web.getBounds().toString() + (web.isShowing() ? " showing" : " NOT showing") + ", root "
				+ m_root.getBounds().toString() + (m_root.isShowing() ? " showing" : " NOT showing") + ", window "
				+ (web.getTopLevelComponent() ? web.getTopLevelComponent()->getBounds().toString() : juce::String("none")));
		}
		if(t == 90)
		{
			// The window chrome as the operating system sees it (standaloneApp.h, P4).
			juce::String chrome = "window: ";
			if(auto* w = dynamic_cast<juce::DocumentWindow*>(m_root.getTopLevelComponent()))
				chrome << "\"" << w->getName() << "\" native title bar " << (w->isUsingNativeTitleBar() ? 1 : 0);
			else
				chrome << "hosted (no document window)";
#if JUCE_MAC
			if(auto* model = juce::MenuBarModel::getMacMainMenu())
				chrome << ", menu bar: " << model->getMenuBarNames().joinIntoString(", ") << " (Editor menu "
					<< model->getMenuForIndex(0, "Editor").getNumItems() << " items)";
#endif
			m_page.log(chrome);
			m_page.log("audio: " + juce::String(m_processor.getSampleRate(), 0) + " Hz, block " + juce::String(m_processor.getBlockSize()) + " frames");
		}
		if(t % 150 == 0)
			m_page.log(juce::String(m_session.status()));
	}
}
