#pragma once

#include "json.h"

#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	struct MmPattern;
	struct MmKit;
	struct MmSong;
	struct MmGlobal;

	// The Monomachine Editor data contract, version 1: doc/modern-ux/mm-data-contract.md
	// and mm-data-contract.schema.json. Values are in firmware units, so
	// value -> JSON -> value is lossless for every firmware dump: bytes that are
	// not understood yet ride along as hex ("hidden").
	//
	// fromJson reports every problem (JSON path + reason) and returns a value only
	// when the document is well formed AND within mmValidate's limits.
	constexpr int g_mmContractVersion = 1;

	json::Value mmPatternToJson(const MmPattern& _pattern);
	json::Value mmKitToJson(const MmKit& _kit);
	json::Value mmSongToJson(const MmSong& _song);
	json::Value mmGlobalToJson(const MmGlobal& _global);

	std::optional<MmPattern> mmPatternFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MmKit> mmKitFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MmSong> mmSongFromJson(const json::Value& _json, std::vector<std::string>& _errors);
	std::optional<MmGlobal> mmGlobalFromJson(const json::Value& _json, std::vector<std::string>& _errors);

	// "A01".."H16" for pattern slots 0-127.
	std::string mmPatternName(unsigned _slot);
}
