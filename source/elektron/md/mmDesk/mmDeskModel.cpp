#include "mmDeskModel.h"

#include "elektronData/mmJson.h"
#include "elektronData/mmMachines.h"
#include "elektronData/mmValidate.h"

namespace mmDesk
{
	namespace ed = elektronData;
	using ed::json::Value;
	using deskCore::Arg;
	using deskCore::ArgType;
	using deskCore::Gate;
	using deskCore::Owner;

	Ref refOf(const Document& _doc)
	{
		struct Visitor
		{
			Ref operator()(const ed::MmPattern& _p) const { return {Kind::Pattern, _p.position}; }
			Ref operator()(const ed::MmKit& _k) const { return {Kind::Kit, _k.position}; }
			Ref operator()(const ed::MmSong& _s) const { return {Kind::Song, _s.position}; }
			Ref operator()(const ed::MmGlobal& _g) const { return {Kind::Global, _g.position}; }
			Ref operator()(const WorkingKit&) const { return {Kind::WorkingKit, 0}; }
		};
		return std::visit(Visitor{}, _doc);
	}

	const char* kindName(const Kind _k)
	{
		switch(_k)
		{
		case Kind::Pattern: return "pattern";
		case Kind::Kit: return "kit";
		case Kind::Song: return "song";
		case Kind::Global: return "global";
		case Kind::WorkingKit: return "workingKit";
		}
		return "";
	}

	std::optional<Kind> kindFromName(const std::string& _name)
	{
		for(const auto k : {Kind::Pattern, Kind::Kit, Kind::Song, Kind::Global, Kind::WorkingKit})
			if(_name == kindName(k))
				return k;
		return {};
	}

	Value documentToJson(const Document& _doc)
	{
		return std::visit([](const auto& _v) -> Value
		{
			using T = std::decay_t<decltype(_v)>;
			if constexpr(std::is_same_v<T, ed::MmPattern>)
				return ed::mmPatternToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MmKit>)
				return ed::mmKitToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MmSong>)
				return ed::mmSongToJson(_v);
			else if constexpr(std::is_same_v<T, WorkingKit>)
				return ed::mmKitToJson(_v.kit);
			else
				return ed::mmGlobalToJson(_v);
		}, _doc);
	}

	std::optional<Document> Documents::get(const Ref& _ref) const
	{
		const auto find = [&](const auto& _map) -> std::optional<Document>
		{
			const auto it = _map.find(_ref.slot);
			return it == _map.end() ? std::nullopt : std::optional<Document>(it->second);
		};
		switch(_ref.kind)
		{
		case Kind::Pattern: return find(patterns);
		case Kind::Kit: return find(kits);
		case Kind::Song: return find(songs);
		case Kind::Global: return find(globals);
		case Kind::WorkingKit: return working ? std::optional<Document>(*working) : std::nullopt;
		}
		return {};
	}

	void Documents::set(const Document& _doc)
	{
		struct Visitor
		{
			Documents& d;
			void operator()(const ed::MmPattern& _p) const { d.patterns[_p.position] = _p; }
			void operator()(const ed::MmKit& _k) const { d.kits[_k.position] = _k; }
			void operator()(const ed::MmSong& _s) const { d.songs[_s.position] = _s; }
			void operator()(const ed::MmGlobal& _g) const { d.globals[_g.position] = _g; }
			void operator()(const WorkingKit& _w) const { d.working = _w; }
		};
		std::visit(Visitor{*this}, _doc);
	}

	void Documents::erase(const Ref& _ref)
	{
		switch(_ref.kind)
		{
		case Kind::Pattern: patterns.erase(_ref.slot); break;
		case Kind::Kit: kits.erase(_ref.slot); break;
		case Kind::Song: songs.erase(_ref.slot); break;
		case Kind::Global: globals.erase(_ref.slot); break;
		case Kind::WorkingKit: working.reset(); break;
		}
	}

	EditResult setDocument(const Documents& _docs, const Value& _command, const EditContext& _context)
	{
		EditResult r;
		const auto* kindValue = _command.find("kind");
		const auto* doc = _command.find("doc");
		const auto kind = kindValue && kindValue->isString() ? kindFromName(kindValue->asString()) : std::nullopt;
		if(!kind)
		{
			r.errors.emplace_back("set: kind must be pattern, kit, workingKit, song or global");
			return r;
		}
		if(!doc)
		{
			r.errors.emplace_back("set: no doc");
			return r;
		}
		std::optional<Document> after;
		switch(*kind)
		{
		case Kind::Pattern: if(auto v = ed::mmPatternFromJson(*doc, r.errors)) after = *v; break;
		case Kind::Kit: if(auto v = ed::mmKitFromJson(*doc, r.errors)) after = *v; break;
		case Kind::Song: if(auto v = ed::mmSongFromJson(*doc, r.errors)) after = *v; break;
		case Kind::Global: if(auto v = ed::mmGlobalFromJson(*doc, r.errors)) after = *v; break;
		case Kind::WorkingKit:
			if(auto v = ed::mmKitFromJson(*doc, r.errors))
			{
				if(v->position != _context.currentKit)
				{
					r.errors.emplace_back("set: the working kit is the kit that plays");
					return r;
				}
				after = WorkingKit{*v};
			}
			break;
		}
		if(!after)
			return r;
		const auto problems = std::visit([](const auto& _v)
		{
			if constexpr(std::is_same_v<std::decay_t<decltype(_v)>, WorkingKit>)
				return ed::validate(_v.kit);
			else
				return ed::validate(_v);
		}, *after);
		if(!problems.empty())
		{
			r.errors = std::move(problems);
			return r;
		}
		const auto ref = refOf(*after);
		const auto before = _docs.get(ref);
		if(!before)
		{
			r.errors.push_back(std::string(kindName(*kind)) + " " + std::to_string(ref.slot + 1) + " is not loaded yet");
			return r;
		}
		if(!(*before == *after))
			r.changes.push_back({*before, *after});
		return r;
	}

	Value MmModel::docMessage(const Ref& _ref, const Document& _doc, const bool _pending, const deskCore::Source _source)
	{
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", kindName(_ref.kind));
		m.set("slot", _ref.kind == Kind::WorkingKit ? std::get<WorkingKit>(_doc).kit.position : _ref.slot);
		m.set("pending", _pending);
		m.set("source", deskCore::sourceName(_source));
		m.set("doc", documentToJson(_doc));
		return m;
	}

	namespace
	{
		Value strings(const std::vector<std::string>& _v)
		{
			Value a = Value::array();
			for(const auto& s : _v)
				a.push(s);
			return a;
		}
	}

	Value MmModel::catalogue()
	{
		Value c = Value::object();
		c.set("schema", "mm-desk/catalogue");
		c.set("version", 1);
		Value machines = Value::array();
		for(const auto& mi : ed::mmMachines())
		{
			Value m = Value::object();
			m.set("id", mi.id);
			m.set("name", mi.name);
			m.set("family", mi.family);
			Value syn = Value::array();
			Value enums = Value::object();
			for(uint8_t i = 0; i < 8; ++i)
			{
				syn.push(mi.synth[i]);
				const auto e = ed::mmSynthEnum(mi.id, i);
				if(!e.empty())
					enums.set(std::to_string(i), strings(e));
			}
			m.set("synth", std::move(syn));
			m.set("enums", std::move(enums));
			m.set("fx", mi.fx);
			m.set("mk2", mi.mk2);
			machines.push(std::move(m));
		}
		c.set("machines", std::move(machines));
		Value pages = Value::array();
		for(uint8_t pg = 1; pg < 8; ++pg)
		{
			Value names = Value::array();
			for(const auto* n : ed::mmFixedPage(pg))
				names.push(n);
			pages.push(std::move(names));
		}
		c.set("fixedPages", std::move(pages));
		Value lfo = Value::object();
		lfo.set("pages", strings(ed::mmLfoPages()));
		lfo.set("trigs", strings(ed::mmLfoTrigs()));
		lfo.set("waves", strings(ed::mmLfoWaves()));
		lfo.set("mults", strings(ed::mmLfoMults()));
		lfo.set("pitchDests", strings(ed::mmPitchDests()));
		c.set("lfo", std::move(lfo));
		c.set("enumRule", "index = floor(value * n / 128); value = ceil(index * 128 / n)");
		return c;
	}


	std::string MmModel::refusal(const deskCore::Lifecycle _l)
	{
		using deskCore::Lifecycle;
		switch(_l)
		{
		case Lifecycle::Missing:
		case Lifecycle::Unsupported: return "No Monomachine OS 1.32B is running.";
		case Lifecycle::Loading: return "The machine is being prepared. Try again in a moment.";
		case Lifecycle::Booting:
		case Lifecycle::Animating: return "The machine is still starting: the editor takes input when its start screen is gone.";
		case Lifecycle::HwConnecting: return "Waiting for the Monomachine to answer on the plug-in's MIDI in and out.";
		case Lifecycle::HwLost: return "The Monomachine has not answered for a while. Check the MIDI cables.";
		case Lifecycle::Ready: break;
		}
		return "The engine is not ready yet.";
	}

	// ---- the command table (MmModel::commands): the model's vocabulary, data only (the adapter maps
	// its ops to its own functions; the plug-in's commands are deskHost's table) ----

	const CommandTable& MmModel::commands()
	{
		using deskCore::Arg;
		using deskCore::ArgType;
		using deskCore::Gate;
		using deskCore::CoreOp;
		using deskCore::Owner;
		const Arg p{"p", ArgType::Integer, 0, 127};
		const Arg kOpt{"k", ArgType::Integer, 0, 127, true};
		const Arg sOpt{"s", ArgType::Integer, 0, 23, true};
		const Arg t6{"t", ArgType::Integer, 0, 5};
		static const CommandTable table({
			// ---- the core: documents ----
			{"ready", Owner::Core, Gate::None, -1, {}, "the page is up: everything is published once more", CoreOp::Ready},
			{"undo", Owner::Core, Gate::Input, -1, {}, "undo the last step (a gesture is one step)", CoreOp::Undo},
			{"redo", Owner::Core, Gate::Input, -1, {}, "", CoreOp::Redo},
			{"set", Owner::Core, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, {"pattern", "kit", "workingKit", "song", "global"}},
				{"doc", ArgType::Object}}, "a whole document as the intent (workingKit: the kit that plays)", CoreOp::Set},
			// ---- the machine ----
			{"load", Owner::Machine, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, {"pattern", "kit", "song", "global"}}, {"slot", ArgType::Integer, 0, 127}}, "read a document now"},
			{"select", Owner::Machine, Gate::Input, -1, {p, {"now", ArgType::Bool, 0, 0, true}},
				"LOAD PATTERN (at the pattern end while playing; now: STOP, LOAD, PLAY)"},
			{"loadKit", Owner::Machine, Gate::Input, -1, {kOpt}, "LOAD KIT (the current kit without k)"},
			{"saveKit", Owner::Machine, Gate::Input, -1, {kOpt}, "SAVE KIT"},
			{"loadSong", Owner::Machine, Gate::Input, -1, {sOpt}, "LOAD SONG (stopped)"},
			{"saveSong", Owner::Machine, Gate::Input, -1, {sOpt}, "SAVE SONG"},
			{"tempo", Owner::Machine, Gate::Input, -1, {{"bpm", ArgType::Number, 30, 300}}, "0x61"},
			{"play", Owner::Machine, Gate::Input, -1, {}, ""},
			{"stop", Owner::Machine, Gate::Input, -1, {}, ""},
			// ---- the editor's setup ----
			{"modSet", Owner::Setup, Gate::None, -1, {{"doc", ArgType::Object}}, "the app modulators (mm-desk/modulators)"},
			// ---- the machine ----
			{"mute", Owner::Machine, Gate::Input, -1, {t6, {"on", ArgType::Bool, 0, 0, true}}, "a synth track's mute"},
		});
		return table;
	}
}
