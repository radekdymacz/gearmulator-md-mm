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
		}
		return "";
	}

	std::optional<Kind> kindFromName(const std::string& _name)
	{
		for(const auto k : {Kind::Pattern, Kind::Kit, Kind::Song, Kind::Global})
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
		}
	}

	EditResult apply(const Documents& _docs, const Value& _command, const EditContext& _context)
	{
		EditResult r;
		const auto* op = _command.find("op");
		if(!op || !op->isString() || op->asString() != "set")
		{
			r.errors.push_back("unknown command " + (op && op->isString() ? op->asString() : std::string()));
			return r;
		}
		const auto* kindValue = _command.find("kind");
		const auto* doc = _command.find("doc");
		const auto kind = kindValue && kindValue->isString() ? kindFromName(kindValue->asString()) : std::nullopt;
		if(!kind)
		{
			r.errors.emplace_back("set: kind must be pattern, kit, song or global");
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
		}
		if(!after)
			return r;
		if(auto problems = std::visit([](const auto& _v) { return ed::validate(_v); }, *after); !problems.empty())
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
			r.changes.push_back({*before, *after, *kind == Kind::Kit && static_cast<int>(ref.slot) != _context.currentKit});
		return r;
	}

	Value MmModel::docMessage(const Ref& _ref, const Document& _doc, const bool _pending, const deskCore::Source _source)
	{
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", kindName(_ref.kind));
		m.set("slot", _ref.slot);
		m.set("pending", _pending);
		m.set("working", _ref.kind == Kind::Kit && (_source == deskCore::Source::Memory || _source == deskCore::Source::Tracked));
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

}
