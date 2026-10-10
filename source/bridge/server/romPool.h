#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
#include <string>

namespace baseLib
{
	class MD5;
}

namespace bridgeServer
{
	struct Config;
	using RomData = std::vector<uint8_t>;

	// The largest ROM the pool holds, on disk (findRoms) and from a client (addRom). A peer decides what it
	// sends, so the pool needs a bound of its own.
	inline constexpr size_t g_maxRomSize = 16 * 1024 * 1024;

	class RomPool
	{
	public:
		RomPool(Config& _config);

		const RomData& getRom(const baseLib::MD5& _hash);
		// _name is what the client calls the ROM, used for the log only: the cache file is named after the
		// hash of _data, never after anything the peer sent.
		void addRom(const std::string& _name, const RomData& _data);

	private:
		std::string getRootPath() const;
		void findRoms();

		const Config& m_config;

		std::mutex m_mutex;
		std::map<baseLib::MD5, RomData> m_roms;
	};
}
