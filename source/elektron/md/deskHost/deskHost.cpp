#include "deskHost.h"

namespace deskHost
{
	const Table& commands()
	{
		using deskCore::Arg;
		using deskCore::ArgType;
		using deskCore::CoreOp;
		using deskCore::Gate;
		using deskCore::Owner;
		const Arg t{"t", ArgType::Integer, 0, 15};
		const Arg pg{"pg", ArgType::Integer, 0, 7, true};
		const Arg i{"i", ArgType::Integer, 0, 24, true};
		const Arg index{"index", ArgType::Integer, 0, 1e6};
		const Arg on{"on", ArgType::Bool, 0, 0, true};
		const auto row = [](const char* _op, std::vector<Arg> _args, const char* _help, const Action _a)
		{
			return deskCore::Command<Action>{_op, Owner::Host, Gate::None, -1, std::move(_args), _help, CoreOp::Edit, 0, _a};
		};
		static const Table table({
			row("engine", {{"kind", ArgType::Text}}, "an entry of the engine map (machine.engines)", Action::Engine),
			row("recheckFirmware", {}, "look for the ROM again", Action::RecheckFirmware),
			row("revealRomFolder", {}, "", Action::RevealRomFolder),
			row("midi", {{"b", ArgType::Array}}, "a channel message from the page: [status 0x80-0xef, data, data]", Action::Midi),
			row("openMenu", {}, "the editor's menu", Action::Menu),
			row("learnStart", {t, pg, i}, "MIDI learn a track's parameter", Action::LearnStart),
			row("learnAdd", {{"cc", ArgType::Integer, 0, 127}, t, pg, i, {"ch", ArgType::Integer, 0, 255, true}},
				"a CC mapping without learning", Action::LearnAdd),
			row("learnSetCc", {{"from", ArgType::Integer, 0, 127}, {"to", ArgType::Integer, 0, 127}},
				"a controller row's CC changed: its mappings follow", Action::LearnSetCc),
			row("learnCancel", {}, "", Action::LearnCancel),
			row("learnRemove", {index}, "", Action::LearnRemove),
			row("learnInvert", {index}, "", Action::LearnInvert),
			row("audio", {}, "the standalone's audio and MIDI devices", Action::AudioPublish),
			row("audioSet", {{"set", ArgType::Text, 0, 0, true}, {"do", ArgType::Text, 0, 0, true}}, "", Action::AudioSet),
			row("audioMeter", {on}, "", Action::AudioMeter),
		});
		return table;
	}

	elektronData::json::Value contractCommands(const elektronData::json::Value& _modelSchema)
	{
		auto out = _modelSchema;
		auto* one = out.find("oneOf");
		if(!one)
			return out;
		const auto host = commands().schema();
		auto merged = elektronData::json::Value::array();
		for(const auto& c : one->asArray())
			merged.push(c);
		for(const auto& c : host.find("oneOf")->asArray())
			merged.push(c);
		*one = std::move(merged);
		out.put("description", "Generated from the model's command table and the plug-in's (deskHost), P6: every command the page may send.");
		return out;
	}

	bool isWindowAction(const Action _a)
	{
		return _a == Action::Menu || _a == Action::AudioPublish || _a == Action::AudioSet || _a == Action::AudioMeter;
	}
}
