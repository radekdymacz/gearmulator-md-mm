#include "mmDeskModel.h"

#include "deskCore/deskKinds.h"

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

	// The Monomachine's document kinds, once (P6): names, counts, dump sizes (DIN timeouts), JSON.
	const std::vector<deskCore::KindSpec<MmModel>>& MmModel::kinds()
	{
		static const std::vector<deskCore::KindSpec<MmModel>> k{
			{Kind::Pattern, "pattern", 128, 3200, true,
				[](const Document& _d) { return ed::mmPatternToJson(std::get<ed::MmPattern>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const EditContext&) { return parsed(ed::mmPatternFromJson(_v, _e), _e); },
				ownSlot},
			{Kind::Kit, "kit", 128, 820, true,
				[](const Document& _d) { return ed::mmKitToJson(std::get<ed::MmKit>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const EditContext&) { return parsed(ed::mmKitFromJson(_v, _e), _e); },
				ownSlot},
			{Kind::Song, "song", 24, 5600, true,
				[](const Document& _d) { return ed::mmSongToJson(std::get<ed::MmSong>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const EditContext&) { return parsed(ed::mmSongFromJson(_v, _e), _e); },
				ownSlot},
			{Kind::Global, "global", 8, 900, true,
				[](const Document& _d) { return ed::mmGlobalToJson(std::get<ed::MmGlobal>(_d)); },
				[](const Value& _v, std::vector<std::string>& _e, const EditContext&) { return parsed(ed::mmGlobalFromJson(_v, _e), _e); },
				ownSlot},
			// The kit that plays: one identity; its doc message names the kit it was loaded from.
			{Kind::WorkingKit, "workingKit", 1, 0, false,
				[](const Document& _d) { return ed::mmKitToJson(std::get<WorkingKit>(_d).kit); },
				[](const Value& _v, std::vector<std::string>& _e, const EditContext& _c) -> std::optional<Document>
				{
					auto k = parsed(ed::mmKitFromJson(_v, _e), _e);
					if(!k)
						return {};
					const auto& kit = std::get<ed::MmKit>(*k);
					if(kit.position != _c.currentKit)
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

	const char* kindName(const Kind _k)
	{
		const auto* s = deskCore::kindSpec<MmModel>(_k);
		return s ? s->name : "";
	}

	std::optional<Kind> kindFromName(const std::string& _name)
	{
		const auto* s = deskCore::kindSpec<MmModel>(_name);
		return s ? std::optional<Kind>(s->kind) : std::nullopt;
	}

	Value documentToJson(const Document& _doc)
	{
		const auto* s = deskCore::kindSpec<MmModel>(refOf(_doc).kind);
		return s ? s->toJson(_doc) : Value();
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
		return deskCore::setDocument<MmModel>(_docs, _command, _context);
	}

	Value MmModel::docMessage(const Ref& _ref, const Document& _doc, const bool _pending, const deskCore::Source _source)
	{
		return deskCore::docMessage<MmModel>(_ref, _doc, _pending, _source);
	}

	const std::vector<deskCore::Unsupported>& MmModel::unsupported()
	{
		// MM-P4 made every feature MM-P3 disabled real; what an engine cannot do (over HW MIDI the MIDI
		// track mutes and RECORD) is its capability (MmMachine::capabilities).
		static const std::vector<deskCore::Unsupported> none;
		return none;
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
		c.set("slots", deskCore::slotCounts<MmModel>());
		c.set("tracks", static_cast<int>(ed::MmKit::g_tracks));	// the synth tracks
		return c;
	}


	std::string MmModel::lifecycleText(const deskCore::Lifecycle _l)
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
		const Arg p = deskCore::slotArg<MmModel>("p", Kind::Pattern);
		const Arg kOpt = deskCore::slotArg<MmModel>("k", Kind::Kit, true);
		const Arg sOpt = deskCore::slotArg<MmModel>("s", Kind::Song, true);
		const Arg t6{"t", ArgType::Integer, 0, 5};
		static const CommandTable table({
			// ---- the core: documents ----
			{"ready", Owner::Core, Gate::None, -1, {}, "the page is up: everything is published once more", CoreOp::Ready},
			{"undo", Owner::Core, Gate::Input, -1, {}, "undo the last step (a gesture is one step)", CoreOp::Undo},
			{"redo", Owner::Core, Gate::Input, -1, {}, "", CoreOp::Redo},
			{"set", Owner::Core, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, deskCore::kindNames<MmModel>()},
				{"doc", ArgType::Object}}, "a whole document as the intent (workingKit: the kit that plays)", CoreOp::Set},
			// ---- the machine ----
			{"load", Owner::Machine, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, deskCore::kindNames<MmModel>(true)}, {"slot", ArgType::Integer, 0, static_cast<double>(deskCore::maxLoadableSlots<MmModel>() - 1)}}, "read a document now"},
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
			// MM-P4
			{"muteMidi", Owner::Machine, Gate::Input, -1, {t6, {"on", ArgType::Bool, 0, 0, true}},
				"a MIDI sequencer track's mute (the MUTE window: FUNCTION + BANK GROUP, TRIG 9-14)"},
			{"poly", Owner::Machine, Gate::Input, -1, {{"on", ArgType::Bool}}, "POLY, the machine's audio mode (SET STATUS 0x20)"},
			{"record", Owner::Machine, Gate::Input, -1, {{"mode", ArgType::Text, 0, 0, false, {"off", "grid", "live"}}},
				"GRID RECORDING (RECORD), LIVE RECORDING (RECORD + PLAY) or off"},
			{"hwSend", Owner::Machine, Gate::Input, -1, {}, "HW MIDI: the machine is on SYSEX RECV; send the dumps that wait for it"},
			{"followHost", Owner::Machine, Gate::Input, -1, {}, "in a DAW: the active GLOBAL follows the host's clock and transport"},
			// MM-P8
			{"chain", Owner::Machine, Gate::Input, -1, {{"patterns", ArgType::Array}}, "BANK held + TRIG keys: the machine's pattern chain"},
			{"chainClear", Owner::Machine, Gate::Input, -1, {}, "BANK + the TRIG key of the pattern that plays: ends the chain"},
		});
		return table;
	}

	std::optional<ed::MmGlobal> MmModel::hostFollowing(const ed::MmGlobal& _global)
	{
		if(_global.tempoSync == 1 && _global.transportIn == 1)
			return std::nullopt;
		auto g = _global;
		g.tempoSync = 1;	// EXT MIDI CLK (CLOCK IN)
		g.transportIn = 1;	// ACCEPT (TRANSPORT IN)
		return g;
	}
}
