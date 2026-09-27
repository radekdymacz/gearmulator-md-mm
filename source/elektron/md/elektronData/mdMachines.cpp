#include "mdMachines.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace elektronData
{
	namespace
	{
		struct Entry
		{
			uint8_t id;
			const char* name;
		};

		constexpr std::array<Entry, 62> g_standard{{
			{0, "GND-EMPTY"}, {1, "GND-SIN"}, {2, "GND-NS"}, {3, "GND-IM"},
			{16, "TRX-BD"}, {17, "TRX-SD"}, {18, "TRX-XT"}, {19, "TRX-CP"}, {20, "TRX-RS"}, {21, "TRX-CB"},
			{22, "TRX-CH"}, {23, "TRX-OH"}, {24, "TRX-CY"}, {25, "TRX-MA"}, {26, "TRX-CL"}, {27, "TRX-XC"},
			{28, "TRX-B2"},
			{32, "EFM-BD"}, {33, "EFM-SD"}, {34, "EFM-XT"}, {35, "EFM-CP"}, {36, "EFM-RS"}, {37, "EFM-CB"},
			{38, "EFM-HH"}, {39, "EFM-CY"},
			{48, "E12-BD"}, {49, "E12-SD"}, {50, "E12-HT"}, {51, "E12-LT"}, {52, "E12-CP"}, {53, "E12-RS"},
			{54, "E12-CB"}, {55, "E12-CH"}, {56, "E12-OH"}, {57, "E12-RC"}, {58, "E12-CC"}, {59, "E12-BR"},
			{60, "E12-TA"}, {61, "E12-TR"}, {62, "E12-SH"}, {63, "E12-BC"},
			{64, "P-I-BD"}, {65, "P-I-SD"}, {66, "P-I-MT"}, {67, "P-I-ML"}, {68, "P-I-MA"}, {69, "P-I-RS"},
			{70, "P-I-RC"}, {71, "P-I-CC"}, {72, "P-I-HH"},
			{80, "INP-GA"}, {81, "INP-GB"}, {82, "INP-FA"}, {83, "INP-FB"}, {84, "INP-EA"}, {85, "INP-EB"},
			{112, "CTR-AL"}, {113, "CTR-8P"}, {120, "CTR-RE"}, {121, "CTR-GB"}, {122, "CTR-EQ"}, {123, "CTR-DX"},
		}};

		std::string numbered(const char* _prefix, const uint32_t _n)
		{
			char text[16];
			std::snprintf(text, sizeof(text), "%s%02u", _prefix, _n);
			return text;
		}

		std::string uwName(const uint32_t _n)
		{
			if(_n < 32)
				return numbered("ROM-", _n + 1);
			if(_n >= 48 && _n < 64)
				return numbered("ROM-", _n - 48 + 33);
			switch(_n)
			{
			case 32: return "RAM-R1";
			case 33: return "RAM-R2";
			case 34: return "RAM-P1";
			case 35: return "RAM-P2";
			case 37: return "RAM-R3";
			case 38: return "RAM-R4";
			case 39: return "RAM-P3";
			case 40: return "RAM-P4";
			default: return {};
			}
		}
	}

	std::string mdMachineName(const uint32_t _model)
	{
		if(_model & ~uint32_t{0xff})
			return {};
		if(_model & g_mdUwFlag)
			return uwName(_model & 0x7f);
		if(_model >= 96 && _model < 112)
			return numbered("MID-", _model - 96 + 1);
		for(const auto& e : g_standard)
			if(e.id == _model)
				return e.name;
		return {};
	}

	std::optional<uint32_t> mdMachineModel(const std::string& _name)
	{
		if(_name.empty())
			return {};
		for(uint32_t model = 0; model < 256; ++model)
			if(mdMachineName(model) == _name)
				return model;
		return {};
	}

	namespace
	{
		struct SynthNames
		{
			const char* machine;
			std::array<const char*, 8> names;
		};

		// Synthesis pages, manual Appendix A.
		const SynthNames g_synthNames[] = {
			{"TRX-BD", {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"}},
			{"TRX-B2", {"PTCH", "DEC", "RAMP", "HOLD", "TICK", "NOIS", "DIRT", "DIST"}},
			{"TRX-SD", {"PTCH", "DEC", "BUMP", "BENV", "SNAP", "TONE", "TUNE", "CLIP"}},
			{"TRX-XT", {"PTCH", "DEC", "RAMP", "RDEC", "DAMP", "DIST", "DTYP", ""}},
			{"TRX-CP", {"CLPY", "TONE", "HARD", "RICH", "RATE", "ROOM", "RSIZ", "RTUN"}},
			{"TRX-RS", {"PTCH", "DEC", "DIST", "", "", "", "", ""}},
			{"TRX-CB", {"PTCH", "DEC", "ENH", "DAMP", "TONE", "BUMP", "", ""}},
			{"TRX-CH", {"GAP", "DEC", "HPF", "LPF", "MTAL", "", "", ""}},
			{"TRX-OH", {"GAP", "DEC", "HPF", "LPF", "MTAL", "", "", ""}},
			{"TRX-CY", {"RICH", "DEC", "TOP", "TTUN", "SIZE", "PEAK", "", ""}},
			{"TRX-MA", {"ATT", "SUS", "REV", "DAMP", "RATL", "RTYP", "TONE", "HARD"}},
			{"TRX-CL", {"PTCH", "DEC", "DUAL", "ENH", "TUNE", "CLIC", "", ""}},
			{"TRX-XC", {"PTCH", "DEC", "RAMP", "RDEC", "DAMP", "DIST", "DTYP", ""}},
			{"EFM-BD", {"PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "MFB"}},
			{"EFM-SD", {"PTCH", "DEC", "NOISE", "NDEC", "MOD", "MFRQ", "MDEC", "HPF"}},
			{"EFM-XT", {"PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "CLIC"}},
			{"EFM-CP", {"PTCH", "DEC", "CLPS", "CDEC", "MOD", "MFRQ", "MDEC", "HPF"}},
			{"EFM-RS", {"PTCH", "DEC", "MOD", "HPF", "SNAR", "SPTC", "SDEC", "SMOD"}},
			{"EFM-CB", {"PTCH", "DEC", "SNAP", "FB", "MOD", "MFRQ", "MDEC", ""}},
			{"EFM-HH", {"PTCH", "DEC", "TREM", "TFRQ", "MOD", "MFRQ", "MDEC", "FB"}},
			{"EFM-CY", {"PTCH", "DEC", "FB", "HPF", "MOD", "MFRQ", "MDEC", ""}},
			{"E12-BD", {"PTCH", "DEC", "SNAP", "SPLEN", "START", "RTRG", "RTIM", "BEND"}},
			{"E12-SD", {"PTCH", "DEC", "HP", "RING", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-HT", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-LT", {"PTCH", "DEC", "HP", "RRTL", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-CP", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-RS", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-CB", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-CH", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-OH", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-RC", {"PTCH", "DEC", "HP", "BELL", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-CC", {"PTCH", "DEC", "HP", "STOP", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-BR", {"PTCH", "DEC", "HP", "REAL", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-TA", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-TR", {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-SH", {"PTCH", "DEC", "HP", "SLEW", "STRT", "RTRG", "RTIM", "BEND"}},
			{"E12-BC", {"PTCH", "DEC", "HP", "BC", "STRT", "RTRG", "RTIM", "BEND"}},
			{"P-I-BD", {"PTCH", "DEC", "HARD", "HAMR", "TENS", "DAMP", "", ""}},
			{"P-I-SD", {"PTCH", "DEC", "HARD", "TENS", "RVOL", "RDEC", "RING", ""}},
			{"P-I-MT", {"PTCH", "DEC", "HARD", "HAMR", "TUNE", "DAMP", "SIZE", "POS"}},
			{"P-I-RS", {"PTCH", "DEC", "HARD", "RING", "RVOL", "RDEC", "", ""}},
			{"P-I-ML", {"PTCH", "DEC", "HARD", "TENS", "", "", "", ""}},
			{"P-I-MA", {"DEC", "GRNS", "GLEN", "SIZE", "HARD", "", "", ""}},
			{"P-I-HH", {"PTCH", "DEC", "CLSN", "RING", "AG", "AU", "BR", "CLOS"}},
			{"P-I-RC", {"PTCH", "DEC", "HARD", "RING", "AG", "AU", "BR", "GRAB"}},
			{"P-I-CC", {"PTCH", "DEC", "HARD", "RING", "AG", "AU", "BR", "GRAB"}},
			{"GND-EMPTY", {"", "", "", "", "", "", "", ""}},
			{"GND-SIN", {"PTCH", "DEC", "RAMP", "RDEC", "", "", "", ""}},
			{"GND-NS", {"DEC", "", "", "", "", "", "", ""}},
			{"GND-IM", {"UP", "UVAL", "DOWN", "DVAL", "", "", "", ""}},
			{"INP-GA", {"VOL", "GATE", "ATCK", "HLD", "DEC", "", "", ""}},
			{"INP-GB", {"VOL", "GATE", "ATCK", "HLD", "DEC", "", "", ""}},
			{"INP-FA", {"ALEV", "GATE", "FATK", "FHLD", "FDEC", "FDPH", "FFRQ", "FQ"}},
			{"INP-FB", {"ALEV", "GATE", "FATK", "FHLD", "FDEC", "FDPH", "FFRQ", "FQ"}},
			{"INP-EA", {"AVOL", "AHLD", "ADEC", "FQ", "FDPH", "FHLD", "FDEC", "FFRQ"}},
			{"INP-EB", {"AVOL", "AHLD", "ADEC", "FQ", "FDPH", "FHLD", "FDEC", "FFRQ"}},
			{"CTR-AL", {"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}},
			{"CTR-8P", {"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}},
			{"CTR-RE", {"TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV"}},
			{"CTR-GB", {"DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"}},
			{"CTR-EQ", {"LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"}},
			{"CTR-DX", {"ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"}},
		};

		constexpr std::array<const char*, 8> g_effects{"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR"};
		constexpr std::array<const char*, 8> g_routing{"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
		constexpr std::array<const char*, 8> g_sample{"PTCH", "DEC", "HOLD", "BRR", "STRT", "END", "RTRG", "RTIM"};
		constexpr std::array<const char*, 8> g_ramRecord{"MLEV", "MBAL", "ILEV", "IBAL", "CUE1", "CUE2", "LEN", "RATE"};
		constexpr std::array<const char*, 8> g_midi{"NOTE", "N2", "N3", "LEN", "VEL", "PB", "MW", "AT"};
		constexpr std::array<const char*, 8> g_midiEffects{"CC1D", "CC1V", "CC2D", "CC2V", "CC3D", "CC3V", "CC4D", "CC4V"};
		constexpr std::array<const char*, 8> g_midiRouting{"CC5D", "CC5V", "PCHG", "", "", "LFOS", "LFOD", "LFOM"};
		constexpr std::array<const char*, 8> g_ctr8pEffects{"P1TR", "P1PA", "P2TR", "P2PA", "P3TR", "P3PA", "P4TR", "P4PA"};
		constexpr std::array<const char*, 8> g_ctr8pRouting{"P5TR", "P5PA", "P6TR", "P6PA", "P7TR", "P7PA", "P8TR", "P8PA"};
		constexpr std::array<const char*, 8> g_none{"", "", "", "", "", "", "", ""};

		void put(MdParamNames& _out, const size_t _page, const std::array<const char*, 8>& _names)
		{
			for(size_t i = 0; i < 8; ++i)
				_out[_page * 8 + i] = _names[i];
		}
	}

	std::string mdMachineFamily(const uint32_t _model)
	{
		const auto name = mdMachineName(_model);
		if(name.empty())
			return {};
		if(name.rfind("P-I-", 0) == 0)
			return "P-I";
		return name.substr(0, 3);
	}

	MdParamNames mdMachineParamNames(const uint32_t _model)
	{
		MdParamNames out{};
		put(out, 0, g_none);
		put(out, 1, g_effects);
		put(out, 2, g_routing);
		const auto name = mdMachineName(_model);
		const auto family = mdMachineFamily(_model);
		if(name.empty())
			return out;
		if(family == "ROM" || name.rfind("RAM-P", 0) == 0)
			put(out, 0, g_sample);
		else if(name.rfind("RAM-R", 0) == 0)
			put(out, 0, g_ramRecord);
		else if(family == "MID")
		{
			put(out, 0, g_midi);
			put(out, 1, g_midiEffects);
			put(out, 2, g_midiRouting);
		}
		else
		{
			for(const auto& e : g_synthNames)
				if(name == e.machine)
					put(out, 0, e.names);
		}
		if(name == "CTR-8P")
		{
			put(out, 1, g_ctr8pEffects);
			put(out, 2, g_ctr8pRouting);
		}
		else if(name == "CTR-RE" || name == "CTR-GB" || name == "CTR-EQ" || name == "CTR-DX")
		{
			put(out, 1, g_none);
			put(out, 2, g_none);
		}
		return out;
	}
}
