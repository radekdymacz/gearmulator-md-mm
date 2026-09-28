#pragma once

#include <cstdint>

namespace deskCore
{
	// The machine's life as one value (P6): what the engine label shows and what gates the
	// page's input. It replaces the booleans the desks kept (firmware, ready, telemetry
	// seen, boot animation, hw, link lost).
	enum class Lifecycle : uint8_t
	{
		Missing,		// no ROM (NO ROM, the first-run screen)
		Unsupported,	// another firmware than the one the editor knows (ROM ERROR)
		Loading,		// the device is prepared or restored (LOADING ROM)
		Booting,		// the firmware starts: MIDI is not taken yet, or no status reply yet
		Animating,		// it answers MIDI, but its start-up animation still ignores panel keys
		Ready,			// takes input
		HwConnecting,	// HW MIDI: nothing has answered yet
		HwLost			// HW MIDI: no answer for a while
	};

	// The facts the lifecycle follows. The adapter gathers them from the device probe,
	// status replies and telemetry.
	struct LifeFacts
	{
		enum class Probe : uint8_t
		{
			Missing, Unsupported, Loading, Booting,
			Running,	// the emulated firmware runs and takes MIDI
			Wire		// HW MIDI: a machine at the end of a cable
		};
		enum class Animation : uint8_t
		{
			Unseen,		// no telemetry looked at yet
			Absent,		// this firmware has no telemetry: the first reply is all there is
			Running,
			Over
		};

		Probe probe = Probe::Booting;
		bool replied = false;			// a status reply since the machine started (or the wire was chosen)
		Animation animation = Animation::Unseen;
		double silentMs = 0;			// Wire: since the last reply (or since the wire was chosen)

		bool operator==(const LifeFacts& _o) const
		{
			return probe == _o.probe && replied == _o.replied && animation == _o.animation && silentMs == _o.silentMs;
		}
	};

	// HW MIDI timing (P4): a machine is lost after 3.5 s without a reply (status is asked for
	// every second); one that never answered, 5 s after the wire was chosen.
	constexpr double g_wireLostMs = 3500;
	constexpr double g_wireFirstReplyMs = 5000;

	// Pure: the state these facts mean (the previous state is not needed: every fact is
	// current, the adapter keeps `replied` and the times).
	Lifecycle lifecycleOf(const LifeFacts& _facts);

	// Loads may run (the firmware answers MIDI).
	bool takesMidi(Lifecycle _l);
	// The page's commands are taken.
	bool takesInput(Lifecycle _l);

	const char* lifecycleName(Lifecycle _l);	// "missing" .. "hwLost"
	// The contract's older strings, derived from the one value (md-desk/machine desk.firmware,
	// desk.boot, desk.link; mm-desk/machine engine).
	const char* legacyFirmware(Lifecycle _l);	// missing, unsupported, loading, booting, ready
	const char* legacyBoot(Lifecycle _l);		// off, starting, animation, ready
	const char* legacyLink(Lifecycle _l, bool _wire);	// local (not HW MIDI), connect, ready, lost
}
