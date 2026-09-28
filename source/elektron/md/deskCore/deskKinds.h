#pragma once

#include "deskCommands.h"
#include "deskCore.h"

#include "elektronData/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace deskCore
{
	// A document kind as one record (P6): its name in the contract, how many there are, what a
	// dump of it costs on the wire, whether the page may ask for it, and its JSON both ways. A
	// model lists its kinds once (Model::kinds()); names, the `set`/`load` vocabularies, the
	// page's doc messages and "set" (a whole document as the intent) all come from the list.
	template<typename Model>
	struct KindSpec
	{
		using Kind = typename Model::Kind;
		using Document = typename Model::Document;
		using Context = typename Model::Context;

		Kind kind;
		const char* name;
		int slots;					// how many documents of the kind (1: one identity)
		size_t replyBytes;			// a dump's size, for timeouts at DIN speed (0: never requested)
		bool loadable;				// the page may ask for it ("load")
		Value (*toJson)(const Document&);
		// The JSON as this kind's document: parsed, validated, and admitted in this context (errors
		// otherwise).
		std::optional<Document> (*fromJson)(const Value&, std::vector<std::string>&, const Context&);
		// The slot its doc message names (the working kit's: the kit it was loaded from).
		int (*messageSlot)(const Document&, int _refSlot);
	};

	template<typename Model>
	const KindSpec<Model>* kindSpec(const typename Model::Kind _k)
	{
		for(const auto& s : Model::kinds())
			if(s.kind == _k)
				return &s;
		return nullptr;
	}

	template<typename Model>
	const KindSpec<Model>* kindSpec(const std::string& _name)
	{
		for(const auto& s : Model::kinds())
			if(_name == s.name)
				return &s;
		return nullptr;
	}

	// The contract's names of the kinds (_loadable: only those the page may ask for).
	template<typename Model>
	std::vector<const char*> kindNames(const bool _loadable = false)
	{
		std::vector<const char*> names;
		for(const auto& s : Model::kinds())
			if(!_loadable || s.loadable)
				names.push_back(s.name);
		return names;
	}

	// {"type":"doc","kind","slot","pending","source","doc"}
	template<typename Model>
	Value docMessage(const typename Model::Ref& _ref, const typename Model::Document& _doc, const bool _pending, const Source _source)
	{
		const auto* spec = kindSpec<Model>(_ref.kind);
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", spec ? spec->name : "");
		m.set("slot", spec ? spec->messageSlot(_doc, _ref.slot) : static_cast<int>(_ref.slot));
		m.set("pending", _pending);
		m.set("source", sourceName(_source));
		m.set("doc", spec ? spec->toJson(_doc) : Value());
		return m;
	}

	// {"op":"set","kind","doc"}: the pure transform "replace it with this admitted value".
	template<typename Model>
	typename Model::EditResult setDocument(const typename Model::Documents& _docs, const Value& _command,
		const typename Model::Context& _context)
	{
		typename Model::EditResult r;
		const auto* kindValue = _command.find("kind");
		const auto* doc = _command.find("doc");
		const auto* spec = kindValue && kindValue->isString() ? kindSpec<Model>(kindValue->asString()) : nullptr;
		if(!spec || !doc)
		{
			r.errors.emplace_back("set: expected a kind and a doc");
			return r;
		}
		auto after = spec->fromJson(*doc, r.errors, _context);
		if(!after || !r.errors.empty())
			return r;
		const auto ref = Model::refOf(*after);
		const auto before = Model::get(_docs, ref);
		if(!before)
		{
			r.errors.push_back(std::string(spec->name) + " " + std::to_string(ref.slot + 1) + " is not loaded yet");
			return r;
		}
		if(!(*before == *after))
			r.changes.push_back({*before, *after});
		return r;
	}

	// A slot argument of a kind: 0 .. slots-1, from the kind's record (the one place the count is).
	template<typename Model>
	Arg slotArg(const char* _name, const typename Model::Kind _kind, const bool _optional = false)
	{
		const auto* k = kindSpec<Model>(_kind);
		return Arg{_name, ArgType::Integer, 0, static_cast<double>(k ? k->slots - 1 : 0), _optional};
	}

	// The largest slot count of the kinds the page may ask for ("load"'s slot range).
	template<typename Model>
	int maxLoadableSlots()
	{
		int n = 1;
		for(const auto& k : Model::kinds())
			if(k.loadable && k.slots > n)
				n = k.slots;
		return n;
	}

	// A slot number the machine reports for a kind, folded into its range (the firmware numbers some
	// slots past the count: songs, globals).
	template<typename Model>
	uint8_t slotIn(const typename Model::Kind _kind, const int _value)
	{
		const auto* k = kindSpec<Model>(_kind);
		const int n = k && k->slots > 0 ? k->slots : 1;
		return static_cast<uint8_t>(((_value % n) + n) % n);
	}
}
