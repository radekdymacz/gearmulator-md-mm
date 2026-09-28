#include "deskCommands.h"

#include <cmath>

namespace deskCore
{
	using elektronData::json::Value;

	namespace
	{
		std::string num(const double _d)
		{
			if(std::floor(_d) == _d)
				return std::to_string(static_cast<long long>(_d));
			return std::to_string(_d);
		}

		Value typeSchema(const Arg& _a)
		{
			Value s = Value::object();
			switch(_a.type)
			{
			case ArgType::Integer:
				s.set("type", "integer");
				s.set("minimum", _a.min);
				s.set("maximum", _a.max);
				break;
			case ArgType::Number:
				s.set("type", "number");
				s.set("minimum", _a.min);
				s.set("maximum", _a.max);
				break;
			case ArgType::IntegerOrNull:
			{
				Value i = Value::object();
				i.set("type", "integer");
				i.set("minimum", _a.min);
				i.set("maximum", _a.max);
				Value n = Value::object();
				n.set("type", "null");
				Value one = Value::array();
				one.push(std::move(n));
				one.push(std::move(i));
				s.set("oneOf", std::move(one));
				break;
			}
			case ArgType::Text: s.set("type", "string"); break;
			case ArgType::Bool:
			{
				Value t = Value::array();
				t.push("boolean");
				t.push("integer");
				s.set("type", std::move(t));
				break;
			}
			case ArgType::Object: s.set("type", "object"); break;
			case ArgType::Array: s.set("type", "array"); break;
			case ArgType::Any: break;
			}
			return s;
		}
	}

	std::string opOf(const Value& _message)
	{
		const auto* op = _message.find("op");
		return op && op->isString() ? op->asString() : std::string();
	}

	const char* ownerName(const Owner _o)
	{
		switch(_o)
		{
		case Owner::Core: return "core";
		case Owner::Machine: return "machine";
		case Owner::Setup: return "setup";
		case Owner::Host: return "host";
		}
		return "";
	}

	const Command* CommandTable::find(const std::string& _op) const
	{
		for(const auto& c : m_commands)
			if(_op == c.op)
				return &c;
		return nullptr;
	}

	std::vector<std::string> CommandTable::check(const Command& _command, const Value& _message)
	{
		std::vector<std::string> errors;
		for(const auto& a : _command.args)
		{
			const auto* v = _message.find(a.name);
			if(!v)
			{
				if(!a.optional)
					errors.push_back(std::string(a.name) + (a.type == ArgType::Text ? ": missing text" : ": missing"
						+ std::string(a.type == ArgType::Integer || a.type == ArgType::Number ? " number" : "")));
				continue;
			}
			const auto range = [&](const double _d, const bool _integer)
			{
				if((_integer && _d != std::floor(_d)) || _d < a.min || _d > a.max)
					errors.push_back(std::string(a.name) + ": " + num(_d) + " is outside " + num(a.min) + ".." + num(a.max));
			};
			switch(a.type)
			{
			case ArgType::Integer:
			case ArgType::Number:
				if(!v->isNumber())
					errors.push_back(std::string(a.name) + ": missing number");
				else
					range(v->asNumber(), a.type == ArgType::Integer);
				break;
			case ArgType::IntegerOrNull:
				if(v->isNumber())
					range(v->asNumber(), true);
				else if(!v->isNull())
					errors.push_back(std::string(a.name) + ": expected a number or null");
				break;
			case ArgType::Text:
				if(!v->isString())
					errors.push_back(std::string(a.name) + ": missing text");
				break;
			case ArgType::Bool:
				if(!v->isBool() && !v->isNumber())
					errors.push_back(std::string(a.name) + ": expected true or false");
				break;
			case ArgType::Object:
				if(!v->isObject())
					errors.push_back(std::string(a.name) + ": expected an object");
				break;
			case ArgType::Array:
				if(!v->isArray())
					errors.push_back(std::string(a.name) + ": expected a list");
				break;
			case ArgType::Any:
				break;
			}
		}
		return errors;
	}

	Value CommandTable::schema() const
	{
		Value one = Value::array();
		for(const auto& c : m_commands)
		{
			Value props = Value::object();
			Value op = Value::object();
			op.set("const", c.op);
			props.set("op", std::move(op));
			Value id = Value::object();
			id.set("type", "integer");
			props.set("id", std::move(id));
			Value required = Value::array();
			required.push("op");
			for(const auto& a : c.args)
			{
				props.set(a.name, typeSchema(a));
				if(!a.optional)
					required.push(a.name);
			}
			Value s = Value::object();
			s.set("type", "object");
			s.set("description", std::string(ownerName(c.owner)) + (*c.help ? std::string(": ") + c.help : std::string()));
			s.set("required", std::move(required));
			s.set("properties", std::move(props));
			one.push(std::move(s));
		}
		Value root = Value::object();
		root.set("description", "Generated from the command table (deskCore::CommandTable::schema, P6): every command the page may send.");
		root.set("oneOf", std::move(one));
		return root;
	}
}
