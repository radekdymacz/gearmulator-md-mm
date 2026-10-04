#include "json.h"

#include <cassert>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
		if(auto* v = find(_key))
		{
			assert(false && "json::Value::set: the key is already set (use put to replace)");
			*v = std::move(_value);
			return *this;
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
		// Finite by the bits, not by std::isfinite: release builds use -Ofast, whose finite-math assumption folds
		// std::isfinite to true. The library is also built without fast math (CMakeLists.txt); this holds either way.
		bool isFinite(const double _n)
		{
			uint64_t bits;
			std::memcpy(&bits, &_n, sizeof(bits));
			constexpr uint64_t exponent = 0x7ff0000000000000ull;
			return (bits & exponent) != exponent;
		}

		// The length of the UTF-8 sequence at _s[_i], or 0 when it is not a valid one (overlong, a surrogate, past
		// U+10FFFF, or cut short).
		size_t utf8Length(const std::string& _s, const size_t _i)
		{
			const auto at = [&](const size_t _k) { return static_cast<unsigned char>(_s[_k]); };
			const auto c = at(_i);
			size_t n;
			uint32_t code;
			if(c >= 0xc2 && c <= 0xdf)
			{
				n = 2;
				code = c & 0x1fu;
			}
			else if(c >= 0xe0 && c <= 0xef)
			{
				n = 3;
				code = c & 0x0fu;
			}
			else if(c >= 0xf0 && c <= 0xf4)
			{
				n = 4;
				code = c & 0x07u;
			}
			else
				return 0;
			if(_i + n > _s.size())
				return 0;
			for(size_t k = 1; k < n; ++k)
			{
				if((at(_i + k) & 0xc0) != 0x80)
					return 0;
				code = (code << 6) | (at(_i + k) & 0x3fu);
			}
			if((n == 3 && code < 0x800) || (n == 4 && (code < 0x10000 || code > 0x10ffff)) || (code >= 0xd800 && code <= 0xdfff))
				return 0;
			return n;
		}

		void appendUtf8(std::string& _out, const uint32_t _code)
		{
			if(_code < 0x80)
				_out += static_cast<char>(_code);
			else if(_code < 0x800)
			{
				_out += static_cast<char>(0xc0 | (_code >> 6));
				_out += static_cast<char>(0x80 | (_code & 0x3f));
			}
			else if(_code < 0x10000)
			{
				_out += static_cast<char>(0xe0 | (_code >> 12));
				_out += static_cast<char>(0x80 | ((_code >> 6) & 0x3f));
				_out += static_cast<char>(0x80 | (_code & 0x3f));
			}
			else
			{
				_out += static_cast<char>(0xf0 | (_code >> 18));
				_out += static_cast<char>(0x80 | ((_code >> 12) & 0x3f));
				_out += static_cast<char>(0x80 | ((_code >> 6) & 0x3f));
				_out += static_cast<char>(0x80 | (_code & 0x3f));
			}
		}

		// Strings are UTF-8: a valid multi-byte sequence is written as it is. A byte that is not part of one (text
		// that is not UTF-8) is escaped as U+00XX (its Latin-1 reading), so the output is always valid JSON.
		void writeString(std::string& _out, const std::string& _s)
		{
			_out += '"';
			for(size_t i = 0; i < _s.size(); ++i)
			{
				const char c = _s[i];
				const auto u = static_cast<unsigned char>(c);
				switch(c)
				{
				case '"': _out += "\\\""; break;
				case '\\': _out += "\\\\"; break;
				case '\n': _out += "\\n"; break;
				case '\r': _out += "\\r"; break;
				case '\t': _out += "\\t"; break;
				default:
					if(u >= 0x80)
					{
						if(const auto n = utf8Length(_s, i))
						{
							_out.append(_s, i, n);
							i += n - 1;
							break;
						}
					}
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

		// Locale-independent: the C library formats with the current LC_NUMERIC decimal point (a comma under
		// pl_PL, for example). Whatever it wrote between the digits is put back as '.'. A non-finite value has no
		// JSON form and is written as null.
		void writeNumber(std::string& _out, const double _n)
		{
			if(!isFinite(_n))
			{
				_out += "null";
				return;
			}
			char text[48];
			if(std::floor(_n) == _n && std::fabs(_n) < 9.0e15)
				std::snprintf(text, sizeof(text), "%.0f", _n);
			else
				std::snprintf(text, sizeof(text), "%.17g", _n);
			bool point = false;
			for(const char* p = text; *p; ++p)
			{
				const char c = *p;
				if((c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e' || c == 'E')
				{
					_out += c;
					continue;
				}
				if(!point)
					_out += '.';
				point = true;
			}
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

			bool digits()
			{
				const auto from = m_pos;
				while(m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
					++m_pos;
				return m_pos > from;
			}

			// The JSON number grammar, then strtod on the token with '.' replaced by the current locale's decimal
			// point, so the result does not depend on LC_NUMERIC. strtod alone would also take what JSON does not
			// (nan, inf, hex, a leading + or .).
			std::optional<Value> number()
			{
				const auto start = m_pos;
				if(m_pos < m_text.size() && m_text[m_pos] == '-')
					++m_pos;
				if(m_pos < m_text.size() && m_text[m_pos] == '0')
					++m_pos;
				else if(!digits())
					return fail("bad number");
				if(m_pos < m_text.size() && m_text[m_pos] == '.')
				{
					++m_pos;
					if(!digits())
						return fail("bad number");
				}
				if(m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
				{
					++m_pos;
					if(m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-'))
						++m_pos;
					if(!digits())
						return fail("bad number");
				}
				std::string token;
				const char* point = std::localeconv()->decimal_point;
				for(size_t i = start; i < m_pos; ++i)
				{
					if(m_text[i] == '.' && point && *point)
						token += point;
					else
						token += m_text[i];
				}
				char* end = nullptr;
				const double n = std::strtod(token.c_str(), &end);
				if(end != token.c_str() + token.size() || !isFinite(n))
				{
					m_pos = start;
					return fail("bad number");
				}
				return Value(n);
			}

			// Four hex digits of a u-escape at m_pos.
			std::optional<uint32_t> hex4()
			{
				if(m_pos + 4 > m_text.size())
					return {};
				uint32_t code = 0;
				for(size_t k = 0; k < 4; ++k)
				{
					const char h = m_text[m_pos + k];
					code <<= 4;
					if(h >= '0' && h <= '9')
						code |= static_cast<uint32_t>(h - '0');
					else if(h >= 'a' && h <= 'f')
						code |= static_cast<uint32_t>(h - 'a' + 10);
					else if(h >= 'A' && h <= 'F')
						code |= static_cast<uint32_t>(h - 'A' + 10);
					else
						return {};
				}
				m_pos += 4;
				return code;
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
						// A code point as UTF-8; above U+FFFF as a surrogate pair. A lone surrogate is refused.
						auto code = hex4();
						if(!code)
						{
							fail("bad \\u escape");
							return {};
						}
						if(*code >= 0xdc00 && *code <= 0xdfff)
						{
							fail("lone low surrogate in \\u escape");
							return {};
						}
						if(*code >= 0xd800 && *code <= 0xdbff)
						{
							if(m_pos + 2 > m_text.size() || m_text[m_pos] != '\\' || m_text[m_pos + 1] != 'u')
							{
								fail("lone high surrogate in \\u escape");
								return {};
							}
							m_pos += 2;
							const auto low = hex4();
							if(!low || *low < 0xdc00 || *low > 0xdfff)
							{
								fail("bad surrogate pair in \\u escape");
								return {};
							}
							*code = 0x10000 + ((*code - 0xd800) << 10) + (*low - 0xdc00);
						}
						appendUtf8(s, *code);
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

	bool operator==(const Value& _a, const Value& _b)
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
				if(!(a[i] == b[i]))
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
				if(!o || !(v == *o))
					return false;
			}
			return true;
		}
		}
		return false;
	}
}
