#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B global settings (SysEx 0x50, format 3/1), as a plain
	// value. The raw payload is 264 bytes (MM-P1-RESULT §5).
	//   0x00  auto track channel, base channel, channel span, multi trig channel,
	//         multi map channel (0-based; the order follows the MIDI CHANNELS screen's values)
	//   0x05   1  CONTROL IN TEMPO SYNC (GLOBAL › MIDI SYNC CLOCK IN): 0 INTERNAL, 1 EXT MIDI CLK
	//              (panel, MM-P4; measured by mmDeskFirmwareTest hostclock, P7)
	//   0x06   1  CONTROL IN TRANSPORT (TRANSPORT IN): 0 IGNORE, 1 ACCEPT (MIDI Start/Stop)
	//              (panel, MM-P4); both 0 as booted
	//   0x07  11  unknown (kept)
	//   0x12   6  MIDI sequencer track channels (inferred)
	//   0x18  24  MIDI sequencer CC numbers CL1-4, 6 x 4 (inferred)
	//   0x30   6  unknown (kept)
	//   0x36   6  CONTROL OUT1 / OUT2 / IN settings (kept, not decoded)
	//   0x3c 192  multi map, 6 fields x 32 ranges (MULTIMAP EDIT, panel, MM-P4): upper key; pattern
	//             (0xff CUR); offset (0xff ---); length; transpose (signed); timing (0 DIR, 1 2 4
	//             8 16 32). Ranges past the last repeat its upper key
	//   0xfc   1  routing mode: 0 3xSTEREO + AB=MIX, 1 3xSTEREO, 2 6xMONO (panel)
	//   0xfd   9  unknown (kept)
	//   0x106  2  master tune, tenths of Hz (4400 = 440.0 Hz) (panel)
	struct MmGlobal
	{
		static constexpr size_t g_rawSize = 264;
		static constexpr size_t g_slots = 8;
		static constexpr size_t g_mapRanges = 32;

		uint8_t version = 3;
		uint8_t revision = 1;
		uint8_t position = 0;

		uint8_t autoChannel = 8, baseChannel = 0, channelSpan = 6, multiTrigChannel = 6, multiMapChannel = 7;
		uint8_t tempoSync = 0, transportIn = 0;
		std::array<uint8_t, 11> x07{};
		std::array<uint8_t, 6> midiSeqChannels{};
		std::array<std::array<uint8_t, 4>, 6> midiSeqCcs{};
		std::array<uint8_t, 6> x30{};
		std::array<uint8_t, 6> control{};
		std::array<std::array<uint8_t, g_mapRanges>, 6> multiMap{};
		uint8_t routingMode = 0;
		std::array<uint8_t, 9> xfd{};
		uint16_t masterTune = 4400;

		bool operator==(const MmGlobal& _o) const;
		bool operator!=(const MmGlobal& _o) const { return !(*this == _o); }
	};

	// B-051: the MIDI channel the machine takes track _track's CCs, notes and mutes on, or none. Measured on OS 1.32B
	// (mmDeskFirmwareTest spanprobe, a CC on each channel under a global): track t is base + t while t < CHANNEL SPAN
	// and base + t is 0-14; channel 16 (15) reaches no track whatever the base, a base past 14 (OFF is 127) none.
	// With CHANNEL SPAN 0 no track has one. One more channel past the span reached T1, the selected track (base + span + 2
	// in every global tried: B-026's "T3's mute muted T1" under span 0), so nothing is sent outside the span.
	std::optional<uint8_t> mmTrackChannel(const MmGlobal& _global, uint8_t _track);
	// NRPN goes on the base channel with the track in its MSB: none while the base channel is past 14.
	bool mmBaseChannelOn(const MmGlobal& _global);

	std::optional<MmGlobal> decodeMmGlobal(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMmGlobal(const MmGlobal& _global);
	std::optional<MmGlobal> mmGlobalFromRaw(const std::vector<uint8_t>& _raw, uint8_t _position);
	std::vector<uint8_t> mmGlobalRaw(const MmGlobal& _global);

	std::vector<uint8_t> mmGlobalRequest(uint8_t _slot);
}
