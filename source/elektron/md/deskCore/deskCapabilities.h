#pragma once

#include "elektronData/json.h"

#include <string>
#include <utility>
#include <vector>

namespace deskCore
{
	// What an engine can do, as data the adapter publishes (P6): machine.capabilities =
	// {engine, label, about, can: {name: bool}, reasons: {name: text}, values: {name: text}}. The
	// pages decide from it only: a control is enabled when can[name] is true, and disabled with
	// reasons[name] as the tooltip otherwise (a name a page does not know: not allowed).
	struct Capabilities
	{
		struct Can
		{
			std::string name;		// "transport", "liveRecord", "chains", ...
			bool yes = false;
			std::string reason;		// why not (empty when yes)
		};

		std::string engine;			// the engine map's id: "emu", "hw"
		std::string label;			// the LCD's engine label: "EMU OS 1.63", "HW MIDI"
		std::string about;			// the engine label's tooltip while it is ready
		std::vector<Can> can;
		std::vector<std::pair<std::string, std::string>> values;	// named text values ("dumps": "recv")

		Capabilities& set(std::string _name, const bool _yes, std::string _reason = {})
		{
			for(auto& c : can)
				if(c.name == _name)
				{
					c.yes = _yes;
					c.reason = _yes ? std::string() : std::move(_reason);
					return *this;
				}
			can.push_back({std::move(_name), _yes, _yes ? std::string() : std::move(_reason)});
			return *this;
		}

		bool has(const std::string& _name) const
		{
			for(const auto& c : can)
				if(c.name == _name)
					return c.yes;
			return false;
		}

		const std::string& reason(const std::string& _name) const
		{
			static const std::string none;
			for(const auto& c : can)
				if(c.name == _name)
					return c.reason;
			return none;
		}

		elektronData::json::Value toJson() const
		{
			elektronData::json::Value v = elektronData::json::Value::object();
			v.set("engine", engine);
			v.set("label", label);
			v.set("about", about);
			auto yes = elektronData::json::Value::object();
			auto reasons = elektronData::json::Value::object();
			for(const auto& c : can)
			{
				yes.set(c.name, c.yes);
				if(!c.yes && !c.reason.empty())
					reasons.set(c.name, c.reason);
			}
			auto texts = elektronData::json::Value::object();
			for(const auto& [k, t] : values)
				texts.set(k, t);
			v.set("can", std::move(yes));
			v.set("reasons", std::move(reasons));
			v.set("values", std::move(texts));
			return v;
		}
	};

	// What an editor does not do yet, on any engine (a model's list, merged into the capabilities).
	struct Unsupported
	{
		const char* name;
		const char* reason;
	};

	// One entry of the engine map (machine.engines): what the page's engine menu offers.
	struct EngineChoice
	{
		std::string id;
		std::string label;
		bool available = true;
		std::string reason;

		elektronData::json::Value toJson() const
		{
			auto v = elektronData::json::Value::object();
			v.set("id", id);
			v.set("label", label);
			v.set("available", available);
			if(!available)
				v.set("reason", reason);
			return v;
		}
	};
}
