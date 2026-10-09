#pragma once

// A standalone application for gearmulator plug-ins with native window chrome: the
// operating system's title bar (traffic lights on macOS) instead of JUCE's drawn one,
// and the editor's menu (GUI scale, skins, settings) in the native menu bar, next to
// Audio/MIDI settings and saving or loading the state. Everything else is JUCE's
// StandaloneFilterWindow and StandalonePluginHolder as before, including the settings
// file name, so existing standalone state is kept.
//
// No "Audio input is muted to avoid feedback loop" bar: the input still starts muted
// (JUCE's default, kept in the settings), and an editor that shows the mute itself (the
// Machinedrum and Monomachine Editors' AUDIO / MIDI panel) says so there. Audio/MIDI
// Settings... opens the editor's own panel when it has one (AudioMidiSettingsEditor, editorTraits.h),
// otherwise JUCE's dialog.
//
// Use: define JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 for the plug-in target and
// compile one file of the plug-in's shared code with
//     #include "jucePluginEditorLib/standaloneApp.h"
//     JUCE_CREATE_APPLICATION_DEFINE(jucePluginEditorLib::StandaloneApp)
// or with a class derived from StandaloneApp that gives the window its title (getWindowTitle)
// and, through windowOpened / windowClosing, menus of its own after "Editor" and "Audio"
// (StandaloneWindow::addMenu).

#include "editorPopupMenu.h"
#include "editorTraits.h"
#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <functional>
#include <vector>

namespace jucePluginEditorLib
{
	class StandaloneWindow : public juce::StandaloneFilterWindow, juce::MenuBarModel
	{
	public:
		// A menu of the application's own, after "Editor" and "Audio": its name and a function
		// that builds it each time it opens.
		struct Menu
		{
			juce::String name;
			std::function<juce::PopupMenu()> build;
		};

		StandaloneWindow(const juce::String& _appName, juce::PropertySet* _settings)
			: juce::StandaloneFilterWindow(_appName,
				juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
				_settings, false)
		{
			// B-016: JUCE's standalone window asks for minimise and close only; without the maximise button the
			// window has no full-screen button on macOS (NSWindowCollectionBehaviorFullScreenPrimary needs both
			// it and a resizable window) and no maximise box on Windows and Linux. Before the native title bar,
			// which makes the window again with these flags.
			setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
			setUsingNativeTitleBar(true);
			hideJuceOptionsButton();
			detachFeedbackBanner();
#if JUCE_MAC
			setAppleMenu();
#else
			setMenuBar(this);
#endif
		}

		~StandaloneWindow() override
		{
#if JUCE_MAC
			juce::MenuBarModel::setMacMainMenu(nullptr);
#else
			setMenuBar(nullptr);
#endif
		}

		void resized() override
		{
			juce::StandaloneFilterWindow::resized();
			hideJuceOptionsButton();
		}

		// B-034: one ordered way out, for the close button and the system's quit. JUCE's saves the state while the
		// audio runs (the save holds the plug-in's lock, so the audio thread waits: a glitch) and then cuts the
		// sound (a click while a pattern plays). Here the output fades to silence over about 30 ms, the audio
		// stops at about 60 ms, then the state is saved with the gain it had, then the application quits.
		void closeButtonPressed() override { quitOrdered(); }

		void quitOrdered()
		{
			if(m_quitting)
				return;
			m_quitting = true;
			auto* processor = dynamic_cast<Processor*>(getAudioProcessor());
			const float gain = processor ? processor->getOutputGain() : 1.0f;
			// the window lives until the application quits (the last step below)
			constexpr int steps = 6;
			for(int i = 1; i <= steps; ++i)
				juce::Timer::callAfterDelay(5 * i, [processor, gain, i]
				{
					if(processor)
						processor->setOutputGain(gain * static_cast<float>(steps - i) / static_cast<float>(steps));
				});
			juce::Timer::callAfterDelay(60, [this, processor, gain]
			{
				pluginHolder->stopPlaying();
				if(processor)
					processor->setOutputGain(gain);
				pluginHolder->savePluginState();
				juce::JUCEApplicationBase::quit();
			});
		}

		// An "About <name>" entry: the application menu's first on macOS, a Help menu elsewhere.
		void setAbout(const juce::String& _name, std::function<void()> _show)
		{
			m_aboutName = _name;
			m_showAbout = std::move(_show);
#if JUCE_MAC
			setAppleMenu();
#else
			addMenu({"Help", [this]
			{
				juce::PopupMenu m;
				m.addItem("About " + m_aboutName + "...", [this] { if(m_showAbout) m_showAbout(); });
				return m;
			}});
#endif
		}

		void addMenu(Menu _menu)
		{
			m_menus.push_back(std::move(_menu));
			menuItemsChanged();
		}

		// The menus' items or their states changed: build them again (the macOS menu bar keeps
		// its items' key equivalents and enabled states from the last build).
		void refreshMenus() { menuItemsChanged(); }

		juce::StringArray getMenuBarNames() override
		{
			juce::StringArray names{"Editor", "Audio"};
			for(const auto& m : m_menus)
				names.add(m.name);
			return names;
		}

		juce::PopupMenu getMenuForIndex(const int _index, const juce::String&) override
		{
			if(_index >= 2)
			{
				const auto i = static_cast<size_t>(_index - 2);
				if(i >= m_menus.size() || !m_menus[i].build)
					return {};
				return m_menus[i].build();
			}
			if(_index == 0)
			{
				if(auto* p = dynamic_cast<Processor*>(getAudioProcessor()))
					if(auto* state = p->getEditorState())
						return createPopupMenu(*state);
				return {};
			}
			juce::PopupMenu m;
			m.addItem("Audio/MIDI Settings...", [this] { showAudioMidiSettings(); });
			m.addSeparator();
			m.addItem("Save current state...", [this] { pluginHolder->askUserToSaveState(); });
			m.addItem("Load a saved state...", [this] { pluginHolder->askUserToLoadState(); });
			return m;
		}

		void menuItemSelected(int, int) override {}

	private:
		std::vector<Menu> m_menus;
		juce::String m_aboutName;
		std::function<void()> m_showAbout;
		bool m_quitting = false;

#if JUCE_MAC
		void setAppleMenu()
		{
			juce::PopupMenu appleExtras;
			if(m_showAbout)
			{
				appleExtras.addItem("About " + m_aboutName, [this] { if(m_showAbout) m_showAbout(); });
				appleExtras.addSeparator();
			}
			// B-016: an editor with its own audio and MIDI panel (the web-page editors) hides upstream's settings
			// page, so its "Settings..." would do nothing: it has Audio/MIDI Settings... only.
			if(!hasOwnAudioMidiPanel())
				appleExtras.addItem("Settings...", [this] { showEditorSettings(); });
			appleExtras.addItem("Audio/MIDI Settings...", [this] { showAudioMidiSettings(); });
			juce::MenuBarModel::setMacMainMenu(this, &appleExtras);
		}
#endif

		bool hasOwnAudioMidiPanel() const
		{
			if(auto* p = dynamic_cast<Processor*>(getAudioProcessor()))
				if(auto* state = p->getEditorState())
					return dynamic_cast<AudioMidiSettingsEditor*>(state->getEditor()) != nullptr;
			return false;
		}

		void showAudioMidiSettings()
		{
			if(auto* p = dynamic_cast<Processor*>(getAudioProcessor()))
				if(auto* state = p->getEditorState())
					if(auto* editor = dynamic_cast<AudioMidiSettingsEditor*>(state->getEditor()); editor && editor->openAudioMidiSettings())
						return;
			pluginHolder->showAudioSettingsDialog();
		}

		// JUCE's content component shows its yellow bar while the input is muted and follows
		// the holder's mute value to do so. Move the holder (its audio callback's mute and the
		// saved setting) to a fresh value with the same state, then clear the old one, which
		// only the bar still follows: the bar goes and never comes back; the mute stays.
		void detachFeedbackBanner()
		{
			auto& mute = pluginHolder->getMuteInputValue();
			juce::Value old;
			old.referTo(mute);
			juce::Value fresh(mute.getValue());
			mute.referTo(fresh);
			old = false;
		}

		void showEditorSettings()
		{
			if(hasOwnAudioMidiPanel())	// the menu was made before the editor: its own panel, never the hidden page
			{
				showAudioMidiSettings();
				return;
			}
			if(auto* p = dynamic_cast<Processor*>(getAudioProcessor()))
				if(auto* state = p->getEditorState())
					if(auto* editor = state->getEditor())
						editor->showSettings(true);
		}

		// JUCE's drawn title bar has an "Options" button; the native one has no room for it,
		// and its entries are in the menu bar.
		void hideJuceOptionsButton()
		{
			for(auto* c : getChildren())
				if(auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Options")
					b->setVisible(false);
		}
	};

	class StandaloneApp : public juce::JUCEApplication
	{
	public:
		StandaloneApp() : StandaloneApp(juce::String(juce::CharPointer_UTF8(JucePlugin_Name)))
		{
		}

		// _settingsName: the settings file's name (<name>.settings), by default the plug-in's name as JUCE's
		// own standalone uses it. A product that must not share it with another build passes its own.
		explicit StandaloneApp(const juce::String& _settingsName)
		{
			m_appProperties.setStorageParameters(settingsOptions(_settingsName));
		}

		static juce::PropertiesFile::Options settingsOptions(const juce::String& _settingsName)
		{
			juce::PropertiesFile::Options options;
			options.applicationName = _settingsName;
			options.filenameSuffix = ".settings";
			options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
			options.folderName = "~/.config";
#else
			options.folderName = "";
#endif
			return options;
		}

		const juce::String getApplicationName() override { return m_appName; }
		const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
		bool moreThanOneInstanceAllowed() override { return true; }
		void anotherInstanceStarted(const juce::String&) override {}

		void initialise(const juce::String&) override
		{
			m_window = std::make_unique<StandaloneWindow>(getApplicationName(), m_appProperties.getUserSettings());
			if(auto* p = m_window->getAudioProcessor())
			{
				const auto title = getWindowTitle(*p);
				if(title.isNotEmpty())
					m_window->setName(title);
			}
			m_window->setVisible(true);
			windowOpened(*m_window);
		}

		// The window's title; empty: the application name.
		virtual juce::String getWindowTitle(juce::AudioProcessor&) const { return {}; }

		// The window is up (the place to add menus of the application's own), and it is about to go.
		virtual void windowOpened(StandaloneWindow&) {}
		virtual void windowClosing(StandaloneWindow&) {}

		void shutdown() override
		{
			if(m_window)
				windowClosing(*m_window);
			m_window = nullptr;
			m_appProperties.saveIfNeeded();
		}

		void systemRequestedQuit() override
		{
			if(juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
			{
				juce::Timer::callAfterDelay(100, []
				{
					if(auto app = juce::JUCEApplicationBase::getInstance())
						app->systemRequestedQuit();
				});
			}
			else if(m_window)
				m_window->quitOrdered();	// B-034: fade, stop, save, quit (the close button's way)
			else
				quit();
		}

	private:
		juce::ApplicationProperties m_appProperties;
		std::unique_ptr<StandaloneWindow> m_window;
		const juce::String m_appName{juce::CharPointer_UTF8(JucePlugin_Name)};
	};
}
