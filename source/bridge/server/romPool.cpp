#include "romPool.h"

#include "config.h"

#include "baseLib/filesystem.h"
#include "baseLib/md5.h"

#include "networkLib/logging.h"

#include <cctype>
#include <string>

namespace bridgeServer
{
	namespace
	{
		// The client's name for its ROM, for the log only: a peer could send separators, control characters or a
		// NUL, or a very long name. Letters, digits and ._- pass, anything else is '?', at most 48 characters.
		std::string logName(const std::string& _name)
		{
			constexpr size_t maxLength = 48;
			std::string result;
			for(const char c : _name)
			{
				if(result.size() == maxLength)
					return result + "...";
				const bool plain = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
				result += plain ? c : '?';
			}
			return result;
		}
	}

	RomPool::RomPool(Config& _config) : m_config(_config)
	{
		findRoms();
	}

	const RomData& RomPool::getRom(const baseLib::MD5& _hash)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_roms.find(_hash);
		if(it == m_roms.end())
			findRoms();
		it = m_roms.find(_hash);
		if(it != m_roms.end())
			return it->second;
		static RomData empty;
		return empty;
	}

	void RomPool::addRom(const std::string& _name, const RomData& _data)
	{
		if(_data.empty() || _data.size() > g_maxRomSize)
		{
			LOGNET(networkLib::LogLevel::Warning, "Not caching ROM " << logName(_name) << ": size " << _data.size()
				<< " is outside 1.." << g_maxRomSize);
			return;
		}

		std::scoped_lock lock(m_mutex);

		const auto hash = baseLib::MD5(_data);
		if(m_roms.find(hash) != m_roms.end())
			return;

		// The name is the hash alone: a peer-sent name could hold "..", a separator or a NUL and so write
		// outside the ROM folder or under a different name. Exclusive: never replace a file that is there.
		const auto filename = getRootPath() + hash.toString() + ".bin";

		if(!baseLib::filesystem::writeFileExclusive(filename, _data))
			LOGNET(networkLib::LogLevel::Warning, "Could not write ROM cache file " << filename << ", ROM "
				<< logName(_name)
				<< " is kept in memory only");

		// Kept whatever the disk did (a full disk, or a file of that name that is not this ROM): the data is the ROM
		// its hash names, and a client that sent it is not asked for it again at every connection.
		m_roms.insert({hash, _data});
	}

	std::string RomPool::getRootPath() const
	{
		return m_config.romsPath;
	}

	void RomPool::findRoms()
	{
		std::vector<std::string> files;
		baseLib::filesystem::findFiles(files, getRootPath(), {}, 0, g_maxRomSize);

		for (const auto& file : files)
		{
			std::vector<uint8_t> romData;

			if(!baseLib::filesystem::readFile(romData, file))
			{
				LOGNET(networkLib::LogLevel::Error, "Failed to load file " << file);
				continue;
			}

			const auto hash = baseLib::MD5(romData);

			if(m_roms.find(hash) != m_roms.end())
				continue;

			m_roms.insert({hash, std::move(romData)});
			LOGNET(networkLib::LogLevel::Info, "Loaded ROM " << baseLib::filesystem::getFilenameWithoutPath(file));
		}
	}
}
