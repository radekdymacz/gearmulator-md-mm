#pragma once

#include "mdDeskChain.h"

namespace mdDesk
{
	// Sequencer telemetry published by the audio thread (MD OS 1.63 RAM bytes), a value.
	struct Telemetry
	{
		int step = -1;			// 0-based, -1 unknown
		int pattern = -1;		// the pattern the sequencer plays, -1 unknown
		bool playing = false;
		bool recording = false;	// live recording (RECORD held + PLAY)
		bool gridEdit = false;	// grid edit (RECORD alone)
		int knobPage = -1;		// DATA ENTRY page: 0 synthesis, 1 effects, 2 routing
		bool valid = false;		// false: no telemetry for this firmware
		// P4: the start-up animation (-1 unknown, 1 running: panel keys are ignored, 0 over),
		// the pattern mutes (bit 0 = track 1, -1 unknown), the firmware's pattern chain and
		// the BANK GROUP (0 A-D, 1 E-H, -1 unknown).
		int bootAnimation = -1;
		int mutes = -1;
		bool chainKnown = false;
		Chain chain;
		int bankGroup = -1;
		// P6: panel packets the device has not sent yet (keys and encoder steps on their way); -1 when
		// the device does not report it (then keys are not held back).
		int panelPending = -1;

		bool operator==(const Telemetry& _o) const
		{
			return step == _o.step && pattern == _o.pattern && playing == _o.playing && recording == _o.recording
				&& gridEdit == _o.gridEdit && knobPage == _o.knobPage && valid == _o.valid && bootAnimation == _o.bootAnimation
				&& mutes == _o.mutes && chainKnown == _o.chainKnown && chain == _o.chain && bankGroup == _o.bankGroup
				&& panelPending == _o.panelPending;
		}
		bool operator!=(const Telemetry& _o) const { return !(*this == _o); }
	};

	// What changed between two telemetry values (P6: one pure function instead of field
	// comparisons at every use).
	struct TelemetryEvents
	{
		bool any = false;
		bool stepped = false;			// the step moved (or playing changed)
		bool playChanged = false;
		bool recordChanged = false;
		bool patternChanged = false;	// the RAM pattern byte, with valid telemetry
		bool wrapped = false;			// the playhead went back to an earlier step: an audible pattern switch
		bool machineChanged = false;	// boot animation, mutes, chain or bank group (the machine document)
	};

	inline TelemetryEvents diff(const Telemetry& _before, const Telemetry& _after)
	{
		TelemetryEvents e;
		e.any = _before != _after;
		e.playChanged = _before.playing != _after.playing;
		e.stepped = _after.valid && (_after.step != _before.step || e.playChanged);
		e.recordChanged = _before.recording != _after.recording;
		e.patternChanged = _after.valid && _after.pattern != _before.pattern;
		e.wrapped = _after.valid && _before.step >= 0 && _after.step >= 0 && _after.step < _before.step;
		e.machineChanged = _before.bootAnimation != _after.bootAnimation || _before.mutes != _after.mutes
			|| _before.chainKnown != _after.chainKnown || _before.chain != _after.chain || _before.bankGroup != _after.bankGroup;
		return e;
	}
}
