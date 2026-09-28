#pragma once

#include "deskCommands.h"
#include "deskCore.h"
#include "deskLifecycle.h"

#include "elektronData/json.h"
#include "elektronData/jsonSchema.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace deskCore::contract
{
	// The executable spec, shared by the Machinedrum and Monomachine tests (P6), pure: the caller
	// reads and writes the schema file. It checks
	// - the messages a desk published against the contract's $defs/message, and the other way: the
	//   message types and the machine document's members the contract declares were published;
	// - the generated parts of the contract: $defs/command (the command tables) and the lifecycle
	//   names;
	// - a model's table against the functions its adapter and desk run for it.
	struct Report
	{
		size_t messages = 0;
		size_t types = 0;
		std::vector<std::string> off;		// problems of messages off the contract (the first few)
		size_t offCount = 0;
		std::vector<std::string> unseen;	// declared, never published
	};

	// The message types the contract's $defs/message declares (its oneOf branches' type consts).
	inline std::set<std::string> messageTypes(const elektronData::json::Value& _root)
	{
		std::set<std::string> types;
		const auto* defs = _root.find("$defs");
		const auto* message = defs ? defs->find("message") : nullptr;
		const auto* one = message ? message->find("oneOf") : nullptr;
		if(one && one->isArray())
			for(const auto& branch : one->asArray())
				if(const auto* p = branch.find("properties"))
					if(const auto* t = p->find("type"))
						if(const auto* c = t->find("const"); c && c->isString())
							types.insert(c->asString());
		return types;
	}

	// _elsewhere: the message types this test cannot publish (the plug-in's host sends them).
	inline Report checkMessages(const elektronData::json::Value& _root, const std::vector<elektronData::json::Value>& _published,
		const std::set<std::string>& _elsewhere = {})
	{
		using elektronData::json::Value;
		const elektronData::json::Schema schema(_root);
		Report r;
		std::map<std::string, size_t> types;
		std::vector<Value> machines, caps;
		for(const auto& m : _published)
		{
			const auto* type = m.find("type");
			const auto name = type && type->isString() ? type->asString() : std::string("?");
			++types[name];
			++r.messages;
			const auto problems = schema.validate(m, "message");
			if(!problems.empty() && r.offCount++ < 5)
				for(const auto& p : problems)
					r.off.push_back(name + ": " + p);
			if(name == "machine")
				if(const auto* d = m.find("doc"))
				{
					machines.push_back(*d);
					if(const auto* c = d->find("capabilities"))
						caps.push_back(*c);
				}
		}
		r.types = types.size();
		for(const auto& t : messageTypes(_root))
			if(!types.count(t) && !_elsewhere.count(t))
				r.unseen.push_back("message type " + t);
		// With additionalProperties false on the same definitions, declared == published.
		r.unseen = [&]
		{
			auto u = r.unseen;
			for(const auto& m : schema.unseen("machine", machines))
				u.push_back(m);
			for(const auto& c : schema.unseen("capabilities", caps))
				u.push_back("capabilities." + c);
			return u;
		}();
		if(machines.empty())
			r.unseen.emplace_back("(no machine document published)");
		return r;
	}

	// The schema's $defs/command is _generated.
	inline bool sameCommands(const elektronData::json::Value& _root, const elektronData::json::Value& _generated)
	{
		const auto* defs = _root.find("$defs");
		const auto* current = defs ? defs->find("command") : nullptr;
		return current && *current == _generated;
	}

	// The schema with its generated parts written in: $defs/command and the lifecycle names.
	inline elektronData::json::Value withGenerated(elektronData::json::Value _root, const elektronData::json::Value& _commands)
	{
		if(auto* defs = _root.find("$defs"))
		{
			defs->put("command", _commands);
			if(auto* life = defs->find("lifecycle"))
			{
				auto names = elektronData::json::Value::array();
				for(const auto& row : lifecycleRows())
					names.push(row.name);
				life->put("enum", std::move(names));
			}
		}
		return _root;
	}

	// The doc message variant ({"type":"doc"}) of $defs/message, or null.
	inline elektronData::json::Value* docMessageVariant(elektronData::json::Value& _root)
	{
		auto* defs = _root.find("$defs");
		auto* message = defs ? defs->find("message") : nullptr;
		auto* variants = message ? message->find("oneOf") : nullptr;
		if(!variants || !variants->isArray())
			return nullptr;
		for(auto& v : variants->asArray())
			if(const auto* p = v.find("properties"))
				if(const auto* t = p->find("type"); t && t->find("const") && t->find("const")->isString() && t->find("const")->asString() == "doc")
					return &v;
		return nullptr;
	}

	inline elektronData::json::Value namesOf(const std::vector<std::string>& _names)
	{
		auto a = elektronData::json::Value::array();
		for(const auto& n : _names)
			a.push(n);
		return a;
	}

	// The observed-document sources (Source) as the contract names them.
	inline std::vector<std::string> sourceNames()
	{
		return {sourceName(Source::None), sourceName(Source::Dump), sourceName(Source::Memory), sourceName(Source::Tracked)};
	}

	// The doc message's generated parts: its kind enum (the model's kinds, Model::kinds()) and its
	// source enum. Each kind also needs its if/then document $ref (written by hand: which $defs
	// shape a kind has); docKindGaps reports a kind without one.
	inline elektronData::json::Value withDocKinds(elektronData::json::Value _root, const std::vector<std::string>& _kinds)
	{
		if(auto* v = docMessageVariant(_root))
			if(auto* p = v->find("properties"))
			{
				if(auto* k = p->find("kind"))
					k->put("enum", namesOf(_kinds));
				if(auto* s = p->find("source"))
					s->put("enum", namesOf(sourceNames()));
			}
		return _root;
	}

	// What the doc message variant does not say as the model does: the kind and source enums, and
	// a kind with no if/then document shape.
	inline std::vector<std::string> docKindGaps(elektronData::json::Value _root, const std::vector<std::string>& _kinds)
	{
		std::vector<std::string> gaps;
		const auto* v = docMessageVariant(_root);
		if(!v)
			return {"no doc message in $defs/message"};
		const auto* p = v->find("properties");
		const auto* k = p ? p->find("kind") : nullptr;
		const auto* s = p ? p->find("source") : nullptr;
		if(!k || !k->find("enum") || !(*k->find("enum") == namesOf(_kinds)))
			gaps.push_back("the doc message's kind enum is not the model's kinds (--write-schema)");
		if(!s || !s->find("enum") || !(*s->find("enum") == namesOf(sourceNames())))
			gaps.push_back("the doc message's source enum is not deskCore's sources (--write-schema)");
		const auto* all = v->find("allOf");
		for(const auto& kind : _kinds)
		{
			bool shaped = false;
			if(all && all->isArray())
				for(const auto& c : all->asArray())
				{
					const auto* ifp = c.find("if") ? c.find("if")->find("properties") : nullptr;
					const auto* kc = ifp && ifp->find("kind") ? ifp->find("kind")->find("const") : nullptr;
					const auto* then = c.find("then") ? c.find("then")->find("properties") : nullptr;
					if(kc && kc->isString() && kc->asString() == kind && then && then->find("doc") && then->find("doc")->find("$ref"))
						shaped = true;
				}
			if(!shaped)
				gaps.push_back("the doc message has no document shape for kind " + kind);
		}
		return gaps;
	}

	// The contract's lifecycle enum is the lifecycle rows' names.
	inline bool sameLifecycle(const elektronData::json::Value& _root)
	{
		const auto* defs = _root.find("$defs");
		const auto* life = defs ? defs->find("lifecycle") : nullptr;
		const auto* e = life ? life->find("enum") : nullptr;
		if(!e || !e->isArray() || e->asArray().size() != lifecycleRows().size())
			return false;
		for(size_t i = 0; i < lifecycleRows().size(); ++i)
			if(!e->asArray()[i].isString() || e->asArray()[i].asString() != lifecycleRows()[i].name)
				return false;
		return true;
	}

	// A model's rows of one owner and the functions run for them, both ways: rows no function runs,
	// and functions for ops the table does not have (as that owner).
	template<typename H>
	std::vector<std::string> handlerGaps(const CommandTable<H>& _table, const Owner _owner, const std::vector<std::string>& _handled)
	{
		std::vector<std::string> gaps;
		const std::set<std::string> handled(_handled.begin(), _handled.end());
		for(const auto& c : _table.commands())
			if(c.owner == _owner && !handled.count(c.op))
				gaps.push_back(std::string("no function for the ") + ownerName(_owner) + " command " + c.op);
		for(const auto& op : _handled)
		{
			const auto* c = _table.find(op);
			if(!c || c->owner != _owner)
				gaps.push_back(std::string("a function for ") + op + ", which is not a " + ownerName(_owner) + " command of the table");
		}
		return gaps;
	}

	// Ops some map is keyed by (asks, reviews) that the table does not have at all.
	template<typename H>
	std::vector<std::string> unknownOps(const CommandTable<H>& _table, const std::vector<std::string>& _ops)
	{
		std::vector<std::string> gaps;
		for(const auto& op : _ops)
			if(!_table.find(op))
				gaps.push_back(op + " is not a command of the table");
		return gaps;
	}
}
