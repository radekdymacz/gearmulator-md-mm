#include "mdDeskChain.h"

#include <set>

namespace mdDesk
{
	std::vector<std::string> validateChain(const std::vector<int>& _patterns)
	{
		std::vector<std::string> errors;
		if(_patterns.size() < 2)
			errors.emplace_back("A chain needs at least two patterns");
		if(_patterns.size() > 16)
			errors.emplace_back("A chain holds at most 16 patterns (one bank)");
		std::set<int> seen;
		for(const int p : _patterns)
		{
			if(p < 0 || p > 127)
			{
				errors.emplace_back("patterns: " + std::to_string(p) + " is not a pattern 0-127");
				return errors;
			}
			if(!seen.insert(p).second)
				errors.emplace_back("Each pattern can be in the chain once (the machine's rule)");
			if((p >> 4) != (_patterns.front() >> 4))
			{
				errors.emplace_back("The machine chains patterns from one bank only");
				return errors;
			}
		}
		return errors;
	}

	std::vector<std::string> chainKeys(const std::vector<int>& _patterns, const int _bankGroup)
	{
		if(!validateChain(_patterns).empty())
			return {};
		const int bank = _patterns.front() >> 4;
		const int wantGroup = bank >= 4 ? 1 : 0;
		if(_bankGroup < 0)
			return {};
		std::vector<std::string> keys;
		if(_bankGroup != wantGroup)
			keys.emplace_back("bankGroup");
		std::string spec = "chain:" + std::to_string(bank & 3) + ":";
		for(size_t i = 0; i < _patterns.size(); ++i)
			spec += (i ? "," : "") + std::to_string(_patterns[i] & 15);
		keys.push_back(spec);
		return keys;
	}
}
