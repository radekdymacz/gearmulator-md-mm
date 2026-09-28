#pragma once

#include <cstdint>
#include <vector>

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

	// One row per state (P6): its name and what it opens. The pages read the machine document's
	// input/midi flags, and the contract's lifecycle enum is these names.
	struct LifeRow
	{
		Lifecycle lifecycle;
		const char* name;
		bool midi;		// loads may run: the firmware answers MIDI
		bool input;		// the page's commands are taken
	};
	const std::vector<LifeRow>& lifecycleRows();
	const LifeRow& lifecycleRow(Lifecycle _l);

	inline bool takesMidi(const Lifecycle _l) { return lifecycleRow(_l).midi; }
	inline bool takesInput(const Lifecycle _l) { return lifecycleRow(_l).input; }
	inline const char* lifecycleName(const Lifecycle _l) { return lifecycleRow(_l).name; }
}
