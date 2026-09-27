#include "mdDeskDelivery.h"

#include "elektronData/mdCommands.h"

namespace mdDesk
{
	namespace ed = elektronData;
	using Kind = LiveEdit::Kind;

	namespace
	{
		constexpr uint8_t g_noGroupTarget = 0x7f;

		LiveEdit edit(const Kind _kind, const size_t _track, const size_t _index, const uint8_t _value)
		{
			LiveEdit e;
			e.kind = _kind;
			e.track = static_cast<uint8_t>(_track);
			e.index = static_cast<uint8_t>(_index);
			e.value = _value;
			return e;
		}

		std::string nameText(const std::array<uint8_t, ed::MdKit::g_nameSize>& _name)
		{
			std::string s;
			for(const auto c : _name)
			{
				if(!c)
					break;
				s += static_cast<char>(c);
			}
			return s;
		}
	}

	Delivery kitDelivery(const ed::MdKit& _before, const ed::MdKit& _after)
	{
		Delivery d;
		if(nameText(_before.name) != nameText(_after.name))
		{
			LiveEdit e;
			e.kind = Kind::KitName;
			e.name = nameText(_after.name);
			d.edits.push_back(e);
		}
		for(size_t t = 0; t < ed::MdKit::g_tracks; ++t)
		{
			const bool newMachine = _before.models[t] != _after.models[t];
			if(newMachine)
			{
				auto e = edit(Kind::Machine, t, 0, 0);
				e.model = _after.models[t];
				d.edits.push_back(e);
			}
			for(size_t i = 0; i < ed::MdKit::g_paramsPerTrack; ++i)
			{
				const bool resent = newMachine && i < 8;
				if(resent || _before.params[t][i] != _after.params[t][i])
					d.edits.push_back(edit(Kind::Param, t, i, _after.params[t][i]));
			}
			if(_before.levels[t] != _after.levels[t])
				d.edits.push_back(edit(Kind::Level, t, 0, _after.levels[t]));

			const auto& a = _before.lfos[t];
			const auto& b = _after.lfos[t];
			const uint8_t before[] = {a.track, a.param, a.shape1, a.shape2, a.update};
			const uint8_t after[] = {b.track, b.param, b.shape1, b.shape2, b.update};
			for(size_t f = 0; f < 5; ++f)
				if(before[f] != after[f])
					d.edits.push_back(edit(Kind::Lfo, t, f, after[f]));
			if(a.state != b.state)
				d.notLive.push_back("LFO " + std::to_string(t + 1) + " running state");

			for(const auto& [kind, from, to] : {std::tuple{Kind::TrigGroup, &_before.trigGroups, &_after.trigGroups},
				std::tuple{Kind::MuteGroup, &_before.muteGroups, &_after.muteGroups}})
			{
				if((*from)[t] == (*to)[t])
					continue;
				d.edits.push_back(edit(kind, t, 0, (*to)[t]));
			}
		}
		for(size_t fx = 0; fx < ed::MdKit::MasterFxCount; ++fx)
			for(size_t i = 0; i < 8; ++i)
				if(_before.masterFx[fx][i] != _after.masterFx[fx][i])
					d.edits.push_back(edit(Kind::MasterFx, fx, i, _after.masterFx[fx][i]));
		return d;
	}

	Delivery globalDelivery(const ed::MdGlobal& _before, const ed::MdGlobal& _after)
	{
		Delivery d;
		for(size_t t = 0; t < ed::MdGlobal::g_tracks; ++t)
			if(_before.routing[t] != _after.routing[t])
				d.edits.push_back(edit(Kind::Route, t, 0, _after.routing[t]));
		if(_before.tempo != _after.tempo)
		{
			auto e = edit(Kind::Tempo, 0, 0, 0);
			e.model = _after.tempo;
			d.edits.push_back(e);
		}
		if(_before.extendedMode != _after.extendedMode)
			d.edits.push_back(edit(Kind::LockMode, 0, 0, _after.extendedMode ? 1 : 0));
		auto rest = _after;
		rest.routing = _before.routing;
		rest.tempo = _before.tempo;
		rest.extendedMode = _before.extendedMode;
		if(rest != _before)
			d.notLive.push_back("global settings other than routing, tempo and mode");
		return d;
	}

	std::vector<uint8_t> liveEditSysex(const LiveEdit& _e)
	{
		switch(_e.kind)
		{
		case Kind::Param:
		case Kind::Level:
			return {};
		case Kind::Machine:
			return ed::mdAssignMachine(_e.track, _e.model, ed::MdMachineInit::Synthesis);
		case Kind::Lfo:
			return ed::mdSetLfo(_e.track, _e.index, _e.value);
		case Kind::TrigGroup:
			// Target 0x7f removes the group: OS 1.63 then stores 0xff ("none") in the
			// kit, measured by mdDeskFirmwareTest probe. 0x10 and 0x40 are ignored,
			// a track number (also the track itself) sets a group.
			return ed::mdSetTrigGroup(_e.track, _e.value == ed::MdKit::g_noGroup ? g_noGroupTarget : _e.value);
		case Kind::MuteGroup:
			return ed::mdSetMuteGroup(_e.track, _e.value == ed::MdKit::g_noGroup ? g_noGroupTarget : _e.value);
		case Kind::MasterFx:
			return ed::mdSetMasterFx(_e.track, _e.index, _e.value);
		case Kind::KitName:
			return ed::mdSetKitName(_e.name);
		case Kind::Route:
			return ed::mdSetTrackRouting(_e.track, _e.value);
		case Kind::Tempo:
			return ed::mdSetTempo(static_cast<uint16_t>(_e.model));
		case Kind::LockMode:
			return ed::mdSetStatus(ed::MdStatus::LockMode, _e.value);
		}
		return {};
	}
}
