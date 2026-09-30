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
#include "juceUiLib/messageRoute.h"

namespace mdJucePlugin
{
	juce::String describeDragTargets(juce::Component& _web);	// mdStudioWebZoom.mm
	int passFileDropsToEditor(juce::Component& _web);	// mdStudioWebZoom.mm: how many views still took drags
	namespace json = elektronData::json;

	// The web view's parent: it takes the files dropped on the window (the web view does not).
	class DropZone final : public juce::Component, public juce::FileDragAndDropTarget
	{
	public:
		std::function<void(const juce::File&)> onFile;
		std::function<void(const juce::String&)> onLog;
		bool isInterestedInFileDrag(const juce::StringArray& _files) override
		{
			for(const auto& f : _files)
				if(PageEditor::wantsFile(juce::File(f)))
				{
					if(!m_announced && onLog)
						onLog("drop: a file is over the window (" + juce::File(f).getFileName() + "), taken");
					m_announced = true;
					return true;
				}
			if(!m_announced && onLog)
				onLog("drop: a file is over the window but it is not a .bin, .zip or .syx (" + _files.joinIntoString(", ") + ")");
			m_announced = true;
			return false;
		}
		void fileDragExit(const juce::StringArray&) override { m_announced = false; }
		void filesDropped(const juce::StringArray& _files, int, int) override
		{
			m_announced = false;
			if(onLog)
				onLog("drop: dropped " + _files.joinIntoString(", "));
			for(const auto& f : _files)
				if(juce::File(f).existsAsFile() && PageEditor::wantsFile(juce::File(f)))
				{
					if(onFile)
						onFile(juce::File(f));
					return;
				}
		}

	private:
		bool m_announced = false;
	};

	PageEditor::PageEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	PageEditor::~PageEditor()
	{
		stopTimer();
		genericUI::messageRoute::setSink({});
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
		// The plug-in's questions and warnings are the page's modals, not native alerts (messageRoute.h).
		genericUI::messageRoute::setSink([this, alive = std::weak_ptr<int>(m_alive)](genericUI::messageRoute::Notice _n)
		{
			juce::MessageManager::callAsync([this, alive, n = std::move(_n)]() mutable
			{
				if(alive.expired() || !m_page)
					return;
				const int id = ++m_noticeId;
				json::Value m = json::Value::object();
				m.set("type", "notice");
				m.set("id", id);
				m.set("title", n.title);
				m.set("text", n.text);
				auto buttons = json::Value::array();
				for(const auto& b : n.buttons)
					buttons.push(json::Value(b));
				m.set("buttons", std::move(buttons));
				m_notices[id] = std::move(n.answered);
				m_page->log("notice " + juce::String(id) + ": " + juce::String(n.title) + " - " + juce::String(n.text).substring(0, 200));
				m_page->send(std::move(m));
			});
		});
		m_dropZone = std::make_unique<DropZone>();
		m_dropZone->onFile = [this](const juce::File& _f) { openFile(_f); };
		m_dropZone->onLog = [this](const juce::String& _l) { m_page->log(_l); };
		// The web view's own way in: WKWebView navigates to a dropped file (mdWebPageHost.cpp).
		m_page->onFileNavigation([this](const juce::File& _f)
		{
			if(wantsFile(_f))
				juce::MessageManager::callAsync([this, _f, alive = std::weak_ptr<int>(m_alive)]
				{
					if(!alive.expired())
						openFile(_f);
				});
		});
		m_dropZone->addAndMakeVisible(m_page->component());
		getRmlComponent()->addAndMakeVisible(*m_dropZone);
		layout();
		m_page->load();
#if JUCE_MAC
		m_page->log("drop: the web view takes drags in " + juce::String(passFileDropsToEditor(m_page->component())) + " views before the page loads");
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
			else if(row->handler.action == deskHost::Action::NoticeAnswer)
			{
				const auto id = static_cast<int>(_message.find("id")->asNumber());
				const auto button = static_cast<int>(_message.find("button")->asNumber());
				if(const auto it = m_notices.find(id); it != m_notices.end())
				{
					auto answered = std::move(it->second);
					m_notices.erase(it);
					if(answered)
						answered(button);
				}
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
#if JUCE_MAC
		// WKWebView registers for drags again when its page loads or its process restarts: keep the
		// window's drops with the editor (checked twice a second; a finding is logged).
		if(m_page && (m_dropCheck == 89 || m_dropCheck == 900))
			m_page->log("drop: views taking file drags after " + juce::String(m_dropCheck / 30) + " s: " + describeDragTargets(m_page->component()));
		if(m_page && ++m_dropCheck % 15 == 0)
			if(const auto n = passFileDropsToEditor(m_page->component()); n > 0)
				m_page->log("drop: the web view had taken drags again in " + juce::String(n) + " views, given back to the window");
#endif
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
