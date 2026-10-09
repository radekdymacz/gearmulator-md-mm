#pragma once

#include <string>

namespace mdJucePlugin
{
	// B-037: what the standalone's AUDIO / MIDI panel says when a MIDI port the user switched on, or chose as the output,
	// did not open (mdAudioMidiLink.cpp), as plain values: tested by mdWindowsPolicyTest. JUCE's AudioDeviceManager
	// leaves such a port off without a word (setMidiInputDeviceEnabled, setDefaultMidiOutputDevice), so the panel's
	// switch just went dark again. On Windows that is the usual case, not a fault: a MIDI port opened through Windows'
	// MIDI API (WinMM, which JUCE uses) belongs to one program at a time, and a DAW opens the ports it is set to use.
	namespace midiPortRefusal
	{
		enum class Kind
		{
			Input,
			Output
		};

		// "" when the port is as asked (_asked: the user wanted it open; _open: it is), otherwise why not and what to do.
		inline std::string text(const Kind _kind, const std::string& _name, const bool _asked, const bool _open, const bool _windows)
		{
			if(!_asked || _open)
				return {};
			const std::string port = (_kind == Kind::Input ? "The MIDI input " : "The MIDI output ") + (_name.empty() ? std::string("port") : _name);
			if(_windows)
				return port + " could not be opened. On Windows a MIDI port works in one program at a time: if a DAW or another "
					"editor is open and uses it, switch it off there (or close that program), then try again here.";
			return port + " could not be opened: the system refused it. Check that it is connected, then try again.";
		}
	}
}
