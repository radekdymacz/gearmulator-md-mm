#pragma once

#include "json.h"

#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	struct MdPattern;
	struct MdKit;
	struct MdSong;
	struct MdGlobal;

	// The MD Desk data contract, version 1: see doc/modern-ux/data-contract.md and
	// doc/modern-ux/md-data-contract.schema.json. Values are carried in firmware
	// units so value -> JSON -> value is lossless for every firmware dump.
	//
	// fromJson reports every problem it finds (JSON path + reason) and returns a
	// value only when the document is well formed AND within the hardware limits
	// of elektronData::validate.
	// Documents are version 2 (P6: the firmware's pass-through fields under "firmware"); readers
	// also take version 1.
	constexpr int g_mdContractVersion = 2;

	json::Value patternToJson(const MdPattern& _pattern);
	json::Value kitToJson(const MdKit& _kit);
	json::Value songToJson(const MdSong& _song);
	json::Value globalToJson(const MdGlobal& _global);

	std::optional<MdPattern> patternFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MdKit> kitFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MdSong> songFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MdGlobal> globalFromJson(const json::Value& _json, std::vector<std::string>& _errors);

	// "A01".."H16" for pattern slots 0-127.
	std::string mdPatternName(unsigned _slot);
}
