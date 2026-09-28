#include "mdMidiLearnCommands.h"

#include "deskCore/deskCommands.h"

#include "jucePluginLib/midiLearnTranslator.h"
#include "jucePluginLib/processor.h"

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		int intOf(const json::Value& _m, const char* _key, const int _default = -1)
		{
			const auto* v = _m.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}
	}

	MidiLearnCommands::MidiLearnCommands(pluginLib::Processor& _processor, Model _model, std::function<void(const Value&)> _publish)
		: m_processor(_processor), m_model(std::move(_model)), m_publish(std::move(_publish))
	{
	}

	MidiLearnCommands::~MidiLearnCommands()
	{
		if(auto* translator = m_processor.getMidiLearnTranslator())
		{
			if(translator->isLearning())
				translator->cancelLearning();
			translator->onMappingLearned = nullptr;
		}
	}

	MidiLearnCommands::Target MidiLearnCommands::targetOf(const Value& _message) const
	{
		return {intOf(_message, "t"), m_model.pages ? intOf(_message, "pg") : -1, intOf(_message, "i", m_model.pages ? 0 : -1)};
	}

	void MidiLearnCommands::reply(const Value& _message, const bool _ok, const std::string& _note) const
	{
		m_publish(deskCore::resultMessage(_message, _ok ? std::vector<std::string>{} : std::vector<std::string>{_note}, _note));
	}

	// One function per action (deskHost's table); the table checked the arguments' types and ranges,
	// the model's record says which tracks and parameters can be learned.
	void MidiLearnCommands::handle(const deskHost::Action _action, const Value& _message)
	{
		using A = deskHost::Action;
		auto* translator = m_processor.getMidiLearnTranslator();
		if(!translator)
		{
			reply(_message, false, "MIDI learn is not available in this build");
			return;
		}
		const auto target = targetOf(_message);
		const auto name = target.t >= 0 && target.t < m_model.tracks ? m_model.parameter(target) : std::string();
		const auto save = [&](const pluginLib::MidiLearnPreset& _preset)
		{
			translator->setPreset(_preset);
			m_processor.saveDefaultMidiLearnPreset();
		};
		switch(_action)
		{
		case A::LearnStart:
		{
			if(name.empty())
			{
				reply(_message, false, m_model.refusal);
				return;
			}
			translator->startLearning(name);
			m_learning = target;
			const auto part = static_cast<uint8_t>(target.t);
			translator->onMappingLearned = [this, translator, part](const pluginLib::MidiLearnMapping& _learned)
			{
				auto mapping = _learned;
				mapping.part = part;
				auto preset = translator->getPreset();
				preset.addMapping(mapping);
				translator->setPreset(preset);
				m_processor.saveDefaultMidiLearnPreset();
				translator->onMappingLearned = nullptr;
				publish();
			};
			reply(_message, true, {});
			break;
		}
		case A::LearnAdd:
		{
			// A mapping made without LEARN: a controller row x a track's parameter.
			const int cc = intOf(_message, "cc");
			const int ch = intOf(_message, "ch", pluginLib::MidiLearnMapping::AllChannels);
			if(name.empty() || (ch > 15 && ch != pluginLib::MidiLearnMapping::AllChannels))
			{
				reply(_message, false, "learnAdd: a learnable parameter of a track, on channel 1-16 or all");
				return;
			}
			pluginLib::MidiLearnMapping mapping;
			mapping.type = pluginLib::MidiLearnMapping::Type::ControlChange;
			mapping.controller = static_cast<uint8_t>(cc);
			mapping.channel = static_cast<uint8_t>(ch);
			mapping.part = static_cast<uint8_t>(target.t);
			mapping.paramName = name;
			auto preset = translator->getPreset();
			preset.addMapping(mapping);
			save(preset);
			reply(_message, true, "CC " + std::to_string(cc) + " -> track " + std::to_string(target.t + 1));
			break;
		}
		case A::LearnSetCc:
		{
			// A controller row's CC number changed: its mappings follow it.
			const int from = intOf(_message, "from"), to = intOf(_message, "to");
			auto preset = translator->getPreset();
			for(auto& m : preset.getMappings())
				if(m.type == pluginLib::MidiLearnMapping::Type::ControlChange && m.controller == from
					&& m.channel == pluginLib::MidiLearnMapping::AllChannels)
					m.controller = static_cast<uint8_t>(to);
			save(preset);
			reply(_message, true, {});
			break;
		}
		case A::LearnCancel:
			translator->cancelLearning();
			translator->onMappingLearned = nullptr;
			reply(_message, true, {});
			break;
		case A::LearnRemove:
		case A::LearnInvert:
		{
			auto preset = translator->getPreset();
			const auto index = static_cast<size_t>(intOf(_message, "index"));
			if(index >= preset.getMappings().size())
			{
				reply(_message, false, "learn: no such mapping");
				return;
			}
			if(_action == A::LearnRemove)
				preset.removeMapping(index);
			else
				preset.getMappings()[index].invert ^= true;
			save(preset);
			reply(_message, true, {});
			break;
		}
		default:
			reply(_message, false, "not a MIDI learn command");
			return;
		}
		publish();
	}

	void MidiLearnCommands::publish()
	{
		auto* translator = m_processor.getMidiLearnTranslator();
		json::Value doc = json::Value::object();
		json::Value mappings = json::Value::array();
		if(translator)
		{
			const auto& list = translator->getPreset().getMappings();
			for(size_t n = 0; n < list.size(); ++n)
			{
				const auto& m = list[n];
				const auto target = m_model.targetOf(m.paramName).value_or(Target{});
				json::Value v = json::Value::object();
				v.set("index", static_cast<int>(n));
				v.set("t", m.part == pluginLib::MidiLearnMapping::AutoPart ? -1 : static_cast<int>(m.part));
				if(m_model.pages)
					v.set("pg", target.pg);
				v.set("i", target.i);
				v.set("name", m.paramName);
				v.set("cc", static_cast<int>(m.controller));
				v.set("ch", static_cast<int>(m.channel));
				v.set("mode", pluginLib::MidiLearnMapping::modeToString(m.mode));
				v.set("invert", m.invert);
				mappings.push(std::move(v));
			}
		}
		doc.set("mappings", std::move(mappings));
		if(translator && translator->isLearning())
		{
			json::Value l = json::Value::object();
			l.set("name", translator->getLearningParamName());
			l.set("t", m_learning.t);
			if(m_model.pages)
				l.set("pg", m_learning.pg);
			l.set("i", m_learning.i);
			doc.set("learning", std::move(l));
		}
		else
			doc.set("learning", json::Value());
		json::Value m = json::Value::object();
		m.set("type", "learn");
		m.set("doc", std::move(doc));
		m_publish(m);
	}
}
