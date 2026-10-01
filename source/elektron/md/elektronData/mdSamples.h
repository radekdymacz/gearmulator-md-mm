#pragma once

#include "json.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	// MD OS 1.63 UW sample memory (P9, mdEditorProbeFirmwareTest samplemap/rammap; doc/modern-ux/
	// data-contract.md 4.9). Pure: the memory comes in through MdSampleMemory, so the same code reads
	// an emulated machine (md::DeskDevice), a test rig and a fixture.
	//
	// ROM slots live in the 8 MiB NOR flash, one record per slot from a 64 KiB sector boundary:
	//   +0 0x18 (a record; 0x7a = a free sector, 0x1a = the record's next sector), +1 slot 0-47,
	//   +2 bits (16, big-endian), +4 period in ns, +8 length in samples, +12 loop start, +16 loop end
	//   (32-bit, low 16-bit word first), +21 loop type (0x7f none, 0 forward), +22 a word of its own (not a
	//   sample; not decoded), +24 the samples:
	//   signed 16-bit big-endian. A longer sample goes on in the next sectors, each with a 4-byte head
	//   (0x1a, slot, 0xff, 0xff).
	// Names: patch RAM 0x7244a, 5 bytes a ROM slot (4 characters, then their sum).
	// RAM slots (RAM-R1..R4) live in DSP2's sample memory only (volatile): X 0x147e00 is a table of 64
	// entries {start, length, loop start (0xffffff none), rate (0x040000 = 44.1 kHz)}; entries 32-35 are
	// RAM 1-4. Samples are 12-bit codes, two to a word (high first), expanded by the table at X 0x146000
	// (4096 signed 24-bit words; code 0x800 = silence).
	constexpr uint32_t g_mdSampleSectorSize = 0x10000;
	constexpr uint32_t g_mdSampleFlashBegin = 0x200000;
	constexpr uint32_t g_mdSampleFlashEnd = 0x7a0000;
	constexpr size_t g_mdSampleHeaderSize = 24;
	constexpr size_t g_mdSampleNextSectorHead = 4;
	constexpr uint32_t g_mdSampleNamesAddress = 0x7244a;	// in patch RAM
	constexpr uint32_t g_mdDspSampleTable = 0x147e00;
	constexpr uint32_t g_mdDspExpander = 0x146000;
	constexpr uint32_t g_mdDspSampleMemoryBegin = 0x150000;
	constexpr uint32_t g_mdDspSampleMemoryEnd = 0x200000;
	constexpr uint32_t g_mdDspRamEntry = 32;
	constexpr uint8_t g_mdRomSlots = 48;
	constexpr uint8_t g_mdRamSlots = 4;
	// What DSP2's sample memory holds in all, in samples: ROM slots and the four RAM buffers share it.
	constexpr uint32_t g_mdSampleCapacity = (g_mdDspSampleMemoryEnd - g_mdDspSampleMemoryBegin) * 2;
	// The overview's peaks a slot (every slot, every md-desk/samples document: light). A slot's detail,
	// asked for one slot at the width it is drawn at (sampleWave), has up to g_mdSampleWaveMaxBins.
	constexpr size_t g_mdSampleBins = 128;
	constexpr size_t g_mdSampleWaveMaxBins = 8192;

	// Where the machine's memory comes from. flash(offset, size, out): false when not readable.
	struct MdSampleMemory
	{
		std::function<bool(uint32_t _offset, size_t _size, uint8_t* _out)> flash;
		std::function<uint8_t(uint32_t _offset)> patch;
		std::function<uint32_t(uint32_t _address)> dspX;
	};

	// A ROM record's head (flash).
	struct MdFlashSample
	{
		uint32_t offset = 0;		// the record's first sector
		uint8_t slot = 0;
		uint32_t length = 0;		// samples
		uint32_t periodNs = 0;
		uint32_t loopStart = 0, loopEnd = 0;
		uint8_t loopType = 0x7f;
	};
	std::optional<MdFlashSample> parseMdFlashSample(const uint8_t* _head, uint32_t _offset);

	// One slot as the page sees it.
	struct MdSampleSlot
	{
		bool ram = false;
		uint8_t slot = 0;			// ROM 0-47, RAM 0-3
		bool empty = true;
		uint32_t length = 0;		// samples
		uint32_t rate = 0;			// Hz
		std::optional<std::pair<uint32_t, uint32_t>> loop;
		std::string name;			// "" = none known
		std::vector<int8_t> peaks;	// min, max per bin (-127..127); empty for an empty slot
		// The samples themselves, 16-bit (RAM: the 12-bit codes through the expander, the top 16 bits):
		// for the detail (sampleWave) and the audition. Null for an empty slot. Shared, never changed.
		std::shared_ptr<const std::vector<int16_t>> pcm;

		bool operator==(const MdSampleSlot& _o) const
		{
			return ram == _o.ram && slot == _o.slot && empty == _o.empty && length == _o.length && rate == _o.rate && loop == _o.loop
				&& name == _o.name && peaks == _o.peaks
				&& (pcm == _o.pcm || (pcm && _o.pcm && *pcm == *_o.pcm));
		}
	};

	// Everything the memory says about the samples, and how to read each slot.
	struct MdSampleIndex
	{
		std::array<std::optional<MdFlashSample>, g_mdRomSlots> rom;
		std::array<std::string, g_mdRomSlots> names;
		bool ramReadable = false;
		std::string ramReason;
		std::array<std::array<uint32_t, 4>, g_mdRamSlots> ram{};		// the DSP table entries
		std::vector<int32_t> expander;									// 4096 entries when readable
		uint64_t signature = 0;		// changes when any of the above changes
	};
	MdSampleIndex indexMdSamples(const MdSampleMemory& _memory);
	// Just the signature (cheap: sector heads, names, the RAM entries).
	uint64_t mdSampleSignature(const MdSampleMemory& _memory);

	MdSampleSlot readMdRomSample(const MdSampleMemory& _memory, const MdSampleIndex& _index, uint8_t _slot, size_t _bins = g_mdSampleBins);
	MdSampleSlot readMdRamSample(const MdSampleMemory& _memory, const MdSampleIndex& _index, uint8_t _slot, size_t _bins = g_mdSampleBins);

	struct MdSampleBank
	{
		std::vector<MdSampleSlot> rom;		// 48
		std::vector<MdSampleSlot> ram;		// 4
		bool ramReadable = false;
		std::string ramReason;
		uint64_t signature = 0;
		uint32_t used() const;				// ROM samples in all

		bool operator==(const MdSampleBank& _o) const
		{
			return rom == _o.rom && ram == _o.ram && ramReadable == _o.ramReadable && ramReason == _o.ramReason;
		}
		bool operator!=(const MdSampleBank& _o) const { return !(*this == _o); }
	};
	MdSampleBank readMdSampleBank(const MdSampleMemory& _memory, size_t _bins = g_mdSampleBins);

	// The md-desk/samples document (data-contract.md 4.9).
	json::Value mdSampleBankToJson(const MdSampleBank& _bank);

	// A slot's detail: min, max per bin of its pcm, -32767..32767, at most min(_bins, length) bins (a
	// bin is at least one sample). Empty for an empty slot.
	std::vector<int16_t> mdWavePeaks(const MdSampleSlot& _slot, size_t _bins);
	// {"type":"sampleWave", bank, slot, length, rate, bins, scale, peaks} (data-contract.md 4.9).
	json::Value mdSampleWaveMessage(const MdSampleSlot& _slot, size_t _bins);

	// Min/max peaks of _length values (_at(i) -> -1..1), _bins bins, as -127..127.
	std::vector<int8_t> mdPeaks(uint32_t _length, size_t _bins, const std::function<float(uint32_t)>& _at);

	// ---- a sample to send (SDS, MIDI Sample Dump Standard) ----

	// SDS for ROM slot _slot (0-47): the header, the name (0x73, 1-4 characters), the 120-byte data
	// packets (16-bit, 40 samples each). The machine answers each with ACK/NAK/WAIT/CANCEL.
	struct MdSdsDump
	{
		std::vector<uint8_t> header;
		std::vector<uint8_t> name;
		std::vector<std::vector<uint8_t>> packets;
		std::vector<uint8_t> bytes() const;		// all of it, in order (as a .syx would hold it)
	};
	std::optional<MdSdsDump> mdSdsDump(uint8_t _slot, const std::vector<int16_t>& _samples, uint32_t _rate, const std::string& _name);

	// An SDS handshake reply: F0 7E dd (7F ACK | 7E NAK | 7C WAIT | 7D CANCEL) pp F7.
	enum class SdsReply : uint8_t { Ack, Nak, Wait, Cancel };
	std::optional<std::pair<SdsReply, uint8_t>> parseSdsReply(const std::vector<uint8_t>& _message);

	// A sample file (WAV or AIFF): its rate and its channels, as -1..1.
	struct AudioClip
	{
		uint32_t rate = 0;
		std::vector<std::vector<float>> channels;
		size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
	};
	// WAV (PCM 8/16/24/32, float 32/64, extensible) and AIFF / AIFC (PCM, sowt, fl32). _error says why not.
	std::optional<AudioClip> decodeAudioFile(const std::vector<uint8_t>& _bytes, std::string& _error);
	// A WAV file (16-bit PCM) of _channels: for tests and fixtures.
	std::vector<uint8_t> encodeWav16(const AudioClip& _clip);

	// What goes to the machine: mono 16-bit at a rate it plays, at most _maxSamples long.
	struct MdSampleUpload
	{
		std::vector<int16_t> samples;
		uint32_t rate = 0;
		std::string name;					// 4 characters from the file name
		std::vector<std::string> notes;		// what was changed, in plain words
	};
	constexpr uint32_t g_mdSampleMaxRate = 44100;
	std::optional<MdSampleUpload> prepareMdSample(const AudioClip& _clip, const std::string& _fileName, uint32_t _maxSamples,
		std::string& _error);
	// 1-4 characters for 0x73 from a file name ("kick 01.wav" -> "KICK").
	std::string mdSampleNameFrom(const std::string& _fileName);
}
