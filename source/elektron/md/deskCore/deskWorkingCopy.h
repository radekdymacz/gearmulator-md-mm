#pragma once

#include "deskAdapter.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace deskCore
{
	// Where the kit that plays comes from, as one value with one policy (P6, both editors). The kit
	// itself is the core's document (observed from memory or its slot's dump, pending while live
	// edits are on their way); this is only what the adapter needs to decide what to report:
	//   - seed: the kit that plays starts as its slot's next dump (until memory shows it);
	//   - region: a memory image not taken yet (it may predate the editor's own edits);
	//   - expect: the live edits sent and not yet seen (deskCore::Expectation);
	//   - image: the last image taken (the edited/clean judgement reads it).
	// Status is the truth for which kit plays: an image of another kit waits and asks for status.
	template<typename Kit>
	struct WorkingCopy
	{
		bool seed = true;
		std::optional<std::vector<uint8_t>> region;
		Expectation<Kit> expect;
		std::optional<Kit> image;
	};

	// Another kit plays (a switch, a reload): nothing of the old one holds; an image not taken yet
	// may already be the new kit's.
	template<typename Kit>
	WorkingCopy<Kit> switched(const WorkingCopy<Kit>& _w = {})
	{
		WorkingCopy<Kit> w;
		w.region = _w.region;
		return w;
	}

	// The playing kit's slot dump arrived: it seeds the working kit once.
	template<typename Kit>
	std::pair<WorkingCopy<Kit>, std::optional<Kit>> fromDump(WorkingCopy<Kit> _w, const Kit& _slot)
	{
		if(!_w.seed)
			return {std::move(_w), std::nullopt};
		_w.seed = false;
		return {std::move(_w), _slot};
	}

	template<typename Kit>
	struct FromImage
	{
		WorkingCopy<Kit> next;
		std::optional<Kit> take;		// the working kit to report
		bool settles = false;			// ... and it settles the live edits on their way
		bool askStatus = false;			// the image is of another kit than status says: ask
	};

	// A memory image (decoded by the model, _imageKit: which kit it is) against what is known: the
	// kit status says plays, the working kit the core shows (_shown, may be null), and whether live
	// edits must hold (_hold: knob moves still on their way). An image that predates the editor's
	// own live edits waits for the next one, until they are too old to wait for. Pure.
	template<typename Kit, typename Reflects>
	FromImage<Kit> fromImage(WorkingCopy<Kit> _w, const Kit& _image, const int _imageKit, const std::optional<int> _currentKit,
		const Kit* _shown, const double _nowMs, const bool _hold, const Reflects& _reflects)
	{
		FromImage<Kit> r;
		if(!_currentKit || *_currentKit != _imageKit)
		{
			r.askStatus = true;
			r.next = std::move(_w);
			return r;
		}
		if((_hold && _w.expect.any()) || !_w.expect.takes(_image, _nowMs, _reflects))
		{
			r.next = std::move(_w);
			return r;
		}
		_w.region.reset();
		_w.seed = false;
		r.settles = _w.expect.any();
		_w.expect.clear();
		_w.image = _image;
		if(r.settles || !_shown || !(*_shown == _image))
			r.take = _image;
		r.next = std::move(_w);
		return r;
	}
}
