#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace elektronData::json
{
	// The JSON Schema (2020-12) keywords the MD and MM data contracts use, so the
	// contracts are an executable spec (doc/modern-ux/*.schema.json): type (incl.
	// "integer"), const, enum, properties, required, additionalProperties,
	// items, prefixItems, minItems, maxItems, uniqueItems, minimum, maximum,
	// minLength, maxLength, pattern, oneOf, anyOf, allOf, not, if/then/else and
	// local $ref ("#/$defs/name"). Annotations (title, description, format, $id)
	// are ignored. Pure: no I/O.
	class Schema
	{
	public:
		explicit Schema(Value _root) : m_root(std::move(_root)) {}

		// Problems of _instance against the whole schema, one line each with a JSON
		// path ("$.tracks[3].lfo.shape1: 7 is above the maximum 5"). Empty = valid.
		std::vector<std::string> validate(const Value& _instance) const;
		// Against one definition of the schema ("pattern" = #/$defs/pattern).
		std::vector<std::string> validate(const Value& _instance, const std::string& _definition) const;

		const Value& root() const { return m_root; }

	private:
		void check(const Value& _schema, const Value& _v, const std::string& _path, std::vector<std::string>& _errors,
			int _depth) const;
		const Value* resolve(const std::string& _ref) const;

		Value m_root;
	};
}
