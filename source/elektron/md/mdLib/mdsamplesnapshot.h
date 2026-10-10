#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "elektronData/mdSamples.h"

namespace md
{
	class Hardware;

	// P9: the UW's sample memory as raw values, copied by the audio thread a few chunks a block (no allocation, no
	// per-sample work), and turned into the editor's sample list (elektronData::MdSampleBank) on the message thread
	// (SampleExchange::read). Only what elektronData's sample reader looks at is copied: the flash from the first
	// sample sector on, the slot names in patch RAM, DSP2's expander and sample table, and the four RAM buffers.
	struct SampleSnapshot
	{
		static constexpr uint32_t g_dspTablesBegin = elektronData::g_mdDspExpander;
		static constexpr uint32_t g_dspTablesEnd = elektronData::g_mdDspSampleTable + 4 * 64;
		static constexpr size_t g_namesSize = 5u * elektronData::g_mdRomSlots;

		SampleSnapshot();	// allocates and touches every page (off the audio thread)

		std::vector<uint8_t> flash;			// flash [g_mdSampleFlashBegin, g_mdSampleFlashEnd)
		std::array<uint8_t, g_namesSize> names{};	// patch RAM from g_mdSampleNamesAddress
		std::vector<uint32_t> dspTables;	// DSP2 X [g_dspTablesBegin, g_dspTablesEnd)
		std::vector<uint32_t> dspSamples;	// DSP2 X [g_mdDspSampleMemoryBegin, End): the RAM buffers' words only

		// The copy as elektronData reads memory. The snapshot must outlive the result.
		elektronData::MdSampleMemory memory() const;
	};

	// The audio thread's side of a copy in progress (md::DeskDevice::scanSamples): call copyNext once a block
	// until it returns true. Plain memory reads of _hardware on the thread that owns it.
	class SampleCopy
	{
	public:
		void begin(SampleSnapshot& _target) { m_target = &_target; m_stage = Stage::Flash; m_position = 0; m_ram = 0; }
		bool active() const { return m_target != nullptr; }
		bool copyNext(Hardware& _hardware);
		SampleSnapshot* target() const { return m_target; }
		void reset() { m_target = nullptr; }

		// what one block may copy: about 20-40 us of plain copies on an M1
		static constexpr size_t g_flashBytesPerBlock = 256 * 1024;
		static constexpr size_t g_dspWordsPerBlock = 16 * 1024;

	private:
		enum class Stage { Flash, Tables, Ram, Done };
		SampleSnapshot* m_target = nullptr;
		Stage m_stage = Stage::Done;
		size_t m_position = 0;
		uint32_t m_ram = 0;
	};

	// Where a DeskDevice's sample copies meet their readers. Shared (the reader may outlive the device). One
	// snapshot buffer, allocated by the first reader: the audio thread takes it (try_lock), fills it, hands it back
	// (try_lock); the next read builds the bank from it and returns the buffer. The audio thread never waits.
	class SampleExchange
	{
	public:
		// Message thread, without the device lock: the newest sample list and its publication (0 = none yet), built
		// from the audio thread's newest copy first if one came in. The first call also asks the audio thread to
		// read the samples at all.
		std::shared_ptr<const elektronData::MdSampleBank> read(uint32_t& _sequence);

		// Audio thread.
		bool wanted() const { return m_wanted.load(std::memory_order_relaxed); }
		std::unique_ptr<SampleSnapshot> tryTakeBuffer();
		bool tryPublish(std::unique_ptr<SampleSnapshot>& _filled);	// false: busy, try again next block

	private:
		std::atomic<bool> m_wanted{false};
		std::mutex m_mutex;		// m_free, m_ready (the audio thread only tries it)
		std::unique_ptr<SampleSnapshot> m_free;
		std::unique_ptr<SampleSnapshot> m_ready;
		std::mutex m_readMutex;	// readers, one at a time: m_allocated, m_bank, m_sequence
		bool m_allocated = false;
		std::shared_ptr<const elektronData::MdSampleBank> m_bank;
		uint32_t m_sequence = 0;
	};
}
