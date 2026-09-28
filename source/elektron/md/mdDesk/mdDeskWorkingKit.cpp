#include "mdDeskWorkingKit.h"

#include "elektronData/mdWorkingKit.h"

namespace mdDesk
{
	namespace ed = elektronData;

	namespace
	{
		template<typename T>
		bool fieldOk(const T& _image, const T& _before, const T& _after)
		{
			return _before == _after || _image == _after;
		}
	}

	bool reflects(const ed::MdKit& _image, const ed::MdKit& _before, const ed::MdKit& _after)
	{
		if(!fieldOk(_image.name, _before.name, _after.name))
			return false;
		for(size_t t = 0; t < ed::MdKit::g_tracks; ++t)
		{
			if(!fieldOk(_image.models[t], _before.models[t], _after.models[t]) || !fieldOk(_image.levels[t], _before.levels[t], _after.levels[t])
				|| !fieldOk(_image.trigGroups[t], _before.trigGroups[t], _after.trigGroups[t])
				|| !fieldOk(_image.muteGroups[t], _before.muteGroups[t], _after.muteGroups[t]))
				return false;
			for(size_t i = 0; i < ed::MdKit::g_paramsPerTrack; ++i)
				if(!fieldOk(_image.params[t][i], _before.params[t][i], _after.params[t][i]))
					return false;
			const auto& a = _after.lfos[t];
			const auto& b = _before.lfos[t];
			const auto& m = _image.lfos[t];
			if(!fieldOk(m.track, b.track, a.track) || !fieldOk(m.param, b.param, a.param) || !fieldOk(m.shape1, b.shape1, a.shape1)
				|| !fieldOk(m.shape2, b.shape2, a.shape2) || !fieldOk(m.update, b.update, a.update))
				return false;
		}
		for(size_t f = 0; f < ed::MdKit::MasterFxCount; ++f)
			if(!fieldOk(_image.masterFx[f], _before.masterFx[f], _after.masterFx[f]))
				return false;
		return true;
	}

	WorkingKit liveEdited(WorkingKit _w, const ed::MdKit& _from, const ed::MdKit& _to, const double _nowMs)
	{
		if(!_w.expecting(_nowMs) || !_w.expectFrom)
			_w.expectFrom = _from;
		_w.expectTo = _to;
		_w.expectedAtMs = _nowMs;
		_w.tracked = _to;
		return _w;
	}

	WorkingKit switched(const WorkingKit& _w)
	{
		WorkingKit w;
		w.region = _w.region;
		return w;
	}

	TakeResult takeMemory(WorkingKit _w, const std::optional<uint8_t> _currentKit, const ed::MdKit* _stored,
		const std::map<std::pair<uint8_t, uint8_t>, uint8_t>& _knobTargets, const double _nowMs)
	{
		TakeResult r;
		if(!_w.region)
		{
			r.next = std::move(_w);
			return r;
		}
		auto kit = ed::mdWorkingKitFromMemory(*_w.region);
		if(!kit)
		{
			_w.region.reset();
			r.next = std::move(_w);
			return r;
		}
		// Memory names the current kit. Status is polled; until it agrees, ask and keep the image.
		if(_currentKit != kit->position)
		{
			r.askKitStatus = true;
			r.next = std::move(_w);
			return r;
		}
		// An image from before the editor's own live edit: wait for the next one.
		if(_w.expecting(_nowMs) && _w.expectFrom && !reflects(*kit, *_w.expectFrom, *_w.expectTo))
		{
			r.next = std::move(_w);
			return r;
		}
		_w.region.reset();
		_w.expectFrom.reset();
		_w.expectTo.reset();
		if(_stored)
		{
			kit->version = _stored->version;
			kit->revision = _stored->revision;
		}
		_w.memory = *kit;
		// Knob moves still on their way stay in the view.
		for(const auto& [key, value] : _knobTargets)
			kit->params[key.first][key.second] = value;
		if(!_w.tracked || !(*_w.tracked == *kit))
			r.show = *kit;
		_w.tracked = *kit;
		r.next = std::move(_w);
		return r;
	}
}
