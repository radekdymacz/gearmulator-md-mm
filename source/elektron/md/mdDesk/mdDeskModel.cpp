#include "mdDeskModel.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdValidate.h"

namespace mdDesk
{
	namespace ed = elektronData;
	using ed::json::Value;

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
		Value doc = Value::object();
		doc.set("schema", "md-desk/machines");
		doc.set("version", 1);
		doc.set("machines", std::move(machines));
		return doc;
	}

	std::string MdModel::refusal(const deskCore::Lifecycle _l)
	{
		using deskCore::Lifecycle;
		if(_l == Lifecycle::Missing || _l == Lifecycle::Unsupported)
			return "No Machinedrum firmware is running";
		if(_l == Lifecycle::Animating)
			return "The machine is still starting: its start-up animation ignores keys. The editor takes input when it is over.";
		return "The machine is starting (device busy). Try again in a moment.";
	}
}
