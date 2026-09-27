#include "mmDump.h"

#include "sysex7bit.h"

namespace elektronData
{
	std::vector<uint8_t> mmRunLengthEncode(const std::vector<uint8_t>& _raw)
	{
		std::vector<uint8_t> out;
		out.reserve(_raw.size());
		for(size_t i = 0; i < _raw.size();)
		{
			size_t j = i;
			while(j < _raw.size() && _raw[j] == _raw[i] && j - i < 127)
				++j;
			const auto n = j - i;
			if(n >= 2 || (_raw[i] & 0x80))
			{
				out.push_back(static_cast<uint8_t>(0x80 | n));
				out.push_back(_raw[i]);
			}
			else
			{
				out.push_back(_raw[i]);
			}
			i = j;
		}
		return out;
	}

	std::optional<std::vector<uint8_t>> mmRunLengthDecode(const std::vector<uint8_t>& _stream)
	{
		std::vector<uint8_t> out;
		for(size_t i = 0; i < _stream.size(); ++i)
		{
			const auto b = _stream[i];
			if(!(b & 0x80))
			{
				out.push_back(b);
				continue;
			}
			const size_t n = b & 0x7f;
			if(n == 0 || i + 1 >= _stream.size())
				return std::nullopt;
			out.insert(out.end(), n, _stream[++i]);
		}
		return out;
	}

	std::optional<MmDump> unpackMmDump(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() < 15 || _sysex[0] != 0xf0 || _sysex[1] != 0x00 || _sysex[2] != 0x20 || _sysex[3] != 0x3c
			|| _sysex[4] != g_mmProductId)
			return std::nullopt;
		for(size_t i = 1; i + 1 < _sysex.size(); ++i)
			if(_sysex[i] > 0x7f)
				return std::nullopt;
		if(!isDumpTrailerValid(_sysex))
			return std::nullopt;
		const auto stream = decode7Bit(_sysex.data() + 10, _sysex.size() - 15);
		auto raw = mmRunLengthDecode(stream);
		if(!raw)
			return std::nullopt;
		return MmDump{_sysex[6], _sysex[7], _sysex[8], _sysex[9], std::move(*raw)};
	}

	std::vector<uint8_t> packMmDump(const MmDump& _dump)
	{
		std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, g_mmProductId, 0x00, _dump.command, _dump.version,
			_dump.revision, static_cast<uint8_t>(_dump.position & 0x7f)};
		const auto stream = mmRunLengthEncode(_dump.raw);
		const auto packed = encode7Bit(stream.data(), stream.size());
		m.insert(m.end(), packed.begin(), packed.end());
		// The firmware writes each group's high-bit byte before the group.
		if(stream.size() % 7 == 0)
			m.push_back(0);
		m.resize(m.size() + 5);
		writeDumpTrailer(m);
		return m;
	}

	std::vector<uint8_t> mmRequest(const uint8_t _command, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, g_mmProductId, 0x00, _command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}

	std::vector<uint8_t> mmMessage(const uint8_t _command, const std::vector<uint8_t>& _args)
	{
		std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, g_mmProductId, 0x00, _command};
		for(const auto a : _args)
			m.push_back(static_cast<uint8_t>(a & 0x7f));
		m.push_back(0xf7);
		return m;
	}

	bool isMmMessage(const std::vector<uint8_t>& _sysex, const uint8_t _command)
	{
		return _sysex.size() >= 8 && _sysex[0] == 0xf0 && _sysex[1] == 0x00 && _sysex[2] == 0x20 && _sysex[3] == 0x3c
			&& _sysex[4] == g_mmProductId && _sysex[6] == _command && _sysex.back() == 0xf7;
	}
}
