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

	namespace
	{
		// The two products' pages as data.
		WebPageHost::Spec specOf(const md::MachineModel _model)
		{
			if(_model == md::MachineModel::Monomachine)
				return {"mmStudio.html", "gearmulator-mmStudio.log", "GEARMULATOR_MMSTUDIO_SELFTEST", {"1", "mmcpu", "p6"}, 1440};
			return {"mdStudio.html", "gearmulator-mdStudio.log", "GEARMULATOR_MDSTUDIO_SELFTEST", {"1", "p4", "p5", "p6"}, 1440};
		}
	}

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
		m_page = std::make_unique<WebPageHost>(specOf(processor.getModel()),
			[this](const std::string& _name)
			{
				uint32_t size = 0;
				const auto* data = findResourceByFilename(_name, size);
				return data ? std::string(data, size) : std::string();
			},
			[this](const json::Value& _m) { onPageMessage(_m); });
		m_audio = std::make_unique<AudioMidiLink>(getProcessor(), [this](json::Value _m) { m_page->send(std::move(_m)); });
		m_session = processor.getDeskSession();
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
		if(m_audio && m_audio->handle(_message))
			return;
		const auto* op = _message.find("op");
		if(op && op->isString() && op->asString() == "openMenu")
		{
			// The editor's menu (skins, scale, settings) where the page was right-clicked.
			if(auto* state = getProcessor().getEditorState())
				state->createPopupMenu().showMenuAsync(juce::PopupMenu::Options().withMousePosition());
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
