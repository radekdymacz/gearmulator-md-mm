#include "mdPageEditor.h"

#include "mdAudioMidiLink.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"
#include "mdWebPageHost.h"

#if MDMM_DIAGNOSTICS
#include "mdDiagnostics.h"
#endif

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	PageEditor::PageEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	PageEditor::~PageEditor()
	{
		stopTimer();
		m_diagnostics.reset();
		if(m_session)
			m_session->detach();
		m_audio.reset();
		m_page.reset();
	}

	void PageEditor::create()
	{
		jucePluginEditorLib::Editor::create();
		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_session = processor.getDeskSession();
		m_page = std::make_unique<WebPageHost>(m_session ? m_session->pageSpec() : WebPageHost::Spec{},
			[this](const std::string& _name)
			{
				uint32_t size = 0;
				const auto* data = findResourceByFilename(_name, size);
				return data ? std::string(data, size) : std::string();
			},
			[this](const json::Value& _m) { onPageMessage(_m); });
		m_audio = std::make_unique<AudioMidiLink>(getProcessor(), [this](json::Value _m) { m_page->send(std::move(_m)); });
		if(m_session)
			m_session->attach([this](const json::Value& _m) { m_page->send(_m); });
		getRmlComponent()->addAndMakeVisible(m_page->component());
		layout();
		m_page->load();
#if MDMM_DIAGNOSTICS
		if(m_session)
			m_diagnostics = std::make_unique<Diagnostics>(*m_page, *m_session, *getRmlComponent(), getProcessor());
#endif
		startTimerHz(30);
	}

	void PageEditor::onPageMessage(const json::Value& _message)
	{
		// deskHost's table says who acts: the window for its menu and the AUDIO / MIDI panel, the
		// session for the rest.
		if(const auto* row = deskHost::commands().find(deskCore::opOf(_message)); row && deskHost::isWindowAction(row->handler))
		{
			if(const auto errors = deskHost::Table::check(*row, _message); !errors.empty())
				m_page->send(deskCore::resultMessage(_message, errors, {}));
			else if(row->handler == deskHost::Action::Menu)
			{
				// The editor's menu (skins, scale, settings) where the page was right-clicked.
				if(auto* state = getProcessor().getEditorState())
					state->createPopupMenu().showMenuAsync(juce::PopupMenu::Options().withMousePosition());
			}
			else if(!m_audio || !m_audio->handle(row->handler, _message))
				m_page->send(deskCore::resultMessage(_message, {"The audio devices are the standalone app's."}, {}));
			return;
		}
		if(m_session)
			m_session->onPageMessage(_message);
	}

	bool PageEditor::openAudioMidiSettings()
	{
		if(!m_audio || !m_audio->standalone() || !m_page || !m_page->pageReady())
			return false;
		json::Value m = json::Value::object();
		m.set("type", "openAudio");
		m_page->send(std::move(m));
		m_audio->publish();
		m_page->flush();
		return true;
	}

	void PageEditor::timerCallback()
	{
		layout();
		if(m_audio)
			m_audio->tick();
		if(m_diagnostics)
			m_diagnostics->tick();
		m_page->flush();
	}

	void PageEditor::layout() const
	{
		if(auto* root = getRmlComponent(); root && m_page)
			m_page->layout(root->getLocalBounds());
	}
}
