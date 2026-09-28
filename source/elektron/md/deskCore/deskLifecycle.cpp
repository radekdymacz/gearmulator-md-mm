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

	bool takesMidi(const Lifecycle _l)
	{
		return _l == Lifecycle::Animating || _l == Lifecycle::Ready || _l == Lifecycle::HwConnecting || _l == Lifecycle::HwLost;
	}

	bool takesInput(const Lifecycle _l)
	{
		return _l == Lifecycle::Ready || _l == Lifecycle::HwLost;
	}

	const char* lifecycleName(const Lifecycle _l)
	{
		switch(_l)
		{
		case Lifecycle::Missing: return "missing";
		case Lifecycle::Unsupported: return "unsupported";
		case Lifecycle::Loading: return "loading";
		case Lifecycle::Booting: return "booting";
		case Lifecycle::Animating: return "animating";
		case Lifecycle::Ready: return "ready";
		case Lifecycle::HwConnecting: return "hwConnecting";
		case Lifecycle::HwLost: return "hwLost";
		}
		return "booting";
	}

	const char* legacyFirmware(const Lifecycle _l)
	{
		switch(_l)
		{
		case Lifecycle::Missing: return "missing";
		case Lifecycle::Unsupported: return "unsupported";
		case Lifecycle::Loading: return "loading";
		case Lifecycle::Ready:
		case Lifecycle::HwLost: return "ready";
		default: return "booting";
		}
	}

	const char* legacyBoot(const Lifecycle _l)
	{
		switch(_l)
		{
		case Lifecycle::Missing:
		case Lifecycle::Unsupported:
		case Lifecycle::Loading: return "off";
		case Lifecycle::Animating: return "animation";
		case Lifecycle::Ready:
		case Lifecycle::HwLost: return "ready";
		default: return "starting";
		}
	}

	const char* legacyLink(const Lifecycle _l, const bool _wire)
	{
		if(!_wire)
			return "local";
		switch(_l)
		{
		case Lifecycle::Ready: return "ready";
		case Lifecycle::HwLost: return "lost";
		default: return "connect";
		}
	}
}
