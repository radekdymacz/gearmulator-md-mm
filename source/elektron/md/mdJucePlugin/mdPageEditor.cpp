#include "mdPageEditor.h"

#include "mdAudioMidiLink.h"
#include "mdDeskSession.h"
#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdWebPageHost.h"
#include "mdPageZoom.h"

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
#include "juceRmlUi/rmlMenu.h"
#include "juceUiLib/messageRoute.h"

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	// The user's page zoom in the editor's config (mdPageZoom.h): one value for every window of this editor.
	constexpr const char* g_zoomKey = "pageZoom";

	PageEditor::PageEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	PageEditor::~PageEditor()
	{
		stopTimer();
		if(m_updateToken)
			m_updater->unsubscribe(m_updateToken);
		m_noticeRoute.reset();	// this window's sink only; another instance's open window keeps its own
		m_diagnostics.reset();
		if(m_session)
		{
			m_session->setLog({});
			m_session->detach();
		}
		m_audio.reset();
		m_page.reset();
	}

	void PageEditor::create()
	{
		jucePluginEditorLib::Editor::create();
		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_noticeOwner = static_cast<const void*>(&processor);
		const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
		m_session = processor.getDeskHost()->session();
		m_page = std::make_unique<WebPageHost>(m_session ? m_session->pageSpec() : WebPageHost::Spec{},
			[this](const std::string& _name)
			{
				uint32_t size = 0;
				const auto* data = findResourceByFilename(_name, size);
				return data ? std::string(data, size) : std::string();
			},
			[this](const json::Value& _m) { onPageMessage(_m); });
		m_page->setUserZoom(getProcessor().getConfig().getDoubleValue(g_zoomKey, 1.0));
		m_audio = std::make_unique<AudioMidiLink>(getProcessor(), [this](json::Value _m) { m_page->send(std::move(_m)); });
		if(m_session)
			m_session->setLog([this](const std::string& _l) { if(m_page) m_page->log(juce::String(_l)); });
		if(m_session)
			m_session->attach([this](const json::Value& _m) { m_page->send(_m); });
		// The plug-in's questions and warnings are the page's modals, not native alerts (messageRoute.h). This window
		// takes its own instance's (and nobody's while it is the newest window).
		m_noticeRoute = genericUI::messageRoute::attach(m_noticeOwner,
			[this, alive = std::weak_ptr<int>(m_alive), noticeOwner = m_noticeOwner](genericUI::messageRoute::Notice _n)
		{
			juce::MessageManager::callAsync([this, alive, noticeOwner, n = std::move(_n)]() mutable
			{
				if(alive.expired() || !m_page)
				{
					// The window closed before it could show it: back to the route (buttons and answer kept), for
					// this instance's next window. When too many wait, the native box shows the text.
					const genericUI::messageRoute::OwnerScope owner(noticeOwner);
					const auto title = n.title, text = n.text;
					if(!genericUI::messageRoute::offer(std::move(n)))
						genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, title, text);
					return;
				}
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
		// I-005: the update banner follows the process's one Updater (its first check once the page is ready, timerCallback).
		m_updateToken = m_updater->subscribe([this] { showUpdateBanner(); });
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
		const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
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
			else if(row->handler.action == deskHost::Action::ChooseSample)
			{
				chooseSample(static_cast<uint8_t>(_message.find("slot")->asNumber()));
				m_page->send(deskCore::resultMessage(_message, {}, {}));
			}
			else if(row->handler.action == deskHost::Action::ChooseSyx || row->handler.action == deskHost::Action::SyxExport)
			{
				chooseSyx(row->handler.action == deskHost::Action::SyxExport);
				m_page->send(deskCore::resultMessage(_message, {}, {}));
			}
			else if(row->handler.action == deskHost::Action::PageZoom)
			{
				setZoom(static_cast<int>(_message.find("step")->asNumber()));
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

	void PageEditor::setZoom(const int _step, const double _zoom)
	{
		if(!m_page)
			return;
		const double z = pageZoom::clampUser(_step == 2 ? _zoom : pageZoom::step(m_page->userZoom(), _step));
		m_page->setUserZoom(z);
		auto& config = getProcessor().getConfig();
		config.setValue(g_zoomKey, z);
		config.saveIfNeeded();
		layout();
	}

	void PageEditor::fillZoomMenu(juceRmlUi::Menu& _menu)
	{
		if(!m_page)
			return;
		const double now = m_page->userZoom();
#if JUCE_MAC
		const std::string key = "Cmd";
#else
		const std::string key = "Ctrl";
#endif
		// The actions run after the menu closed; the window may have closed by then.
		const auto act = [this, alive = std::weak_ptr<int>(m_alive)](const int _step, const double _zoom)
		{
			return [this, alive, _step, _zoom]
			{
				if(!alive.expired())
					setZoom(_step, _zoom);
			};
		};
		juceRmlUi::Menu zoom;
		zoom.addEntry("Zoom In (" + key + " +)", now < pageZoom::g_steps.back() - 0.001, false, act(1, 0));
		zoom.addEntry("Zoom Out (" + key + " -)", now > pageZoom::g_steps.front() + 0.001, false, act(-1, 0));
		zoom.addEntry("Actual Size (" + key + " 0)", act(0, 0));
		zoom.addSeparator();
		for(const double s : pageZoom::g_steps)
			zoom.addEntry(std::to_string(static_cast<int>(s * 100 + 0.5)) + " %", std::abs(s - now) < 0.001, act(2, s));
		_menu.addSubMenu("Page Zoom (" + std::to_string(static_cast<int>(now * 100 + 0.5)) + " %)", std::move(zoom));
	}

	void PageEditor::chooseRom()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose the firmware image (.bin, or a .zip with it)",
			juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.bin;*.zip");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
			{
				const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
				const auto f = _c.getResult();
				if(f.existsAsFile())
					m_session->installRom(f);
			});
	}

	// P9: a sample for a UW ROM slot; the session reads the file, the page only hears the progress.
	void PageEditor::chooseSample(const uint8_t _slot)
	{
		char title[64];
		std::snprintf(title, sizeof(title), "Choose a sample for ROM-%02d (WAV or AIFF)", _slot + 1);
		m_chooser = std::make_unique<juce::FileChooser>(title, juce::File::getSpecialLocation(juce::File::userMusicDirectory),
			"*.wav;*.wave;*.aif;*.aiff;*.aifc");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this, _slot](const juce::FileChooser& _c)
			{
				const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
				const auto f = _c.getResult();
				if(f.existsAsFile() && m_session)
					m_session->loadSampleFile(_slot, f);
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
			const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
			const auto f = _c.getResult();
			if(f == juce::File() || !m_session)
				return;
			if(_save)
				m_session->exportSyx(f.withFileExtension("syx"));
			else if(f.existsAsFile())
				m_session->openSyx(f);
		});
	}

	bool PageEditor::openAudioMidiSettings()
	{
		const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
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
		const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
		layout();
		if(m_audio)
			m_audio->tick();
		if(m_diagnostics)
			m_diagnostics->tick();
		// I-005: ask the Updater every minute whether the daily check is due (it decides; DESIGN-updates.md 3.3)
		if(m_page->pageReady())
		{
			const auto now = juce::Time::currentTimeMillis();
			if(m_nextUpdatePoll == 0)
			{
				// the first check waits until the window has settled (nothing competes with its start-up)
				m_nextUpdatePoll = now + 20 * 1000;
				showUpdateBanner();	// a window opened while an update was already known
			}
			else if(now >= m_nextUpdatePoll)
			{
				m_nextUpdatePoll = now + 60 * 1000;
				m_updater->poll(getProcessor().getConfig());
			}
		}
		m_page->flush();
	}

	void PageEditor::showUpdateBanner()
	{
		if(!m_page)
			return;
		const auto banner = m_updater->banner();
		std::string shown = banner.title + "\n" + banner.text;
		for(const auto& button : banner.buttons)
			shown += "\n" + button.first;
		if(shown == m_bannerShown || (banner.title.empty() && m_bannerId == 0))
			return;
		m_bannerShown = shown;
		if(m_bannerId)
			m_notices.erase(m_bannerId);	// a newer banner replaces it on the page: its answer is no longer wanted
		// A notice with "modal": false is the page's banner, not its dialog (FOUNDATION.md, the notice route); one
		// with no title and no text takes the banner away.
		const int id = ++m_noticeId;
		json::Value m = json::Value::object();
		m.set("type", "notice");
		m.set("id", id);
		m.set("title", banner.title);
		m.set("text", banner.text);
		auto buttons = json::Value::array();
		std::vector<updates::Action> actions;
		for(const auto& button : banner.buttons)
		{
			buttons.push(json::Value(button.first));
			actions.push_back(button.second);
		}
		m.set("buttons", std::move(buttons));
		m.set("modal", false);
		m_bannerId = banner.title.empty() ? 0 : id;
		if(m_bannerId)
		{
			m_notices[id] = [this, alive = std::weak_ptr<int>(m_alive), actions](const int _button)
			{
				if(alive.expired() || _button < 0 || static_cast<size_t>(_button) >= actions.size())
					return;
				m_bannerId = 0;		// the page closed it
				m_bannerShown.clear();
				m_updater->act(actions[static_cast<size_t>(_button)], getProcessor().getConfig());
			};
		}
		m_page->send(std::move(m));
	}

	void PageEditor::fillUpdateMenu(juceRmlUi::Menu& _menu)
	{
		// The actions run after the menu closed; the window may have closed by then.
		const auto alive = std::weak_ptr<int>(m_alive);
		juceRmlUi::Menu menu;
		menu.addEntry("Check for Updates Now", [this, alive]
		{
			if(!alive.expired())
				m_updater->checkNow(getProcessor().getConfig());
		});
		menu.addEntry("Check Daily", updates::Updater::enabled(getProcessor().getConfig()), [this, alive]
		{
			if(alive.expired())
				return;
			auto& config = getProcessor().getConfig();
			updates::Updater::setEnabled(config, !updates::Updater::enabled(config));
		});
		menu.addSeparator();
		menu.addEntry("This version: " + mdmmUpdate::toString(updates::Updater::currentVersion()), false, false, {});
		_menu.addSubMenu("Updates", std::move(menu));
	}

	void PageEditor::layout() const
	{
		if(auto* root = getRmlComponent(); root && m_page)
		{
			m_page->layout(root->getLocalBounds());
		}
	}
}
