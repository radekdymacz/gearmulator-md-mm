#include "mdCommands.h"

#include "dumpIo.h"

namespace elektronData
{
	namespace
	{
		constexpr uint8_t g_statusRequestId = 0x70;
		constexpr uint8_t g_setStatusId = 0x71;
		constexpr uint8_t g_statusResponseId = 0x72;
		constexpr uint8_t g_loadPatternId = 0x57;

		bool isMdMessage(const std::vector<uint8_t>& _m)
		{
			return _m.size() >= 9 && _m[0] == 0xf0 && _m[1] == 0x00 && _m[2] == 0x20 && _m[3] == 0x3c
				&& _m[4] == dumpIo::g_mdProductId && _m.back() == 0xf7;
		}
	}

	std::vector<uint8_t> mdStatusRequest(const MdStatus _param)
	{
		return dumpIo::request(g_statusRequestId, static_cast<uint8_t>(_param));
	}

	std::vector<uint8_t> mdSetStatus(const MdStatus _param, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, dumpIo::g_mdProductId, 0x00, g_setStatusId, static_cast<uint8_t>(_param),
			static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}

	std::optional<MdStatusValue> parseMdStatusResponse(const std::vector<uint8_t>& _m)
	{
		if(_m.size() != 10 || !isMdMessage(_m) || _m[6] != g_statusResponseId || _m[8] > 0x7f)
			return {};
		switch(static_cast<MdStatus>(_m[7]))
		{
		case MdStatus::GlobalSlot:
		case MdStatus::Kit:
		case MdStatus::Pattern:
		case MdStatus::Song:
		case MdStatus::SequencerMode:
		case MdStatus::LockMode:
		case MdStatus::Track:
			return MdStatusValue{static_cast<MdStatus>(_m[7]), _m[8]};
		}
		return {};
	}

	std::vector<uint8_t> mdLoadPattern(const uint8_t _slot)
	{
		return dumpIo::request(g_loadPatternId, _slot);
	}

	uint8_t mdDumpCommand(const std::vector<uint8_t>& _m)
	{
		if(!isMdMessage(_m))
			return 0;
		switch(_m[6])
		{
		case g_mdGlobalDump:
		case g_mdKitDump:
		case g_mdPatternDump:
		case g_mdSongDump:
			return _m[6];
		default:
			return 0;
		}
	}
}
