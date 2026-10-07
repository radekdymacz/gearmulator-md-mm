#pragma once

// The standalone app's Record menu (macOS): Start Recording / Stop Recording (⇧⌘R) and Show
// Recordings in Finder. A take is the window's content and the app's own sound
// (mdScreenRecorder.h) in ~/Movies/<editor>/<editor> <yyyy-mm-dd hh-mm-ss>.mp4, with a sidecar
// .json beside it for the reels; the window's title says "● REC" while it records. What it has to
// say (not allowed, ended early, failed, needs macOS 15) is the page's own notice dialog
// (messageRoute.h), the system's alert only when no page takes it. Built into the _Standalone
// targets only (mdmmPlugins.cmake).
//
// ⇧⌘R: the pages' key map (skins/shared/deskKeys.js) matches exact modifiers and binds ⇧⌘ to Z
// and clicks only; no page handler prevents a ⌘ key (deskModal.js lets them through to the menu bar).

#include "mdScreenRecorder.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class StandaloneWindow;
}

namespace mdJucePlugin
{
	class RecordMenu : juce::ApplicationCommandTarget
	{
	public:
		// _noticeOwner: the plug-in instance whose window shows the notices (the processor, as
		// PageEditor names it).
		RecordMenu(jucePluginEditorLib::StandaloneWindow& _window, juce::String _editorName, juce::String _model, const void* _noticeOwner);
		~RecordMenu() override;

		// The menu, as it is now.
		juce::PopupMenu menu();

		// Ends a take before the app quits, its file closed.
		void close();

		// ~/Movies/<editor>, and a take's file in it for a start time.
		static juce::File folder(const juce::String& _editorName);
		static juce::File takeFile(const juce::File& _folder, const juce::String& _editorName, const juce::Time& _start);

	private:
		enum Command : juce::CommandID
		{
			ToggleRecording = 0x4d4d5201,
			ShowRecordings = 0x4d4d5202
		};

		ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
		void getAllCommands(juce::Array<juce::CommandID>& _commands) override;
		void getCommandInfo(juce::CommandID _id, juce::ApplicationCommandInfo& _info) override;
		bool perform(const InvocationInfo& _info) override;

		juce::String toggleText() const;
		void toggle();
		void start();
		void started(const ScreenRecorder::Started& _started);
		void ended(const ScreenRecorder::Ended& _ended);
		void showState();
		void writeSidecar(const ScreenRecorder::Ended& _ended) const;
		void askForPermission() const;
		void notify(const juce::String& _title, const juce::String& _text, const std::vector<juce::String>& _buttons,
			std::function<void(int)> _answered = {}) const;

		jucePluginEditorLib::StandaloneWindow& m_window;
		const juce::String m_editorName;
		const juce::String m_model;
		const juce::String m_title;
		const void* const m_noticeOwner;
		juce::ApplicationCommandManager m_commands;
		ScreenRecorder m_recorder;
		juce::Time m_startTime;
		ScreenRecorder::Picture m_picture;
		bool m_closing = false;
	};
}
