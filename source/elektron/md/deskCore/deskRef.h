#pragma once

#include <cstdint>

namespace deskCore
{
	// A document's name: its kind (the model's enum: pattern, kit, song, global) and slot.
	// A plain value; the MD and MM models alias it with their own Kind.
	template<typename Kind>
	struct Ref
	{
		Kind kind{};
		uint8_t slot = 0;

		bool operator==(const Ref& _o) const { return kind == _o.kind && slot == _o.slot; }
		bool operator!=(const Ref& _o) const { return !(*this == _o); }
		bool operator<(const Ref& _o) const { return kind != _o.kind ? kind < _o.kind : slot < _o.slot; }
	};
}
