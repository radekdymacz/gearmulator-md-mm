#pragma once

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
}
