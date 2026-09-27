#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B kit (SysEx 0x52, format 2/1), as a plain value. The
	// raw payload is 698 bytes. Offsets into it (MM-P1-RESULT §3; "live" = found
	// by a live SysEx or CC and a kit diff, "panel" = by a panel edit):
	//   0x000  11  name, 7-bit, NUL padded (0x55, live)
	//   0x00b   6  track levels (CC 7, live)
	//   0x011 432  6 tracks x 72: 7 DATA pages x 8 (SYN AMP FLT EFX LF1 LF2 LF3, CC 48-119, live),
	//              the MIDI page x 8 (LEN VEL PB PCHG CC1-4, NRPN 0x38-0x3f), the MULTI ENV x 6
	//              (NRPN 0x40-0x45: ATK DEC SUS REL PORT, one more; the same on every track in the
	//              factory set), 2 unknown bytes (kept)
	//   0x1c1   6  machines (0x5B ids, live)
	//   0x1c7   6  routing (0x5C, live): output buses in bits 0-2 (AB 1, CD 2, EF 4), input in
	//              bits 3-5 (0 NEIGHBOR, 1 INP A, 2 INP B, 3 INP A+B, 4-6 BUS AB/CD/EF; FX machines)
	//   0x1cd   5  unknown (kept)
	//   0x1d2  72  ASSIGN pages, 6 tracks x 6 sources x 2 rows (panel)
	//   0x21a  72  ASSIGN destinations (panel)
	//   0x262  72  ASSIGN amounts, signed (panel)
	//                sources: JOY R/L (JOY R), JOY L, JOY U, JOY D, VELOCITY, KEY TRACKING
	//   0x2aa   3  track bit masks: JOY mirror, key tracking HPF, LPF (inferred from the factory set)
	//   0x2ad   6  TRIG POS: the track that forwards notes, 0xff = --- (panel)
	//   0x2b3   3  track bit masks: LEGATO AMP (panel), FLT, LFO (inferred)
	//   0x2b6   4  unknown (kept)
	// The working kit (the one that plays, unsaved edits included) is this raw
	// payload in patch RAM at 0x700028 (MM-P0-RESULT §5).
	struct MmKitTrack
	{
		std::array<std::array<uint8_t, 8>, 7> pages{};
		std::array<uint8_t, 8> midi{};
		std::array<uint8_t, 6> multiEnv{};
		std::array<uint8_t, 2> extra{};

		bool operator==(const MmKitTrack& _o) const
		{
			return pages == _o.pages && midi == _o.midi && multiEnv == _o.multiEnv && extra == _o.extra;
		}
	};

	struct MmKit
	{
		static constexpr size_t g_tracks = 6;
		static constexpr size_t g_nameSize = 11;
		static constexpr size_t g_rawSize = 698;
		static constexpr size_t g_slots = 128;
		static constexpr size_t g_assignSources = 6;
		static constexpr uint8_t g_noTrigPos = 0xff;

		uint8_t version = 2;
		uint8_t revision = 1;
		uint8_t position = 0;

		std::array<uint8_t, g_nameSize> name{};
		std::array<uint8_t, g_tracks> levels{};
		std::array<MmKitTrack, g_tracks> tracks{};
		std::array<uint8_t, g_tracks> machines{};
		std::array<uint8_t, g_tracks> routing{};
		std::array<uint8_t, 5> x1cd{};
		std::array<std::array<uint8_t, 12>, g_tracks> assignPage{}, assignDest{};
		std::array<std::array<int8_t, 12>, g_tracks> assignAdd{};	// signed
		uint8_t mirrorMask = 0x3f, hpfMask = 0x3f, lpfMask = 0x3f;
		std::array<uint8_t, g_tracks> trigPos{};
		uint8_t legatoAmp = 0xff, legatoFilter = 0xff, legatoLfo = 0xff;
		std::array<uint8_t, 4> x2b6{};

		bool operator==(const MmKit& _o) const;
		bool operator!=(const MmKit& _o) const { return !(*this == _o); }
	};

	std::optional<MmKit> decodeMmKit(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMmKit(const MmKit& _kit);
	std::optional<MmKit> mmKitFromRaw(const std::vector<uint8_t>& _raw, uint8_t _position);
	std::vector<uint8_t> mmKitRaw(const MmKit& _kit);

	std::vector<uint8_t> mmKitRequest(uint8_t _slot);

	inline uint8_t mmRoutingOutputs(const uint8_t _routing) { return _routing & 7; }
	inline uint8_t mmRoutingInput(const uint8_t _routing) { return (_routing >> 3) & 7; }
	inline uint8_t mmRouting(const uint8_t _outputs, const uint8_t _input)
	{
		return static_cast<uint8_t>((_outputs & 7) | ((_input & 7) << 3));
	}

	// The working kit in patch RAM: the raw payload at this CPU address.
	constexpr uint32_t g_mmWorkingKitAddress = 0x700028;
}
