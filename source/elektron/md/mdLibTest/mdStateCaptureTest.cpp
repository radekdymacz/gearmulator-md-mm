// A state save without the plug-in's lock (codex review 2026-10, item 3): the table-driven CRC-32 of the state and
// cache formats (mdstate.cpp), and md::Device's capture (mdstatecapture.h): copied under the lock, encoded after it,
// the same bytes as the save under the lock wrote. The capture part needs the firmware (GEARMULATOR_MD_FIRMWARE_BIN,
// GEARMULATOR_MM_FIRMWARE_BIN): md::Device takes only the supported ROMs.

#include "baseLib/filesystem.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdromdata.h"
#include "mdLib/mdstate.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void check(const bool _condition, const char* const _message)
	{
		std::printf("[%s] %s\n", _condition ? "PASS" : "FAIL", _message);
		if(!_condition)
			++g_failures;
	}

	std::vector<uint8_t> makePatchRam()
	{
		std::vector<uint8_t> result(md::g_patchRamStateSize);
		uint32_t value = 0x12345678;
		for(auto& byte : result)
		{
			value = value * 1664525u + 1013904223u;
			byte = static_cast<uint8_t>(value >> 24);
		}
		return result;
	}

	// The bitwise CRC-32 the state format was written with: the reference for the table-driven one
	uint32_t crc32(const uint8_t* const _data, const size_t _size)
	{
		uint32_t crc = 0xffffffffu;
		for(size_t i = 0; i < _size; ++i)
		{
			crc ^= _data[i];
			for(uint32_t bit = 0; bit < 8; ++bit)
				crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
		return ~crc;
	}

	// The state CRC is table-driven: it must give exactly what the bitwise CRC-32 gave, or old states and caches
	// stop loading. The standard check value, then random buffers of awkward lengths against the bitwise reference.
	void testCrc32()
	{
		const std::string checkInput = "123456789";
		check(md::crc32(reinterpret_cast<const uint8_t*>(checkInput.data()), checkInput.size()) == 0xcbf43926u,
			"CRC-32 of \"123456789\" is 0xCBF43926");
		check(md::crc32(nullptr, 0) == 0, "CRC-32 of nothing is 0");

		uint32_t seed = 0x9e3779b9u;
		bool same = true;
		for(const size_t size : {size_t(1), size_t(3), size_t(7), size_t(64), size_t(4093),
			size_t(md::g_uwFlashSectorSize), size_t(md::g_patchRamStateSize)})
		{
			std::vector<uint8_t> data(size);
			for(auto& byte : data)
			{
				seed = seed * 1664525u + 1013904223u;
				byte = static_cast<uint8_t>(seed >> 24);
			}
			same = same && md::crc32(data.data(), data.size()) == crc32(data.data(), data.size());
		}
		check(same, "table CRC-32 equals the bitwise CRC-32 on random buffers");

		using clock = std::chrono::steady_clock;
		const auto patchRam = makePatchRam();
		const auto t0 = clock::now();
		const auto bitwise = crc32(patchRam.data(), patchRam.size());
		const auto t1 = clock::now();
		const auto table = md::crc32(patchRam.data(), patchRam.size());
		const auto t2 = clock::now();
		std::printf("  CRC-32 of 1 MiB: bitwise %.2f ms, table %.2f ms\n",
			std::chrono::duration<double, std::milli>(t1 - t0).count(),
			std::chrono::duration<double, std::milli>(t2 - t1).count());
		check(bitwise == table, "and on the patch RAM image");
	}

	// A state save captures under the plug-in's lock and encodes after it (md::Device::beginStateCapture). The
	// bytes must be what the encoder gives for the machine's memories, as the save under the lock wrote them, in
	// every case the device picks: sparse against the factory baseline, a complete image before there is one, a
	// restore in progress (the requested bytes, or its pending overlay for the other state type), and the
	// Monomachine's patch RAM plus user flash. Needs the firmware: md::Device takes only the supported ROMs.
	struct CaptureResult
	{
		std::vector<uint8_t> bytes;
		double lockedMs = 0;
		double encodeMs = 0;
	};

	CaptureResult captureState(md::Device& _device, const synthLib::StateType _type)
	{
		using clock = std::chrono::steady_clock;
		CaptureResult result;
		const auto t0 = clock::now();
		auto capture = _device.beginStateCapture(_type);
		const auto t1 = clock::now();
		if(!capture || !capture->encode(result.bytes))
			result.bytes.clear();
		const auto t2 = clock::now();
		result.lockedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
		result.encodeMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
		return result;
	}

	std::unique_ptr<md::Device> makeDevice(const std::vector<uint8_t>& _rom, const md::MachineModel _model,
		const std::string& _home)
	{
		synthLib::DeviceCreateParams params;
		params.romData = _rom;
		params.romName = _model == md::MachineModel::Monomachine ? "mm-state-test.bin" : "md-state-test.bin";
		params.customData = md::deviceCustomData(_model);
		params.homePath = _home;
		return std::make_unique<md::Device>(params);
	}

	bool sameAsGetState(md::Device& _device, const synthLib::StateType _type, const std::vector<uint8_t>& _captured)
	{
		std::vector<uint8_t> state;
		return _device.getState(state, _type) && state == _captured;
	}

	void testMachinedrumStateCapture(const std::vector<uint8_t>& _rom)
	{
		// A machine-local factory cache for this ROM: 34 initialized sectors, as the OS 1.63 cache has
		auto factory = _rom;
		for(size_t sector = 0; sector < 34; ++sector)
			for(size_t i = 0; i < md::g_uwFlashSectorSize; i += 61)
				factory[(sector * 3 + 1) * md::g_uwFlashSectorSize + i] ^= static_cast<uint8_t>(sector + 1);
		std::vector<uint8_t> cache;
		check(md::encodeFactoryFlashCache(cache, factory, _rom), "capture: a factory cache for the firmware encodes");

		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto home = baseLib::filesystem::getCurrentDirectory() + ".md-state-capture-test-"
			+ std::to_string(nonce) + "/";
		const auto cacheFile = home + "nvram/md-uw-1.63-factory-v2.cache";
		baseLib::filesystem::createDirectory(home + "nvram");
		check(baseLib::filesystem::writeFile(cacheFile, cache), "capture: the factory cache is stored");
		auto cached = makeDevice(_rom, md::MachineModel::Machinedrum, home);
		baseLib::filesystem::remove(cacheFile);
		baseLib::filesystem::remove(home + "nvram");
		baseLib::filesystem::remove(home);
		check(cached->isValid(), "capture: the Machinedrum boots from the factory cache");

		// User samples in 16 sectors, then the sparse state against the factory baseline
		auto& hardware = cached->getHardware();
		auto flash = hardware.copyFlashData();
		for(size_t sector = 0; sector < 16; ++sector)
			for(size_t i = 0; i < md::g_uwFlashSectorSize; i += 97)
				flash[(64 + sector) * md::g_uwFlashSectorSize + i] ^= static_cast<uint8_t>(sector * 31 + 7);
		hardware.replaceFlashData(flash, true);
		std::vector<uint8_t> sparseExpected;
		check(md::encodeStateWithFactoryBaseline(sparseExpected, hardware.copyPatchRam(), flash, factory, _rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal), "capture: the sparse reference encodes");
		const auto first = captureState(*cached, synthLib::StateTypeGlobal);
		const auto again = captureState(*cached, synthLib::StateTypeGlobal);
		check(first.bytes == sparseExpected && again.bytes == sparseExpected,
			"capture: sparse state against the factory baseline, byte for byte");
		check(sameAsGetState(*cached, synthLib::StateTypeGlobal, sparseExpected), "capture: getState writes the same");
		std::printf("  state save, MD from the factory cache, 16 sample sectors: under the lock %.2f ms then %.2f ms, "
			"encode %.2f ms (first, decodes the cache) then %.2f ms\n",
			first.lockedMs, again.lockedMs, first.encodeMs, again.encodeMs);

		// No factory cache yet: a complete flash image
		auto firstRun = makeDevice(_rom, md::MachineModel::Machinedrum, {});
		std::vector<uint8_t> completeExpected;
		check(md::encodeState(completeExpected, firstRun->getHardware().copyPatchRam(),
			firstRun->getHardware().copyFlashData(), _rom, _rom, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal), "capture: the complete-image reference encodes");
		const auto complete = captureState(*firstRun, synthLib::StateTypeGlobal);
		check(complete.bytes == completeExpected, "capture: complete flash image without a factory baseline");
		check(sameAsGetState(*firstRun, synthLib::StateTypeGlobal, completeExpected),
			"capture: getState writes the same complete image");
		std::printf("  state save, MD without a factory cache (complete image): under the lock %.2f ms, encode %.2f ms\n",
			complete.lockedMs, complete.encodeMs);

		// Restoring the sparse state without its factory baseline leaves it pending
		auto pending = makeDevice(_rom, md::MachineModel::Machinedrum, {});
		check(pending->setState(sparseExpected, synthLib::StateTypeGlobal) && pending->isProjectStateRestorePending(),
			"capture: a sparse restore without the factory baseline is pending");
		const auto requested = captureState(*pending, synthLib::StateTypeGlobal);
		check(requested.bytes == sparseExpected, "capture: a pending restore saves the requested state");
		md::DecodedState decoded;
		std::vector<uint8_t> overlayExpected;
		check(md::decodeState(decoded, sparseExpected, _rom, md::MachineModel::Machinedrum, synthLib::StateTypeGlobal)
			&& md::encodeState(overlayExpected, decoded.patchRam, decoded.flashOverlay, _rom,
				md::MachineModel::Machinedrum, synthLib::StateTypeCurrentProgram),
			"capture: the pending-overlay reference encodes");
		const auto overlay = captureState(*pending, synthLib::StateTypeCurrentProgram);
		check(overlay.bytes == overlayExpected, "capture: the other state type saves the pending overlay");
		check(sameAsGetState(*pending, synthLib::StateTypeCurrentProgram, overlayExpected),
			"capture: getState writes the same pending overlay");
	}

	void testMonomachineStateCapture(const std::vector<uint8_t>& _rom)
	{
		auto mm = makeDevice(_rom, md::MachineModel::Monomachine, {});
		check(mm->isValid(), "capture: the Monomachine boots");
		std::vector<uint8_t> expected;
		check(md::encodeState(expected, mm->getHardware().copyPatchRam(), md::MachineModel::Monomachine,
			synthLib::StateTypeGlobal, mm->getHardware().copyUserFlash()), "capture: the Monomachine reference encodes");
		const auto captured = captureState(*mm, synthLib::StateTypeGlobal);
		check(captured.bytes == expected, "capture: Monomachine patch RAM and user flash, byte for byte");
		check(sameAsGetState(*mm, synthLib::StateTypeGlobal, expected), "capture: getState writes the same");
		std::printf("  state save, MM: under the lock %.2f ms, encode %.2f ms\n", captured.lockedMs, captured.encodeMs);
	}

	void testStateCapture()
	{
		const auto* const mdPath = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		const auto* const mmPath = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		const auto* const required = std::getenv("GEARMULATOR_REQUIRE_FIRMWARE_TESTS");
		const bool require = required && std::string(required) == "1";
		std::vector<uint8_t> rom;
		if(mdPath && *mdPath && baseLib::filesystem::readFile(rom, mdPath))
			testMachinedrumStateCapture(rom);
		else if(require)
			check(false, "capture: GEARMULATOR_MD_FIRMWARE_BIN is required");
		else
			std::puts("  SKIP the Machinedrum state capture (GEARMULATOR_MD_FIRMWARE_BIN not set)");
		rom.clear();
		if(mmPath && *mmPath && baseLib::filesystem::readFile(rom, mmPath))
			testMonomachineStateCapture(rom);
		else if(require)
			check(false, "capture: GEARMULATOR_MM_FIRMWARE_BIN is required");
		else
			std::puts("  SKIP the Monomachine state capture (GEARMULATOR_MM_FIRMWARE_BIN not set)");
	}
}

int main()
{
	std::puts("Machinedrum and Monomachine state capture tests\n");
	testCrc32();
	testStateCapture();
	std::printf("\n%s (%d failures)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
	return g_failures == 0 ? 0 : 1;
}
