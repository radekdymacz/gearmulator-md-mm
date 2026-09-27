#include "mdGlobal.h"

#include "dumpIo.h"

namespace elektronData
{
	namespace
	{
		constexpr uint8_t g_globalDumpId = 0x50;
		constexpr uint8_t g_globalRequestId = 0x51;

		constexpr size_t g_routingOffset = 0x0a;
		constexpr size_t g_keymapOffset = 0x1a;
		constexpr size_t g_settingsOffset = 0xad;

		static_assert(g_keymapOffset == g_routingOffset + MdGlobal::g_tracks);
		static_assert(g_settingsOffset == g_keymapOffset + encoded7BitSize(128));
		static_assert(MdGlobal::g_dumpSize == g_settingsOffset + 19 + 5);
	}

	bool MdGlobal::operator==(const MdGlobal& _o) const
	{
		return version == _o.version && revision == _o.revision && position == _o.position
			&& routing == _o.routing && keymap == _o.keymap && baseChannel == _o.baseChannel && unused == _o.unused
			&& tempo == _o.tempo && extendedMode == _o.extendedMode && syncFlags == _o.syncFlags
			&& localControl == _o.localControl && inputSettings == _o.inputSettings
			&& programChange == _o.programChange && trigMode == _o.trigMode;
	}

	std::optional<MdGlobal> decodeMdGlobal(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() != MdGlobal::g_dumpSize || !dumpIo::isValidDump(_sysex, g_globalDumpId))
			return {};

		MdGlobal g;
		g.version = _sysex[7];
		g.revision = _sysex[8];
		g.position = _sysex[9];
		for(size_t t = 0; t < MdGlobal::g_tracks; ++t)
			g.routing[t] = _sysex[g_routingOffset + t];
		auto keys = dumpIo::section(_sysex, g_keymapOffset, 128);
		for(auto& k : g.keymap)
			k = keys.u8();

		const auto* s = _sysex.data() + g_settingsOffset;
		g.baseChannel = s[0];
		g.unused = s[1];
		g.tempo = static_cast<uint16_t>((s[2] << 7) | s[3]);
		g.extendedMode = s[4];
		g.syncFlags = s[5];
		g.localControl = s[6];
		for(size_t i = 0; i < g.inputSettings.size(); ++i)
			g.inputSettings[i] = s[7 + i];
		g.programChange = s[17];
		g.trigMode = s[18];
		return g;
	}

	std::vector<uint8_t> encodeMdGlobal(const MdGlobal& _g)
	{
		auto m = dumpIo::header(g_globalDumpId, _g.version, _g.revision, _g.position);
		m.reserve(MdGlobal::g_dumpSize);
		m.insert(m.end(), _g.routing.begin(), _g.routing.end());
		dumpIo::Writer keys;
		for(const auto k : _g.keymap)
			keys.u8(k);
		keys.appendEncodedTo(m);
		m.insert(m.end(), {_g.baseChannel, _g.unused, static_cast<uint8_t>((_g.tempo >> 7) & 0x7f),
			static_cast<uint8_t>(_g.tempo & 0x7f), _g.extendedMode, _g.syncFlags, _g.localControl});
		m.insert(m.end(), _g.inputSettings.begin(), _g.inputSettings.end());
		m.insert(m.end(), {_g.programChange, _g.trigMode});
		dumpIo::finish(m);
		return m;
	}

	std::vector<uint8_t> mdGlobalRequest(const uint8_t _slot)
	{
		return dumpIo::request(g_globalRequestId, _slot);
	}

	std::vector<uint8_t> mdSetActiveGlobal(const uint8_t _slot)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x56, static_cast<uint8_t>(_slot & 7), 0xf7};
	}
}
