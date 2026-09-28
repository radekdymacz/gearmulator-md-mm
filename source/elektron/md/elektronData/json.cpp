#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace elektronData::json
{
	const Value* Value::find(const std::string& _key) const
	{
		if(m_type != Type::Object)
			return nullptr;
		for(const auto& [key, value] : m_object)
			if(key == _key)
				return &value;
		return nullptr;
	}

	Value* Value::find(const std::string& _key)
	{
		return const_cast<Value*>(static_cast<const Value&>(*this).find(_key));
	}

	Value& Value::put(const std::string& _key, Value _value)
	{
		if(auto* v = find(_key))
		{
			*v = std::move(_value);
			return *this;
		}
		return set(_key, std::move(_value));
	}

	Value& Value::set(const std::string& _key, Value _value)
	{
		if(m_type != Type::Object)
		{
			*this = object();
		}
		m_object.emplace_back(_key, std::move(_value));
		return *this;
	}

	Value& Value::push(Value _value)
	{
		if(m_type != Type::Array)
			*this = array();
		m_array.push_back(std::move(_value));
		return *this;
	}

	namespace
	{
		void writeString(std::string& _out, const std::string& _s)
		{
			_out += '"';
			for(const auto c : _s)
			{
				const auto u = static_cast<unsigned char>(c);
				switch(c)
				{
				case '"': _out += "\\\""; break;
				case '\\': _out += "\\\\"; break;
				case '\n': _out += "\\n"; break;
				case '\r': _out += "\\r"; break;
				case '\t': _out += "\\t"; break;
				default:
					if(u < 0x20 || u >= 0x7f)
					{
						char esc[8];
						std::snprintf(esc, sizeof(esc), "\\u%04x", u);
						_out += esc;
					}
					else
						_out += c;
				}
			}
			_out += '"';
		}

		void writeNumber(std::string& _out, const double _n)
		{
			char text[32];
			if(std::floor(_n) == _n && std::fabs(_n) < 9.0e15)
				std::snprintf(text, sizeof(text), "%.0f", _n);
			else
				std::snprintf(text, sizeof(text), "%.17g", _n);
			_out += text;
		}

		void newline(std::string& _out, const int _indent, const int _depth)
		{
			if(_indent < 0)
				return;
			_out += '\n';
			_out.append(static_cast<size_t>(_indent * _depth), ' ');
		}

		// Arrays of plain numbers stay on one line even when indenting.
		bool isFlat(const Value::Array& _a)
		{
			for(const auto& v : _a)
				if(v.isArray() || v.isObject())
					return false;
			return true;
		}

		void writeValue(std::string& _out, const Value& _v, const int _indent, const int _depth)
		{
			switch(_v.type())
			{
			case Value::Type::Null: _out += "null"; break;
			case Value::Type::Bool: _out += _v.asBool() ? "true" : "false"; break;
			case Value::Type::Number: writeNumber(_out, _v.asNumber()); break;
			case Value::Type::String: writeString(_out, _v.asString()); break;
			case Value::Type::Array:
			{
				const auto& a = _v.asArray();
				const bool flat = isFlat(a);
				_out += '[';
				for(size_t i = 0; i < a.size(); ++i)
				{
					if(i)
						_out += flat && _indent >= 0 ? ", " : ",";
					if(!flat)
						newline(_out, _indent, _depth + 1);
					writeValue(_out, a[i], _indent, _depth + 1);
				}
				if(!flat && !a.empty())
					newline(_out, _indent, _depth);
				_out += ']';
				break;
			}
			case Value::Type::Object:
			{
				const auto& o = _v.asObject();
				_out += '{';
				for(size_t i = 0; i < o.size(); ++i)
				{
					if(i)
						_out += ',';
					newline(_out, _indent, _depth + 1);
					writeString(_out, o[i].first);
					_out += _indent >= 0 ? ": " : ":";
					writeValue(_out, o[i].second, _indent, _depth + 1);
				}
				if(!o.empty())
					newline(_out, _indent, _depth);
				_out += '}';
				break;
			}
			}
		}

		class Parser
		{
		public:
			explicit Parser(const std::string& _text) : m_text(_text) {}

			std::optional<Value> document()
			{
				auto v = value(0);
				skipSpace();
				if(v && m_pos != m_text.size())
					return fail("trailing characters");
				return v;
			}

			std::string error;

		private:
			static constexpr int g_maxDepth = 64;

			std::optional<Value> fail(const char* _what)
			{
				if(error.empty())
					error = std::string(_what) + " at offset " + std::to_string(m_pos);
				return {};
			}

			void skipSpace()
			{
				while(m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\n'
					|| m_text[m_pos] == '\r' || m_text[m_pos] == '\t'))
					++m_pos;
			}

			bool literal(const char* _word)
			{
				size_t n = 0;
				while(_word[n])
					++n;
				if(m_text.compare(m_pos, n, _word) != 0)
					return false;
				m_pos += n;
				return true;
			}

			std::optional<Value> value(const int _depth)
			{
				if(_depth > g_maxDepth)
					return fail("nesting too deep");
				skipSpace();
				if(m_pos >= m_text.size())
					return fail("unexpected end");
				const char c = m_text[m_pos];
				if(c == '{')
					return object(_depth);
				if(c == '[')
					return array(_depth);
				if(c == '"')
				{
					auto s = string();
					if(!s)
						return {};
					return Value(std::move(*s));
				}
				if(literal("true"))
					return Value(true);
				if(literal("false"))
					return Value(false);
				if(literal("null"))
					return Value();
				return number();
			}

			std::optional<Value> number()
			{
				const char* begin = m_text.c_str() + m_pos;
				char* end = nullptr;
				const double n = std::strtod(begin, &end);
				if(end == begin || !std::isfinite(n))
					return fail("bad number");
				m_pos += static_cast<size_t>(end - begin);
				return Value(n);
			}

			std::optional<std::string> string()
			{
				++m_pos;
				std::string s;
				while(m_pos < m_text.size())
				{
					const char c = m_text[m_pos++];
					if(c == '"')
						return s;
					if(static_cast<unsigned char>(c) < 0x20)
						break;
					if(c != '\\')
					{
						s += c;
						continue;
					}
					if(m_pos >= m_text.size())
						break;
					const char e = m_text[m_pos++];
					switch(e)
					{
					case '"': s += '"'; break;
					case '\\': s += '\\'; break;
					case '/': s += '/'; break;
					case 'b': s += '\b'; break;
					case 'f': s += '\f'; break;
					case 'n': s += '\n'; break;
					case 'r': s += '\r'; break;
					case 't': s += '\t'; break;
					case 'u':
					{
						if(m_pos + 4 > m_text.size())
							return {};
						const auto code = std::strtoul(m_text.substr(m_pos, 4).c_str(), nullptr, 16);
						m_pos += 4;
						// The contract only needs Latin-1; wider code points are refused.
						if(code > 0xff)
						{
							fail("unsupported \\u escape");
							return {};
						}
						s += static_cast<char>(code);
						break;
					}
					default:
						fail("bad escape");
						return {};
					}
				}
				fail("unterminated string");
				return {};
			}

			std::optional<Value> array(const int _depth)
			{
				++m_pos;
				Value a = Value::array();
				skipSpace();
				if(m_pos < m_text.size() && m_text[m_pos] == ']')
				{
					++m_pos;
					return a;
				}
				while(true)
				{
					auto v = value(_depth + 1);
					if(!v)
						return {};
					a.push(std::move(*v));
					skipSpace();
					if(m_pos < m_text.size() && m_text[m_pos] == ',')
					{
						++m_pos;
						continue;
					}
					if(m_pos < m_text.size() && m_text[m_pos] == ']')
					{
						++m_pos;
						return a;
					}
					return fail("expected , or ]");
				}
			}

			std::optional<Value> object(const int _depth)
			{
				++m_pos;
				Value o = Value::object();
				skipSpace();
				if(m_pos < m_text.size() && m_text[m_pos] == '}')
				{
					++m_pos;
					return o;
				}
				while(true)
				{
					skipSpace();
					if(m_pos >= m_text.size() || m_text[m_pos] != '"')
						return fail("expected key");
					auto key = string();
					if(!key)
						return {};
					skipSpace();
					if(m_pos >= m_text.size() || m_text[m_pos] != ':')
						return fail("expected :");
					++m_pos;
					auto v = value(_depth + 1);
					if(!v)
						return {};
					o.set(*key, std::move(*v));
					skipSpace();
					if(m_pos < m_text.size() && m_text[m_pos] == ',')
					{
						++m_pos;
						continue;
					}
					if(m_pos < m_text.size() && m_text[m_pos] == '}')
					{
						++m_pos;
						return o;
					}
					return fail("expected , or }");
				}
			}

			const std::string& m_text;
			size_t m_pos = 0;
		};
	}

	std::string write(const Value& _value, const int _indent)
	{
		std::string out;
		writeValue(out, _value, _indent, 0);
		return out;
	}

	std::optional<Value> parse(const std::string& _text, std::string* _error)
	{
		Parser p(_text);
		auto v = p.document();
		if(!v && _error)
			*_error = p.error;
		return v;
	}
}
