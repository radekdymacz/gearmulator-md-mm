#include "mdDeskSetup.h"

#include <set>

namespace mdDesk
{
	namespace json = elektronData::json;

	bool DeskSetup::operator==(const DeskSetup& _o) const
	{
		return modulators.sources == _o.modulators.sources && modulators.links == _o.modulators.links && knobCcs == _o.knobCcs;
	}

	std::vector<std::string> validateKnobCcs(const std::vector<int>& _ccs)
	{
		std::vector<std::string> errors;
		if(_ccs.size() != 8)
			errors.emplace_back("$.knobCcs: expected 8 CC numbers");
		std::set<int> seen;
		for(size_t i = 0; i < _ccs.size(); ++i)
		{
			if(_ccs[i] < 0 || _ccs[i] > 127)
				errors.push_back("$.knobCcs[" + std::to_string(i) + "]: " + std::to_string(_ccs[i]) + " is outside 0..127");
			else if(!seen.insert(_ccs[i]).second)
				errors.push_back("$.knobCcs[" + std::to_string(i) + "]: CC " + std::to_string(_ccs[i]) + " is used by another knob row");
		}
		return errors;
	}

	std::optional<DeskSetup> deskSetupFromJson(const json::Value& _doc, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const auto* schema = _doc.find("schema");
		if(!schema || !schema->isString() || schema->asString() != "md-desk/setup")
			_errors.emplace_back("$.schema: expected md-desk/setup");
		const auto* version = _doc.find("version");
		if(!version || !version->isNumber() || version->asNumber() != 1)
			_errors.emplace_back("$.version: expected 1");
		DeskSetup s;
		if(const auto* m = _doc.find("modulators"))
		{
			std::vector<std::string> modErrors;
			auto mods = modSetupFromJson(*m, modErrors);
			for(const auto& e : modErrors)
				_errors.push_back("$.modulators" + (e.rfind("$", 0) == 0 ? e.substr(1) : ": " + e));
			if(mods)
				s.modulators = std::move(*mods);
		}
		if(const auto* k = _doc.find("knobCcs"))
		{
			std::vector<int> ccs;
			if(k->isArray())
				for(const auto& v : k->asArray())
					ccs.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
			const auto errors = validateKnobCcs(ccs);
			_errors.insert(_errors.end(), errors.begin(), errors.end());
			if(errors.empty())
				for(size_t i = 0; i < 8; ++i)
					s.knobCcs[i] = static_cast<uint8_t>(ccs[i]);
		}
		if(_errors.size() != before)
			return {};
		return s;
	}

	json::Value deskSetupToJson(const DeskSetup& _setup)
	{
		json::Value d = json::Value::object();
		d.set("schema", "md-desk/setup");
		d.set("version", 1);
		d.set("modulators", modSetupToJson(_setup.modulators));
		json::Value k = json::Value::array();
		for(const auto cc : _setup.knobCcs)
			k.push(static_cast<int>(cc));
		d.set("knobCcs", std::move(k));
		return d;
	}
}
