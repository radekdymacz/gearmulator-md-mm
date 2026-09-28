#pragma once

#include "elektronData/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskCore
{
	// The page's command vocabulary as data (P6): one table per model, read by every
	// dispatcher (the host session routes by owner, the core checks arguments and gates,
	// apply picks the document kind), and the contract's $defs/command is generated
	// from it.
	enum class Owner : uint8_t
	{
		Core,		// documents: edits (pure apply), undo/redo, ready
		Machine,	// the adapter: transport, selection, loads, slot actions on the machine
		Setup,		// the editor's own setup (app modulators, knob rows)
		Host		// the plug-in: MIDI learn, the ROM folder, the engine map, menus
	};

	enum class Gate : uint8_t
	{
		None,		// any time
		Midi,		// the firmware answers MIDI (loads)
		Input		// the machine takes input (lifecycle Ready)
	};

	enum class ArgType : uint8_t
	{
		Integer, Number, Text, Bool, Object, Array,
		IntegerOrNull,	// a value, or null to clear
		Any
	};

	struct Arg
	{
		const char* name = "";
		ArgType type = ArgType::Integer;
		double min = 0;
		double max = 0;
		bool optional = false;
	};

	struct Command
	{
		const char* op = "";
		Owner owner = Owner::Core;
		Gate gate = Gate::Input;
		int kind = -1;				// the document kind the command edits (the model's enum), -1: none
		std::vector<Arg> args;
		const char* help = "";
		const char* group = "";		// the model's grouping ("library": the MD's kit library and pattern chooser)
	};

	class CommandTable
	{
	public:
		explicit CommandTable(std::vector<Command> _commands) : m_commands(std::move(_commands)) {}

		const Command* find(const std::string& _op) const;
		const std::vector<Command>& commands() const { return m_commands; }

		// The problems with _message's arguments, one line each ("p: missing number",
		// "p: 200 is outside 0..127"). Every message may carry id, g (a gesture) and force.
		static std::vector<std::string> check(const Command& _command, const elektronData::json::Value& _message);

		// The JSON Schema of every command: {"oneOf":[{"properties":{"op":{"const":...},...}}]}.
		elektronData::json::Value schema() const;

	private:
		std::vector<Command> m_commands;
	};

	const char* ownerName(Owner _o);
	// The op of a page message, "" when there is none.
	std::string opOf(const elektronData::json::Value& _message);
}
