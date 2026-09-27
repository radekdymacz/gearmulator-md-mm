#include "mdKit.h"

#include "dumpIo.h"

namespace elektronData
{
	namespace
	{
		constexpr uint8_t g_kitDumpId = 0x52;
		constexpr uint8_t g_kitRequestId = 0x53;
		constexpr uint8_t g_loadKitId = 0x58;
		constexpr uint8_t g_saveKitId = 0x59;

		constexpr size_t g_nameOffset = 0x0a;
		constexpr size_t g_paramsOffset = 0x1a;
		constexpr size_t g_levelsOffset = 0x19a;
		constexpr size_t g_modelsOffset = 0x1aa;
		constexpr size_t g_lfoOffset = 0x1f4;
		constexpr size_t g_fxOffset = 0x487;
		constexpr size_t g_groupsOffset = 0x4a7;

		constexpr size_t g_lfoSize = 36;
		constexpr size_t g_modelsSize = MdKit::g_tracks * 4;
		constexpr size_t g_lfosSize = MdKit::g_tracks * g_lfoSize;
		constexpr size_t g_groupsSize = MdKit::g_tracks * 2;

		static_assert(g_paramsOffset == g_nameOffset + MdKit::g_nameSize);
		static_assert(g_levelsOffset == g_paramsOffset + MdKit::g_tracks * MdKit::g_paramsPerTrack);
		static_assert(g_modelsOffset == g_levelsOffset + MdKit::g_tracks);
		static_assert(g_lfoOffset == g_modelsOffset + encoded7BitSize(g_modelsSize));
		static_assert(g_fxOffset == g_lfoOffset + encoded7BitSize(g_lfosSize));
		static_assert(g_groupsOffset == g_fxOffset + MdKit::MasterFxCount * 8);
		static_assert(MdKit::g_dumpSize == g_groupsOffset + encoded7BitSize(g_groupsSize) + 5);
	}

	bool MdKit::operator==(const MdKit& _o) const
	{
		return version == _o.version && revision == _o.revision && position == _o.position && name == _o.name
			&& params == _o.params && levels == _o.levels && models == _o.models && lfos == _o.lfos
			&& masterFx == _o.masterFx && trigGroups == _o.trigGroups && muteGroups == _o.muteGroups;
	}

	std::optional<MdKit> decodeMdKit(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() != MdKit::g_dumpSize || !dumpIo::isValidDump(_sysex, g_kitDumpId))
			return {};

		MdKit k;
		k.version = _sysex[7];
		k.revision = _sysex[8];
		k.position = _sysex[9];

		for(size_t i = 0; i < MdKit::g_nameSize; ++i)
			k.name[i] = _sysex[g_nameOffset + i];
		for(size_t t = 0; t < MdKit::g_tracks; ++t)
		{
			for(size_t p = 0; p < MdKit::g_paramsPerTrack; ++p)
				k.params[t][p] = _sysex[g_paramsOffset + t * MdKit::g_paramsPerTrack + p];
			k.levels[t] = _sysex[g_levelsOffset + t];
		}

		auto models = dumpIo::section(_sysex, g_modelsOffset, g_modelsSize);
		for(auto& m : k.models)
			m = models.u32();

		auto lfos = dumpIo::section(_sysex, g_lfoOffset, g_lfosSize);
		for(auto& l : k.lfos)
		{
			l.track = lfos.u8();
			l.param = lfos.u8();
			l.shape1 = lfos.u8();
			l.shape2 = lfos.u8();
			l.update = lfos.u8();
			for(auto& s : l.state)
				s = lfos.u8();
		}

		for(size_t f = 0; f < MdKit::MasterFxCount; ++f)
			for(size_t p = 0; p < 8; ++p)
				k.masterFx[f][p] = _sysex[g_fxOffset + f * 8 + p];

		auto groups = dumpIo::section(_sysex, g_groupsOffset, g_groupsSize);
		for(auto& g : k.trigGroups)
			g = groups.u8();
		for(auto& g : k.muteGroups)
			g = groups.u8();
		return k;
	}

	std::vector<uint8_t> encodeMdKit(const MdKit& _k)
	{
		auto m = dumpIo::header(g_kitDumpId, _k.version, _k.revision, _k.position);
		m.reserve(MdKit::g_dumpSize);

		m.insert(m.end(), _k.name.begin(), _k.name.end());
		for(const auto& track : _k.params)
			m.insert(m.end(), track.begin(), track.end());
		m.insert(m.end(), _k.levels.begin(), _k.levels.end());

		dumpIo::Writer models;
		for(const auto v : _k.models)
			models.u32(v);
		models.appendEncodedTo(m);

		dumpIo::Writer lfos;
		for(const auto& l : _k.lfos)
		{
			lfos.u8(l.track);
			lfos.u8(l.param);
			lfos.u8(l.shape1);
			lfos.u8(l.shape2);
			lfos.u8(l.update);
			for(const auto s : l.state)
				lfos.u8(s);
		}
		lfos.appendEncodedTo(m);

		for(const auto& fx : _k.masterFx)
			m.insert(m.end(), fx.begin(), fx.end());

		dumpIo::Writer groups;
		for(const auto g : _k.trigGroups)
			groups.u8(g);
		for(const auto g : _k.muteGroups)
			groups.u8(g);
		groups.appendEncodedTo(m);

		dumpIo::finish(m);
		return m;
	}

	std::vector<uint8_t> mdKitRequest(const uint8_t _slot)
	{
		return dumpIo::request(g_kitRequestId, _slot);
	}

	std::vector<uint8_t> mdLoadKit(const uint8_t _slot)
	{
		return dumpIo::request(g_loadKitId, _slot);
	}

	std::vector<uint8_t> mdSaveKit(const uint8_t _slot)
	{
		return dumpIo::request(g_saveKitId, _slot);
	}
}
