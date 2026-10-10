#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace deskCore
{
	// A block of steps, the same on both machines (DESIGN-step-selection.md §4-5): steps [from, to) of tracks
	// [track, track + rows), a selection; a track page is a block of one row. copySteps, clearSteps, pasteSteps and
	// copyStepsTo take it on both editors. Where a block put down lands, what of it does not fit and how the result
	// says so are here, once; what a step holds (and so how a row is taken and put) is the machine's
	// (mdDeskEdit.cpp, mmDeskEdit.cpp).

	// What of a block put down lands: rows from its track, steps from its step; and what does not: rows past the
	// machine's last track, steps past the pattern's length.
	struct BlockLanding
	{
		size_t rows = 0, steps = 0;
		size_t cutRows = 0, cutSteps = 0;
	};

	// A block of _blockRows tracks x _blockLength steps put down with its first step at _at on track _track, in a
	// pattern of _tracks tracks and _length steps: it stops at the length (steps past it do not play) and at the last
	// track.
	inline BlockLanding blockLanding(const size_t _blockRows, const size_t _blockLength, const size_t _track, const size_t _at,
		const size_t _tracks, const size_t _length)
	{
		BlockLanding l;
		const auto end = std::min(_at + _blockLength, _length);
		l.steps = end > _at ? end - _at : 0;
		l.cutSteps = _blockLength - l.steps;
		l.rows = std::min(_blockRows, _tracks - std::min(_track, _tracks));
		l.cutRows = _blockRows - l.rows;
		return l;
	}

	// The result's note of a block put down: "<verb> <tracks>, steps a-b", then what was left out: the steps past
	// the pattern's length (_length) and the tracks below the last one (_lastTrack, the machine's name for it).
	inline std::string blockNote(const std::string& _verb, const std::string& _tracks, const size_t _at, const BlockLanding& _l,
		const size_t _length, const std::string& _lastTrack)
	{
		auto note = _verb + " " + _tracks + ", steps " + std::to_string(_at + 1) + "-" + std::to_string(_at + _l.steps);
		if(_l.cutSteps)
			note += ". " + std::to_string(_l.cutSteps) + " step(s) past the pattern's length (" + std::to_string(_length) + ") left out";
		if(_l.cutRows)
			note += ". " + std::to_string(_l.cutRows) + " track(s) below " + _lastTrack + " left out";
		return note;
	}
}
