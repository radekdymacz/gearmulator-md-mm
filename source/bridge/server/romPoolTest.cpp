// The DSP bridge server's ROM cache (romPool.cpp): a client sends a ROM with a name of its choosing, so the cache
// file must be named after the ROM's content only, inside the ROM folder, and the size must be bounded.

#include "config.h"
#include "romPool.h"

#include "baseLib/filesystem.h"
#include "baseLib/md5.h"

#include "networkLib/logging.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#	include <direct.h>
#else
#	include <unistd.h>
#endif

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::cout << (_ok ? "ok   " : "FAIL ") << _what << '\n';
		if(!_ok)
			++g_failures;
	}

	// A fresh folder under the system's temporary folder, with a trailing separator
	std::string makeTempDir()
	{
		const char* base = std::getenv("TMPDIR");
		if(!base || !*base)
			base = std::getenv("TEMP");
		std::string root = base && *base ? base : "/tmp";
		if(root.back() != '/' && root.back() != '\\')
			root += '/';

		std::mt19937_64 random(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ std::random_device()());
		for(int attempt = 0; attempt < 100; ++attempt)
		{
			const auto dir = root + "bridgeServerRomPoolTest-" + std::to_string(random());
			if(baseLib::filesystem::createDirectory(dir))
				return dir + '/';
		}
		return {};
	}

	void removeDir(const std::string& _dir)
	{
		std::vector<std::string> entries;
		baseLib::filesystem::getDirectoryEntries(entries, _dir);
		for(const auto& entry : entries)
		{
			if(baseLib::filesystem::isDirectory(entry))
				removeDir(entry);
			else
				baseLib::filesystem::remove(entry);
		}
#ifdef _WIN32
		_rmdir(_dir.c_str());
#else
		rmdir(_dir.c_str());
#endif
	}

	std::vector<std::string> listNames(const std::string& _dir)
	{
		std::vector<std::string> entries;
		baseLib::filesystem::getDirectoryEntries(entries, _dir);
		std::vector<std::string> names;
		names.reserve(entries.size());
		for(const auto& entry : entries)
			names.push_back(baseLib::filesystem::getFilenameWithoutPath(entry));
		std::sort(names.begin(), names.end());
		return names;
	}

	bridgeServer::RomData makeRom(const size_t _size, const uint8_t _seed)
	{
		bridgeServer::RomData data(_size);
		for(size_t i = 0; i < _size; ++i)
			data[i] = static_cast<uint8_t>(_seed + i * 31 + (i >> 8));
		return data;
	}

	// Every file in the ROM folder is "<md5 of its content>.bin"
	bool onlyHashNamedFiles(const std::string& _romsPath, const size_t _expectedCount)
	{
		std::vector<std::string> files;
		baseLib::filesystem::getDirectoryEntries(files, _romsPath);
		if(files.size() != _expectedCount)
		{
			std::cout << "     " << files.size() << " files in the ROM folder, expected " << _expectedCount << '\n';
			return false;
		}
		for(const auto& file : files)
		{
			std::vector<uint8_t> data;
			if(!baseLib::filesystem::readFile(data, file))
				return false;
			const auto name = baseLib::filesystem::getFilenameWithoutPath(file);
			if(name != baseLib::MD5(data).toString() + ".bin")
			{
				std::cout << "     unexpected file name: " << name << '\n';
				return false;
			}
		}
		return true;
	}
}

int main()
{
	// Keep the output to the checks: only warnings and errors from the pool
	networkLib::setLogFunc([](const networkLib::LogLevel _level, const char*, int, const std::string& _message)
	{
		if(_level >= networkLib::LogLevel::Warning)
			std::cout << "     log: " << _message << '\n';
	});

	const auto root = makeTempDir();
	if(root.empty())
	{
		std::cerr << "bridgeServerRomPoolTest: no temporary folder\n";
		return 1;
	}

	const auto romsPath = root + "roms/";
	const auto pluginsPath = root + "plugins/";

	// The command line keeps the test out of the user's documents folder (the default data path)
	std::vector<std::string> args = {"bridgeServerRomPoolTest", "-config", root + "none.cfg",
		"-pluginsPath", pluginsPath, "-romsPath", romsPath};
	std::vector<char*> argv;
	for(auto& arg : args)
		argv.push_back(arg.data());
	const auto rootEntries = std::vector<std::string>{"plugins", "roms"};

	{
		bridgeServer::Config config(static_cast<int>(argv.size()), argv.data());
		check(config.romsPath == romsPath && listNames(root) == rootEntries, "the ROM folder is a fresh temporary one");

		bridgeServer::RomPool pool(config);

		// A valid ROM under a name that climbs out of the ROM folder
		const auto rom = makeRom(4096, 1);
		const baseLib::MD5 hash(rom);
		pool.addRom("../escaped", rom);
		check(listNames(romsPath) == std::vector<std::string>{hash.toString() + ".bin"}, "a ROM is cached as <md5>.bin, whatever its name");
		check(listNames(root) == rootEntries, "a name with \"..\" writes nothing outside the ROM folder");
		check(pool.getRom(hash) == rom, "the pool serves the cached ROM");

		// Names with separators, "..", an embedded NUL (which cut the old name before its suffix)
		pool.addRom(std::string("ok\0/../../nul", 13), makeRom(2048, 7));
		pool.addRom("..", makeRom(1024, 9));
		pool.addRom("a/../../b", makeRom(1500, 11));
		pool.addRom("/absolute/path", makeRom(1600, 13));
		check(onlyHashNamedFiles(romsPath, 5), "names with separators, \"..\" or a NUL: every cache file is named after its content");
		check(listNames(root) == rootEntries, "and nothing is written outside the ROM folder");

		// The same ROM again: no second file, the first one stays
		pool.addRom("again", rom);
		check(onlyHashNamedFiles(romsPath, 5) && pool.getRom(hash) == rom, "adding a cached ROM again writes nothing");

		// Size bounds
		pool.addRom("empty", {});
		check(onlyHashNamedFiles(romsPath, 5), "empty data is not cached");

		const auto oversized = makeRom(bridgeServer::g_maxRomSize + 1, 3);
		pool.addRom("oversized", oversized);
		check(onlyHashNamedFiles(romsPath, 5) && pool.getRom(baseLib::MD5(oversized)).empty(), "data over g_maxRomSize is not cached");

		const auto largest = makeRom(bridgeServer::g_maxRomSize, 5);
		pool.addRom("largest", largest);
		check(onlyHashNamedFiles(romsPath, 6) && pool.getRom(baseLib::MD5(largest)) == largest, "data of exactly g_maxRomSize is cached");

		// findRoms shares the bound: a larger file in the folder is not loaded
		baseLib::filesystem::writeFile(romsPath + "planted.bin", oversized);
		bridgeServer::RomPool reloaded(config);
		check(reloaded.getRom(hash) == rom && reloaded.getRom(baseLib::MD5(largest)) == largest, "a new pool loads the cached ROMs from the folder");
		check(reloaded.getRom(baseLib::MD5(oversized)).empty(), "and skips a file over g_maxRomSize");
	}

	removeDir(root);

	if(g_failures)
	{
		std::cout << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "bridgeServerRomPoolTest: all checks passed\n";
	return 0;
}
