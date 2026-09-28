#include "mdDeskModel.h"

#include "deskCore/deskKinds.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdNames.h"
#include "elektronData/mdValidate.h"

namespace mdDesk
{
	namespace ed = elektronData;
	using ed::json::Value;

	namespace
	{
		template<typename T>
		std::optional<Document> parsed(std::optional<T> _v, std::vector<std::string>& _errors)
		{
			if(!_v)
				return {};
			auto problems = ed::validate(*_v);
			if(!problems.empty())
			{
				_errors.insert(_errors.end(), problems.begin(), problems.end());
				return {};
			}
			return Document(*_v);
		}

		int ownSlot(const Document&, const int _refSlot) { return _refSlot; }
	}

	// The Machinedrum's document kinds, once (P6): names, counts, dump sizes (DIN timeouts), JSON.
	const std::vector<deskCore::KindSpec<MdModel>>& MdModel::kinds()
	{
		static const std::vector<deskCore::KindSpec<MdModel>> k{
			{DocKind::Pattern, "pattern", 128, 5410, true,
				[](const Document& _d) { return ed::patternToJson(std::get<ed::MdPattern>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const Context&) { return parsed(ed::patternFromJson(_v, _e), _e); },
				ownSlot},
			{DocKind::Kit, "kit", 64, 1233, true,
				[](const Document& _d) { return ed::kitToJson(std::get<ed::MdKit>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const Context&) { return parsed(ed::kitFromJson(_v, _e), _e); },
				ownSlot},
			{DocKind::Song, "song", 32, 3100, true,
				[](const Document& _d) { return ed::songToJson(std::get<ed::MdSong>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const Context&) { return parsed(ed::songFromJson(_v, _e), _e); },
				ownSlot},
			{DocKind::Global, "global", 8, 197, true,
				[](const Document& _d) { return ed::globalToJson(std::get<ed::MdGlobal>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const Context&) { return parsed(ed::globalFromJson(_v, _e), _e); },
				ownSlot},
			// The kit that plays: one identity; its doc message names the kit it was loaded from. It
			// comes from memory (or its slot's dump), never by a request of its own.
			{DocKind::WorkingKit, "workingKit", 1, 0, false,
				[](const Document& _d) { return ed::kitToJson(std::get<WorkingKit>(_d).kit); },
				[](const Value& _v, std::vector<std::string>& _e, const Context& _c) -> std::optional<Document>
				{
					auto k = parsed(ed::kitFromJson(_v, _e), _e);
					if(!k)
						return {};
					const auto& kit = std::get<ed::MdKit>(*k);
					if(!_c.currentKit || kit.position != *_c.currentKit)
					{
						_e.emplace_back("set: the working kit is the kit that plays");
						return {};
					}
					return Document(WorkingKit{kit});
				},
				[](const Document& _d, int) { return static_cast<int>(std::get<WorkingKit>(_d).kit.position); }},
		};
		return k;
	}

	const char* kindName(const DocKind _k)
	{
		const auto* s = deskCore::kindSpec<MdModel>(_k);
		return s ? s->name : "";
	}

	std::optional<DocKind> kindFromName(const std::string& _name)
	{
		const auto* s = deskCore::kindSpec<MdModel>(_name);
		return s ? std::optional<DocKind>(s->kind) : std::nullopt;
	}

	Value documentToJson(const Document& _doc)
	{
		const auto* s = deskCore::kindSpec<MdModel>(refOf(_doc).kind);
		return s ? s->toJson(_doc) : Value();
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
		case DocKind::WorkingKit: _docs.working.reset(); break;
		}
	}

	MdModel::EditResult MdModel::apply(const Documents& _docs, const Value& _command, const Clipboard& _clip, const Context& _context)
	{
		return mdDesk::apply(_docs, _command, _clip, _context);
	}

	MdModel::EditResult MdModel::setDocument(const Documents& _docs, const Value& _command, const Context& _context)
	{
		return deskCore::setDocument<MdModel>(_docs, _command, _context);
	}

	Value MdModel::docMessage(const Ref& _ref, const Document& _doc, const bool _pending, const deskCore::Source _source)
	{
		return deskCore::docMessage<MdModel>(_ref, _doc, _pending, _source);
	}

	std::optional<Value> MdModel::clipboardDocument(const Clipboard& _clip)
	{
		Value c = Value::object();
		c.set("steps", _clip.steps.has_value());
		c.set("sound", _clip.sound.has_value());
		c.set("songRow", _clip.songRow.has_value());
		c.set("kit", _clip.kit ? Value(static_cast<int>(_clip.kit->position)) : Value());
		c.set("pattern", _clip.pattern ? Value(static_cast<int>(_clip.pattern->position)) : Value());
		return c;
	}

	Value MdModel::catalogue()
	{
		Value machines = Value::array();
		for(uint32_t model = 0; model < 256; ++model)
		{
			const auto name = ed::mdMachineName(model);
			if(name.empty())
				continue;
			Value m = Value::object();
			m.set("model", static_cast<int>(model));
			m.set("machine", name);
			m.set("family", ed::mdMachineFamily(model));
			Value params = Value::array();
			for(const auto* p : ed::mdMachineParamNames(model))
				params.push(p && *p ? Value(p) : Value());
			m.set("params", std::move(params));
			machines.push(std::move(m));
		}
		const auto names = [](const auto& _list)
		{
			Value a = Value::array();
			for(const auto* n : _list)
				a.push(n);
			return a;
		};
		// The named values the page shows (elektronData/mdNames.h, one source).
		Value enums = Value::object();
		enums.set("tempoMultipliers", names(ed::g_mdTempoMultipliers));
		enums.set("masterFx", names(ed::g_mdMasterFx));
		enums.set("outputs", names(ed::g_mdOutputs));
		enums.set("lfoFields", names(ed::g_mdLfoFields));
		enums.set("lfoUpdates", names(ed::g_mdLfoUpdates));
		Value lfoParams = Value::object();
		for(const auto& p : ed::g_mdLfoParams)
			lfoParams.set(p.name, p.index);
		enums.set("lfoParams", std::move(lfoParams));
		Value doc = Value::object();
		doc.set("schema", "md-desk/machines");
		doc.set("version", 1);
		doc.set("machines", std::move(machines));
		doc.set("enums", std::move(enums));
		return doc;
	}

	std::string MdModel::lifecycleText(const deskCore::Lifecycle _l)
	{
		using deskCore::Lifecycle;
		switch(_l)
		{
		case Lifecycle::Missing: return "No Machinedrum OS 1.63 firmware is running.";
		case Lifecycle::Unsupported: return "This firmware is not the Machinedrum OS 1.63 the editor knows.";
		case Lifecycle::Loading: return "The machine is being prepared. Editing starts when it is ready.";
		case Lifecycle::Booting: return "The machine is starting. Editing starts when it answers.";
		case Lifecycle::Animating: return "The machine is still starting: its start-up animation ignores keys. The editor takes input when it is over.";
		case Lifecycle::Ready: return "The machine takes input.";
		case Lifecycle::HwConnecting: return "Waiting for the Machinedrum to answer on the MIDI in and out.";
		case Lifecycle::HwLost: return "The Machinedrum has not answered for a while. Check the MIDI cables and that its SYSEX is on.";
		}
		return {};
	}

	// ---- the command table (MdModel::commands): every op the model knows, its owner, gate,
	// arguments; vocabulary only (which adapter function runs a machine command is the adapter's
	// own map; the plug-in's commands are deskHost's table) ----

	const CommandTable& MdModel::commands()
	{
		using deskCore::Arg;
		using deskCore::ArgType;
		using deskCore::Gate;
		using deskCore::CoreOp;
		using deskCore::Owner;
		constexpr auto P = static_cast<int>(DocKind::Pattern);
		constexpr auto K = static_cast<int>(DocKind::Kit);
		constexpr auto W = static_cast<int>(DocKind::WorkingKit);
		const auto list = [](const auto& _names) { return std::vector<const char*>(_names.begin(), _names.end()); };
		constexpr auto S = static_cast<int>(DocKind::Song);
		constexpr auto G = static_cast<int>(DocKind::Global);
		const auto slot = [](const char* _name, const DocKind _kind) { return deskCore::slotArg<MdModel>(_name, _kind); };
		const Arg p = slot("p", DocKind::Pattern);
		const Arg k = slot("k", DocKind::Kit);
		const Arg s = slot("s", DocKind::Song);
		const Arg t{"t", ArgType::Integer, 0, 15};
		const Arg step{"s", ArgType::Integer, 0, 63};
		const Arg i24{"i", ArgType::Integer, 0, 23};
		const Arg v127{"v", ArgType::Integer, 0, 127};
		const Arg on{"on", ArgType::Bool, 0, 0, true};
		const Arg row{"i", ArgType::Integer, 0, 255};
		const Arg any{"v", ArgType::Any, 0, 0, true};
		const auto opt = [](Arg _a) { _a.optional = true; return _a; };
		static const CommandTable table({
			// ---- the core: documents ----
			{"ready", Owner::Core, Gate::None, -1, {}, "the page is up: everything is published once more", CoreOp::Ready},
			{"undo", Owner::Core, Gate::Input, -1, {}, "undo the last step (a drag is one step)", CoreOp::Undo},
			{"redo", Owner::Core, Gate::Input, -1, {}, "", CoreOp::Redo},
			{"set", Owner::Core, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, deskCore::kindNames<MdModel>()}, {"doc", ArgType::Object}}, "a whole document as the intent", CoreOp::Set},
			{"trig", Owner::Core, Gate::Input, P, {p, t, step, on}, "a trig on or off (toggles without on)"},
			{"accent", Owner::Core, Gate::Input, P, {p, t, step, on}, ""},
			{"slide", Owner::Core, Gate::Input, P, {p, t, step, on}, ""},
			{"lock", Owner::Core, Gate::Input, P, {p, t, i24, step, {"v", ArgType::IntegerOrNull, 0, 127}}, "a parameter lock; v null clears it"},
			{"clearLane", Owner::Core, Gate::Input, P, {p, t, i24}, ""},
			{"length", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 1, 64}}, ""},
			{"totalLength", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 16, 64}}, ""},
			{"speed", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Text, 0, 0, false, list(ed::g_mdTempoMultipliers)}}, "the tempo multiplier"},
			{"swing", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 50, 80}}, "percent"},
			{"accentAmount", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 0, 15}}, ""},
			{"patternKit", Owner::Core, Gate::Input, P, {p, slot("v", DocKind::Kit)}, ""},
			{"clearSteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}, {"to", ArgType::Integer, 1, 64}}, ""},
			{"copySteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}, {"to", ArgType::Integer, 1, 64}}, ""},
			{"pasteSteps", Owner::Core, Gate::Input, P, {p, t, {"from", ArgType::Integer, 0, 63}}, ""},
			{"patCopy", Owner::Core, Gate::Input, P, {p}, "the pattern chooser", CoreOp::Edit, g_library},
			{"patPaste", Owner::Core, Gate::Input, P, {p}, "", CoreOp::Edit, g_library},
			{"patCopyTo", Owner::Core, Gate::Input, P, {slot("from", DocKind::Pattern), slot("to", DocKind::Pattern)}, "drag-copy", CoreOp::Edit, g_library},
			{"patClear", Owner::Core, Gate::Input, P, {p}, "", CoreOp::Edit, g_library},
			{"param", Owner::Core, Gate::Input, W, {k, t, i24, v127}, "a kit parameter 0-23"},
			{"level", Owner::Core, Gate::Input, W, {k, t, v127}, ""},
			{"machine", Owner::Core, Gate::Input, W, {k, t, {"model", ArgType::Integer, 0, 255}, {"keepFx", ArgType::Bool, 0, 0, true}}, ""},
			{"lfo", Owner::Core, Gate::Input, W, {k, t, {"field", ArgType::Text, 0, 0, false, list(ed::g_mdLfoFields)}, {"v", ArgType::Integer, 0, 23}}, "an LFO setting"},
			{"group", Owner::Core, Gate::Input, W, {k, t, {"kind", ArgType::Text, 0, 0, false, {"mute", "trig"}}, {"target", ArgType::IntegerOrNull, 0, 15, true}}, "a mute or trig group"},
			{"masterFx", Owner::Core, Gate::Input, W, {k, {"fx", ArgType::Text, 0, 0, false, list(ed::g_mdMasterFx)}, {"i", ArgType::Integer, 0, 7}, v127}, ""},
			{"kitName", Owner::Core, Gate::Input, W, {k, {"name", ArgType::Text}}, "the kit that plays, live (0x55)"},
			{"copySound", Owner::Core, Gate::Input, W, {k, t}, ""},
			{"pasteSound", Owner::Core, Gate::Input, W, {k, t}, ""},
			{"clearSound", Owner::Core, Gate::Input, W, {k, t}, ""},
			{"kitCopy", Owner::Core, Gate::Input, K, {k}, "the kit library", CoreOp::Edit, g_library},
			{"kitPaste", Owner::Core, Gate::Input, K, {k}, "", CoreOp::Edit, g_library},
			{"kitCopyTo", Owner::Core, Gate::Input, K, {slot("from", DocKind::Kit), slot("to", DocKind::Kit)}, "", CoreOp::Edit, g_library},
			{"kitClear", Owner::Core, Gate::Input, K, {k}, "", CoreOp::Edit, g_library},
			{"kitRename", Owner::Core, Gate::Input, K, {k, {"name", ArgType::Text}}, "", CoreOp::Edit, g_library},
			{"rowSet", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object}}, "song rows"},
			{"rowInsert", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object, 0, 0, true}}, ""},
			{"rowDelete", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"rowMove", Owner::Core, Gate::Input, S, {s, {"from", ArgType::Integer, 0, 255}, {"to", ArgType::Integer, 0, 255}}, ""},
			{"copyRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"pasteRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"route", Owner::Core, Gate::Input, G, {t, {"out", ArgType::Text, 0, 0, false, list(ed::g_mdOutputs)}}, "a track's output"},
			{"tempo", Owner::Core, Gate::Input, G, {{"bpm", ArgType::Number, 30, 300}}, ""},
			{"extended", Owner::Core, Gate::Input, G, {{"on", ArgType::Bool}}, "EXTENDED or CLASSIC"},
			{"globalSet", Owner::Core, Gate::Input, G, {{"field", ArgType::Text, 0, 0, false, list(ed::g_mdGlobalFields)}, opt(on), any, {"note", ArgType::Integer, 0, 127, true},
				{"target", ArgType::IntegerOrNull, 0, 31, true}}, "a GLOBAL setting by name"},
			// ---- the machine ----
			{"load", Owner::Machine, Gate::Midi, -1, {{"kind", ArgType::Text, 0, 0, false, deskCore::kindNames<MdModel>(true)}, {"slot", ArgType::Integer, 0, static_cast<double>(deskCore::maxLoadableSlots<MdModel>() - 1)}}, "read a document now"},
			{"select", Owner::Machine, Gate::Input, -1, {p, {"now", ArgType::Bool, 0, 0, true}},
				"LOAD PATTERN (queued while playing; now: STOP, LOAD, PLAY)"},
			{"saveKit", Owner::Machine, Gate::Input, -1, {}, "SAVE KIT to the current slot"},
			{"reloadKit", Owner::Machine, Gate::Input, -1, {}, "LOAD KIT of the current slot"},
			{"kitLoad", Owner::Machine, Gate::Input, -1, {k}, "LOAD KIT"},
			{"kitSaveAs", Owner::Machine, Gate::Input, -1, {k}, "SAVE KIT n"},
			{"record", Owner::Machine, Gate::Input, -1, {}, "REC as on the machine"},
			{"recTrig", Owner::Machine, Gate::Input, -1, {t}, "a TRIG key while recording"},
			{"chain", Owner::Machine, Gate::Input, -1, {{"patterns", ArgType::Array}}, "BANK held + TRIG keys"},
			{"chainClear", Owner::Machine, Gate::Input, -1, {}, ""},
			{"globalSlot", Owner::Machine, Gate::Input, -1, {slot("slot", DocKind::Global)}, "the active GLOBAL slot"},
			{"selectSong", Owner::Machine, Gate::Input, -1, {s}, "LOAD SONG (stopped)"},
			{"reloadSong", Owner::Machine, Gate::Input, -1, {}, "stop, load, play"},
			{"sampleName", Owner::Machine, Gate::Input, -1, {{"slot", ArgType::Integer, 0, 47}, {"name", ArgType::Text}}, "0x73"},
			{"play", Owner::Machine, Gate::Input, -1, {}, ""},
			{"stop", Owner::Machine, Gate::Input, -1, {}, ""},
			{"mute", Owner::Machine, Gate::Input, -1, {t, on}, ""},
			// ---- the editor's setup ----
			{"modSet", Owner::Setup, Gate::None, -1, {{"doc", ArgType::Object}}, "the app modulators (md-desk/modulators)"},
			{"knobs", Owner::Setup, Gate::None, -1, {{"ccs", ArgType::Array}}, "the eight knob rows' CCs"},
		});
		return table;
	}
}
