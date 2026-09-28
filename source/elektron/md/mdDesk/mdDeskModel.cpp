#include "mdDeskModel.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdValidate.h"

namespace mdDesk
{
	namespace ed = elektronData;
	using ed::json::Value;
	using deskCore::Arg;
	using deskCore::ArgType;
	using deskCore::Gate;
	using deskCore::Owner;

	const char* kindName(const DocKind _k)
	{
		switch(_k)
		{
		case DocKind::Pattern: return "pattern";
		case DocKind::Kit: return "kit";
		case DocKind::Song: return "song";
		case DocKind::Global: return "global";
		}
		return "";
	}

	std::optional<DocKind> kindFromName(const std::string& _name)
	{
		for(const auto k : {DocKind::Pattern, DocKind::Kit, DocKind::Song, DocKind::Global})
			if(_name == kindName(k))
				return k;
		return {};
	}

	Value documentToJson(const Document& _doc)
	{
		return std::visit([](const auto& _v) -> Value
		{
			using T = std::decay_t<decltype(_v)>;
			if constexpr(std::is_same_v<T, ed::MdPattern>)
				return ed::patternToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MdKit>)
				return ed::kitToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MdSong>)
				return ed::songToJson(_v);
			else
				return ed::globalToJson(_v);
		}, _doc);
	}

	void MdModel::erase(Documents& _docs, const Ref& _ref)
	{
		switch(_ref.kind)
		{
		case DocKind::Pattern: _docs.patterns.erase(_ref.slot); break;
		case DocKind::Kit: _docs.kits.erase(_ref.slot); break;
		case DocKind::Song: _docs.songs.erase(_ref.slot); break;
		case DocKind::Global:
			if(_docs.global && _docs.global->position == _ref.slot)
				_docs.global.reset();
			break;
		}
	}

	MdModel::EditResult MdModel::apply(const Documents& _docs, const Value& _command, const Clipboard& _clip, const Context& _context)
	{
		const auto* op = _command.find("op");
		if(!op || !op->isString() || op->asString() != "set")
			return mdDesk::apply(_docs, _command, _clip, _context);
		// A whole document: validated like every edit, then it replaces the one the desk holds.
		EditResult r;
		const auto* kindValue = _command.find("kind");
		const auto* doc = _command.find("doc");
		const auto kind = kindValue && kindValue->isString() ? kindFromName(kindValue->asString()) : std::nullopt;
		if(!kind || !doc)
		{
			r.errors.emplace_back("set: expected a kind (pattern, kit, song, global) and a doc");
			return r;
		}
		std::optional<Document> after;
		switch(*kind)
		{
		case DocKind::Pattern: if(auto v = ed::patternFromJson(*doc, r.errors)) after = *v; break;
		case DocKind::Kit: if(auto v = ed::kitFromJson(*doc, r.errors)) after = *v; break;
		case DocKind::Song: if(auto v = ed::songFromJson(*doc, r.errors)) after = *v; break;
		case DocKind::Global: if(auto v = ed::globalFromJson(*doc, r.errors)) after = *v; break;
		}
		if(!after)
			return r;
		if(auto problems = std::visit([](const auto& _v) { return ed::validate(_v); }, *after); !problems.empty())
		{
			r.errors = std::move(problems);
			return r;
		}
		const auto before = _docs.get(refOf(*after));
		if(!before)
		{
			r.errors.push_back(std::string(kindName(*kind)) + " " + std::to_string(refOf(*after).slot + 1) + " is not loaded yet");
			return r;
		}
		if(!(*before == *after))
			r.changes.push_back({*before, *after, *kind == DocKind::Kit && (!_context.currentKit || *_context.currentKit != refOf(*after).slot)});
		return r;
	}

	Value MdModel::docMessage(const Ref& _ref, const Document& _doc, const bool _pending, const deskCore::Source _source)
	{
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", kindName(_ref.kind));
		m.set("slot", _ref.slot);
		m.set("pending", _pending);
		m.set("source", deskCore::sourceName(_source));
		m.set("doc", documentToJson(_doc));
		return m;
	}

	void MdModel::decorate(Value& _machine, const History& _history, const deskCore::Machine<MdModel>&)
	{
		auto* desk = _machine.find("desk");
		if(!desk)
			return;
		desk->put("undo", _history.canUndo());
		desk->put("redo", _history.canRedo());
		desk->put("undoCount", static_cast<int>(_history.size()));
		desk->put("redoCount", static_cast<int>(_history.redoSize()));
	}

	const deskCore::CommandTable& commandTable()
	{
		constexpr auto P = static_cast<int>(DocKind::Pattern);
		constexpr auto K = static_cast<int>(DocKind::Kit);
		constexpr auto S = static_cast<int>(DocKind::Song);
		constexpr auto G = static_cast<int>(DocKind::Global);
		const Arg p{"p", ArgType::Integer, 0, 127};
		const Arg k{"k", ArgType::Integer, 0, 63};
		const Arg s{"s", ArgType::Integer, 0, 31};
		const Arg t{"t", ArgType::Integer, 0, 15};
		const Arg step{"s", ArgType::Integer, 0, 63};
		const Arg i24{"i", ArgType::Integer, 0, 23};
		const Arg v127{"v", ArgType::Integer, 0, 127};
		const Arg on{"on", ArgType::Bool, 0, 0, true};
		const Arg row{"i", ArgType::Integer, 0, 255};
		const Arg any{"v", ArgType::Any, 0, 0, true};
		const auto opt = [](Arg _a) { _a.optional = true; return _a; };
		static const deskCore::CommandTable table({
			// ---- the core: documents ----
			{"ready", Owner::Core, Gate::None, -1, {}, "the page is up: everything is published once more"},
			{"undo", Owner::Core, Gate::Input, -1, {}, "undo the last step (a drag is one step)"},
			{"redo", Owner::Core, Gate::Input, -1, {}, ""},
			{"set", Owner::Core, Gate::Input, -1, {{"kind", ArgType::Text}, {"doc", ArgType::Object}}, "a whole document as the intent"},
			{"trig", Owner::Core, Gate::Input, P, {p, t, step, on}, "a trig on or off (toggles without on)"},
			{"accent", Owner::Core, Gate::Input, P, {p, t, step, on}, ""},
			{"slide", Owner::Core, Gate::Input, P, {p, t, step, on}, ""},
			{"lock", Owner::Core, Gate::Input, P, {p, t, i24, step, {"v", ArgType::IntegerOrNull, 0, 127}}, "a parameter lock; v null clears it"},
			{"clearLane", Owner::Core, Gate::Input, P, {p, t, i24}, ""},
			{"length", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 1, 64}}, ""},
			{"totalLength", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 16, 64}}, ""},
			{"speed", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Text}}, "1X, 2X, 3/4X, 3/2X"},
			{"swing", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 50, 80}}, "percent"},
			{"accentAmount", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 0, 15}}, ""},
			{"patternKit", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 0, 63}}, ""},
			{"clearSteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}, {"to", ArgType::Integer, 1, 64}}, ""},
			{"copySteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}, {"to", ArgType::Integer, 1, 64}}, ""},
			{"pasteSteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}}, ""},
			{"patCopy", Owner::Core, Gate::Input, P, {p}, "the pattern chooser"},
			{"patPaste", Owner::Core, Gate::Input, P, {p}, ""},
			{"patCopyTo", Owner::Core, Gate::Input, P, {{"from", ArgType::Integer, 0, 127}, {"to", ArgType::Integer, 0, 127}}, "drag-copy"},
			{"patClear", Owner::Core, Gate::Input, P, {p}, ""},
			{"param", Owner::Core, Gate::Input, K, {k, t, i24, v127}, "a kit parameter 0-23"},
			{"level", Owner::Core, Gate::Input, K, {k, t, v127}, ""},
			{"machine", Owner::Core, Gate::Input, K, {k, t, {"model", ArgType::Integer, 0, 255}, {"keepFx", ArgType::Bool, 0, 0, true}}, ""},
			{"lfo", Owner::Core, Gate::Input, K, {k, t, {"field", ArgType::Text}, {"v", ArgType::Integer, 0, 23}}, "track, param, shape1, shape2, update"},
			{"group", Owner::Core, Gate::Input, K, {k, t, {"kind", ArgType::Text}, {"target", ArgType::IntegerOrNull, 0, 15, true}}, "mute or trig group"},
			{"masterFx", Owner::Core, Gate::Input, K, {k, {"fx", ArgType::Text}, {"i", ArgType::Integer, 0, 7}, v127}, ""},
			{"kitName", Owner::Core, Gate::Input, K, {k, {"name", ArgType::Text}}, "the kit that plays, live (0x55)"},
			{"copySound", Owner::Core, Gate::Input, K, {k, t}, ""},
			{"pasteSound", Owner::Core, Gate::Input, K, {k, t}, ""},
			{"clearSound", Owner::Core, Gate::Input, K, {k, t}, ""},
			{"kitCopy", Owner::Core, Gate::Input, K, {k}, "the kit library"},
			{"kitPaste", Owner::Core, Gate::Input, K, {k}, ""},
			{"kitCopyTo", Owner::Core, Gate::Input, K, {{"from", ArgType::Integer, 0, 63}, {"to", ArgType::Integer, 0, 63}}, ""},
			{"kitClear", Owner::Core, Gate::Input, K, {k}, ""},
			{"kitRename", Owner::Core, Gate::Input, K, {k, {"name", ArgType::Text}}, ""},
			{"rowSet", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object}}, "song rows"},
			{"rowInsert", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object, 0, 0, true}}, ""},
			{"rowDelete", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"rowMove", Owner::Core, Gate::Input, S, {s, {"from", ArgType::Integer, 0, 255}, {"to", ArgType::Integer, 0, 255}}, ""},
			{"copyRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"pasteRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"route", Owner::Core, Gate::Input, G, {t, {"out", ArgType::Text}}, "a track's output: A-F or MAIN"},
			{"tempo", Owner::Core, Gate::Input, G, {{"bpm", ArgType::Number, 30, 300}}, ""},
			{"extended", Owner::Core, Gate::Input, G, {{"on", ArgType::Bool}}, "EXTENDED or CLASSIC"},
			{"globalSet", Owner::Core, Gate::Input, G, {{"field", ArgType::Text}, opt(on), any, {"note", ArgType::Integer, 0, 127, true},
				{"target", ArgType::IntegerOrNull, 0, 31, true}}, "a GLOBAL setting by name"},
			// ---- the machine ----
			{"load", Owner::Machine, Gate::Midi, -1, {{"kind", ArgType::Text}, {"slot", ArgType::Integer, 0, 127}}, "read a document now"},
			{"select", Owner::Machine, Gate::Input, -1, {p, {"now", ArgType::Bool, 0, 0, true}, {"chainOk", ArgType::Bool, 0, 0, true}},
				"LOAD PATTERN (queued while playing; now: STOP, LOAD, PLAY)"},
			{"saveKit", Owner::Machine, Gate::Input, -1, {}, "SAVE KIT to the current slot"},
			{"reloadKit", Owner::Machine, Gate::Input, -1, {}, "LOAD KIT of the current slot"},
			{"kitLoad", Owner::Machine, Gate::Input, -1, {k}, "LOAD KIT"},
			{"kitSaveAs", Owner::Machine, Gate::Input, -1, {k}, "SAVE KIT n"},
			{"record", Owner::Machine, Gate::Input, -1, {}, "REC as on the machine"},
			{"recTrig", Owner::Machine, Gate::Input, -1, {t}, "a TRIG key while recording"},
			{"chain", Owner::Machine, Gate::Input, -1, {{"patterns", ArgType::Array}}, "BANK held + TRIG keys"},
			{"chainClear", Owner::Machine, Gate::Input, -1, {}, ""},
			{"globalSlot", Owner::Machine, Gate::Input, -1, {{"slot", ArgType::Integer, 0, 7}}, "the active GLOBAL slot"},
			{"selectSong", Owner::Machine, Gate::Input, -1, {s}, "LOAD SONG (stopped)"},
			{"reloadSong", Owner::Machine, Gate::Input, -1, {}, "stop, load, play"},
			{"sampleName", Owner::Machine, Gate::Input, -1, {{"slot", ArgType::Integer, 0, 47}, {"name", ArgType::Text}}, "0x73"},
			{"play", Owner::Machine, Gate::Input, -1, {}, ""},
			{"stop", Owner::Machine, Gate::Input, -1, {}, ""},
			{"mute", Owner::Machine, Gate::Input, -1, {t, on}, ""},
			// ---- the editor's setup ----
			{"modSet", Owner::Setup, Gate::None, -1, {{"doc", ArgType::Object}}, "the app modulators (md-desk/modulators)"},
			{"knobs", Owner::Setup, Gate::None, -1, {{"ccs", ArgType::Array}}, "the eight knob rows' CCs"},
			// ---- the plug-in ----
			{"engine", Owner::Host, Gate::None, -1, {{"kind", ArgType::Text}}, "an entry of the engine map: emu, hw"},
			{"recheckFirmware", Owner::Host, Gate::None, -1, {}, ""},
			{"revealRomFolder", Owner::Host, Gate::None, -1, {}, ""},
			{"openMenu", Owner::Host, Gate::None, -1, {}, "the editor's menu"},
			{"learnStart", Owner::Host, Gate::None, -1, {t, {"i", ArgType::Integer, 0, 24}}, "MIDI learn"},
			{"learnAdd", Owner::Host, Gate::None, -1, {{"cc", ArgType::Integer, 0, 127}, t, {"i", ArgType::Integer, 0, 24},
				{"ch", ArgType::Integer, 0, 255, true}}, ""},
			{"learnSetCc", Owner::Host, Gate::None, -1, {{"from", ArgType::Integer, 0, 127}, {"to", ArgType::Integer, 0, 127}}, ""},
			{"learnCancel", Owner::Host, Gate::None, -1, {}, ""},
			{"learnRemove", Owner::Host, Gate::None, -1, {{"index", ArgType::Integer, 0, 1e6}}, ""},
			{"learnInvert", Owner::Host, Gate::None, -1, {{"index", ArgType::Integer, 0, 1e6}}, ""},
			{"audio", Owner::Host, Gate::None, -1, {}, "the standalone's audio and MIDI devices"},
			{"audioSet", Owner::Host, Gate::None, -1, {{"set", ArgType::Text, 0, 0, true}, {"do", ArgType::Text, 0, 0, true}}, ""},
			{"audioMeter", Owner::Host, Gate::None, -1, {on}, ""},
		});
		return table;
	}
}
