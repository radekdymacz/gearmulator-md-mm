#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace elektronData
{
	// Machinedrum OS 1.63 machine ids as stored in a kit's model words.
	// Standard machines use the numbers of the manual's "SYSEX assign machine"
	// table (Appendix C). UW machines (ROM/RAM) set bit 7: the stored model is
	// 0x80 + the table's "c = 1" number. Verified on firmware: assigning ROM-05
	// with c = 1 stores 0x84.
	constexpr uint32_t g_mdUwFlag = 0x80;

	// The manual's name ("TRX-BD", "P-I-SD", "ROM-05"), or empty for an id the
	// OS 1.63 table does not define.
	std::string mdMachineName(uint32_t _model);
	std::optional<uint32_t> mdMachineModel(const std::string& _name);
	inline bool isKnownMdMachine(const uint32_t _model) { return !mdMachineName(_model).empty(); }
}
