#pragma once

#include "elektronData/json.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deskCore
{
	// The page's command vocabulary as data (P6): one table per model, read by the one router
	// (deskCore::Desk): the owner column says who acts, the gate when, the arguments what is
	// valid, the handler column which function of the machine adapter runs, the host column
	// which plug-in action. The contract's $defs/command is generated from it.
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

	// What the plug-in does for an Owner::Host command (the same for both editors).
	enum class HostOp : uint8_t
	{
		None,
		Engine,				// switch the engine (the engine map)
		RecheckFirmware,
		RevealRomFolder,
		Midi,				// a channel message from the page
		Learn,				// MIDI learn (mdMidiLearnCommands)
		Audio,				// the standalone's audio and MIDI devices (the window's)
		Menu				// the editor's menu (the window's)
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

	// One row. H is the model's handler type (a member function of its machine adapter).
	template<typename H = std::nullptr_t>
	struct Command
	{
		const char* op = "";
		Owner owner = Owner::Core;
		Gate gate = Gate::Input;
		int kind = -1;				// the document kind the command edits (the model's enum), -1: none
		std::vector<Arg> args;
		const char* help = "";
		H handler{};				// Owner::Machine: the adapter's function
		HostOp host = HostOp::None;	// Owner::Host: the plug-in's action
		const char* group = "";		// the model's grouping ("library": the MD's kit library and pattern chooser)
	};

	const char* ownerName(Owner _o);
	// The op of a page message, "" when there is none.
	std::string opOf(const elektronData::json::Value& _message);
	// The one result message of the page protocol: {"type":"result","op","id","ok","errors","note"}.
	elektronData::json::Value resultMessage(const elektronData::json::Value& _command, const std::vector<std::string>& _errors,
		const std::string& _note);
	// The problems with one argument, one line each ("p: 200 is outside 0..127").
	void checkArg(const Arg& _arg, const elektronData::json::Value* _v, std::vector<std::string>& _errors);
	// The JSON Schema of one command.
	elektronData::json::Value commandSchema(const char* _op, Owner _owner, const char* _help, const std::vector<Arg>& _args);

	template<typename H = std::nullptr_t>
	class CommandTable
	{
	public:
		using Row = Command<H>;

		explicit CommandTable(std::vector<Row> _commands) : m_commands(std::move(_commands)) {}

		const Row* find(const std::string& _op) const
		{
			for(const auto& c : m_commands)
				if(_op == c.op)
					return &c;
			return nullptr;
		}
		const std::vector<Row>& commands() const { return m_commands; }

		// Every message may carry id, g (a gesture) and force besides its arguments.
		static std::vector<std::string> check(const Row& _command, const elektronData::json::Value& _message)
		{
			std::vector<std::string> errors;
			for(const auto& a : _command.args)
				checkArg(a, _message.find(a.name), errors);
			return errors;
		}

		// {"oneOf":[{"properties":{"op":{"const":...},...}}]}
		elektronData::json::Value schema() const
		{
			auto one = elektronData::json::Value::array();
			for(const auto& c : m_commands)
				one.push(commandSchema(c.op, c.owner, c.help, c.args));
			auto root = elektronData::json::Value::object();
			root.set("description", "Generated from the command table (deskCore::CommandTable::schema, P6): every command the page may send.");
			root.set("oneOf", std::move(one));
			return root;
		}

	private:
		std::vector<Row> m_commands;
	};
}
