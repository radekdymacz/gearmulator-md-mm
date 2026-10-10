#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B live SysEx commands (manual Appendix C). MM-P0 found
	// that none of these is gated by SYSEX RECV: they act on every screen.
	enum class MmStatus : uint8_t
	{
		Global = 0x01,
		Kit = 0x02,
		Pattern = 0x04,
		Song = 0x08,
		SongMode = 0x10,
		Poly = 0x20,
		MidiSeq = 0x21,
		AudioTrack = 0x22,	// request only: SET STATUS ignores it (MM-P0 §4)
		MidiTrack = 0x23
	};

	std::vector<uint8_t> mmStatusRequest(MmStatus _param);
	// A request the machine answers with a reply: a status (0x70) or the dump of one slot (global 0x51, kit 0x53,
	// pattern 0x68, song 0x6a). The editor's stream queues each at most once (deskCore::Stream::ask, B-031).
	bool mmIsRequest(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> mmSetStatus(MmStatus _param, uint8_t _value);
	struct MmStatusReply
	{
		MmStatus param;
		uint8_t value;
	};
	std::optional<MmStatusReply> parseMmStatusResponse(const std::vector<uint8_t>& _sysex);

	std::vector<uint8_t> mmLoadPattern(uint8_t _slot);		// 0x57
	std::vector<uint8_t> mmLoadKit(uint8_t _slot);			// 0x58
	std::vector<uint8_t> mmSaveKit(uint8_t _slot);			// 0x59 (also makes the slot current)
	std::vector<uint8_t> mmLoadSong(uint8_t _slot);			// 0x6C
	std::vector<uint8_t> mmSaveSong(uint8_t _slot);			// 0x6D
	std::vector<uint8_t> mmSetActiveGlobal(uint8_t _slot);	// 0x56
	// 0x5B: init 0 keeps the data pages, 1 initialises them all, 2 the SYN page.
	std::vector<uint8_t> mmAssignMachine(uint8_t _track, uint8_t _machine, uint8_t _init);
	// 0x5C: output bus bits (AB 1, CD 2, EF 4) and the input (FX machines; 0 NEIGHBOR,
	// 1 INP A, 2 INP B, 3 INP A+B, 4-6 BUS AB/CD/EF).
	std::vector<uint8_t> mmSetRouting(uint8_t _track, uint8_t _outputs, uint8_t _input);
	std::vector<uint8_t> mmSetKitName(const std::string& _name);	// 0x55, 11 characters
	std::vector<uint8_t> mmSetTempo(double _bpm);					// 0x61, BPM x 24
}
