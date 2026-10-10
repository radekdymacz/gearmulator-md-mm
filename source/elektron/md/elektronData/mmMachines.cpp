#include "mmMachines.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

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

	namespace
	{
		struct EnvPoint
		{
			uint8_t value;
			double ms;
		};

		// between two measured points the time grows geometrically (the curves double about every 8 or 10)
		template<size_t N> double envAt(const std::array<EnvPoint, N>& _points, const uint8_t _v)
		{
			if(_v <= _points.front().value)
				return _points.front().ms;
			for(size_t i = 1; i < N; ++i)
			{
				const auto& a = _points[i - 1];
				const auto& b = _points[i];
				if(_v > b.value)
					continue;
				const double f = static_cast<double>(_v - a.value) / static_cast<double>(b.value - a.value);
				return a.ms * std::pow(b.ms / a.ms, f);
			}
			return _points.back().ms;
		}
	}

	double mmAmpAttackMs(const uint8_t _atk)
	{
		// ms from the NOTE ON to 90 %; up to 40 the rise is within the measurement's own 10-20 ms (the values there
		// only keep the curve rising)
		static constexpr std::array<EnvPoint, 33> points{{
			{0, 1}, {4, 1.2}, {8, 1.5}, {12, 2}, {16, 2.5}, {20, 3}, {24, 4}, {28, 5.5}, {32, 7.5}, {36, 10}, {40, 15},
			{44, 27}, {48, 40}, {52, 54}, {56, 73}, {60, 101}, {64, 139}, {68, 193}, {72, 269}, {76, 378}, {80, 518},
			{84, 719}, {88, 1006}, {92, 1416}, {96, 1953}, {100, 2752}, {104, 3791}, {108, 5299}, {112, 7487},
			{116, 10269}, {120, 14666}, {124, 20509}, {127, 26831}}};
		return envAt(points, std::min<uint8_t>(_atk, 127));
	}

	double mmAmpHoldSixteenths(const uint8_t _hold)
	{
		// measured: HOLD 16 0.25 s, 32 0.5 s, 64 1 s, 127 2 s at 120 BPM; HOLD 64 2 s at 60 BPM
		return std::min<uint8_t>(_hold, 127) / 8.0;
	}

	double mmAmpFallMs(const uint8_t _decOrRel)
	{
		if(_decOrRel >= 127)
			return std::numeric_limits<double>::infinity();
		// ms to -20 dB: REL from the NOTE OFF (DEC from the end of HOLD gives the same within a few per cent)
		static constexpr std::array<EnvPoint, 33> points{{
			{0, 12}, {4, 15}, {8, 18}, {12, 21}, {16, 26}, {20, 33}, {24, 41}, {28, 52}, {32, 68}, {36, 87}, {40, 114},
			{44, 146}, {48, 191}, {52, 251}, {56, 323}, {60, 422}, {64, 555}, {68, 719}, {72, 940}, {76, 1238},
			{80, 1613}, {84, 2106}, {88, 2764}, {92, 3606}, {96, 4720}, {100, 6156}, {104, 8044}, {108, 10556},
			{112, 13796}, {116, 18052}, {120, 23627}, {124, 30726}, {126, 35040}}};
		return envAt(points, _decOrRel);
	}

	std::vector<std::string> mmSynthEnum(const uint8_t _machine, const uint8_t _slot)
	{
		static const std::vector<std::string> onOff{"OFF", "ON"};
		switch(_machine)
		{
		case 2: return _slot == 2 ? onOff : std::vector<std::string>{};						// GND-NOIS STON
		case 3:																				// SID-6581
			if(_slot == 2) return onOff;													// PWRS
			if(_slot == 3) return {"TRI", "SAW", "PULS", "MIX", "NOIS"};					// WAVE
			if(_slot == 4) return {"OFF", "RING", "SYNC", "R+S"};							// MOD
			if(_slot == 5) return {"MFRQ", "PRCH"};											// MSRC
			return {};
		case 5: return _slot == 6 ? onOff : std::vector<std::string>{};						// SWAVE-PULS PWRS
		case 6:																				// DPRO-WAVE
			if(_slot == 0)																	// WAVE: the 32 DigiPro waveforms, by number
			{
				std::vector<std::string> waves;
				for(int w = 1; w <= 32; ++w)
					waves.push_back((w < 10 ? "0" : "") + std::to_string(w));
				return waves;
			}
			return _slot == 3 || _slot == 4 ? onOff : std::vector<std::string>{};			// WPRS SYNC
		case 11:																			// VO-6
			if(_slot == 2) return onOff;													// V-SW
			if(_slot == 4) return {"-", "B", "D", "F", "G", "H", "J", "K", "L", "M", "N", "P", "R", "RR", "S", "SJ", "T", "TH", "TJ", "V", "Z"};
			return {};
		default: return {};
		}
	}

	const std::vector<std::string>& mmLfoPages()
	{
		static const std::vector<std::string> v{"PTCH", "SYNT", "AMP", "FILT", "EFFX", "LFO1", "LFO2", "LFO3", "MIDI"};
		return v;
	}
	const std::vector<std::string>& mmLfoTrigs()
	{
		static const std::vector<std::string> v{"FREE", "TRIG", "HOLD", "ONE", "HALF"};
		return v;
	}
	const std::vector<std::string>& mmLfoWaves()
	{
		static const std::vector<std::string> v{"TRI", "ITRI", "SAW", "ISAW", "SQR", "ISQR", "EXP", "IEXP", "RMP", "IRMP", "RND"};
		return v;
	}
	const std::vector<std::string>& mmLfoMults()
	{
		static const std::vector<std::string> v{"1X", "2X", "4X", "8X", "16X", "32X", "64X"};
		return v;
	}
	const std::vector<std::string>& mmPitchDests()
	{
		static const std::vector<std::string> v{"1/12", "2/12", "7/12", "1OCT", "2OCT", "4OCT", "8OCT", "16OCT"};
		return v;
	}
}
