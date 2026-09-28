#pragma once

#include "elektronData/json.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pluginLib
{
	class Processor;
}

namespace mdJucePlugin
{
	// The page's MIDI learn commands on the plug-in's MIDI learn (P6: one implementation for both
	// editors). Which parameters can be learned is data: a target {t, pg, i} names a plug-in
	// parameter through the model's table (the Machinedrum has no pages: pg is -1 there).
	//   learnStart {t, [pg,] i}, learnAdd {cc, t, [pg,] i, [ch]}, learnSetCc {from, to},
	//   learnCancel, learnRemove {index}, learnInvert {index}
	// The published document: {"type":"learn","doc":{"mappings":[...],"learning":{...}|null}}.
	class MidiLearnCommands
	{
	public:
		using Value = elektronData::json::Value;

		struct Target
		{
			int t = -1, pg = -1, i = -1;
		};

		struct Model
		{
			bool pages = false;			// targets carry pg (Monomachine)
			int tracks = 16;
			// The plug-in parameter of a target, "" when it cannot be learned.
			std::function<std::string(const Target&)> parameter;
			// The target of a parameter name (for the published mappings), t left -1.
			std::function<std::optional<Target>(const std::string&)> targetOf;
			const char* refusal = "learn: expected a track and a parameter";
		};

		MidiLearnCommands(pluginLib::Processor& _processor, Model _model, std::function<void(const Value&)> _publish);
		~MidiLearnCommands();

		MidiLearnCommands(const MidiLearnCommands&) = delete;
		MidiLearnCommands& operator=(const MidiLearnCommands&) = delete;

		static bool isLearnCommand(const std::string& _op) { return _op.rfind("learn", 0) == 0; }

		// Handles a learn command: the reply (a result message) and the new document are published.
		void handle(const Value& _message);
		void publish();

	private:
		Target targetOf(const Value& _message) const;
		void reply(const Value& _message, bool _ok, const std::string& _note) const;

		pluginLib::Processor& m_processor;
		Model m_model;
		std::function<void(const Value&)> m_publish;
		Target m_learning;
	};
}
