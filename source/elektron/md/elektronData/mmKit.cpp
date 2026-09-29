#include "mmKit.h"

#include "mmDump.h"
#include "mmLayout.h"

namespace elektronData
{
	namespace
	{
		template<typename IO, typename K>
		void layout(IO& _io, K& _k)
		{
			_io.bytes(_k.name);
			_io.bytes(_k.levels);
			for(auto& t : _k.tracks)
			{
				_io.bytes(t.pages);
				_io.bytes(t.midi);
				_io.bytes(t.multiEnv);
				_io.bytes(t.extra);
			}
			_io.bytes(_k.machines);
			_io.bytes(_k.routing);
			_io.bytes(_k.x1cd);
			_io.u8(_k.mirrorMask);
			_io.u8(_k.x1d1);
			_io.bytes(_k.assignPage);
			_io.bytes(_k.assignDest);
			_io.bytes(_k.assignAdd);
			_io.u8(_k.lpfMask);
			_io.u8(_k.hpfMask);
			_io.u8(_k.portamentoMask);
			_io.bytes(_k.trigPos);
			_io.u8(_k.legatoAmp);
			_io.u8(_k.legatoFilter);
			_io.u8(_k.legatoLfo);
			_io.u8(_k.multiTrigMode);
			_io.u8(_k.multiTrigTiming);
			_io.u8(_k.splitKey);
			_io.u8(_k.splitTrack);
		}
	}

	bool MmKit::operator==(const MmKit& _o) const
	{
		return mmKitRaw(*this) == mmKitRaw(_o) && version == _o.version && revision == _o.revision && position == _o.position;
	}

	std::optional<MmKit> mmKitFromRaw(const std::vector<uint8_t>& _raw, const uint8_t _position)
	{
		if(_raw.size() != MmKit::g_rawSize)
			return std::nullopt;
		MmKit k;
		k.position = _position;
		mmLayout::Reader r(_raw);
		layout(r, k);
		return r.position() == MmKit::g_rawSize ? std::optional<MmKit>(k) : std::nullopt;
	}

	std::vector<uint8_t> mmKitRaw(const MmKit& _kit)
	{
		mmLayout::Writer w;
		layout(w, _kit);
		return w.raw();
	}

	std::optional<MmKit> decodeMmKit(const std::vector<uint8_t>& _sysex)
	{
		const auto dump = unpackMmDump(_sysex);
		if(!dump || dump->command != g_mmKitDump)
			return std::nullopt;
		auto k = mmKitFromRaw(dump->raw, dump->position);
		if(k)
		{
			k->version = dump->version;
			k->revision = dump->revision;
		}
		return k;
	}

	std::vector<uint8_t> encodeMmKit(const MmKit& _kit)
	{
		return packMmDump({g_mmKitDump, _kit.version, _kit.revision, _kit.position, mmKitRaw(_kit)});
	}

	std::vector<uint8_t> mmKitRequest(const uint8_t _slot) { return mmRequest(0x53, _slot); }
}
