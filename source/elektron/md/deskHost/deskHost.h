#pragma once

#include "deskCore/deskCommands.h"

#include <cstdint>

namespace deskHost
{
	// What the plug-in does for one of its commands: the handler column of its table. The session
	// acts on most; the window on the ones that need it (the menu, the audio devices).
	enum class Action : uint8_t
	{
		Engine,				// switch to an entry of the engine map
		RecheckFirmware,	// look for the ROM again
		RevealRomFolder,
		Midi,				// a channel message from the page (keys, joystick), to the engine
		Menu,				// the editor's menu (the window)
		LearnStart,			// MIDI learn (mdMidiLearnCommands)
		LearnAdd,
		LearnSetCc,
		LearnCancel,
		LearnRemove,
		LearnInvert,
		AudioPublish,		// the standalone's audio and MIDI devices (the window)
		AudioSet,
		AudioMeter
	};

	using Table = deskCore::CommandTable<Action>;

	// The plug-in's commands, one table for both editors. A model's limits (which tracks and
	// parameters can be learned, whether the page has a keyboard) are the session's data; the
	// arguments here are the widest the contract allows.
	const Table& commands();

	// The contract's $defs/command: a model's table and the plug-in's, as one list.
	elektronData::json::Value contractCommands(const elektronData::json::Value& _modelSchema);

	// The window acts on it (the menu and the audio devices), not the session.
	bool isWindowAction(Action _a);
}
