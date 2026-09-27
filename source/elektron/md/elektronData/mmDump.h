#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B user-data dumps on the wire (MM-P0-RESULT §1):
	//   F0 00 20 3C 03 00 <command> <version> <revision> <position>
	//   <7-bit packing of a run-length stream>  <14-bit checksum> <14-bit length> F7
	// The run-length stream: a byte with bit 7 set is a count 0x80|n, followed
	// by the value it repeats n times; any other byte stands for itself.
	//
	// packMmDump reproduces the firmware's own choices exactly (every factory
	// dump round-trips byte for byte):
	//   - runs of 2-127 equal bytes become (0x80|n, v); longer runs split at 127;
	//   - a single byte with bit 7 set becomes (0x81, v);
	//   - when the run-length stream is a multiple of 7 bytes long, one more 0x00
	//     follows (the high-bit byte of an empty last 7-bit group).
	// The firmware drops a dump that is not run-length coded this way (an
	// uncompressed pattern is too long for it).
	constexpr uint8_t g_mmProductId = 0x03;

	constexpr uint8_t g_mmGlobalDump = 0x50;
	constexpr uint8_t g_mmKitDump = 0x52;
	constexpr uint8_t g_mmPatternDump = 0x67;
	constexpr uint8_t g_mmSongDump = 0x69;

	struct MmDump
	{
		uint8_t command = 0;
		uint8_t version = 0;
		uint8_t revision = 0;
		uint8_t position = 0;
		std::vector<uint8_t> raw;	// the payload after the run-length decode

		bool operator==(const MmDump& _o) const
		{
			return command == _o.command && version == _o.version && revision == _o.revision
				&& position == _o.position && raw == _o.raw;
		}
	};

	// Empty unless the message is a complete, checksum-valid MM dump.
	std::optional<MmDump> unpackMmDump(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> packMmDump(const MmDump& _dump);

	// The run-length stream alone (exposed for tests).
	std::vector<uint8_t> mmRunLengthEncode(const std::vector<uint8_t>& _raw);
	std::optional<std::vector<uint8_t>> mmRunLengthDecode(const std::vector<uint8_t>& _stream);

	// F0 00 20 3C 03 00 <command> <value> F7 (dump requests, loads, saves).
	std::vector<uint8_t> mmRequest(uint8_t _command, uint8_t _value);
	// F0 00 20 3C 03 00 <command> <args...> F7
	std::vector<uint8_t> mmMessage(uint8_t _command, const std::vector<uint8_t>& _args);
	// Is it an MM SysEx message with this command byte?
	bool isMmMessage(const std::vector<uint8_t>& _sysex, uint8_t _command);
}
