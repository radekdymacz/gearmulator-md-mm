#include "mdfactorybaseline.h"

#include "mdhardware.h"
#include "mdrom.h"
#include "mdstate.h"

namespace md
{
	// FNV-1a 64 of a flash image, as md::Hardware fingerprints the ROM and the captured baseline (mdhardware.cpp)
	uint64_t fingerprintRom(const std::vector<uint8_t>& _data);

	FactoryFlashBaseline::FactoryFlashBaseline(std::vector<uint8_t> _cache, std::shared_ptr<const Rom> _rom)
		: m_cache(std::move(_cache)), m_rom(std::move(_rom))
	{
	}

	FactoryFlashBaseline::FactoryFlashBaseline(std::vector<uint8_t> _baseline,
		const std::optional<uint64_t> _fingerprint)
		: m_baseline(std::make_shared<const std::vector<uint8_t>>(std::move(_baseline)))
		, m_fingerprint(_fingerprint)
	{
	}

	bool FactoryFlashBaseline::get(std::shared_ptr<const std::vector<uint8_t>>& _baseline, uint64_t& _fingerprint)
	{
		std::lock_guard lock(m_mutex);
		if(!m_baseline && !m_failed)
		{
			std::vector<uint8_t> decoded;
			if(m_rom && decodeFactoryFlashCache(decoded, m_cache, m_rom->data()))
				m_baseline = std::make_shared<const std::vector<uint8_t>>(std::move(decoded));
			else
				m_failed = true;
			std::vector<uint8_t>().swap(m_cache);
			m_rom.reset();
		}
		if(!m_baseline)
			return false;
		if(!m_fingerprint)
			m_fingerprint = fingerprintRom(*m_baseline);
		_baseline = m_baseline;
		_fingerprint = *m_fingerprint;
		return true;
	}

	// md::Hardware's hook (mdhardware.h), defined here with the class it makes
	std::shared_ptr<FactoryFlashBaseline> Hardware::factoryFlashBaseline()
	{
		if(!m_factoryFlashReady.load(std::memory_order_acquire))
			return {};
		std::lock_guard lock(m_factoryFlashMutex);
		// The same choice as copyFactoryFlashSnapshot: the cache when there is one, else the captured image. Both
		// stay as they are until replaceFactoryFlashCache or exchangePersistentFlashState, which replace this too.
		if(!m_factoryBaseline && !m_factoryFlashCache.empty())
			m_factoryBaseline = std::make_shared<FactoryFlashBaseline>(m_factoryFlashCache, m_rom);
		else if(!m_factoryBaseline && !m_factoryFlashBaseline.empty())
			m_factoryBaseline = std::make_shared<FactoryFlashBaseline>(m_factoryFlashBaseline,
				m_factoryBaselineFingerprint);
		return m_factoryBaseline;
	}
}
