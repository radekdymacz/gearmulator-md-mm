#pragma once

#include "deskCore/deskAdapter.h"

#include "elektronData/mdKit.h"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mdDesk
{
	// What the adapter knows of the kit that plays from the machine's memory (P6), one value: the
	// last image taken, an image not taken yet, and the live edits sent and not yet seen. The kit
	// itself is the core's (DocKind::WorkingKit): observed from memory, pending while edits are on
	// their way. Pure transitions.
	struct KitMemory
	{
		std::optional<elektronData::MdKit> image;		// the last image taken
		std::optional<elektronData::MdKit> shown;		// ... as published (with knob moves on their way)
		std::optional<std::vector<uint8_t>> region;		// an image not taken yet
		deskCore::Expectation<elektronData::MdKit> expect;
	};

	// The kit that plays changed (another kit, a reload): nothing of the old one holds; an image not
	// taken yet may already be the new kit's.
	KitMemory switched(const KitMemory& _m = {});

	struct TakeResult
	{
		KitMemory next;
		std::optional<elektronData::MdKit> take;	// a working kit to publish
		bool settles = false;						// ... and it settles the live edits on their way
		bool askKitStatus = false;					// memory names another kit than status: ask
	};

	// A memory image (_m.region) against what is known: status' current kit, the stored slot (for
	// the dump format bytes) and the knob moves still on their way while recording (kept in the
	// view). An image that predates the editor's own live edits waits for the next one (until the
	// edits are too old to wait for). Pure.
	TakeResult takeMemory(KitMemory _m, std::optional<uint8_t> _currentKit, const elektronData::MdKit* _stored,
		const std::map<std::pair<uint8_t, uint8_t>, uint8_t>& _knobTargets, double _nowMs);

	// Every field that differs between _before and _after has _after's value in _image.
	bool reflects(const elektronData::MdKit& _image, const elektronData::MdKit& _before, const elektronData::MdKit& _after);
}
