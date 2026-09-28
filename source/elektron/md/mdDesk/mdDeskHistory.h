#pragma once

#include "mdDeskEdit.h"

#include "deskCore/deskHistory.h"
#include "deskCore/deskPush.h"

namespace mdDesk
{
	// Undo/redo and latest-wins pushes are the engine-neutral ones (deskCore, P6).
	using History = deskCore::History<Change>;
	template<typename T>
	using PushSlot = deskCore::PushSlot<T>;
}
