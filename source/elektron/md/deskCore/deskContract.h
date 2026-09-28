#pragma once

#include "deskCommands.h"

#include "elektronData/json.h"
#include "elektronData/jsonSchema.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace deskCore::contract
{
	// The executable spec, shared by the Machinedrum and Monomachine tests (P6): the messages a desk
	// published against the contract's $defs/message, the contract's machine document and
	// capabilities against what was published (both ways), the command tables against
	// $defs/command, and a model's machine commands against its adapter's map. Pure; the caller
	// passes the messages it recorded.
	struct Report
	{
		size_t messages = 0;
		size_t types = 0;
		std::vector<std::string> off;		// problems of messages off the contract (the first few)
		size_t offCount = 0;
		std::vector<std::string> unseen;	// declared members never published
	};

	inline std::optional<elektronData::json::Value> loadSchema(const char* _path)
	{
		std::ifstream in(_path);
		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		return elektronData::json::parse(text);
	}

	inline Report checkMessages(const elektronData::json::Value& _root, const std::vector<elektronData::json::Value>& _published)
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
		// The other way: every member declared for the machine document and the capabilities is
		// published (with additionalProperties false there, the two are the same set).
		r.unseen = schema.unseen("machine", machines);
		const auto unseenCaps = schema.unseen("capabilities", caps);
		r.unseen.insert(r.unseen.end(), unseenCaps.begin(), unseenCaps.end());
		if(machines.empty())
			r.unseen.emplace_back("(no machine document published)");
		return r;
	}

	// The schema's $defs/command is _generated; with _write it is rewritten when it differs. True when
	// they are the same (or were just written).
	inline bool checkCommands(elektronData::json::Value& _root, const elektronData::json::Value& _generated, const char* _path,
		const bool _write)
	{
		auto* defs = _root.find("$defs");
		const auto* current = defs ? defs->find("command") : nullptr;
		if(current && *current == _generated)
			return true;
		if(!_write || !defs)
			return false;
		defs->put("command", _generated);
		std::ofstream out(_path);
		out << elektronData::json::write(_root, 2) << "\n";
		return true;
	}

	// A model's machine commands and its adapter's map, both ways: rows no function runs, and
	// functions for ops the table does not have.
	template<typename H>
	std::vector<std::string> handlerGaps(const CommandTable<H>& _table, const std::vector<std::string>& _handled)
	{
		std::vector<std::string> gaps;
		const std::set<std::string> handled(_handled.begin(), _handled.end());
		for(const auto& c : _table.commands())
			if(c.owner == Owner::Machine && !handled.count(c.op))
				gaps.push_back(std::string("no adapter function for ") + c.op);
		for(const auto& op : _handled)
		{
			const auto* c = _table.find(op);
			if(!c || c->owner != Owner::Machine)
				gaps.push_back("the adapter runs " + op + ", which is not a machine command of the table");
		}
		return gaps;
	}
}
