#pragma once

namespace jucePluginEditorLib
{
	// What an editor can say about itself beyond jucePluginEditorLib::Editor, for this fork's own
	// windows (doc/modern-ux/UPSTREAM.md). An editor derives from the ones that apply; the window
	// asks with a dynamic_cast, so Editor itself stays upstream's.

	// A page that lays itself out in any size (the Machinedrum and Monomachine Editors' web pages):
	// its window resizes freely and remembers its width and height (EditorWindow, editorWindowFit.h).
	// An editor drawn to its skin's fixed size keeps the skin's aspect ratio.
	class FreeSizeEditor
	{
	public:
		virtual ~FreeSizeEditor() = default;
	};

	// An editor with its own audio and MIDI panel: the standalone's Audio/MIDI Settings... opens it
	// (true) instead of JUCE's dialog (standaloneApp.h).
	class AudioMidiSettingsEditor
	{
	public:
		virtual ~AudioMidiSettingsEditor() = default;
		virtual bool openAudioMidiSettings() = 0;
	};
}
