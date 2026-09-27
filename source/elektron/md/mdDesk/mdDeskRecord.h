#pragma once

#include "elektronData/mdKit.h"
#include "elektronData/mdPattern.h"

#include <cstdint>
#include <map>
#include <optional>
#include <utility>

namespace mdDesk
{
	// Live recording on MD OS 1.63 (P3, mdEditorProbeFirmwareTest liverec,
	// livelocks, recknobs): the firmware records TRIG keys and MIDI notes as
	// trigs, and DATA ENTRY knob turns of the selected track as locks on that
	// track's next note. CCs are not recorded. So a knob move made in the editor
	// while recording has to become panel steps: select the track, show the right
	// knob page (SYNTHESIS / EFFECTS / ROUTING), turn the knob by the difference.
	// One knob step is one value step, with no acceleration (measured).
	//
	// KnobRecorder is that plan as plain data: the page says which value it
	// wants, the machine's memory says where the value is, next() says what to
	// press. It sends nothing itself.
	struct KnobStep
	{
		enum class Kind : uint8_t
		{
			SelectTrack,	// SET STATUS 0x22 = track
			PageKey,		// press SYNTHESIS/EFFECTS/ROUTING once
			Turn			// DATA ENTRY encoder (0-7) by steps (+/-)
		};

		Kind kind = Kind::Turn;
		uint8_t track = 0;
		uint8_t encoder = 0;
		int steps = 0;

		bool operator==(const KnobStep& _o) const
		{
			return kind == _o.kind && track == _o.track && encoder == _o.encoder && steps == _o.steps;
		}
	};

	class KnobRecorder
	{
	public:
		static constexpr double g_selectGapMs = 40;		// SET STATUS before the first turn
		static constexpr double g_pageGapMs = 160;		// a key press and its release
		static constexpr double g_turnGapMs = 50;		// let memory show the last turn
		static constexpr int g_maxStepsPerTurn = 32;

		// The value the user wants for kit parameter _index (0-23) of _track; the
		// latest wins.
		void want(uint8_t _track, uint8_t _index, uint8_t _value);

		// The next panel step, or none (nothing to do, or waiting). _memory is the
		// working kit read from the machine; _knobPage 0-2 or -1 unknown.
		std::optional<KnobStep> next(double _nowMs, int _knobPage, const elektronData::MdKit* _memory);

		// Recording started or stopped: forget which track the machine has
		// selected (the panel may have changed it) and what was pending.
		void reset();

		bool pending() const { return !m_targets.empty(); }
		// Values still on their way, so the view can keep showing them.
		const std::map<std::pair<uint8_t, uint8_t>, uint8_t>& targets() const { return m_targets; }

	private:
		std::map<std::pair<uint8_t, uint8_t>, uint8_t> m_targets;
		std::optional<uint8_t> m_selected;
		double m_lastMs = -1e9;
		double m_lastPageMs = -1e9;
	};

	// Which trig a DATA ENTRY turn locks while live recording (P4, mdP4ProbeFirmwareTest
	// lockwindow): the track's next programmed trig whose step starts after the turn. A turn
	// 8 ms before a trig's step locks it; one at or after the step start is too late for it
	// and goes to the track's following trig. (A note played live in the same moment is not
	// reliable: its lock came through in 3 of 9 tries.) _currentStep is the step playing when
	// the turn lands; the pattern loops at its length. None when the track has no trig.
	std::optional<uint8_t> nextLockStep(const elektronData::MdPattern& _pattern, uint8_t _track, int _currentStep);
}
