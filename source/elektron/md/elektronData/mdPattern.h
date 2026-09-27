#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Machinedrum OS 1.63 pattern dump (SysEx command 0x67), as a plain value.
	//
	// Layout (offsets into the complete F0..F7 message), verified against dumps
	// produced by the emulated firmware and edits made on its virtual panel:
	//   0x000        F0 00 20 3C 02 00 67 <version> <revision> <position>
	//   0x00a  74    7-bit: trigs, 16 x u32 big-endian, bit n = step n+1 (steps 1-32)
	//   0x054  74    7-bit: lock masks, 16 x u32, bit n = parameter n is locked
	//   0x09e  19    7-bit: accent, slide, swing patterns (u32 each), swing amount
	//   0x0b1   6    accent amount, length, double tempo, scale, kit, locked rows
	//   0x0b7 2341   7-bit: 64 lock rows x 32 steps (0xff = no lock on that step)
	//   0x9dc  234   7-bit: accent/slide/swing edit-all, per-track accent/slide/swing
	//   0xac6 2647   7-bit, extended (64-step) dumps only: steps 33-64 of the trig,
	//                pattern-wide accent/slide/swing, lock row and per-track fields
	//   end     5    checksum (14 bit), length (14 bit), F7
	//
	// Lock rows are assigned in (track, parameter) ascending order over the set
	// lock-mask bits; the firmware re-sorts rows into this order when it dumps.
	// Unused rows are zero.
	struct MdPattern
	{
		static constexpr size_t g_tracks = 16;
		static constexpr size_t g_maxSteps = 64;
		static constexpr size_t g_lockRows = 64;
		static constexpr size_t g_shortDumpSize = 0xacb;
		static constexpr size_t g_extendedDumpSize = 0x1522;
		static constexpr uint8_t g_noLock = 0xff;

		using LockRow = std::array<uint8_t, g_maxSteps>;

		uint8_t version = 3;
		uint8_t revision = 1;
		uint8_t position = 0;
		bool extended = true;

		std::array<uint64_t, g_tracks> trigs{};
		std::array<uint32_t, g_tracks> lockMasks{};
		uint64_t accentPattern = 0;
		uint64_t slidePattern = 0;
		uint64_t swingPattern = 0;
		uint32_t swingAmount = 0;

		uint8_t accentAmount = 0;
		uint8_t length = 16;
		uint8_t tempoMultiplier = 0;
		uint8_t scale = 0;
		uint8_t kit = 0;
		uint8_t lockedRows = 0;

		std::array<LockRow, g_lockRows> lockRows{};

		uint32_t accentEditAll = 1;
		uint32_t slideEditAll = 1;
		uint32_t swingEditAll = 1;
		std::array<uint64_t, g_tracks> trackAccent{};
		std::array<uint64_t, g_tracks> trackSlide{};
		std::array<uint64_t, g_tracks> trackSwing{};

		bool operator==(const MdPattern& _other) const;
		bool operator!=(const MdPattern& _other) const { return !(*this == _other); }
	};

	// Returns no value for anything that is not a complete, checksum-valid MD
	// pattern dump of a known size.
	std::optional<MdPattern> decodeMdPattern(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMdPattern(const MdPattern& _pattern);

	std::vector<uint8_t> mdPatternRequest(uint8_t _position);

	// Pure edits. Track and step indices are zero-based.
	bool hasTrig(const MdPattern& _pattern, size_t _track, size_t _step);
	MdPattern withTrig(const MdPattern& _pattern, size_t _track, size_t _step, bool _on);

	size_t usedLockRows(const MdPattern& _pattern);
	std::optional<size_t> lockRowIndex(const MdPattern& _pattern, size_t _track, size_t _param);
	std::optional<uint8_t> lockValue(const MdPattern& _pattern, size_t _track, size_t _param, size_t _step);
	// Fails when a new row is needed and all 64 are in use (the MD lock budget).
	std::optional<MdPattern> withLock(const MdPattern& _pattern, size_t _track, size_t _param,
		size_t _step, uint8_t _value);
}
