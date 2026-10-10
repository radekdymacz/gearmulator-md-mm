#include "mmCommands.h"

#include "mmDump.h"

#include <cmath>

namespace elektronData
{
	std::vector<uint8_t> mmStatusRequest(const MmStatus _param) { return mmRequest(0x70, static_cast<uint8_t>(_param)); }

	bool mmIsRequest(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() != 9)
			return false;
		for(const uint8_t command : {uint8_t{0x70}, uint8_t{0x51}, uint8_t{0x53}, uint8_t{0x68}, uint8_t{0x6a}})
			if(isMmMessage(_sysex, command))
				return true;
		return false;
	}

	std::vector<uint8_t> mmSetStatus(const MmStatus _param, const uint8_t _value)
	{
		return mmMessage(0x71, {static_cast<uint8_t>(_param), _value});
	}

	std::optional<MmStatusReply> parseMmStatusResponse(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() != 10 || !isMmMessage(_sysex, 0x72))
			return std::nullopt;
		return MmStatusReply{static_cast<MmStatus>(_sysex[7]), _sysex[8]};
	}

	std::vector<uint8_t> mmLoadPattern(const uint8_t _slot) { return mmRequest(0x57, _slot); }
	std::vector<uint8_t> mmLoadKit(const uint8_t _slot) { return mmRequest(0x58, _slot); }
	std::vector<uint8_t> mmSaveKit(const uint8_t _slot) { return mmRequest(0x59, _slot); }
	std::vector<uint8_t> mmLoadSong(const uint8_t _slot) { return mmRequest(0x6c, _slot); }
	std::vector<uint8_t> mmSaveSong(const uint8_t _slot) { return mmRequest(0x6d, _slot); }
	std::vector<uint8_t> mmSetActiveGlobal(const uint8_t _slot) { return mmRequest(0x56, _slot); }

	std::vector<uint8_t> mmAssignMachine(const uint8_t _track, const uint8_t _machine, const uint8_t _init)
	{
		return mmMessage(0x5b, {_track, _machine, _init});
	}

	std::vector<uint8_t> mmSetRouting(const uint8_t _track, const uint8_t _outputs, const uint8_t _input)
	{
		return mmMessage(0x5c, {_track, _outputs, _input});
	}

	std::vector<uint8_t> mmSetKitName(const std::string& _name)
	{
		std::vector<uint8_t> n(11, 0);
		for(size_t i = 0; i < _name.size() && i < 11; ++i)
			n[i] = static_cast<uint8_t>(_name[i] & 0x7f);
		return mmMessage(0x55, n);
	}

	std::vector<uint8_t> mmSetTempo(const double _bpm)
	{
		const auto v = static_cast<uint32_t>(std::lround(_bpm * 24.0));
		return mmMessage(0x61, {static_cast<uint8_t>((v >> 7) & 0x7f), static_cast<uint8_t>(v & 0x7f)});
	}
}
