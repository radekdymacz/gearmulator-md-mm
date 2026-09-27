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

	std::vector<uint8_t> mdSetSampleName(const uint8_t _slot, const std::string& _name)
	{
		if(_slot > 47 || _name.empty() || _name.size() > 4)
			return {};
		std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, dumpIo::g_mdProductId, 0x00, 0x73, _slot};
		for(size_t i = 0; i < 4; ++i)
		{
			const char c = i < _name.size() ? _name[i] : ' ';
			if(c < 0x20 || c > 0x7e)
				return {};
			m.push_back(static_cast<uint8_t>(c));
		}
		m.push_back(0xf7);
		return m;
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

	namespace
	{
		std::vector<uint8_t> command(const uint8_t _id, std::initializer_list<uint8_t> _data)
		{
			std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, dumpIo::g_mdProductId, 0x00, _id};
			for(const auto b : _data)
				m.push_back(static_cast<uint8_t>(b & 0x7f));
			m.push_back(0xf7);
			return m;
		}

		constexpr uint8_t g_assignMachineId = 0x5b;
		constexpr uint8_t g_trackRoutingId = 0x5c;
		constexpr uint8_t g_tempoId = 0x61;
		constexpr uint8_t g_lfoId = 0x62;
		constexpr uint8_t g_trigGroupId = 0x65;
		constexpr uint8_t g_muteGroupId = 0x66;
		constexpr uint8_t g_kitNameId = 0x55;
		// Indexed by MdKit::MasterFx: gate box 0x5e, rhythm echo 0x5d, EQ 0x5f, dynamix 0x60.
		constexpr uint8_t g_masterFxIds[4] = {0x5e, 0x5d, 0x5f, 0x60};
	}

	std::vector<uint8_t> mdAssignMachine(const uint8_t _track, const uint32_t _model, const MdMachineInit _init)
	{
		const auto uw = static_cast<uint8_t>((_model & 0x80) ? 1 : 0);
		return command(g_assignMachineId, {static_cast<uint8_t>(_track & 0x0f), static_cast<uint8_t>(_model & 0x7f), uw,
			static_cast<uint8_t>(_init)});
	}

	std::vector<uint8_t> mdSetMasterFx(const size_t _fx, const uint8_t _param, const uint8_t _value)
	{
		return command(g_masterFxIds[_fx & 3], {static_cast<uint8_t>(_param & 7), _value});
	}

	std::vector<uint8_t> mdSetLfo(const uint8_t _lfo, const uint8_t _param, const uint8_t _value)
	{
		return command(g_lfoId, {static_cast<uint8_t>(((_lfo & 0x0f) << 3) | (_param & 7)), _value});
	}

	std::vector<uint8_t> mdSetTrigGroup(const uint8_t _track, const uint8_t _target)
	{
		return command(g_trigGroupId, {static_cast<uint8_t>(_track & 0x0f), _target});
	}

	std::vector<uint8_t> mdSetMuteGroup(const uint8_t _track, const uint8_t _target)
	{
		return command(g_muteGroupId, {static_cast<uint8_t>(_track & 0x0f), _target});
	}

	std::vector<uint8_t> mdSetKitName(const std::string& _name)
	{
		std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, dumpIo::g_mdProductId, 0x00, g_kitNameId};
		for(size_t i = 0; i < 16; ++i)
			m.push_back(i < _name.size() ? static_cast<uint8_t>(_name[i] & 0x7f) : 0);
		m.push_back(0xf7);
		return m;
	}

	std::vector<uint8_t> mdSetTrackRouting(const uint8_t _track, const uint8_t _output)
	{
		return command(g_trackRoutingId, {static_cast<uint8_t>(_track & 0x0f), static_cast<uint8_t>(_output & 7)});
	}

	std::vector<uint8_t> mdSetTempo(const uint16_t _tempo)
	{
		return command(g_tempoId, {static_cast<uint8_t>((_tempo >> 7) & 0x7f), static_cast<uint8_t>(_tempo & 0x7f)});
	}
}
