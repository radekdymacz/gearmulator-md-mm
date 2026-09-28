#include "mdPluginEditorState.h"

#include "mdEditor.h"
#include "mdPageEditor.h"
#include "mdPluginProcessor.h"
#include "mdProductSkinPolicy.h"
#include "mdStandaloneRendererPolicy.h"

#include "mdProductSkins.h"

#include "juce_events/juce_events.h"
#include "jucePluginEditorLib/rendererPreferenceKeys.h"
#include "juceRmlUi/rmlMenu.h"

namespace mdJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor)
		: jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(),
			productSkins(_processor.getModel()))
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

#if MDMM_DIAGNOSTICS
		// GEARMULATOR_MDSTUDIO_SELFTEST=p5skin: switch skins live through the same call the
		// Editor > Skins menu makes, and log the editor each time (P5). Diagnostics builds only.
		if(juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDSTUDIO_SELFTEST", {}) == "p5skin")
		{
			const auto logLine = [](const juce::String& _l)
			{
				juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mdStudio.log")
					.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " P5: " + _l + "\n");
			};
			const auto report = [this, logLine](const juce::String& _what)
			{
				auto* e = getEditor();
				auto* ui = getUiRoot();
				logLine(_what + ": current skin \"" + juce::String(getCurrentSkin().displayName) + "\" (" + juce::String(getCurrentSkin().filename)
					+ "), editor " + (dynamic_cast<PageEditor*>(e) ? "Machinedrum Editor page" : e ? "panel" : "none")
					+ (ui ? ", " + juce::String(ui->getWidth()) + " x " + juce::String(ui->getHeight()) : juce::String()));
			};
			juce::String names;
			for(const auto& skin : getIncludedSkins())
				names << "\"" << skin.displayName << "\" ";
			logLine("Editor > Skins lists: " + names);
			// The editor page re-made twice, as a settings change or a host reopening it does.
			juce::Timer::callAfterDelay(20000, [this, report] { report("at start"); loadSkin(getIncludedSkins()[0]);
				juce::Timer::callAfterDelay(6000, [this, report] { report("re-made once"); loadSkin(getIncludedSkins()[0]);
					juce::Timer::callAfterDelay(8000, [report] { report("re-made twice"); }); }); });
		}
#endif

		const auto configuredSkin = readSkinFromConfig();
		if(configuredSkin.isValid() && configuredSkin.folder.empty() && isSkinCompatible(_processor.getModel(),
			configuredSkin.displayName, configuredSkin.filename))
		{
			loadSkin(getIncludedSkins().front());
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

	// The editor page is the only UI: whatever skin is asked for, the product's page is made.
	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		const auto& page = getIncludedSkins().front();
		const bool mm = static_cast<AudioPluginAudioProcessor&>(m_processor).getModel() == md::MachineModel::Monomachine;
		const bool own = mm ? isMmStudioSkin(_skin.displayName, _skin.filename) : isStudioSkin(_skin.displayName, _skin.filename);
		return new PageEditor(m_processor, own ? _skin : page);
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
}
