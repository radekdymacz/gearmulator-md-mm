#pragma once

#include "elektronData/mdKit.h"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mdDesk
{
	// The kit that plays, as one value (P6). What the machine's memory image says, what the
	// editor sent live and has not yet seen in memory, and what the page is shown. Pure
	// transitions; the adapter keeps one of these and nothing else about the working kit.
	//
	// Timing on facts: a memory image read right after a live edit can predate it (the edit is
	// still in the MIDI queue). An image is taken once it reflects every field the editor changed
	// (expectFrom -> expectTo), or once the edit is older than g_expectTimeoutMs (the machine
	// then holds something else: it wins).
	struct WorkingKit
	{
		static constexpr double g_expectTimeoutMs = 1000;

		std::optional<elektronData::MdKit> memory;		// the last image taken from memory
		std::optional<elektronData::MdKit> tracked;		// the working copy shown (memory, or the edits the editor saw)
		std::optional<std::vector<uint8_t>> region;		// an image not taken yet
		std::optional<elektronData::MdKit> expectFrom;	// the kit before the first live edit not yet seen
		std::optional<elektronData::MdKit> expectTo;	// ... and after the latest
		double expectedAtMs = 0;

		bool expecting(double _nowMs) const { return expectTo && _nowMs - expectedAtMs < g_expectTimeoutMs; }
		bool fromMemory(uint8_t _kit) const { return memory && memory->position == _kit; }
	};

	// A live edit went out (_from -> _to): the working copy is _to until memory shows it.
	WorkingKit liveEdited(WorkingKit _w, const elektronData::MdKit& _from, const elektronData::MdKit& _to, double _nowMs);
	// The kit that plays changed (another kit, a reload): nothing of the old one holds; an image not
	// taken yet may already be the new kit's.
	WorkingKit switched(const WorkingKit& _w = {});

	struct TakeResult
	{
		WorkingKit next;
		std::optional<elektronData::MdKit> show;	// a new working copy for the page
		bool askKitStatus = false;					// memory names another kit than status: ask
	};

	// A memory image (_w.region) against what is known: status' current kit, the stored slot (for
	// the dump format bytes) and the knob moves still on their way while recording (kept in the
	// view). Pure.
	TakeResult takeMemory(WorkingKit _w, std::optional<uint8_t> _currentKit, const elektronData::MdKit* _stored,
		const std::map<std::pair<uint8_t, uint8_t>, uint8_t>& _knobTargets, double _nowMs);

	// Every field that differs between _before and _after has _after's value in _image.
	bool reflects(const elektronData::MdKit& _image, const elektronData::MdKit& _before, const elektronData::MdKit& _after);
}
