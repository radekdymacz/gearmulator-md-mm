#include "mdMachines.h"

#include <array>
#include <cstdio>

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
}
