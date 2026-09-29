#include "mdPageEditor.h"

#include "mdAudioMidiLink.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdWebPageHost.h"

#if MDMM_DIAGNOSTICS
#include "mdDiagnostics.h"
#else
namespace mdJucePlugin
{
	// A release build has no diagnostics: the editor's slot for them stays empty.
	class Diagnostics
	{
	public:
		void tick() {}
	};
}
#endif

#include "jucePluginEditorLib/editorPopupMenu.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"

namespace mdJucePlugin
{
	bool passFileDropsToEditor(juce::Component& _web);	// mdStudioWebZoom.mm
	namespace json = elektronData::json;

	// The web view's parent: it takes the files dropped on the window (the web view does not).
	class DropZone final : public juce::Component, public juce::FileDragAndDropTarget
	{
	public:
		std::function<void(const juce::File&)> onFile;
		bool isInterestedInFileDrag(const juce::StringArray& _files) override
		{
			for(const auto& f : _files)
				if(PageEditor::wantsFile(juce::File(f)))
					return true;
			return false;
		}
		void filesDropped(const juce::StringArray& _files, int, int) override
		{
			for(const auto& f : _files)
				if(juce::File(f).existsAsFile() && PageEditor::wantsFile(juce::File(f)))
				{
					if(onFile)
						onFile(juce::File(f));
					return;
				}
		}
	};

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
		m_dropZone.reset();
	}

	void PageEditor::create()
	{
		jucePluginEditorLib::Editor::create();
		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_session = processor.getDeskHost()->session();
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
		m_dropZone = std::make_unique<DropZone>();
		m_dropZone->onFile = [this](const juce::File& _f) { openFile(_f); };
		m_dropZone->addAndMakeVisible(m_page->component());
		getRmlComponent()->addAndMakeVisible(*m_dropZone);
		layout();
		m_page->load();
#if JUCE_MAC
		passFileDropsToEditor(m_page->component());
#endif
#if MDMM_DIAGNOSTICS
		if(m_session)
			m_diagnostics = std::make_unique<Diagnostics>(*m_page, *m_session, *getRmlComponent(), getProcessor());
#endif
		startTimerHz(30);
	}

	void PageEditor::onPageMessage(const json::Value& _message)
	{
		// deskHost's table says who acts (its actor column): the window for its menu and the AUDIO /
		// MIDI panel, the session for the rest.
		if(const auto* row = deskHost::commands().find(deskCore::opOf(_message)); row && row->handler.actor == deskHost::Actor::Window)
		{
			if(const auto errors = deskHost::Table::check(*row, _message); !errors.empty())
				m_page->send(deskCore::resultMessage(_message, errors, {}));
			else if(row->handler.action == deskHost::Action::ChooseRom)
			{
				chooseRom();
				m_page->send(deskCore::resultMessage(_message, {}, {}));
			}
			else if(row->handler.action == deskHost::Action::ChooseSyx || row->handler.action == deskHost::Action::SyxExport)
			{
				chooseSyx(row->handler.action == deskHost::Action::SyxExport);
				m_page->send(deskCore::resultMessage(_message, {}, {}));
			}
			else if(row->handler.action == deskHost::Action::Menu)
			{
				// The editor's menu (skins, scale, settings) where the page was right-clicked.
				if(auto* state = getProcessor().getEditorState())
					jucePluginEditorLib::createPopupMenu(*state).showMenuAsync(juce::PopupMenu::Options().withMousePosition());
			}
			else if(!m_audio || !m_audio->handle(row->handler.action, _message))
				m_page->send(deskCore::resultMessage(_message, {"The audio devices are the standalone app's."}, {}));
			return;
		}
		if(m_session)
			m_session->onPageMessage(_message);
	}

	void PageEditor::chooseRom()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose the firmware image (.bin, or a .zip with it)",
			juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.bin;*.zip");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
			{
				const auto f = _c.getResult();
				if(f.existsAsFile())
					openFile(f);
			});
	}

	void PageEditor::chooseSyx(const bool _save)
	{
		const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		m_chooser = std::make_unique<juce::FileChooser>(_save ? "Export SysEx" : "Import SysEx (.syx)",
			_save ? dir.getChildFile(juce::String(getProcessor().getProperties().name) + " backup.syx") : dir, "*.syx");
		const auto flags = _save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
			: juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
		m_chooser->launchAsync(flags, [this, _save](const juce::FileChooser& _c)
		{
			const auto f = _c.getResult();
			if(f == juce::File() || !m_session)
				return;
			if(_save)
				m_session->exportSyx(f.withFileExtension("syx"));
			else if(f.existsAsFile())
				m_session->openSyx(f);
		});
	}

	void PageEditor::openFile(const juce::File& _file)
	{
		if(!m_session)
			return;
		if(_file.hasFileExtension("syx"))
			m_session->openSyx(_file);
		else if(_file.hasFileExtension("bin") || _file.hasFileExtension("zip"))
			m_session->installRom(_file);
	}

	bool PageEditor::wantsFile(const juce::File& _file)
	{
		return _file.hasFileExtension("bin") || _file.hasFileExtension("zip") || _file.hasFileExtension("syx");
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
		{
			if(m_dropZone && m_dropZone->getBounds() != root->getLocalBounds())
				m_dropZone->setBounds(root->getLocalBounds());
			m_page->layout(m_dropZone ? m_dropZone->getLocalBounds() : root->getLocalBounds());
		}
	}
}
