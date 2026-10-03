#include "mdDeskChain.h"

#include "deskCore/deskChain.h"

namespace mdDesk
{
	std::vector<std::string> validateChain(const std::vector<int>& _patterns)
	{
		return deskCore::validateChain(_patterns, 16, 128);
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
