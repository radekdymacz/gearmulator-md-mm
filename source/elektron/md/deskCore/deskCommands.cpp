#include "deskCommands.h"

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
			case ArgType::Text:
				s.set("type", "string");
				if(!_a.oneOf.empty())
				{
					Value e = Value::array();
					for(const auto* v : _a.oneOf)
						e.push(v);
					s.set("enum", std::move(e));
				}
				break;
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

	Value resultMessage(const Value& _command, const std::vector<std::string>& _errors, const std::string& _note)
	{
		Value r = Value::object();
		r.set("type", "result");
		r.set("op", opOf(_command));
		if(const auto* id = _command.find("id"); id && id->isNumber())
			r.set("id", *id);
		r.set("ok", _errors.empty());
		Value errors = Value::array();
		for(const auto& e : _errors)
			errors.push(e);
		r.set("errors", std::move(errors));
		r.set("note", _errors.empty() ? _note : std::string());
		return r;
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

	void checkArg(const Arg& _a, const Value* _v, std::vector<std::string>& _errors)
	{
		if(!_v)
		{
			if(!_a.optional)
				_errors.push_back(std::string(_a.name) + (_a.type == ArgType::Text ? ": missing text" : ": missing"
					+ std::string(_a.type == ArgType::Integer || _a.type == ArgType::Number ? " number" : "")));
			return;
		}
		const auto range = [&](const double _d, const bool _integer)
		{
			if((_integer && _d != std::floor(_d)) || _d < _a.min || _d > _a.max)
				_errors.push_back(std::string(_a.name) + ": " + num(_d) + " is outside " + num(_a.min) + ".." + num(_a.max));
		};
		switch(_a.type)
		{
		case ArgType::Integer:
		case ArgType::Number:
			if(!_v->isNumber())
				_errors.push_back(std::string(_a.name) + ": missing number");
			else
				range(_v->asNumber(), _a.type == ArgType::Integer);
			break;
		case ArgType::IntegerOrNull:
			if(_v->isNumber())
				range(_v->asNumber(), true);
			else if(!_v->isNull())
				_errors.push_back(std::string(_a.name) + ": expected a number or null");
			break;
		case ArgType::Text:
			if(!_v->isString())
				_errors.push_back(std::string(_a.name) + ": missing text");
			else if(!_a.oneOf.empty())
			{
				bool found = false;
				std::string list;
				for(const auto* v : _a.oneOf)
				{
					found = found || _v->asString() == v;
					list += (list.empty() ? "" : ", ") + std::string(v);
				}
				if(!found)
					_errors.push_back(std::string(_a.name) + ": expected one of " + list);
			}
			break;
		case ArgType::Bool:
			if(!_v->isBool() && !_v->isNumber())
				_errors.push_back(std::string(_a.name) + ": expected true or false");
			break;
		case ArgType::Object:
			if(!_v->isObject())
				_errors.push_back(std::string(_a.name) + ": expected an object");
			break;
		case ArgType::Array:
			if(!_v->isArray())
				_errors.push_back(std::string(_a.name) + ": expected a list");
			break;
		case ArgType::Any:
			break;
		}
	}

	Value commandSchema(const char* _op, const Owner _owner, const char* _help, const std::vector<Arg>& _args)
	{
		Value props = Value::object();
		Value op = Value::object();
		op.set("const", _op);
		props.set("op", std::move(op));
		Value id = Value::object();
		id.set("type", "integer");
		props.set("id", std::move(id));
		// Every command may carry a gesture (one undo step for a drag) and force (the answer to an ask).
		Value g = Value::object();
		g.set("type", "integer");
		g.set("minimum", 0);
		props.set("g", std::move(g));
		Value force = Value::object();
		Value ft = Value::array();
		ft.push("boolean");
		ft.push("integer");
		force.set("type", std::move(ft));
		props.set("force", std::move(force));
		Value required = Value::array();
		required.push("op");
		for(const auto& a : _args)
		{
			props.set(a.name, typeSchema(a));
			if(!a.optional)
				required.push(a.name);
		}
		Value s = Value::object();
		s.set("type", "object");
		s.set("description", std::string(ownerName(_owner)) + (*_help ? std::string(": ") + _help : std::string()));
		s.set("required", std::move(required));
		s.set("properties", std::move(props));
		return s;
	}
}
