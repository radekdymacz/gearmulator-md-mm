#pragma once

#include "deskCore/deskCommands.h"

#include <cstdint>
#include <optional>
#include <string>

namespace deskHost
{
	// THE switch for MIDI mapping (MIDI learn and the pages' CONTROL workspace). Off while the
	// controller feature is being reworked: the learn commands are refused, the plug-in applies no
	// learned mappings (MIDI reaches the machine raw), and the learn document tells the pages
	// ("enabled") to hide the CONTROL workspace and the LEARN key. All of the code stays. Turn it on
	// with -DMDMM_MIDI_MAPPING=1 (or change the default here); nothing else is needed.
#ifndef MDMM_MIDI_MAPPING
#define MDMM_MIDI_MAPPING 0
#endif
	inline constexpr bool midiMappingEnabled = MDMM_MIDI_MAPPING != 0;

	// What the plug-in does for one of its commands: the handler column of its table.
	enum class Action : uint8_t
	{
		Engine,				// switch to an entry of the engine map
		RecheckFirmware,	// look for the ROM again
		RevealRomFolder,
		Midi,				// a channel message from the page (keys, joystick), to the engine
		Menu,				// the editor's menu
		LearnStart,			// MIDI learn (mdMidiLearnCommands)
		LearnAdd,
		LearnSetCc,
		LearnCancel,
		LearnRemove,
		LearnInvert,
		AudioPublish,		// the standalone's audio and MIDI devices
		AudioSet,
		AudioMeter,
		ChooseRom,			// P7: the native file chooser for a firmware file (.bin, or a .zip with it)
		ChooseSyx,			// P7: the native file chooser for a .syx to import (its preview follows)
		SyxImport,			// P7: import the previewed .syx (the kinds chosen)
		SyxCancel,
		SyxExport			// P7: the native save dialog, then every document the editor holds as one .syx
	};

	// Who acts on a row: the session (it outlives the window) or the window (the menu and the
	// audio devices are the window's).
	enum class Actor : uint8_t
	{
		Session,
		Window
	};

	struct Handler
	{
		Action action = Action::Engine;
		Actor actor = Actor::Session;
	};

	using Table = deskCore::CommandTable<Handler>;

	// The plug-in's commands, one table for both editors. A model's limits (which tracks and
	// parameters can be learned, whether the page has a keyboard) are the session's data (the
	// learn document publishes them); the arguments here are the widest the contract allows.
	const Table& commands();

	// The contract's $defs/command: a model's table and the plug-in's, as one list.
	elektronData::json::Value contractCommands(const elektronData::json::Value& _modelSchema);

	// audioSet's vocabulary (its set and do arguments): the row's oneOf lists are these names.
	enum class AudioSetting : uint8_t
	{
		Driver,			// device: the driver type
		Output,			// device: the output device
		Input,			// device: the input device ("" = none)
		Mute,			// on: the input muted
		OutputChannel,	// index, on: one output channel
		SampleRate,		// value
		BufferSize,		// value
		MidiInput,		// device, on: a MIDI input enabled
		MidiOutput,		// device: the MIDI output
		Count
	};

	enum class AudioAction : uint8_t
	{
		Test,			// play the test sound
		Bluetooth,		// the Bluetooth MIDI pairing dialogue
		Count
	};

	const char* audioSettingName(AudioSetting _s);
	const char* audioActionName(AudioAction _a);
	std::optional<AudioSetting> audioSettingOf(const std::string& _name);
	std::optional<AudioAction> audioActionOf(const std::string& _name);
}
