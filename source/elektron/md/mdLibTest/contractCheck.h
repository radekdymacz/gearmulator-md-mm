#pragma once

// P6, the executable spec in the firmware smoke tests: every message a desk publishes to
// its page is checked against the contract's JSON Schema ($defs/message).

#include "elektronData/json.h"
#include "elektronData/jsonSchema.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace contractCheck
{
	class Checker
	{
	public:
		explicit Checker(const char* _schemaPath)
		{
			std::ifstream in(_schemaPath);
			const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			if(const auto root = elektronData::json::parse(text))
				m_schema.emplace(*root);
		}

		void operator()(const elektronData::json::Value& _message)
		{
			++m_seen;
			if(!m_schema)
				return;
			const auto problems = m_schema->validate(_message, "message");
			if(problems.empty())
				return;
			const auto* type = _message.find("type");
			const auto name = type && type->isString() ? type->asString() : std::string("?");
			if(m_bad++ < 5)
				for(const auto& p : problems)
					std::printf("  contract: %s message: %s\n", name.c_str(), p.c_str());
		}

		bool loaded() const { return m_schema.has_value(); }
		size_t seen() const { return m_seen; }
		size_t bad() const { return m_bad; }
		std::string summary() const
		{
			return std::to_string(m_seen) + " published messages on the contract, " + std::to_string(m_bad) + " off it";
		}

	private:
		std::optional<elektronData::json::Schema> m_schema;
		size_t m_seen = 0;
		size_t m_bad = 0;
	};
}
