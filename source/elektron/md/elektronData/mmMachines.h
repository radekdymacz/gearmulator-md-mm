#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B machines: the SysEx 0x5B ids (manual Appendix C) and
	// the SYN page's eight slot names as the firmware's own main screen shows
	// them after assigning each machine (mmEditorProbeFirmwareTest lab, lab22).
	// An empty name is a slot the machine does not use. The fixed pages come from
	// the same screens.
	struct MmMachineInfo
	{
		uint8_t id;
		const char* name;		// the manual's name, e.g. "SWAVE-SAW"
		const char* family;		// GND SID SWAVE DPRO FM+ VO FX
		std::array<const char*, 8> synth;
		bool fx;				// processes other audio (needs an input and a trig)
		bool mk2;				// MKII only (user waveforms)
	};

	const std::vector<MmMachineInfo>& mmMachines();
	const MmMachineInfo* mmMachine(uint8_t _id);

	// The fixed DATA pages (AMP FLT EFX LF1-3) and the MIDI page, eight names each.
	// Pages 0-7: SYN AMP FLT EFX LF1 LF2 LF3 MIDI.
	const std::array<const char*, 8>& mmPageNames();
	const std::array<const char*, 8>& mmFixedPage(uint8_t _page);
	// A parameter's name on a track with this machine ("" for an unused slot).
	std::string mmParamName(uint8_t _machine, uint8_t _page, uint8_t _param);

	// Appendix B: the CC of a DATA page parameter on the track's channel.
	// Pages 0-6. Level is CC 7.
	uint8_t mmParamCc(uint8_t _page, uint8_t _param);
	constexpr uint8_t g_mmLevelCc = 7;
	constexpr uint8_t g_mmMuteCc = 3;
}
