#pragma once

#include "elektronData/json.h"

#include <set>
#include <string>
#include <variant>
#include <vector>

namespace deskCore
{
	// What the page asks of the machine's pattern chain (both machines chain with the panel: hold BANK,
	// press the TRIG keys in play order). Two variants, so CLEAR is said, not an empty list.
	struct ChainOf
	{
		std::vector<int> patterns;		// play order, one bank, each once
	};
	struct ChainClear {};
	using ChainRequest = std::variant<ChainOf, ChainClear>;

	// The patterns of a "chain" command ({"patterns":[...]}); a member that is not a number is -1.
	inline std::vector<int> chainPatterns(const elektronData::json::Value& _command)
	{
		std::vector<int> patterns;
		if(const auto* list = _command.find("patterns"); list && list->isArray())
			for(const auto& v : list->asArray())
				patterns.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		return patterns;
	}

	// Problems with a chain request, as messages for the page; empty = it can be sent. The machine's rule
	// (MD manual p.37, MM manual 1-46): at least two patterns, one bank only (_bankSize patterns), each
	// pattern once, of _patternCount patterns. Pure.
	inline std::vector<std::string> validateChain(const std::vector<int>& _patterns, const int _bankSize, const int _patternCount)
	{
		std::vector<std::string> errors;
		if(_patterns.size() < 2)
			errors.emplace_back("A chain needs at least two patterns");
		if(_patterns.size() > static_cast<size_t>(_bankSize))
			errors.emplace_back("A chain holds at most " + std::to_string(_bankSize) + " patterns (one bank)");
		std::set<int> seen;
		for(const int p : _patterns)
		{
			if(p < 0 || p >= _patternCount)
			{
				errors.emplace_back("patterns: " + std::to_string(p) + " is not a pattern 0-" + std::to_string(_patternCount - 1));
				return errors;
			}
			if(!seen.insert(p).second)
				errors.emplace_back("Each pattern can be in the chain once (the machine's rule)");
			if(p / _bankSize != _patterns.front() / _bankSize)
			{
				errors.emplace_back("The machine chains patterns from one bank only");
				return errors;
			}
		}
		return errors;
	}
}
