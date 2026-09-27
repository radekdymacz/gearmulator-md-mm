#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B pattern (SysEx 0x67, format 6/1), as a plain value.
	// The raw payload is 6,520 bytes after the run-length decode. Offsets are
	// into that payload. Found by single panel edits on an empty pattern and a
	// dump diff each (mmEditorProbeFirmwareTest lab, MM-P1-RESULT §2).
	//
	// Step masks: 6 tracks x u64, bit s = step s (0-based), big-endian:
	//   0x000 amp        AMP trig track (a trig in ALL sets amp, filter, lfo and pitch)
	//   0x030 filter     FILTER trig track
	//   0x060 lfo        LFO trig track
	//   0x090 noteOff    NOTE OFF (FUNCTION + TRIG)
	//   0x0c0 midiTrig   MIDI sequencer trig
	//   0x0f0 midiNoteOff
	//   0x120 pitch      the step carries a pitch (notes[] below); clear = pitchless
	//   0x150 chord      the step has chord notes in chordNotes[]
	//   0x180 midiNote   the MIDI step carries notes in midiNotes[]
	//   0x1b0 slide      slide track
	//   0x1e0 swing      swing track (default every 2nd 16th: 0xaa..)
	//   0x210 midiSlide
	//   0x240 midiSwing
	// 0x270   3  unknown (kept)
	// 0x273   1  swing amount, 0-30 = 50-80 %
	// 0x274  48  lock masks, 6 tracks x 8 pages (SYN AMP FLT EFX LF1 LF2 LF3 MIDI), bit p = parameter p
	// 0x2a4 384  synth notes, 6 x 64 steps, MIDI note number, 0xff = none
	// 0x424   1  length, total steps 2-64
	// 0x425   1  tempo multiplier 0-3 = 1X 2X 3/4X 3/2X
	// 0x426   1  kit 0-127 (LOAD KIT and SAVE KIT write it: the pattern plays that kit)
	// 0x427   1  pattern transpose, signed
	// 0x428  18  synth track transpose (signed), scale (0 --- 1 FIX 2 MAJ 3 MIN), key, 6 each
	// 0x43a  18  MIDI track transpose, scale, key
	// 0x44c 132  synth arpeggiators, 6 each: play | ojmp<<4, mode, range, speed, trigs (bit 0 AMP,
	//            1 FLT, 2 LFO), length; then 6 x 16 rhythm/offset steps (0x40 = offset 0, 0xff = muted)
	// 0x4d0 126  MIDI arpeggiators: the same without trigs
	// 0x54e   4  unknown (kept)
	// 0x552   2  MIDI note count (entries in use in midiNotes)
	// 0x554   1  chord note count (entries in use in chordNotes)
	// 0x555   1  unknown (kept)
	// 0x556   1  lock row count
	// 0x557 3968 62 lock rows x 64 steps, 0xff = no lock on the step. Row r belongs to the r-th set
	//            bit of the lock masks in (track, page, parameter) order: the firmware keeps rows sorted
	// 0x14d7  1  unknown (kept)
	// 0x14d8 800 MIDI notes, 400 x u16: note << 9 | track << 6 | step, sorted by (step, track)
	// 0x17f8 384 synth chord notes, 192 x u16, the same form (the base note is in notes[])
	// Rows and entries past their counts hold firmware residue in factory data: kept byte for byte.
	struct MmArpBlock
	{
		std::array<uint8_t, 6> playOjmp{};
		std::array<uint8_t, 6> mode{};
		std::array<uint8_t, 6> range{};
		std::array<uint8_t, 6> speed{};
		std::array<uint8_t, 6> trigs{};		// synth only
		std::array<uint8_t, 6> length{};
		std::array<std::array<uint8_t, 16>, 6> steps{};

		bool operator==(const MmArpBlock& _o) const
		{
			return playOjmp == _o.playOjmp && mode == _o.mode && range == _o.range && speed == _o.speed
				&& trigs == _o.trigs && length == _o.length && steps == _o.steps;
		}
	};

	struct MmTranspose
	{
		std::array<int8_t, 6> track{};
		std::array<uint8_t, 6> scale{};
		std::array<uint8_t, 6> key{};

		bool operator==(const MmTranspose& _o) const { return track == _o.track && scale == _o.scale && key == _o.key; }
	};

	struct MmPattern
	{
		static constexpr size_t g_tracks = 6;
		static constexpr size_t g_steps = 64;
		static constexpr size_t g_pages = 8;
		static constexpr size_t g_lockRows = 62;
		static constexpr size_t g_midiNoteCapacity = 400;
		static constexpr size_t g_chordNoteCapacity = 192;
		static constexpr size_t g_rawSize = 6520;
		static constexpr size_t g_slots = 128;
		static constexpr uint8_t g_noNote = 0xff;
		static constexpr uint8_t g_noLock = 0xff;

		uint8_t version = 6;
		uint8_t revision = 1;
		uint8_t position = 0;

		using Masks = std::array<uint64_t, g_tracks>;
		Masks amp{}, filter{}, lfo{}, noteOff{}, midiTrig{}, midiNoteOff{}, pitch{}, chord{}, midiNote{},
			slide{}, swing{}, midiSlide{}, midiSwing{};
		std::array<uint8_t, 3> x270{};
		uint8_t swingAmount = 0;
		std::array<std::array<uint8_t, g_pages>, g_tracks> lockMasks{};
		std::array<std::array<uint8_t, g_steps>, g_tracks> notes{};
		uint8_t length = 16;
		uint8_t multiplier = 0;
		uint8_t kit = 0;
		int8_t patternTranspose = 0;
		MmTranspose transpose, midiTranspose;
		MmArpBlock arp, midiArp;
		std::array<uint8_t, 4> x54e{};
		uint16_t midiNoteCount = 0;
		uint8_t chordNoteCount = 0;
		uint8_t x555 = 0;
		uint8_t lockRowCount = 0;
		std::array<std::array<uint8_t, g_steps>, g_lockRows> lockRows{};
		uint8_t x14d7 = 0;
		std::array<uint16_t, g_midiNoteCapacity> midiNotes{};
		std::array<uint16_t, g_chordNoteCapacity> chordNotes{};

		bool operator==(const MmPattern& _o) const;
		bool operator!=(const MmPattern& _o) const { return !(*this == _o); }
	};

	// A lock row's parameter: track 0-5, page 0-7 (SYN AMP FLT EFX LF1 LF2 LF3 MIDI), parameter 0-7.
	struct MmLockParam
	{
		uint8_t track = 0;
		uint8_t page = 0;
		uint8_t param = 0;

		bool operator==(const MmLockParam& _o) const { return track == _o.track && page == _o.page && param == _o.param; }
		bool operator<(const MmLockParam& _o) const
		{
			return (track * 64 + page * 8 + param) < (_o.track * 64 + _o.page * 8 + _o.param);
		}
	};

	// A note entry of midiNotes / chordNotes.
	struct MmNoteEntry
	{
		uint8_t track = 0;
		uint8_t step = 0;
		uint8_t note = 0;

		bool operator==(const MmNoteEntry& _o) const { return track == _o.track && step == _o.step && note == _o.note; }
	};

	constexpr uint16_t mmNoteEntryWord(const MmNoteEntry& _e)
	{
		return static_cast<uint16_t>(((_e.note & 0x7f) << 9) | ((_e.track & 7) << 6) | (_e.step & 63));
	}
	constexpr MmNoteEntry mmNoteEntry(const uint16_t _w)
	{
		return {static_cast<uint8_t>((_w >> 6) & 7), static_cast<uint8_t>(_w & 63), static_cast<uint8_t>(_w >> 9)};
	}

	std::optional<MmPattern> decodeMmPattern(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMmPattern(const MmPattern& _pattern);
	std::optional<MmPattern> mmPatternFromRaw(const std::vector<uint8_t>& _raw, uint8_t _position);
	std::vector<uint8_t> mmPatternRaw(const MmPattern& _pattern);

	std::vector<uint8_t> mmPatternRequest(uint8_t _slot);

	// The parameters of the rows in use, in row order (the set bits of lockMasks).
	std::vector<MmLockParam> mmLockParams(const MmPattern& _pattern);
	// Row index of a locked parameter, or -1.
	int mmLockRow(const MmPattern& _pattern, const MmLockParam& _param);

	inline bool mmStepSet(const uint64_t _mask, const size_t _step) { return (_mask >> _step) & 1; }
	inline uint64_t mmStepBit(const size_t _step) { return uint64_t{1} << _step; }
}
