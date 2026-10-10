#include "mmGlobal.h"

#include "mmDump.h"
#include "mmLayout.h"

namespace elektronData
{
	namespace
	{
		template<typename IO, typename G>
		void layout(IO& _io, G& _g)
		{
			_io.u8(_g.autoChannel);
			_io.u8(_g.baseChannel);
			_io.u8(_g.channelSpan);
			_io.u8(_g.multiTrigChannel);
			_io.u8(_g.multiMapChannel);
			_io.u8(_g.tempoSync);
			_io.u8(_g.transportIn);
			_io.bytes(_g.x07);
			_io.u8(_g.transportOut);
			_io.u8(_g.clockOut);
			_io.u8(_g.programChangeOut);
			_io.bytes(_g.x0d);
			_io.bytes(_g.midiSeqChannels);
			_io.bytes(_g.midiSeqCcs);
			_io.bytes(_g.x30);
			_io.bytes(_g.control);
			_io.bytes(_g.multiMap);
			_io.u8(_g.routingMode);
			_io.bytes(_g.xfd);
			_io.u16(_g.masterTune);
		}
	}

	bool MmGlobal::operator==(const MmGlobal& _o) const
	{
		return mmGlobalRaw(*this) == mmGlobalRaw(_o) && version == _o.version && revision == _o.revision
			&& position == _o.position;
	}

	bool mmBaseChannelOn(const MmGlobal& _global) { return _global.baseChannel <= 14; }

	std::optional<uint8_t> mmTrackChannel(const MmGlobal& _global, const uint8_t _track)
	{
		const int ch = _global.baseChannel + _track;
		if(!mmBaseChannelOn(_global) || _track >= _global.channelSpan || ch > 14)
			return std::nullopt;
		return static_cast<uint8_t>(ch);
	}

	std::optional<MmGlobal> mmGlobalFromRaw(const std::vector<uint8_t>& _raw, const uint8_t _position)
	{
		if(_raw.size() != MmGlobal::g_rawSize)
			return std::nullopt;
		MmGlobal g;
		g.position = _position;
		mmLayout::Reader r(_raw);
		layout(r, g);
		return r.position() == MmGlobal::g_rawSize ? std::optional<MmGlobal>(g) : std::nullopt;
	}

	std::vector<uint8_t> mmGlobalRaw(const MmGlobal& _global)
	{
		mmLayout::Writer w;
		layout(w, _global);
		return w.raw();
	}

	std::optional<MmGlobal> decodeMmGlobal(const std::vector<uint8_t>& _sysex)
	{
		const auto dump = unpackMmDump(_sysex);
		if(!dump || dump->command != g_mmGlobalDump)
			return std::nullopt;
		auto g = mmGlobalFromRaw(dump->raw, dump->position);
		if(g)
		{
			g->version = dump->version;
			g->revision = dump->revision;
		}
		return g;
	}

	std::vector<uint8_t> encodeMmGlobal(const MmGlobal& _global)
	{
		return packMmDump({g_mmGlobalDump, _global.version, _global.revision, _global.position, mmGlobalRaw(_global)});
	}

	std::vector<uint8_t> mmGlobalRequest(const uint8_t _slot) { return mmRequest(0x51, _slot); }
}
