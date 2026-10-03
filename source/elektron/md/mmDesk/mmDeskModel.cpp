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
		// DESIGN-UNIFY.md 4.1: the edit intents. Pattern edits name the pattern (p); the live kit edits the kit that
		// plays (k, its working kit); the song edits a song (s); the global edits the active global; the library's ops
		// (group g_library) their slots. A track t 0-11: the six synth tracks, then the six MIDI sequencer tracks
		// (6-11), as the page counts them.
		constexpr auto P = static_cast<int>(Kind::Pattern);
		constexpr auto W = static_cast<int>(Kind::WorkingKit);
		constexpr auto G = static_cast<int>(Kind::Global);
		constexpr auto S = static_cast<int>(Kind::Song);
		constexpr auto K = static_cast<int>(Kind::Kit);
		const Arg s = deskCore::slotArg<MmModel>("s", Kind::Song);
		const Arg row{"i", ArgType::Integer, 0, 199};
		const auto slotP = [](const char* _name) { return deskCore::slotArg<MmModel>(_name, Kind::Pattern); };
		const auto slotK = [](const char* _name) { return deskCore::slotArg<MmModel>(_name, Kind::Kit); };
		const Arg k = deskCore::slotArg<MmModel>("k", Kind::Kit);
		const Arg t12{"t", ArgType::Integer, 0, 11};
		const Arg step{"s", ArgType::Integer, 0, 63};
		const Arg on{"on", ArgType::Bool};
		const Arg page{"page", ArgType::Integer, 0, 7, true};
		const Arg i8{"i", ArgType::Integer, 0, 7};
		const Arg from{"from", ArgType::Integer, 0, 63}, to{"to", ArgType::Integer, 1, 64};
		static const CommandTable table({
			// ---- the core: documents ----
			{"ready", Owner::Core, Gate::None, -1, {}, "the page is up: everything is published once more", CoreOp::Ready},
			{"undo", Owner::Core, Gate::Input, -1, {}, "undo the last step (a gesture is one step)", CoreOp::Undo},
			{"redo", Owner::Core, Gate::Input, -1, {}, "", CoreOp::Redo},
			{"set", Owner::Core, Gate::Input, -1, {{"kind", ArgType::Text, 0, 0, false, deskCore::kindNames<MmModel>()},
				{"doc", ArgType::Object}}, "a whole document as the intent (workingKit: the kit that plays)", CoreOp::Set},
			// ---- the edit intents: Mix (the kit that plays, the active global) ----
			{"level", Owner::Core, Gate::Input, W, {k, t6, {"v", ArgType::Integer, 0, 127}}, "a track's level (LEV)"},
			{"route", Owner::Core, Gate::Input, W, {k, t6, {"out", ArgType::Integer, 0, 7}}, "a track's mix buses: AB 1, CD 2, EF 4"},
			{"input", Owner::Core, Gate::Input, W, {k, t6, {"v", ArgType::Integer, 0, 6}},
				"an FX machine's input: 0 NEIGHBOR, 1 INP A, 2 INP B, 3 INP A+B, 4-6 BUS AB CD EF"},
			{"param", Owner::Core, Gate::Input, W, {k, t12, page, i8, {"v", ArgType::Integer, 0, 127}},
				"a kit value: a synth track's page 0-6 (SYN AMP FLT EFX LF1 LF2 LF3), a MIDI track's MIDI page (7)"},
			{"trigPos", Owner::Core, Gate::Input, W, {k, t6, {"v", ArgType::IntegerOrNull, 0, 5}}, "TRIG POS: the track that also plays this one's notes, null none"},
			{"legato", Owner::Core, Gate::Input, W, {k, t6, {"env", ArgType::Text, 0, 0, false, {"amp", "filter", "lfo"}}, on},
				"LEGATO: whether that envelope fires again when notes overlap"},
			{"portamento", Owner::Core, Gate::Input, W, {k, t6, {"v", ArgType::Text, 0, 0, false, {"always", "legato"}}}, "PORTAMENTO: ALWAYS or ONLY LEGATO"},
			{"routing", Owner::Core, Gate::Input, G, {{"v", ArgType::Text, 0, 0, false, {"3xSTEREO+AB=MIX", "3xSTEREO", "6xMONO"}}}, "the routing mode"},
			{"midiTrack", Owner::Core, Gate::Input, G, {t6, {"ch", ArgType::Integer, 0, 15, true}, {"cc", ArgType::Array, 0, 0, true}},
				"a MIDI sequencer track's channel and its CL1-4 CC numbers (four of 0-127, 128 = AFT)"},
			// ---- the edit intents: Sequence (a pattern) ----
			{"step", Owner::Core, Gate::Input, P, {p, t12, step, {"v", ArgType::Any}},
				"a step's value: null empty, {off:true} a NOTE OFF, {n:[note...], a, f, l, notrig?} a trig (n: its pitch, chord notes after the base; a f l: AMP FILTER LFO)"},
			{"slide", Owner::Core, Gate::Input, P, {p, t12, step, on}, "a step on the slide track"},
			{"swingStep", Owner::Core, Gate::Input, P, {p, t12, step, on}, "a step on the swing track"},
			{"lock", Owner::Core, Gate::Input, P, {p, t12, page, i8, step, {"v", ArgType::IntegerOrNull, 0, 127}},
				"a parameter lock (page as param's; MIDI tracks: 7); v null clears it. 62 locked parameters a pattern"},
			{"clearLane", Owner::Core, Gate::Input, P, {p, t12, page, i8}, "every lock of one parameter of a track"},
			{"clearLocks", Owner::Core, Gate::Input, P, {p, t12}, "every lock of a track (Alt + the lock lane's clear key)"},
			{"clearPattern", Owner::Core, Gate::Input, P, {p}, "every track's steps, slides and locks (Alt + CLR)"},
			{"steps", Owner::Core, Gate::Input, P, {p, from, to, {"rows", ArgType::Array}},
				"tracks' steps in [from, to) become exactly these (the generators, every-n fill): rows [{t, steps: [[s, v]...], slide?: [s...], "
				"locks?: [[page, i, s, v]...]}]; a step not listed is empty; slide and locks, when given, are the range's"},
			{"rotate", Owner::Core, Gate::Input, P, {p, t12, {"by", ArgType::Integer, -63, 63}},
				"a track's steps, slides and locks move by steps, wrapping at the length (swing stays: the groove)"},
			{"doublePattern", Owner::Core, Gate::Input, P, {p}, "length x2, the new half a copy of every track's steps, slides, swing and locks"},
			{"length", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 2, 64}}, ""},
			{"speed", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Text, 0, 0, false, {"1X", "2X", "3/4X", "3/2X"}}}, "the tempo multiplier"},
			{"swing", Owner::Core, Gate::Input, P, {p, {"v", ArgType::Integer, 50, 80}}, "percent"},
			{"transpose", Owner::Core, Gate::Input, P, {p, {"t", ArgType::Integer, 0, 11, true}, {"v", ArgType::Integer, -64, 63, true},
				{"scale", ArgType::Integer, 0, 3, true}, {"key", ArgType::Integer, 0, 11, true}},
				"TRANSPOSE: the pattern's (no t: v) or a track's (v semitones, scale 0 --- 1 FIX 2 MAJ 3 MIN, key)"},
			{"arp", Owner::Core, Gate::Input, P, {p, t12, {"field", ArgType::Text, 0, 0, false, {"play", "ojmp", "mode", "range", "speed", "trigs", "length", "step"}},
				{"v", ArgType::Integer, 0, 255}, {"i", ArgType::Integer, 0, 15, true}},
				"ARPEGGIATOR, a field in firmware units (step i: 64 + offset, 255 muted)"},
			{"clearSteps", Owner::Core, Gate::Input, P, {p, t12, from, to}, "a track's steps, slides and locks in [from, to)"},
			{"copySteps", Owner::Core, Gate::Input, P, {p, t12, from, to}, "a track page to the clipboard"},
			{"pasteSteps", Owner::Core, Gate::Input, P, {p, t12, from, {"to", ArgType::Integer, 1, 64, true}},
				"the clipboard's page from step from; [from, to) is cleared first (default: the page's own length)"},
			// ---- the edit intents: Sound and Perform (the kit that plays) ----
			{"machine", Owner::Core, Gate::Input, W, {k, t6, {"model", ArgType::Integer, 0, 255}, {"keepFx", ArgType::Bool, 0, 0, true}},
				"a track's machine (model: its SysEx 0x5B id): its SYN page starts at the machine's defaults; without keepFx (default on) the other pages too"},
			{"clearSound", Owner::Core, Gate::Input, W, {k, t6}, "CLEAR MACHINE: GND-SIN, every page at its start"},
			{"copySound", Owner::Core, Gate::Input, W, {k, t6}, "a track's machine and its seven pages to the clipboard"},
			{"pasteSound", Owner::Core, Gate::Input, W, {k, t6}, "the clipboard's machine and pages onto a track"},
			{"params", Owner::Core, Gate::Input, W, {k, {"values", ArgType::Array}},
				"many kit values at once (MUTATE, a screen's handle, Control All): values [[t 0-11, page, i 0-7, v 0-127]...], page as param's"},
			{"assign", Owner::Core, Gate::Input, W, {k, t6, {"src", ArgType::Integer, 0, 5, true}, {"row", ArgType::Integer, 0, 1, true},
				{"page", ArgType::Integer, 0, 8, true}, {"dest", ArgType::Integer, 0, 7, true}, {"add", ArgType::Integer, -64, 63, true},
				{"mirror", ArgType::Bool, 0, 0, true}, {"hpf", ArgType::Bool, 0, 0, true}, {"lpf", ArgType::Bool, 0, 0, true}},
				"ASSIGN: a source's row (src 0 JOY R, 1 JOY L, 2 JOY U, 3 JOY D, 4 VEL, 5 KEY; row 0-1) gets page, dest, add; MIRROR, HPF, LPF"},
			{"multiEnv", Owner::Core, Gate::Input, W, {k, {"i", ArgType::Integer, 0, 5}, {"v", ArgType::Integer, 0, 127}},
				"MULTI ENV: one value (ATK DEC SUS REL PORT ...) for every track"},
			{"multiTrig", Owner::Core, Gate::Input, W, {k, {"mode", ArgType::Integer, 0, 3, true}, {"splitKey", ArgType::Integer, 0, 127, true},
				{"splitTrack", ArgType::Integer, 0, 5, true}, {"timing", ArgType::Integer, 0, 6, true}},
				"MULTI TRIG: mode (ALL TRK, SPLIT, SEQ STRT, SEQ TRNS), split key, split track, timing"},
			{"kitName", Owner::Core, Gate::Input, W, {k, {"name", ArgType::Text}}, "the kit that plays, renamed live (0x55): at most 11 characters"},
			// ---- the edit intents: the MULTI MAP (the active global) ----
			{"multiMap", Owner::Core, Gate::Input, G, {{"i", ArgType::Integer, 0, 31}, {"hi", ArgType::Integer, 0, 127, true},
				{"pat", ArgType::Integer, 0, 255, true}, {"ofs", ArgType::Integer, 0, 255, true}, {"len", ArgType::Integer, 0, 64, true},
				{"trn", ArgType::Integer, -64, 63, true}, {"tim", ArgType::Integer, 0, 6, true}},
				"a MULTI MAP range: upper key, pattern (255 CUR), offset (255 ---), length, transpose, timing (0 DIR, 1 2 4 8 16 32)"},
			{"multiMapSplit", Owner::Core, Gate::Input, G, {{"i", ArgType::Integer, 0, 31}}, "a MULTI MAP range splits at its middle key"},
			{"multiMapDelete", Owner::Core, Gate::Input, G, {{"i", ArgType::Integer, 0, 31}}, "a MULTI MAP range goes (the last then reaches the top key)"},
			// ---- the edit intents: Song rows (as the Machinedrum's) ----
			{"rowSet", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object}}, "song rows (contract rows; i the row)"},
			{"rowInsert", Owner::Core, Gate::Input, S, {s, row, {"row", ArgType::Object, 0, 0, true}}, ""},
			{"rowDelete", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"rowMove", Owner::Core, Gate::Input, S, {s, {"from", ArgType::Integer, 0, 199}, {"to", ArgType::Integer, 0, 199}}, ""},
			{"copyRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			{"pasteRow", Owner::Core, Gate::Input, S, {s, row}, ""},
			// ---- the edit intents: the library (stored slots; the machine asks before one loses something) ----
			{"patCopy", Owner::Core, Gate::Input, P, {p}, "the pattern library", CoreOp::Edit, g_library},
			{"patPaste", Owner::Core, Gate::Input, P, {p}, "", CoreOp::Edit, g_library},
			{"patCopyTo", Owner::Core, Gate::Input, P, {slotP("from"), slotP("to")}, "drag-copy", CoreOp::Edit, g_library},
			{"patClear", Owner::Core, Gate::Input, P, {p}, "", CoreOp::Edit, g_library},
			{"kitCopy", Owner::Core, Gate::Input, K, {k}, "the kit library", CoreOp::Edit, g_library},
			{"kitPaste", Owner::Core, Gate::Input, K, {k}, "", CoreOp::Edit, g_library},
			{"kitCopyTo", Owner::Core, Gate::Input, K, {slotK("from"), slotK("to")}, "", CoreOp::Edit, g_library},
			{"kitClear", Owner::Core, Gate::Input, K, {k}, "", CoreOp::Edit, g_library},
			{"kitRename", Owner::Core, Gate::Input, K, {k, {"name", ArgType::Text}}, "a stored kit's name (kitName renames the kit that plays)", CoreOp::Edit, g_library},
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
			// The note intent (deskCore/deskNotes.h), as on the MD
			{"noteOn", Owner::Machine, Gate::Input, -1, {t6, {"vel", ArgType::Integer, 1, 127}, {"pitch", ArgType::Integer, -48, 79}},
				"the page's keyboard: synth track t's note on its own MIDI channel, pitch semitones from C3 (MIDI note 48)"},
			{"noteOff", Owner::Machine, Gate::Input, -1, {t6, {"pitch", ArgType::Integer, -48, 79, true}}, "the key let go: track t's note of that pitch (none: all of them)"},
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
