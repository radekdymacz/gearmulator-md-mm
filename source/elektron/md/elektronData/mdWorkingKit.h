#pragma once

#include "mdKit.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// The Machinedrum OS 1.63 working kit as it sits in the machine's memory: the
	// kit that plays, with edits not saved to its slot (P3 probe,
	// mdEditorProbeFirmwareTest workkit). The firmware keeps it as the kit dump's
	// raw fields in dump order, not 7-bit packed:
	//   +0x000   16  name
	//   +0x010  384  16 x 24 track parameters
	//   +0x190   16  track levels
	//   +0x1a0   64  16 x u32 machine model, big-endian
	//   +0x1e0  576  16 x 36-byte LFO blocks
	//   +0x420   32  master effects (gate box, rhythm echo, EQ, dynamix)
	//   +0x440   32  16 trig-group targets, then 16 mute-group targets
	// Pure: the device edge reads the bytes, this turns them into a value.
	// CPU address: patch (battery) RAM 0x70000a. The byte at 0x700008, just before
	// it, is the current kit number (the one status 0x02 reports); the device edge
	// reads both as one region.
	constexpr uint32_t g_mdWorkingKitAddress = 0x70000a;
	constexpr size_t g_mdWorkingKitParamsOffset = 0x010;
	constexpr size_t g_mdWorkingKitSize = 0x460;
	constexpr uint32_t g_mdWorkingKitRegionAddress = 0x700008;
	constexpr size_t g_mdWorkingKitRegionSize = 2 + g_mdWorkingKitSize;

	// The image the firmware would hold for _kit (test oracle and the inverse).
	std::vector<uint8_t> mdWorkingKitImage(const MdKit& _kit);

	// _image must be g_mdWorkingKitSize bytes. _slot becomes the value's position.
	std::optional<MdKit> mdWorkingKitFromImage(const std::vector<uint8_t>& _image, uint8_t _slot);

	// The region at g_mdWorkingKitRegionAddress: kit number, one byte, the image.
	// Empty when the size or the kit number is wrong.
	std::optional<MdKit> mdWorkingKitFromMemory(const std::vector<uint8_t>& _region);

	// Same sound: every field a kit dump carries except the opaque LFO running
	// state, which the firmware may keep differently in the working copy.
	bool mdSameKitSound(const MdKit& _a, const MdKit& _b);
}
