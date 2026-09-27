#include "mdWorkingKit.h"

namespace elektronData
{
	namespace
	{
		constexpr size_t g_lfoSize = 36;
	}

	std::vector<uint8_t> mdWorkingKitImage(const MdKit& _k)
	{
		std::vector<uint8_t> b;
		b.reserve(g_mdWorkingKitSize);
		b.insert(b.end(), _k.name.begin(), _k.name.end());
		for(const auto& t : _k.params)
			b.insert(b.end(), t.begin(), t.end());
		b.insert(b.end(), _k.levels.begin(), _k.levels.end());
		for(const auto m : _k.models)
			for(int shift = 24; shift >= 0; shift -= 8)
				b.push_back(static_cast<uint8_t>(m >> shift));
		for(const auto& l : _k.lfos)
		{
			b.push_back(l.track);
			b.push_back(l.param);
			b.push_back(l.shape1);
			b.push_back(l.shape2);
			b.push_back(l.update);
			b.insert(b.end(), l.state.begin(), l.state.end());
		}
		for(const auto& fx : _k.masterFx)
			b.insert(b.end(), fx.begin(), fx.end());
		b.insert(b.end(), _k.trigGroups.begin(), _k.trigGroups.end());
		b.insert(b.end(), _k.muteGroups.begin(), _k.muteGroups.end());
		return b;
	}

	std::optional<MdKit> mdWorkingKitFromMemory(const std::vector<uint8_t>& _region)
	{
		if(_region.size() != g_mdWorkingKitRegionSize || _region[0] >= MdKit::g_slots)
			return {};
		return mdWorkingKitFromImage(std::vector<uint8_t>(_region.begin() + 2, _region.end()), _region[0]);
	}

	bool mdSameKitSound(const MdKit& _a, const MdKit& _b)
	{
		if(_a.name != _b.name || _a.params != _b.params || _a.levels != _b.levels || _a.models != _b.models
			|| _a.masterFx != _b.masterFx || _a.trigGroups != _b.trigGroups || _a.muteGroups != _b.muteGroups)
			return false;
		for(size_t t = 0; t < MdKit::g_tracks; ++t)
		{
			const auto& x = _a.lfos[t];
			const auto& y = _b.lfos[t];
			if(x.track != y.track || x.param != y.param || x.shape1 != y.shape1 || x.shape2 != y.shape2
				|| x.update != y.update)
				return false;
		}
		return true;
	}

	std::optional<MdKit> mdWorkingKitFromImage(const std::vector<uint8_t>& _b, const uint8_t _slot)
	{
		if(_b.size() != g_mdWorkingKitSize)
			return {};
		MdKit k;
		k.position = _slot;
		size_t p = 0;
		for(auto& c : k.name)
			c = _b[p++];
		for(auto& t : k.params)
			for(auto& v : t)
				v = _b[p++];
		for(auto& v : k.levels)
			v = _b[p++];
		for(auto& m : k.models)
		{
			m = 0;
			for(int i = 0; i < 4; ++i)
				m = (m << 8) | _b[p++];
		}
		for(auto& l : k.lfos)
		{
			l.track = _b[p++];
			l.param = _b[p++];
			l.shape1 = _b[p++];
			l.shape2 = _b[p++];
			l.update = _b[p++];
			for(auto& s : l.state)
				s = _b[p++];
		}
		for(auto& fx : k.masterFx)
			for(auto& v : fx)
				v = _b[p++];
		for(auto& g : k.trigGroups)
			g = _b[p++];
		for(auto& g : k.muteGroups)
			g = _b[p++];
		static_assert(g_lfoSize == 5 + 31);
		return k;
	}
}
