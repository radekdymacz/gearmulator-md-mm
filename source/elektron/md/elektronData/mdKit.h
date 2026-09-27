#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Machinedrum OS 1.63 kit dump (SysEx command 0x52), as a plain value.
	//
	// Layout (offsets into the complete F0..F7 message, 1233 bytes), verified by
	// diffing firmware dumps before and after single SysEx edits (0x55 name,
	// 0x5b machine, 0x5d-0x60 master effects, 0x62 LFO, 0x65/0x66 groups, CC):
	//   0x000    10   F0 00 20 3C 02 00 52 <version 4> <revision 1> <position 0-63>
	//   0x00a    16   name, 7-bit ASCII, NUL terminated; bytes after the NUL are kept as-is
	//   0x01a   384   16 tracks x 24 parameters: synthesis 0-7, effects 8-15, routing 16-23
	//   0x19a    16   track levels
	//   0x1aa    74   7-bit: 16 x u32 machine model (see mdMachines.h)
	//   0x1f4   659   7-bit: 16 x 36-byte LFO blocks (track, parameter, shape 1, shape 2,
	//                 update mode, then 31 bytes of firmware LFO state, kept opaque)
	//   0x487    32   master effects, 4 x 8 parameters: gate box (set by 0x5e),
	//                 rhythm echo (0x5d), EQ (0x5f), dynamix (0x60)
	//   0x4a7    37   7-bit: 16 trig-group targets then 16 mute-group targets, 0xff = none
	//   0x4cc     5   checksum, length, F7
	//
	// LFO speed, depth and shape mix are not in the LFO block: they are the track's
	// routing parameters 21-23 (LFOS, LFOD, LFOM).
	struct MdLfo
	{
		uint8_t track = 0;
		uint8_t param = 0;
		uint8_t shape1 = 0;
		uint8_t shape2 = 0;
		uint8_t update = 0;		// 0 FREE, 1 TRIG, 2 HOLD
		std::array<uint8_t, 31> state{};

		bool operator==(const MdLfo& _o) const
		{
			return track == _o.track && param == _o.param && shape1 == _o.shape1 && shape2 == _o.shape2
				&& update == _o.update && state == _o.state;
		}
	};

	struct MdKit
	{
		static constexpr size_t g_tracks = 16;
		static constexpr size_t g_paramsPerTrack = 24;
		static constexpr size_t g_nameSize = 16;
		static constexpr size_t g_dumpSize = 0x4d1;
		static constexpr size_t g_slots = 64;
		static constexpr uint8_t g_noGroup = 0xff;

		enum MasterFx : size_t
		{
			GateBox,
			RhythmEcho,
			Eq,
			Dynamix,
			MasterFxCount
		};

		uint8_t version = 4;
		uint8_t revision = 1;
		uint8_t position = 0;

		std::array<uint8_t, g_nameSize> name{};
		std::array<std::array<uint8_t, g_paramsPerTrack>, g_tracks> params{};
		std::array<uint8_t, g_tracks> levels{};
		std::array<uint32_t, g_tracks> models{};
		std::array<MdLfo, g_tracks> lfos{};
		std::array<std::array<uint8_t, 8>, MasterFxCount> masterFx{};
		std::array<uint8_t, g_tracks> trigGroups{};
		std::array<uint8_t, g_tracks> muteGroups{};

		bool operator==(const MdKit& _o) const;
		bool operator!=(const MdKit& _o) const { return !(*this == _o); }
	};

	std::optional<MdKit> decodeMdKit(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMdKit(const MdKit& _kit);

	std::vector<uint8_t> mdKitRequest(uint8_t _slot);
	// Working kit <-> stored slot (manual Appendix C). A kit dump always writes the
	// stored slot; only LOAD KIT makes it the sound that plays.
	std::vector<uint8_t> mdLoadKit(uint8_t _slot);
	std::vector<uint8_t> mdSaveKit(uint8_t _slot);
}
