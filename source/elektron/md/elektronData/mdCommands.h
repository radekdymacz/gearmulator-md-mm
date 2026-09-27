#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Short Machinedrum control messages (manual Appendix C). Pure byte builders
	// and parsers; sending them is the transport's job.
	enum class MdStatus : uint8_t
	{
		GlobalSlot = 0x01,
		Kit = 0x02,
		Pattern = 0x04,
		Song = 0x08,
		SequencerMode = 0x10,	// 0 pattern mode, 1 song mode
		LockMode = 0x20,		// 0 classic, 1 extended
		Track = 0x22
	};

	struct MdStatusValue
	{
		MdStatus param;
		uint8_t value;
	};

	std::vector<uint8_t> mdStatusRequest(MdStatus _param);
	std::vector<uint8_t> mdSetStatus(MdStatus _param, uint8_t _value);
	std::optional<MdStatusValue> parseMdStatusResponse(const std::vector<uint8_t>& _sysex);

	std::vector<uint8_t> mdLoadPattern(uint8_t _slot);

	// Which dump a complete MD SysEx message is, by command byte; 0 if none.
	uint8_t mdDumpCommand(const std::vector<uint8_t>& _sysex);

	constexpr uint8_t g_mdGlobalDump = 0x50;
	constexpr uint8_t g_mdKitDump = 0x52;
	constexpr uint8_t g_mdPatternDump = 0x67;
	constexpr uint8_t g_mdSongDump = 0x69;
}
