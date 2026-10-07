#include "mdPluginEditorState.h"

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

	void PluginEditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		if(processor.getModel() == md::MachineModel::Machinedrum)
		{
			const bool available = processor.isRamRecordingModeAvailable();
			const auto mode = processor.getRamRecordingMode();
			juceRmlUi::Menu ramRecording;
			ramRecording.addEntry("Complete tails (recommended)", available,
				mode == md::RamRecordingMode::CompleteTail, [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::CompleteTail);
				});
			ramRecording.addEntry("Original finalization", available,
				mode == md::RamRecordingMode::Original, [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::Original);
				});
			_menu.addSubMenu("RAM recording", std::move(ramRecording));
		}
		juceRmlUi::Menu diagnostics;
		diagnostics.addEntry(processor.performanceDiagnosticsActive()
			? "Stop performance capture" : "Start performance capture", [this]
			{
				auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
				processor.setPerformanceDiagnosticsEnabled(!processor.performanceDiagnosticsActive());
			});
		diagnostics.addEntry("Open logs folder", [folder = processor.performanceDiagnosticsFolder()]
			{
				// Open Finder/Explorer after the Rml menu has closed.
				juce::MessageManager::callAsync([folder]
					{
						if(folder.createDirectory().wasOk()) folder.revealToUser();
					});
			});
		diagnostics.addSeparator();
		diagnostics.addEntry(processor.performanceDiagnosticsStatus(), false, false, {});
		_menu.addSubMenu("Performance diagnostics", std::move(diagnostics));

		// B-001: the editor page's zoom (mdPageEditor.h; a hook, doc/modern-ux/UPSTREAM.md)
		if(auto* page = dynamic_cast<PageEditor*>(getEditor()))
			page->fillZoomMenu(_menu);

		auto* const editor = dynamic_cast<Editor*>(getEditor());
		if(!editor)
			return;

		const bool active = editor->isUserSysexTransferActive();
		if(editor->canResumeUserSysexTransfer())
			_menu.addEntry("Resume SysEx Transfer - machine is ready", true, false,
				[editor] { editor->resumeUserSysexTransfer(); });
		const bool cancellable = editor->canCancelUserSysexTransfer();
		_menu.addEntry(editor->getUserSysexMenuText(),
			!active || cancellable, false, [this, editor, cancellable]
			{
				if(cancellable)
				{
					editor->cancelUserSysexTransfer();
					return;
				}
				// Menu actions run before the Rml menu closes. Defer the native picker
				// until that teardown has completed.
				const auto lifetime = editor->getLifetimeToken();
				juce::MessageManager::callAsync([lifetime, editor]
				{
					if(!lifetime.expired())
						editor->chooseUserSysexFile();
				});
			});
	}

	// The editors are web pages: upstream's RmlUi settings page has nothing to show over them. What the
	// page has is its AUDIO / MIDI panel, which is the standalone's; in a plug-in the host owns audio and
	// MIDI, and the other settings (window scale, RAM recording, diagnostics) are entries of this menu.
	void PluginEditorState::addSettingsEntry(juceRmlUi::Menu& _menu)
	{
		if(!juce::JUCEApplicationBase::isStandaloneApp())
			return;
		_menu.addSeparator();
		_menu.addEntry("Audio/MIDI Settings...", [this]
		{
			// After the menu has closed, as the menu bar's entry does.
			juce::MessageManager::callAsync([this]
			{
				if(auto* editor = dynamic_cast<jucePluginEditorLib::AudioMidiSettingsEditor*>(getEditor()))
					editor->openAudioMidiSettings();
			});
		});
	}
}
