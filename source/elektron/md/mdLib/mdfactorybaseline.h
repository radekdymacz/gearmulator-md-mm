#pragma once

// The factory baseline a Machinedrum state save is stored against, decoded once (codex review 2026-10, item 3). The
// fork's file (doc/modern-ux/UPSTREAM.md); md::Hardware holds one (its m_factoryBaseline and factoryFlashBaseline()).

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace md
{
	class Rom;

	// The initialized factory flash a Machinedrum state is stored relative to, with its FNV-1a 64 fingerprint.
	// Built once and then kept: from the machine-local cache that is three 8 MiB scans and a CRC of every cached
	// sector, which a state save used to repeat every time under the plug-in's lock. Immutable once built, so a
	// state save shares it, and whoever asks first builds it (outside the plug-in's lock when a capture does).
	// Memory: from the machine-local cache (the usual case) the decoded image is kept, 8 MiB more per Machinedrum
	// instance once a state was saved (its copy of the 2 MiB cache is freed once decoded); from a completed capture
	// (the first start, before there is a cache) it is md::Hardware's own image, shared, nothing more.
	class FactoryFlashBaseline
	{
	public:
		// From the machine-local cache, decoded against the ROM on first use
		FactoryFlashBaseline(std::vector<uint8_t> _cache, std::shared_ptr<const Rom> _rom);
		// From an initialized image (a cache already decoded), with its fingerprint if known
		FactoryFlashBaseline(std::vector<uint8_t> _baseline, std::optional<uint64_t> _fingerprint);
		// From md::Hardware's completed capture, shared (it no longer changes), with its fingerprint if known
		FactoryFlashBaseline(std::shared_ptr<const std::vector<uint8_t>> _baseline,
			std::optional<uint64_t> _fingerprint);

		FactoryFlashBaseline(const FactoryFlashBaseline&) = delete;
		FactoryFlashBaseline& operator=(const FactoryFlashBaseline&) = delete;

		// Any thread. False when the cache does not decode against its ROM.
		bool get(std::shared_ptr<const std::vector<uint8_t>>& _baseline, uint64_t& _fingerprint);

	private:
		std::mutex m_mutex;
		std::vector<uint8_t> m_cache;			// until decoded
		std::shared_ptr<const Rom> m_rom;		// until decoded
		std::shared_ptr<const std::vector<uint8_t>> m_baseline;
		std::optional<uint64_t> m_fingerprint;
		bool m_failed = false;
	};
}
