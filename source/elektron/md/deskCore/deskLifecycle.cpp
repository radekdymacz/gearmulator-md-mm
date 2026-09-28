#include "deskLifecycle.h"

namespace deskCore
{
	Lifecycle lifecycleOf(const LifeFacts& _f)
	{
		using P = LifeFacts::Probe;
		using A = LifeFacts::Animation;
		switch(_f.probe)
		{
		case P::Missing: return Lifecycle::Missing;
		case P::Unsupported: return Lifecycle::Unsupported;
		case P::Loading: return Lifecycle::Loading;
		case P::Booting: return Lifecycle::Booting;
		case P::Running:
			if(!_f.replied)
				return Lifecycle::Booting;
			return _f.animation == A::Absent || _f.animation == A::Over ? Lifecycle::Ready : Lifecycle::Animating;
		case P::Wire:
			if(!_f.replied)
				return _f.silentMs > g_wireFirstReplyMs ? Lifecycle::HwLost : Lifecycle::HwConnecting;
			return _f.silentMs > g_wireLostMs ? Lifecycle::HwLost : Lifecycle::Ready;
		}
		return Lifecycle::Booting;
	}

	const std::vector<LifeRow>& lifecycleRows()
	{
		static const std::vector<LifeRow> rows{
			{Lifecycle::Missing, "missing", false, false},
			{Lifecycle::Unsupported, "unsupported", false, false},
			{Lifecycle::Loading, "loading", false, false},
			{Lifecycle::Booting, "booting", false, false},
			{Lifecycle::Animating, "animating", true, false},
			{Lifecycle::Ready, "ready", true, true},
			{Lifecycle::HwConnecting, "hwConnecting", true, false},
			{Lifecycle::HwLost, "hwLost", true, true}};
		return rows;
	}

	const LifeRow& lifecycleRow(const Lifecycle _l)
	{
		for(const auto& r : lifecycleRows())
			if(r.lifecycle == _l)
				return r;
		return lifecycleRows()[3];
	}
}
