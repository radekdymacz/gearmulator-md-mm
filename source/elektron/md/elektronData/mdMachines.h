#pragma once

#include <array>
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

	// The 24 parameter names of a kit track holding this machine (manual
	// Appendix A): SYNTHESIS 0-7, EFFECTS 8-15, ROUTING 16-23. An empty name is
	// an unused slot. MID and CTR machines put their own controls on the effects
	// and routing pages; the slot of MID's CC5D..PCHG within ROUTING is inferred
	// (LFOS/LFOD/LFOM stay at 21-23, where every other machine keeps them).
	using MdParamNames = std::array<const char*, 24>;
	MdParamNames mdMachineParamNames(uint32_t _model);

	// "TRX", "EFM", "E12", "P-I", "GND", "INP", "MID", "CTR", "ROM", "RAM"; empty
	// for an unknown model.
	std::string mdMachineFamily(uint32_t _model);

	// What the editors ask of a machine, derived from its name in one place (DESIGN-REVIEW-2026-10-02.md
	// finding 16; the MD page's machineFacts in mdDeskModel.js is the same record): its family; whether it
	// plays a sample slot (ROM-nn, RAM-Pn: PTCH is its pitch) or records one (RAM-Rn: its synthesis page is
	// its recording setup); whether it makes sound of its own (not MID, not CTR); the empty track.
	struct MdMachineFacts
	{
		std::string family;	// mdMachineFamily; empty for an unknown model
		bool sampler = false;
		bool recorder = false;
		bool audio = true;
		bool empty = false;
	};
	MdMachineFacts mdMachineFacts(uint32_t _model);
}
