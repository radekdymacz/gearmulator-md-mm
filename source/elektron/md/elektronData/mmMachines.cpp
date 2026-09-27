#include "mmMachines.h"

namespace elektronData
{
	const std::vector<MmMachineInfo>& mmMachines()
	{
		static const std::vector<MmMachineInfo> machines{
			{0, "GND-GND", "GND", {"", "", "", "", "", "", "", ""}, false, false},
			{1, "GND-SIN", "GND", {"", "", "", "", "", "", "", "TUNE"}, false, false},
			{2, "GND-NOIS", "GND", {"ST", "RED", "STON", "", "", "", "", "TUNE"}, false, false},
			{3, "SID-6581", "SID", {"PW", "PWAD", "PWRS", "WAVE", "MOD", "MSRC", "MFRQ", "TUNE"}, false, false},
			{4, "SWAVE-SAW", "SWAVE", {"UNIL", "UNIW", "UNIX", "", "SUBX", "SUB1", "SUB2", "TUNE"}, false, false},
			{5, "SWAVE-PULS", "SWAVE", {"UNIL", "UNIW", "SUB1", "SUB2", "PW", "PWAD", "PWRS", "TUNE"}, false, false},
			{14, "SWAVE-ENS", "SWAVE", {"PCH2", "PCH3", "PCH4", "WAVE", "PW", "CHRL", "CHRW", "TUNE"}, false, false},
			{6, "DPRO-WAVE", "DPRO", {"WAVE", "WP", "WPM", "WPRS", "SYNC", "SFRQ", "", "TUNE"}, false, false},
			{7, "DPRO-BBOX", "DPRO", {"PTCH", "STRT", "", "", "RTRG", "RTIM", "", ""}, false, false},
			{32, "DPRO-DDRW", "DPRO", {"WAV1", "MIX", "WAV2", "TIME", "BR1", "WID", "BR2", "TUNE"}, false, true},
			{33, "DPRO-DENS", "DPRO", {"PCH2", "PCH3", "PCH4", "WAVE", "", "CHRL", "CHRW", "TUNE"}, false, true},
			{8, "FM+STAT", "FM+", {"1FRQ", "1FIN", "1ENV", "1FB", "2FRQ", "2VOL", "TONE", "TUNE"}, false, false},
			{9, "FM+PAR", "FM+", {"1FRQ", "1ENV", "2FRQ", "2ENV", "3FRQ", "3ENV", "TONE", "TUNE"}, false, false},
			{10, "FM+DYN", "FM+", {"1FRQ", "1FEN", "1VOL", "1VEN", "2FRQ", "2ENV", "2FB", "TUNE"}, false, false},
			{11, "VO-6", "VO", {"VOC1", "VOC2", "V-SW", "VOIC", "CONS", "CLEN", "CVOL", "TUNE"}, false, false},
			{12, "FX-THRU", "FX", {"", "", "", "", "", "", "", "INP"}, true, false},
			{13, "FX-REVERB", "FX", {"DEC", "DAMP", "GATE", "MIX", "HP", "LP", "", "INP"}, true, false},
			{15, "FX-CHORUS", "FX", {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "LP", "INP"}, true, false},
			{16, "FX-DYNAMIX", "FX", {"ATK", "REL", "THRS", "MIX", "RAT", "GAIN", "RMS", "INP"}, true, false},
			{17, "FX-RINGMOD", "FX", {"WAVE", "EXT", "", "MIX", "", "", "", "INP"}, true, false},
			{18, "FX-PHASER", "FX", {"CNTR", "DEP", "SPD", "MIX", "FB", "WID", "", "INP"}, true, false},
			{19, "FX-FLANGER", "FX", {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "", "INP"}, true, false},
		};
		return machines;
	}

	const MmMachineInfo* mmMachine(const uint8_t _id)
	{
		for(const auto& m : mmMachines())
			if(m.id == _id)
				return &m;
		return nullptr;
	}

	const std::array<const char*, 8>& mmPageNames()
	{
		static const std::array<const char*, 8> names{"SYN", "AMP", "FLT", "EFX", "LF1", "LF2", "LF3", "MIDI"};
		return names;
	}

	const std::array<const char*, 8>& mmFixedPage(const uint8_t _page)
	{
		static const std::array<const char*, 8> amp{"ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"};
		static const std::array<const char*, 8> flt{"BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS"};
		static const std::array<const char*, 8> efx{"EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID"};
		static const std::array<const char*, 8> lfo{"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH"};
		static const std::array<const char*, 8> midi{"LEN", "VEL", "PB", "PCHG", "CC1", "CC2", "CC3", "CC4"};
		static const std::array<const char*, 8> none{"", "", "", "", "", "", "", ""};
		switch(_page)
		{
		case 1: return amp;
		case 2: return flt;
		case 3: return efx;
		case 4: case 5: case 6: return lfo;
		case 7: return midi;
		default: return none;
		}
	}

	std::string mmParamName(const uint8_t _machine, const uint8_t _page, const uint8_t _param)
	{
		if(_param > 7)
			return {};
		if(_page == 0)
		{
			const auto* m = mmMachine(_machine);
			return m ? m->synth[_param] : "";
		}
		return mmFixedPage(_page)[_param];
	}

	uint8_t mmParamCc(const uint8_t _page, const uint8_t _param)
	{
		// SYN 48, AMP 56, FILTER 72, EFFECTS 80, LFO1 88, LFO2 104, LFO3 112.
		static constexpr uint8_t bases[7]{48, 56, 72, 80, 88, 104, 112};
		return _page < 7 && _param < 8 ? static_cast<uint8_t>(bases[_page] + _param) : 0;
	}
}
