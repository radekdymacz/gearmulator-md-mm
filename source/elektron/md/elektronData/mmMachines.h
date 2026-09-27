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

	// Enumerated values. The firmware stores them as 0-127 and shows the n names in
	// equal bands: index = floor(v * n / 128) (measured by sweeping each CC and
	// grouping the screens, mmEditorProbeFirmwareTest enums). mmEnumValue gives the
	// lowest value of a band.
	constexpr int mmEnumIndex(const int _v, const int _n) { return _v * _n / 128; }
	constexpr int mmEnumValue(const int _i, const int _n) { return (_i * 128 + _n - 1) / _n; }
	// The names a SYN slot shows for this machine, empty when it is a plain 0-127 value.
	std::vector<std::string> mmSynthEnum(uint8_t _machine, uint8_t _slot);
	// LFO page parameters, in the firmware's order.
	const std::vector<std::string>& mmLfoPages();	// PTCH SYNT AMP FILT EFFX LFO1 LFO2 LFO3 MIDI
	const std::vector<std::string>& mmLfoTrigs();	// FREE TRIG HOLD ONE HALF
	const std::vector<std::string>& mmLfoWaves();	// TRI ITRI SAW ISAW SQR ISQR EXP IEXP RMP IRMP RND
	const std::vector<std::string>& mmLfoMults();	// 1X .. 64X
	const std::vector<std::string>& mmPitchDests();	// 1/12 .. 16OCT (DEST when PAGE is PTCH)
	constexpr uint8_t g_mmMuteCc = 3;
}
