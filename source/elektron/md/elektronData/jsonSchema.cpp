#include "jsonSchema.h"

#include <cmath>
#include <regex>

namespace elektronData::json
{
	namespace
	{
		bool equal(const Value& _a, const Value& _b)
		{
			if(_a.type() != _b.type())
				return false;
			switch(_a.type())
			{
			case Value::Type::Null: return true;
			case Value::Type::Bool: return _a.asBool() == _b.asBool();
			case Value::Type::Number: return _a.asNumber() == _b.asNumber();
			case Value::Type::String: return _a.asString() == _b.asString();
			case Value::Type::Array:
			{
				const auto& a = _a.asArray();
				const auto& b = _b.asArray();
				if(a.size() != b.size())
					return false;
				for(size_t i = 0; i < a.size(); ++i)
					if(!equal(a[i], b[i]))
						return false;
				return true;
			}
			case Value::Type::Object:
			{
				const auto& a = _a.asObject();
				if(a.size() != _b.asObject().size())
					return false;
				for(const auto& [k, v] : a)
				{
					const auto* o = _b.find(k);
					if(!o || !equal(v, *o))
						return false;
				}
				return true;
			}
			}
			return false;
		}

		bool isType(const Value& _v, const std::string& _t)
		{
			if(_t == "null") return _v.isNull();
			if(_t == "boolean") return _v.isBool();
			if(_t == "number") return _v.isNumber();
			if(_t == "integer") return _v.isNumber() && std::floor(_v.asNumber()) == _v.asNumber();
			if(_t == "string") return _v.isString();
			if(_t == "array") return _v.isArray();
			if(_t == "object") return _v.isObject();
			return false;
		}

		std::string brief(const Value& _v)
		{
			auto s = write(_v);
			return s.size() > 40 ? s.substr(0, 37) + "..." : s;
		}

		std::string num(const double _d)
		{
			if(std::floor(_d) == _d && std::fabs(_d) < 1e15)
				return std::to_string(static_cast<long long>(_d));
			return std::to_string(_d);
		}
	}

	const Value* Schema::resolve(const std::string& _ref) const
	{
		static const std::string prefix = "#/$defs/";
		if(_ref == "#")
			return &m_root;
		if(_ref.rfind(prefix, 0) != 0)
			return nullptr;
		const auto* defs = m_root.find("$defs");
		return defs ? defs->find(_ref.substr(prefix.size())) : nullptr;
	}

	std::vector<std::string> Schema::validate(const Value& _instance) const
	{
		std::vector<std::string> errors;
		check(m_root, _instance, "$", errors, 0);
		return errors;
	}

	std::vector<std::string> Schema::validate(const Value& _instance, const std::string& _definition) const
	{
		std::vector<std::string> errors;
		const auto* def = resolve("#/$defs/" + _definition);
		if(!def)
			return {"schema: no definition " + _definition};
		check(*def, _instance, "$", errors, 0);
		return errors;
	}

	void Schema::check(const Value& _s, const Value& _v, const std::string& _path, std::vector<std::string>& _errors,
		const int _depth) const
	{
		if(_depth > 64)
		{
			_errors.push_back(_path + ": schema nests too deep (a $ref loop?)");
			return;
		}
		if(_s.isBool())
		{
			if(!_s.asBool())
				_errors.push_back(_path + ": not allowed");
			return;
		}
		if(!_s.isObject())
			return;
		const auto err = [&](const std::string& _what) { _errors.push_back(_path + ": " + _what); };

		if(const auto* ref = _s.find("$ref"); ref && ref->isString())
		{
			const auto* target = resolve(ref->asString());
			if(!target)
				err("schema: unresolved " + ref->asString());
			else
				check(*target, _v, _path, _errors, _depth + 1);
		}
		if(const auto* t = _s.find("type"))
		{
			bool ok = false;
			if(t->isString())
				ok = isType(_v, t->asString());
			else if(t->isArray())
				for(const auto& x : t->asArray())
					ok = ok || (x.isString() && isType(_v, x.asString()));
			if(!ok)
			{
				err(brief(_v) + " is not of type " + brief(*t));
				return;
			}
		}
		if(const auto* c = _s.find("const"); c && !equal(*c, _v))
			err(brief(_v) + " is not " + brief(*c));
		if(const auto* e = _s.find("enum"); e && e->isArray())
		{
			bool found = false;
			for(const auto& x : e->asArray())
				found = found || equal(x, _v);
			if(!found)
				err(brief(_v) + " is not one of " + brief(*e));
		}
		if(_v.isNumber())
		{
			if(const auto* m = _s.find("minimum"); m && m->isNumber() && _v.asNumber() < m->asNumber())
				err(num(_v.asNumber()) + " is below the minimum " + num(m->asNumber()));
			if(const auto* m = _s.find("maximum"); m && m->isNumber() && _v.asNumber() > m->asNumber())
				err(num(_v.asNumber()) + " is above the maximum " + num(m->asNumber()));
		}
		if(_v.isString())
		{
			const auto n = static_cast<double>(_v.asString().size());
			if(const auto* m = _s.find("minLength"); m && m->isNumber() && n < m->asNumber())
				err("shorter than " + num(m->asNumber()));
			if(const auto* m = _s.find("maxLength"); m && m->isNumber() && n > m->asNumber())
				err("longer than " + num(m->asNumber()));
			if(const auto* p = _s.find("pattern"); p && p->isString())
			{
				try
				{
					if(!std::regex_search(_v.asString(), std::regex(p->asString(), std::regex::ECMAScript)))
						err(brief(_v) + " does not match " + p->asString());
				}
				catch(const std::regex_error&)
				{
					err("schema: bad pattern " + p->asString());
				}
			}
		}
		if(_v.isArray())
		{
			const auto& a = _v.asArray();
			const auto n = static_cast<double>(a.size());
			if(const auto* m = _s.find("minItems"); m && m->isNumber() && n < m->asNumber())
				err(num(n) + " items, fewer than " + num(m->asNumber()));
			if(const auto* m = _s.find("maxItems"); m && m->isNumber() && n > m->asNumber())
				err(num(n) + " items, more than " + num(m->asNumber()));
			size_t prefix = 0;
			if(const auto* p = _s.find("prefixItems"); p && p->isArray())
			{
				prefix = p->asArray().size();
				for(size_t i = 0; i < prefix && i < a.size(); ++i)
					check(p->asArray()[i], a[i], _path + "[" + std::to_string(i) + "]", _errors, _depth + 1);
			}
			if(const auto* items = _s.find("items"))
				for(size_t i = prefix; i < a.size(); ++i)
					check(*items, a[i], _path + "[" + std::to_string(i) + "]", _errors, _depth + 1);
			if(const auto* u = _s.find("uniqueItems"); u && u->isBool() && u->asBool())
				for(size_t i = 0; i < a.size(); ++i)
					for(size_t j = i + 1; j < a.size(); ++j)
						if(equal(a[i], a[j]))
							err("items " + std::to_string(i) + " and " + std::to_string(j) + " are the same");
		}
		if(_v.isObject())
		{
			if(const auto* r = _s.find("required"); r && r->isArray())
				for(const auto& k : r->asArray())
					if(k.isString() && !_v.find(k.asString()))
						err("missing " + k.asString());
			const auto* props = _s.find("properties");
			if(props && props->isObject())
				for(const auto& [k, sub] : props->asObject())
					if(const auto* member = _v.find(k))
						check(sub, *member, _path + "." + k, _errors, _depth + 1);
			if(const auto* extra = _s.find("additionalProperties"))
				for(const auto& [k, member] : _v.asObject())
					if(!props || !props->find(k))
						check(*extra, member, _path + "." + k, _errors, _depth + 1);
		}
		const auto count = [&](const Value& _sub)
		{
			std::vector<std::string> e;
			check(_sub, _v, _path, e, _depth + 1);
			return e;
		};
		if(const auto* all = _s.find("allOf"); all && all->isArray())
			for(const auto& sub : all->asArray())
				check(sub, _v, _path, _errors, _depth + 1);
		if(const auto* any = _s.find("anyOf"); any && any->isArray())
		{
			bool ok = false;
			for(const auto& sub : any->asArray())
				ok = ok || count(sub).empty();
			if(!ok)
				err("matches none of anyOf");
		}
		if(const auto* one = _s.find("oneOf"); one && one->isArray())
		{
			size_t matches = 0;
			std::vector<std::string> closest;
			for(const auto& sub : one->asArray())
			{
				auto e = count(sub);
				// The closest branch: the one whose discriminating consts match (no error on a
				// direct member), then the fewest problems.
				const auto misses = [&](const std::vector<std::string>& _e)
				{
					size_t n = 0;
					for(const auto& x : _e)
						n += x.find(" is not \"") != std::string::npos && x.find('.', _path.size() + 1) == std::string::npos;
					return n;
				};
				if(e.empty())
					++matches;
				else if(closest.empty() || misses(e) < misses(closest) || (misses(e) == misses(closest) && e.size() < closest.size()))
					closest = std::move(e);
			}
			if(matches == 0)
			{
				err("matches none of oneOf; the closest:");
				_errors.insert(_errors.end(), closest.begin(), closest.end());
			}
			else if(matches > 1)
				err("matches " + std::to_string(matches) + " of oneOf");
		}
		if(const auto* no = _s.find("not"); no && count(*no).empty())
			err("matches a schema it must not");
		if(const auto* i = _s.find("if"))
		{
			const bool holds = count(*i).empty();
			if(const auto* then = holds ? _s.find("then") : _s.find("else"))
				check(*then, _v, _path, _errors, _depth + 1);
		}
	}
}
