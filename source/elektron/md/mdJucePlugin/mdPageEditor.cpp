#include "mdPageEditor.h"

#include "mdAudioMidiLink.h"
#include "mdDeskSession.h"
#include "mdPluginEditorState.h"
#include "mdPluginProcessor.h"
#include "mdDeskHost.h"
#include "mdWebPageHost.h"
#include "mdPageZoom.h"

#include <cmath>

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
				const int id = m_notices.add(n.buttons.size(), std::move(n.answered));
				json::Value m = json::Value::object();
				m.set("type", "notice");
				m.set("id", id);
				m.set("title", n.title);
				m.set("text", n.text);
				auto buttons = json::Value::array();
				for(const auto& b : n.buttons)
					buttons.push(json::Value(b));
				m.set("buttons", std::move(buttons));
				m_page->log("notice " + juce::String(id) + ": " + juce::String(n.title) + " - " + juce::String(n.text).substring(0, 200));
				m_page->send(std::move(m));
			});
		});
		// I-005: the update banner follows the process's one Updater (its first check once the page is ready, timerCallback).
		m_updateToken = m_updater->subscribe([this] { showUpdateBanner(); });
		getRmlComponent()->addAndMakeVisible(m_page->component());
		layout();
		// B-022: the start-up log a user can send (the editor's menu: Open Log Folder)
		const auto startupLog = processor.performanceDiagnosticsFolder().getChildFile(
			"editor-" + juce::File::createLegalFileName(juce::String(m_session ? m_session->pageSpec().page : "page")).upToLastOccurrenceOf(".", false, false) + ".log");
		m_page->setStartupLog(startupLog);
		m_page->setFallbackMenu([this] { openMenu(); });	// I-008: no page up, the native menu
		// Files dragged onto the window (macOS): taken when the page is up and one of them is a kind it knows; the page
		// shows where they go while they are over it, and decides what each becomes once dropped
		m_page->setFileDrop({[this](const std::vector<std::string>& _paths)
			{
				return m_page->pageReady() && !m_page->failed() && droppedFiles::accepts(_paths);
			},
			[this](const bool _over)
			{
				m_page->send(droppedFiles::dragMessage(_over));
				m_page->flush();
			},
			[this](const std::vector<std::string>& _paths, const double _x, const double _y)
			{
				const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
				filesDropped(_paths, _x, _y);
			}});
		// Linux: a page that started again has nothing (WebPageHost::onAck): the session's documents once more, as on
		// its ready, and the update banner (the old one's answer is no longer wanted)
		m_page->setOnRestart([this]
		{
			const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
			if(m_bannerId)
				m_notices.forget(m_bannerId);
			m_bannerId = 0;
			m_bannerShown.clear();
			if(m_session)
				m_session->republish();
			showUpdateBanner();
		});
		// B-035: the processor's start-up lines (the host's audio calls, the machine's boot) go into the same log
		processor.bootDiagnostics().setLog(startupLog);
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
				// "notice" names the notice; "id" is this request's (its result's)
				const auto notice = static_cast<int>(_message.find("notice")->asNumber());
				const auto button = static_cast<int>(_message.find("button")->asNumber());
				const auto refused = m_notices.answer(notice, button);
				if(!refused.empty())
					m_page->log("noticeAnswer: " + juce::String(refused));
				m_page->send(deskCore::resultMessage(_message, refused.empty() ? std::vector<std::string>{} : std::vector<std::string>{refused}, {}));
			}
			else if(row->handler.action == deskHost::Action::ChooseSample)
			{
				chooseSample(static_cast<uint8_t>(_message.find("slot")->asNumber()));
				m_page->send(deskCore::resultMessage(_message, {}, {}));
			}
			else if(row->handler.action == deskHost::Action::DropRom || row->handler.action == deskHost::Action::DropSyx
				|| row->handler.action == deskHost::Action::DropSample)
			{
				const auto kind = row->handler.action == deskHost::Action::DropRom ? droppedFiles::Kind::Rom
					: row->handler.action == deskHost::Action::DropSyx ? droppedFiles::Kind::Sysex : droppedFiles::Kind::Sample;
				const auto why = useDrop(kind, _message);
				m_page->send(deskCore::resultMessage(_message, why.empty() ? std::vector<std::string>{} : std::vector<std::string>{why}, {}));
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
				openMenu();	// I-008: the page draws it where it was right-clicked
			}
			else if(row->handler.action == deskHost::Action::MenuPick)
			{
				m_page->send(deskCore::resultMessage(_message, {}, {}));
				pickMenu(static_cast<int>(_message.find("menu")->asNumber()), static_cast<size_t>(_message.find("n")->asNumber()));
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

	editorMenu::Item PageEditor::zoomMenu(editorMenu::Item _windowSize)
	{
		using namespace editorMenu;
		const double now = m_page ? m_page->userZoom() : 1.0;
#if JUCE_MAC
		const std::string cmd = "\xe2\x8c\x98";	// ⌘
#else
		const std::string cmd = "Ctrl ";
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
		const auto percent = [](const double _z) { return std::to_string(static_cast<int>(_z * 100 + 0.5)); };
		std::vector<Item> zoom;
		zoom.push_back(action("zoom-in", "Zoom In", act(1, 0), cmd + "+", false, now < pageZoom::g_steps.back() - 0.001));
		zoom.push_back(action("zoom-out", "Zoom Out", act(-1, 0), cmd + "\xe2\x88\x92", false, now > pageZoom::g_steps.front() + 0.001));
		zoom.push_back(action("zoom-actual", "Actual Size", act(0, 0), cmd + "0"));
		zoom.push_back(separator());
		for(const double s : pageZoom::g_steps)
			zoom.push_back(action("zoom-" + percent(s), percent(s) + " %", act(2, s), {}, std::abs(s - now) < 0.001));
		zoom.push_back(separator());
		// The window's own size (the page fits itself into it): its own submenu, so the two zooms are not mixed up
		zoom.push_back(std::move(_windowSize));
		auto item = submenu("zoom", "Zoom", std::move(zoom));
		item.key = percent(now) + " %";
		return item;
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
#if MDMM_DIAGNOSTICS
		// B-019's journeys: the file is the run's (GEARMULATOR_MDMM_SYX_FILE), no chooser in front of anything
		if(const auto given = juce::File(juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_SYX_FILE", {}));
			!_save && given.existsAsFile() && m_session)
		{
			m_session->openSyx(given);
			return;
		}
#endif
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

	// Files dropped on the window: kept here by their number (the page never sees a path), and the page told what each is
	// and where they were dropped (its CSS pixels): it decides what each becomes (deskDrop.js).
	void PageEditor::filesDropped(const std::vector<std::string>& _paths, const double _x, const double _y)
	{
		if(!m_page)
			return;
		const int drop = m_drops.add(_paths, juce::Time::getMillisecondCounterHiRes());
		const auto items = droppedFiles::classify(_paths);
		m_page->log("drop " + juce::String(drop) + ": " + juce::String(static_cast<int>(items.size())) + " files at " + juce::String(_x, 1) + ", " + juce::String(_y, 1));
		m_page->send(droppedFiles::dropMessage(drop, items, _x, _y));
		m_page->flush();
	}

	// The file the page chose of a drop, as its chooser would give it: a ROM to install, a .syx to preview, a sample for a
	// UW ROM slot. Each file serves once, as the kind it was dropped as.
	std::string PageEditor::useDrop(const droppedFiles::Kind _kind, const json::Value& _message)
	{
		const auto drop = static_cast<int>(_message.find("drop")->asNumber());
		const auto n = static_cast<size_t>(_message.find("n")->asNumber());
		const auto path = m_drops.take(drop, n, _kind, juce::Time::getMillisecondCounterHiRes());
		if(!path)
			return "The window no longer holds that file: drop it again.";
		const juce::File file(juce::String::fromUTF8(path->c_str()));
		if(!file.existsAsFile())
			return file.getFileName().toStdString() + " is no longer there.";
		if(!m_session)
			return "The editor has no machine to give it to.";
		m_page->log("drop " + juce::String(drop) + ": file " + juce::String(static_cast<int>(n)) + " (" + file.getFileName() + ") used as " + droppedFiles::kindName(_kind));
		switch(_kind)
		{
		case droppedFiles::Kind::Rom:		m_session->installRom(file); break;
		case droppedFiles::Kind::Sysex:		m_session->openSyx(file); break;
		case droppedFiles::Kind::Sample:	m_session->loadSampleFile(static_cast<uint8_t>(_message.find("slot")->asNumber()), file); break;
		case droppedFiles::Kind::Unknown:	break;
		}
		return {};
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
		m_page->checkStarted();	// B-022
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
			m_notices.forget(m_bannerId);	// a newer banner replaces it on the page: its answer is no longer wanted
		// A notice with "modal": false is the page's banner, not its dialog (FOUNDATION.md, the notice route); one
		// with no title and no text takes the banner away (number 0: it waits for no answer).
		auto buttons = json::Value::array();
		std::vector<updates::Action> actions;
		for(const auto& button : banner.buttons)
		{
			buttons.push(json::Value(button.first));
			actions.push_back(button.second);
		}
		m_bannerId = banner.title.empty() ? 0 : m_notices.add(actions.size(), [this, alive = std::weak_ptr<int>(m_alive), actions](const int _button)
		{
			if(alive.expired() || _button < 0 || static_cast<size_t>(_button) >= actions.size())
				return;
			m_bannerId = 0;		// the page closed it
			m_bannerShown.clear();
			m_updater->act(actions[static_cast<size_t>(_button)], getProcessor().getConfig());
		});
		json::Value m = json::Value::object();
		m.set("type", "notice");
		m.set("id", m_bannerId);
		m.set("title", banner.title);
		m.set("text", banner.text);
		m.set("buttons", std::move(buttons));
		m.set("modal", false);
		m_page->send(std::move(m));
	}

	editorMenu::Item PageEditor::updateMenu()
	{
		using namespace editorMenu;
		// The actions run after the menu closed; the window may have closed by then.
		const auto alive = std::weak_ptr<int>(m_alive);
		std::vector<Item> items;
		items.push_back(action("update-now", "Check for Updates Now", [this, alive]
		{
			if(!alive.expired())
				m_updater->checkNow(getProcessor().getConfig());
		}));
		items.push_back(action("update-daily", "Check Daily", [this, alive]
		{
			if(alive.expired())
				return;
			auto& config = getProcessor().getConfig();
			updates::Updater::setEnabled(config, !updates::Updater::enabled(config));
		}, {}, updates::Updater::enabled(getProcessor().getConfig())));
		items.push_back(separator());
		items.push_back(note("This version: " + mdmmUpdate::toString(updates::Updater::currentVersion())));
		return submenu("updates", "Updates", std::move(items));
	}

	// I-008: the editor's menu drawn by the page (an editorMenu message, deskMenu.js); the page answers menuPick with
	// the entry's number. No page up (it failed to start, no web view): the same menu as a native one.
	void PageEditor::openMenu()
	{
		auto* state = dynamic_cast<PluginEditorState*>(getProcessor().getEditorState());
		if(!state)
			return;
		if(!m_page || !m_page->pageReady() || m_page->failed())
		{
			jucePluginEditorLib::createPopupMenu(*state).showMenuAsync(juce::PopupMenu::Options().withMousePosition());
			return;
		}
		auto m = editorMenu::toJson(state->menu(), m_menuActions);
		m.set("type", "editorMenu");
		m.set("menu", ++m_menuSerial);
		m_page->send(std::move(m));
		m_page->flush();
	}

	void PageEditor::pickMenu(const int _menu, const size_t _n)
	{
		if(_menu != m_menuSerial || _n >= m_menuActions.size())
			return;	// an older menu's entry (a newer one was sent since): nothing
		auto action = m_menuActions[_n];
		m_menuActions.clear();	// one choice a menu
		++m_menuSerial;
		if(action)
			action();
	}

	void PageEditor::layout() const
	{
		if(auto* root = getRmlComponent(); root && m_page)
		{
			m_page->layout(root->getLocalBounds());
		}
	}
}
