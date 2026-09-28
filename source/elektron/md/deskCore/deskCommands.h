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
	// valid, the core column which core action. The model's table is the vocabulary only: which
	// adapter function runs a machine command is the adapter's own map, and the plug-in's
	// commands are the plug-in's table (a CommandTable<its action type>, whose handler column
	// names the action). The contract's $defs/command is generated from the tables.
	enum class Owner : uint8_t
	{
		Core,		// documents: edits (pure apply), undo/redo, ready
		Machine,	// the adapter: transport, selection, loads, slot actions on the machine
		Setup,		// the editor's own setup (app modulators, knob rows)
		Host		// the plug-in: MIDI learn, the ROM folder, the engine map, menus
	};

	// What the core does for an Owner::Core command.
	enum class CoreOp : uint8_t
	{
		Edit,		// a pure edit (the model's apply)
		Set,		// a whole document as the intent (the model's setDocument)
		Ready,		// the page is up: everything once more
		Undo,
		Redo
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
		Bytes,			// a list of `length` integers in min..max (a MIDI message)
		Any
	};

	struct Arg
	{
		const char* name = "";
		ArgType type = ArgType::Integer;
		double min = 0;
		double max = 0;
		bool optional = false;
		std::vector<const char*> oneOf;	// Text: the values allowed (empty: any text)
		int length = -1;				// Bytes: how many
	};

	// One row. H is what the table's owner acts with (nothing for a model's table; the plug-in's
	// action for the plug-in's table).
	template<typename H = std::nullptr_t>
	struct Command
	{
		const char* op = "";
		Owner owner = Owner::Core;
		Gate gate = Gate::Input;
		int kind = -1;				// the document kind the command edits (the model's enum), -1: none
		std::vector<Arg> args;
		const char* help = "";
		CoreOp core = CoreOp::Edit;	// Owner::Core: which core action
		int group = 0;				// the model's grouping (the MD's kit library and pattern chooser), 0: none
		H handler{};				// the table owner's action (the plug-in's table)
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

		// Every message may carry id, g (a gesture) and force besides its arguments, and nothing else.
		static std::vector<std::string> check(const Row& _command, const elektronData::json::Value& _message)
		{
			std::vector<std::string> errors;
			for(const auto& a : _command.args)
				checkArg(a, _message.find(a.name), errors);
			if(_message.isObject())
				for(const auto& [k, v] : _message.asObject())
				{
					bool declared = k == "op" || k == "id" || k == "g" || k == "force";
					for(const auto& a : _command.args)
						declared = declared || k == a.name;
					if(!declared)
						errors.push_back(k + ": not an argument of " + _command.op);
				}
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
