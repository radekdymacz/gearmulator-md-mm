#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace elektronData::json
{
	// A minimal JSON value for the MD Desk data contract: no dependencies, object
	// keys keep insertion order, numbers are doubles (every contract number is an
	// integer below 2^32 or a BPM with at most a few decimals).
	class Value
	{
	public:
		enum class Type
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object
		};

		using Array = std::vector<Value>;
		using Member = std::pair<std::string, Value>;
		using Object = std::vector<Member>;

		Value() = default;
		Value(std::nullptr_t) {}
		Value(bool _b) : m_type(Type::Bool), m_bool(_b) {}
		Value(int _n) : m_type(Type::Number), m_number(_n) {}
		Value(unsigned _n) : m_type(Type::Number), m_number(_n) {}
		Value(long _n) : m_type(Type::Number), m_number(static_cast<double>(_n)) {}
		Value(unsigned long _n) : m_type(Type::Number), m_number(static_cast<double>(_n)) {}
		Value(long long _n) : m_type(Type::Number), m_number(static_cast<double>(_n)) {}
		Value(unsigned long long _n) : m_type(Type::Number), m_number(static_cast<double>(_n)) {}
		Value(double _n) : m_type(Type::Number), m_number(_n) {}
		Value(const char* _s) : m_type(Type::String), m_string(_s) {}
		Value(std::string _s) : m_type(Type::String), m_string(std::move(_s)) {}
		Value(Array _a) : m_type(Type::Array), m_array(std::move(_a)) {}
		Value(Object _o) : m_type(Type::Object), m_object(std::move(_o)) {}

		static Value array() { return Value(Array{}); }
		static Value object() { return Value(Object{}); }

		Type type() const { return m_type; }
		bool isNull() const { return m_type == Type::Null; }
		bool isBool() const { return m_type == Type::Bool; }
		bool isNumber() const { return m_type == Type::Number; }
		bool isString() const { return m_type == Type::String; }
		bool isArray() const { return m_type == Type::Array; }
		bool isObject() const { return m_type == Type::Object; }

		bool asBool() const { return m_bool; }
		double asNumber() const { return m_number; }
		const std::string& asString() const { return m_string; }
		const Array& asArray() const { return m_array; }
		Array& asArray() { return m_array; }
		const Object& asObject() const { return m_object; }

		// Object access: nullptr when absent or not an object.
		const Value* find(const std::string& _key) const;
		Value* find(const std::string& _key);
		// Object building: replaces the member when it exists, appends it otherwise.
		Value& put(const std::string& _key, Value _value);
		// Object building: a new member. A key set twice is a bug (asserted); a release build keeps
		// one member per key (the last), so a document never carries the same key twice.
		Value& set(const std::string& _key, Value _value);
		// Array building.
		Value& push(Value _value);

	private:
		Type m_type = Type::Null;
		bool m_bool = false;
		double m_number = 0;
		std::string m_string;
		Array m_array;
		Object m_object;
	};

	// Values are equal when they are the same JSON value: objects by their members, whatever the order.
	bool operator==(const Value& _a, const Value& _b);
	inline bool operator!=(const Value& _a, const Value& _b) { return !(_a == _b); }

	std::string write(const Value& _value, int _indent = -1);
	// Empty on any syntax error; _error (optional) gets a short description.
	std::optional<Value> parse(const std::string& _text, std::string* _error = nullptr);
}
