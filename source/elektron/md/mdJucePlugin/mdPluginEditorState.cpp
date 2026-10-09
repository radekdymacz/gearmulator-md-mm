#include "mdPluginEditorState.h"

#include "mdAbout.h"

#include "mdEditor.h"
#include "mdEditorPages.h"
#include "mdPageEditor.h"
#include "mdPluginProcessor.h"
#include "mdProductSkinPolicy.h"
#include "mdStandaloneRendererPolicy.h"

#include "mdProductSkins.h"

#include "juce_events/juce_events.h"
#include "jucePluginEditorLib/editorTraits.h"
#include "jucePluginEditorLib/rendererPreferenceKeys.h"
#include "juceRmlUi/rmlMenu.h"

#include <cmath>

namespace mdJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor)
		: jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(),
			editorPageSkins(_processor.getModel()))
	{
		#if JUCE_MAC
		constexpr bool isMacOS = true;
		#else
		constexpr bool isMacOS = false;
		#endif

		auto& config = _processor.getConfig();
		using namespace jucePluginEditorLib;
		if(shouldRemoveLegacyStandaloneSoftwareRenderer(isMacOS,
			juce::JUCEApplicationBase::isStandaloneApp(),
			_processor.getForceSoftwareRendererForSession().has_value(),
			config.getBoolValue(forceSoftwareRendererUserSelectedKey, false),
			config.containsKey(forceSoftwareRendererKey),
			config.getBoolValue(forceSoftwareRendererKey, false)))
		{
			config.removeValue(forceSoftwareRendererKey);
			config.saveIfNeeded();
		}

		keepEditorPage(*this);
		const auto configuredSkin = readSkinFromConfig();
		if(configuredSkin.isValid() && isSkinCompatible(_processor.getModel(),
			configuredSkin.displayName, configuredSkin.filename))
		{
			loadSkin(configuredSkin);
			return;
		}

		const auto* const defaultSkin = defaultSkinName(_processor.getModel());

		for(const auto& skin : getIncludedSkins())
		{
			if(skin.displayName == defaultSkin)
			{
				loadSkin(skin);
				return;
			}
		}

		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return createEditorPage(*this, m_processor, _skin);
	}

	// I-008: the editor's menu, one tree for the page (which draws it), the menu bar and the native fallback.
	editorMenu::Menu PluginEditorState::menu()
	{
		using namespace editorMenu;
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		Menu m;
		m.title = about::title(processor.getModel() == md::MachineModel::Monomachine);	// 0.3.4: which editor, which version

		// The two zooms: the page's own (Cmd - / Cmd + / Cmd 0, B-001) and, inside it, the window's size (upstream's GUI Scale)
		auto* const page = dynamic_cast<PageEditor*>(getEditor());
		if(page)
			m.items.push_back(page->zoomMenu(windowSizeMenu()));
		else
			m.items.push_back(windowSizeMenu());
		m.items.push_back(separator());

		if(page)
			m.items.push_back(page->updateMenu());	// I-005 (doc/modern-ux/DESIGN-updates.md)
		// B-022: the folder of the start-up log (and the performance captures), for a report
		m.items.push_back(action("open-log-folder", "Open Log Folder", [folder = processor.performanceDiagnosticsFolder()]
		{
			juce::MessageManager::callAsync([folder]	// after the menu closed
			{
				if(folder.createDirectory().wasOk())
					folder.revealToUser();
			});
		}));
		if(juce::JUCEApplicationBase::isStandaloneApp())
		{
			// B-007: the page's AUDIO / MIDI panel (in a plug-in the host owns audio and MIDI)
			m.items.push_back(action("audio-midi", "Audio/MIDI Settings...", [this]
			{
				juce::MessageManager::callAsync([this]	// after the menu closed, as the menu bar's entry does
				{
					if(auto* editor = dynamic_cast<jucePluginEditorLib::AudioMidiSettingsEditor*>(getEditor()))
						editor->openAudioMidiSettings();
				});
			}));
		}
		addSysexEntries(m.items);
		m.items.push_back(separator());

		// What a developer or a bug report asks for: the performance captures, the Machinedrum's RAM recording mode
		std::vector<Item> developer;
		developer.push_back(action("perf-capture", processor.performanceDiagnosticsActive()
			? "Stop Performance Capture" : "Start Performance Capture", [this]
			{
				auto& p = static_cast<AudioPluginAudioProcessor&>(m_processor);
				p.setPerformanceDiagnosticsEnabled(!p.performanceDiagnosticsActive());
			}));
		developer.push_back(note(processor.performanceDiagnosticsStatus()));
		if(processor.getModel() == md::MachineModel::Machinedrum)
		{
			const bool available = processor.isRamRecordingModeAvailable();
			const auto mode = processor.getRamRecordingMode();
			developer.push_back(separator());
			developer.push_back(heading("RAM recording"));
			developer.push_back(action("ram-complete", "Complete tails (recommended)", [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::CompleteTail);
				}, {}, mode == md::RamRecordingMode::CompleteTail, available));
			developer.push_back(action("ram-original", "Original finalization", [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::Original);
				}, {}, mode == md::RamRecordingMode::Original, available));
		}
		// For testers: tells in minutes whether a CPU problem comes from the event-driven SIM stepping (L5).
		// Same audio either way; ticked is the slower per-instruction stepping of before.
		developer.push_back(separator());
		const bool legacySimStepping = processor.isLegacySimStepping();
		developer.push_back(action("legacy-sim-stepping", "Legacy ColdFire timer stepping (slower)",
			[&processor, legacySimStepping]
			{
				processor.setLegacySimStepping(!legacySimStepping);
			}, {}, legacySimStepping));
		m.items.push_back(submenu("developer", "Developer", std::move(developer)));
		return m;
	}

	// Upstream's GUI Scale: the window at a size of the page's design size (1440 x 924); the page fits itself into it.
	editorMenu::Item PluginEditorState::windowSizeMenu()
	{
		using namespace editorMenu;
		const auto now = m_processor.getConfig().getDoubleValue("scale", 100);
		const double w = getWidth() * getRootScale(), h = getHeight() * getRootScale();
		std::vector<Item> sizes;
		for(const int percent : {50, 65, 75, 85, 100, 125, 150, 175, 200, 250, 300})
		{
			auto label = std::to_string(percent) + " %";
			if(w > 0 && h > 0)
				label += "  (" + std::to_string(static_cast<int>(w * percent / 100)) + " \xc3\x97 " + std::to_string(static_cast<int>(h * percent / 100)) + ")";
			sizes.push_back(action("window-" + std::to_string(percent), label, [this, percent]
			{
				juce::MessageManager::callAsync([this, percent] { evSetGuiScale(percent); });	// after the menu closed
			}, {}, std::abs(now - percent) < 0.5));
		}
		return submenu("window-size", "Window Size", std::move(sizes));
	}

	// The panel editor's SysEx transfer (upstream's skins; the editor page has its own import, deskSyx.js).
	void PluginEditorState::addSysexEntries(std::vector<editorMenu::Item>& _items)
	{
		auto* const editor = dynamic_cast<Editor*>(getEditor());
		if(!editor)
			return;
		const bool active = editor->isUserSysexTransferActive();
		if(editor->canResumeUserSysexTransfer())
			_items.push_back(editorMenu::action("sysex-resume", "Resume SysEx Transfer - machine is ready",
				[editor] { editor->resumeUserSysexTransfer(); }));
		const bool cancellable = editor->canCancelUserSysexTransfer();
		_items.push_back(editorMenu::action("sysex", editor->getUserSysexMenuText(), [editor, cancellable]
			{
				if(cancellable)
				{
					editor->cancelUserSysexTransfer();
					return;
				}
				// Defer the native picker until the menu's teardown has completed.
				const auto lifetime = editor->getLifetimeToken();
				juce::MessageManager::callAsync([lifetime, editor]
				{
					if(!lifetime.expired())
						editor->chooseUserSysexFile();
				});
			}, {}, false, !active || cancellable));
	}

	// The same tree as an RmlUi menu: the menu bar and the native fallback show it (jucePluginEditorLib::toPopupMenu).
	void PluginEditorState::fillMenu(juceRmlUi::Menu& _menu)
	{
		const auto m = menu();
		juceRmlUi::Menu out;
		out.addEntry(m.title, false, false, {});
		out.addSeparator();
		toRml(m.items, out);
		_menu = std::move(out);
	}

	void PluginEditorState::toRml(const std::vector<editorMenu::Item>& _items, juceRmlUi::Menu& _menu)
	{
		using Kind = editorMenu::Item::Kind;
		for(const auto& i : _items)
		{
			const auto label = i.key.empty() ? i.label : i.label + "  (" + i.key + ")";
			switch(i.kind)
			{
			case Kind::Separator:
				_menu.addSeparator();
				break;
			case Kind::Heading:
			case Kind::Note:
				_menu.addEntry(i.label, false, false, {});
				break;
			case Kind::Submenu:
				{
					juceRmlUi::Menu sub;
					toRml(i.items, sub);
					_menu.addSubMenu(label, std::move(sub));
				}
				break;
			case Kind::Action:
				_menu.addEntry(label, i.enabled && i.action != nullptr, i.checked, i.action);
				break;
			}
		}
	}

	// The editors are web pages: upstream's RmlUi settings page has nothing to show over them. What the page has is
	// its AUDIO / MIDI panel, the standalone's (menu(): Audio/MIDI Settings...); in a plug-in the host owns audio and
	// MIDI. The other settings (zoom, window size, RAM recording, diagnostics) are entries of the menu.
	void PluginEditorState::addSettingsEntry(juceRmlUi::Menu&)
	{
	}
}
