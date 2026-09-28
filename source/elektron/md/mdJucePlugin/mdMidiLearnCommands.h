#pragma once

#include "deskHost/deskHost.h"

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
	// editors). Which parameters can be learned is data: the model's list of targets {pg, i} with
	// their plug-in parameter names (the Machinedrum has no pages: pg is -1 there).
	//   learnStart {t, [pg,] i}, learnAdd {cc, t, [pg,] i, [ch]}, learnSetCc {from, to},
	//   learnCancel, learnRemove {index}, learnInvert {index}
	// The published document: {"type":"learn","doc":{"mappings":[...],"learning":{...}|null,
	// "limits":{"tracks":n,"params":[{[pg,] i, name}]}}}; the limits are exactly the learnable targets.
	class MidiLearnCommands
	{
	public:
		using Value = elektronData::json::Value;

		struct Target
		{
			int t = -1, pg = -1, i = -1;
		};

		// One learnable parameter of a track (t is left -1).
		struct Param
		{
			Target at;
			std::string name;		// the plug-in parameter
		};

		struct Model
		{
			bool pages = false;			// targets carry pg (Monomachine)
			int tracks = 16;
			std::vector<Param> params;
			const char* refusal = "learn: expected a track and a parameter";
		};

		MidiLearnCommands(pluginLib::Processor& _processor, Model _model, std::function<void(const Value&)> _publish);
		~MidiLearnCommands();

		MidiLearnCommands(const MidiLearnCommands&) = delete;
		MidiLearnCommands& operator=(const MidiLearnCommands&) = delete;

		// A learn command (deskHost's LearnStart..LearnInvert): the reply (a result message) and the new
		// document are published.
		void handle(deskHost::Action _action, const Value& _message);
		void publish();

	private:
		Target targetOf(const Value& _message) const;
		// The plug-in parameter of a target, "" when it cannot be learned.
		std::string parameterOf(const Target& _t) const;
		// The target of a parameter name (for the published mappings).
		std::optional<Target> targetOfName(const std::string& _name) const;
		Value limits() const;
		void reply(const Value& _message, bool _ok, const std::string& _note) const;

		pluginLib::Processor& m_processor;
		Model m_model;
		std::function<void(const Value&)> m_publish;
		Target m_learning;
	};
}
