#include "mdsamplesnapshot.h"

#include "mdhardware.h"

#include <algorithm>
#include <cstring>

namespace md
{
	namespace ed = elektronData;

	SampleSnapshot::SampleSnapshot()
		: flash(ed::g_mdSampleFlashEnd - ed::g_mdSampleFlashBegin, 0)
		, dspTables(g_dspTablesEnd - g_dspTablesBegin, 0)
		, dspSamples(ed::g_mdDspSampleMemoryEnd - ed::g_mdDspSampleMemoryBegin, 0)
	{
	}

	ed::MdSampleMemory SampleSnapshot::memory() const
	{
		ed::MdSampleMemory m;
		m.flash = [this](const uint32_t _offset, const size_t _size, uint8_t* _out)
		{
			if(_offset < ed::g_mdSampleFlashBegin || _offset > ed::g_mdSampleFlashEnd
				|| _size > ed::g_mdSampleFlashEnd - _offset)
				return false;
			std::memcpy(_out, flash.data() + (_offset - ed::g_mdSampleFlashBegin), _size);
			return true;
		};
		m.patch = [this](const uint32_t _offset) -> uint8_t
		{
			return _offset >= ed::g_mdSampleNamesAddress && _offset - ed::g_mdSampleNamesAddress < names.size()
				? names[_offset - ed::g_mdSampleNamesAddress] : 0;
		};
		m.dspX = [this](const uint32_t _address) -> uint32_t
		{
			if(_address >= g_dspTablesBegin && _address < g_dspTablesEnd)
				return dspTables[_address - g_dspTablesBegin];
			if(_address >= ed::g_mdDspSampleMemoryBegin && _address < ed::g_mdDspSampleMemoryEnd)
				return dspSamples[_address - ed::g_mdDspSampleMemoryBegin];
			return 0;
		};
		return m;
	}

	bool SampleCopy::copyNext(Hardware& _hardware)
	{
		if(!m_target)
			return false;
		auto& t = *m_target;
		auto& uc = _hardware.getUC();
		auto& x = _hardware.getDspProducer().dsp().memory();
		switch(m_stage)
		{
		case Stage::Flash:
			{
				const auto n = std::min(g_flashBytesPerBlock, t.flash.size() - m_position);
				if(!uc.copyFlashDataRangeRealtime(t.flash.data() + m_position,
					ed::g_mdSampleFlashBegin + m_position, n))
					std::fill_n(t.flash.data() + m_position, n, uint8_t{0xff});	// not readable: no records
				m_position += n;
				if(m_position < t.flash.size())
					return false;
				m_stage = Stage::Tables;
				m_position = 0;
				return false;
			}
		case Stage::Tables:
			for(size_t i = 0; i < t.names.size(); ++i)
				t.names[i] = uc.read8(0x700000 + ed::g_mdSampleNamesAddress + static_cast<uint32_t>(i));
			for(size_t i = 0; i < t.dspTables.size(); ++i)
				t.dspTables[i] = static_cast<uint32_t>(x.get(dsp56k::MemArea_X,
					SampleSnapshot::g_dspTablesBegin + static_cast<uint32_t>(i)));
			m_stage = Stage::Ram;
			m_ram = 0;
			m_position = 0;
			return false;
		case Stage::Ram:
			{
				// the words of RAM buffer m_ram as its table entry says (elektronData::indexMdSamples checks the
				// same range before it reads them)
				size_t budget = g_dspWordsPerBlock;
				while(m_ram < ed::g_mdRamSlots && budget)
				{
					const auto entry = SampleSnapshot::g_dspTablesBegin;
					const auto at = ed::g_mdDspSampleTable + 4 * (ed::g_mdDspRamEntry + m_ram) - entry;
					const auto start = t.dspTables[at] & 0xffffff;
					const auto length = t.dspTables[at + 1] & 0xffffff;
					const bool valid = start >= ed::g_mdDspSampleMemoryBegin && start < ed::g_mdDspSampleMemoryEnd
						&& length <= (ed::g_mdDspSampleMemoryEnd - start) * 2;
					const size_t words = valid ? (size_t(length) + 1) / 2 : 0;
					const auto n = std::min(budget, words - std::min(words, m_position));
					for(size_t w = 0; w < n; ++w)
					{
						const auto address = start + static_cast<uint32_t>(m_position + w);
						t.dspSamples[address - ed::g_mdDspSampleMemoryBegin] =
							static_cast<uint32_t>(x.get(dsp56k::MemArea_X, address));
					}
					m_position += n;
					budget -= n;
					if(m_position < words)
						return false;
					++m_ram;
					m_position = 0;
				}
				if(m_ram < ed::g_mdRamSlots)
					return false;
				m_stage = Stage::Done;
				return true;
			}
		case Stage::Done:
			return true;
		}
		return true;
	}

	std::unique_ptr<SampleSnapshot> SampleExchange::tryTakeBuffer()
	{
		std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock())
			return {};
		return std::move(m_free);
	}

	bool SampleExchange::tryPublish(std::unique_ptr<SampleSnapshot>& _filled)
	{
		std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock() || m_ready)
			return false;
		m_ready = std::move(_filled);
		return true;
	}

	std::shared_ptr<const ed::MdSampleBank> SampleExchange::read(uint32_t& _sequence)
	{
		std::lock_guard readLock(m_readMutex);
		if(!m_allocated)
		{
			auto buffer = std::make_unique<SampleSnapshot>();	// ~8.6 MB, touched here, not on the audio thread
			std::lock_guard lock(m_mutex);
			m_free = std::move(buffer);
			m_allocated = true;
		}
		m_wanted.store(true, std::memory_order_relaxed);
		std::unique_ptr<SampleSnapshot> ready;
		{
			std::lock_guard lock(m_mutex);
			ready = std::move(m_ready);
		}
		if(ready)
		{
			auto bank = std::make_shared<const ed::MdSampleBank>(ed::readMdSampleBank(ready->memory()));
			{
				std::lock_guard lock(m_mutex);
				m_free = std::move(ready);
			}
			m_bank = std::move(bank);
			++m_sequence;
		}
		_sequence = m_sequence;
		return m_bank;
	}
}
