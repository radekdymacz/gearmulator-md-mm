#include "baseLib/filesystem.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdromdata.h"
#include "mdLib/mdstate.h"

#include <algorithm>
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

	void writeU32(std::vector<uint8_t>& _data, const size_t _offset,
		const uint32_t _value)
	{
		_data[_offset] = static_cast<uint8_t>(_value >> 24);
		_data[_offset + 1] = static_cast<uint8_t>(_value >> 16);
		_data[_offset + 2] = static_cast<uint8_t>(_value >> 8);
		_data[_offset + 3] = static_cast<uint8_t>(_value);
	}

	void testSparseProjectState()
	{
		const auto patchRam = makePatchRam();
		std::vector<uint8_t> rom(md::g_romSize, 0xff);
		for(size_t i = 0; i < rom.size(); i += 4093)
			rom[i] = static_cast<uint8_t>(i >> 8);
		auto factory = rom;
		factory[md::g_uwFlashSectorSize + 7] = 0x61;

		std::vector<uint8_t> factoryOnly;
		check(md::encodeStateWithFactoryBaseline(factoryOnly, patchRam, factory,
			factory, rom, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal),
			"validated factory-relative UW state encodes");
		check(factoryOnly.size() == 52 + patchRam.size(),
			"ROM-equal or initialized factory data stays out of project state");

		// Equality with the ROM is not evidence that no validated factory cache
		// exists. This is the compactness regression fixed after PR #8.
		std::vector<uint8_t> romEqualFactory;
		check(md::encodeStateWithFactoryBaseline(romEqualFactory, patchRam, rom,
			rom, rom, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal),
			"ROM-equal validated factory baseline encodes");
		check(romEqualFactory.size() == 52 + patchRam.size(),
			"ROM-equal validated baseline remains exactly header plus patch RAM");

		auto flashA = factory;
		flashA[3 * md::g_uwFlashSectorSize + 19] = 0x18;
		flashA.back() = 0x00;
		std::vector<uint8_t> encodedA;
		check(md::encodeStateWithFactoryBaseline(encodedA, patchRam, flashA,
			factory, rom, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal), "two-sector UW state encodes");
		constexpr size_t changedSectors = 2;
		constexpr size_t stateHeaderSize = 52;
		constexpr size_t entryHeaderSize = 8;
		check(encodedA.size() == stateHeaderSize + patchRam.size()
			+ changedSectors * (entryHeaderSize + md::g_uwFlashSectorSize),
			"UW state contains exactly the two changed sectors");

		md::DecodedState decodedA;
		std::vector<uint8_t> restoredA;
		check(md::decodeState(decodedA, encodedA, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state decodes against its ROM");
		check(decodedA.containsFlash && decodedA.patchRam == patchRam,
			"UW decode preserves patch RAM and flash marker");
		check(md::applyFlashOverlay(restoredA, decodedA.flashOverlay, factory),
			"UW sparse overlay applies to its factory baseline");
		check(restoredA == flashA, "patch RAM and flash round-trip byte exactly");

		// Version 3 was briefly published before its header-integrity gap was
		// discovered. Keep those Monday-night project states readable while all new
		// states use version 4 and bind the baseline metadata into the flash CRC.
		auto version3State = encodedA;
		version3State[4] = 0;
		version3State[5] = 3;
		const auto overlayOffset = stateHeaderSize + patchRam.size();
		writeU32(version3State, 48, crc32(version3State.data() + overlayOffset,
			version3State.size() - overlayOffset));
		md::DecodedState decodedVersion3;
		check(md::decodeState(decodedVersion3, version3State, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"briefly published version-3 UW state remains compatible");

		auto flashB = factory;
		flashB[7 * md::g_uwFlashSectorSize + 11] = 0x77;
		std::vector<uint8_t> encodedB;
		md::DecodedState decodedB;
		std::vector<uint8_t> restoredB;
		check(md::encodeStateWithFactoryBaseline(encodedB, patchRam, flashB,
			factory, rom, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal)
			&& md::decodeState(decodedB, encodedB, rom,
				md::MachineModel::Machinedrum, synthLib::StateTypeGlobal)
			&& md::applyFlashOverlay(restoredB, decodedB.flashOverlay, factory),
			"second UW instance state round-trips");
		check(restoredB == flashB && restoredB != restoredA,
			"two UW instances retain isolated flash images");

		auto wrongRom = rom;
		wrongRom[123] ^= 1;
		md::DecodedState unchanged;
		unchanged.patchRam = {1, 2, 3};
		check(!md::decodeState(unchanged, encodedA, wrongRom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state rejects the wrong ROM");
		check(unchanged.patchRam == std::vector<uint8_t>({1, 2, 3}),
			"wrong-ROM rejection leaves decoded output unchanged");
		auto wrongFactory = factory;
		wrongFactory[456] ^= 1;
		const std::vector<uint8_t> applySentinel{4, 5, 6};
		std::vector<uint8_t> unchangedFlash = applySentinel;
		check(!md::applyFlashOverlay(unchangedFlash, decodedA.flashOverlay,
			wrongFactory), "UW state rejects the wrong factory baseline");
		check(unchangedFlash == applySentinel,
			"wrong-factory rejection leaves flash output unchanged");

		auto corrupt = encodedA;
		corrupt.back() ^= 1;
		check(!md::decodeState(unchanged, corrupt, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state rejects corrupt flash CRC data");
		auto corruptBaselineFingerprint = encodedA;
		corruptBaselineFingerprint[28] ^= 1;
		check(!md::decodeState(unchanged, corruptBaselineFingerprint, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state rejects corrupt baseline metadata");
		auto truncated = encodedA;
		truncated.pop_back();
		check(!md::decodeState(unchanged, truncated, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state rejects truncation");
		auto trailing = encodedA;
		trailing.push_back(0);
		check(!md::decodeState(unchanged, trailing, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"UW state rejects trailing bytes");

		// Before a machine-local factory cache exists, every sector must be carried.
		// This records even a factory-populated sector erased back to ROM bytes.
		auto firstRunFlash = factory;
		std::copy_n(rom.begin() + md::g_uwFlashSectorSize,
			md::g_uwFlashSectorSize,
			firstRunFlash.begin() + md::g_uwFlashSectorSize);
		firstRunFlash[9 * md::g_uwFlashSectorSize + 31] = 0x27;
		std::vector<uint8_t> firstRunState;
		md::DecodedState decodedFirstRun;
		std::vector<uint8_t> restoredFirstRun;
		check(md::encodeState(firstRunState, patchRam, firstRunFlash, rom, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"pre-baseline UW fallback state encodes");
		check(firstRunState.size() == stateHeaderSize + patchRam.size()
			+ (md::g_romSize / md::g_uwFlashSectorSize)
				* (entryHeaderSize + md::g_uwFlashSectorSize),
			"pre-baseline fallback contains every flash sector");
		check(md::decodeState(decodedFirstRun, firstRunState, rom,
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal)
			&& md::applyFlashOverlay(restoredFirstRun,
				decodedFirstRun.flashOverlay, factory),
			"complete fallback decodes without the original baseline");
		check(restoredFirstRun == firstRunFlash,
			"complete fallback preserves a ROM-equal deletion");

		std::vector<uint8_t> legacy;
		md::DecodedState decodedLegacy;
		check(md::encodeState(legacy, patchRam, md::MachineModel::Machinedrum,
			synthLib::StateTypeGlobal)
			&& md::decodeState(decodedLegacy, legacy, rom,
				md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"legacy version-1 Machinedrum state remains compatible");
		check(!decodedLegacy.containsFlash && decodedLegacy.patchRam == patchRam,
			"legacy state changes patch RAM without replacing flash");
	}

	void testMonomachineUserFlashState()
	{
		const auto patchRam = makePatchRam();
		std::vector<uint8_t> userFlash(md::g_mmUserFlashStateSize);
		for(size_t i = 0; i < userFlash.size(); ++i)
			userFlash[i] = static_cast<uint8_t>((i * 37u) ^ (i >> 9));

		std::vector<uint8_t> encoded;
		check(md::encodeState(encoded, patchRam, md::MachineModel::Monomachine,
			synthLib::StateTypeGlobal, userFlash),
			"Monomachine DigiPRO user flash encodes");
		check(encoded.size() == 28 + patchRam.size() + userFlash.size(),
			"Monomachine state has bounded patch-plus-user-flash size");
		md::DecodedState decoded;
		check(md::decodeState(decoded, encoded, {},
			md::MachineModel::Monomachine, synthLib::StateTypeGlobal),
			"Monomachine DigiPRO user flash decodes");
		check(decoded.containsFlash && decoded.patchRam == patchRam
			&& decoded.userFlash == userFlash,
			"Monomachine patch RAM and DigiPRO flash round-trip byte exactly");

		auto corrupt = encoded;
		corrupt.back() ^= 1;
		md::DecodedState unchanged;
		unchanged.patchRam = {1, 2, 3};
		check(!md::decodeState(unchanged, corrupt, {},
			md::MachineModel::Monomachine, synthLib::StateTypeGlobal),
			"Monomachine state rejects corrupt DigiPRO flash");
		check(unchanged.patchRam == std::vector<uint8_t>({1, 2, 3}),
			"corrupt Monomachine state leaves decoded output unchanged");
		check(!md::decodeState(unchanged, encoded, {},
			md::MachineModel::Machinedrum, synthLib::StateTypeGlobal),
			"Monomachine user-flash state rejects the wrong model");

		std::vector<uint8_t> legacy;
		md::DecodedState decodedLegacy;
		check(md::encodeState(legacy, patchRam, md::MachineModel::Monomachine,
			synthLib::StateTypeGlobal)
			&& md::decodeState(decodedLegacy, legacy, {},
				md::MachineModel::Monomachine, synthLib::StateTypeGlobal),
			"legacy version-1 Monomachine state remains compatible");
		check(!decodedLegacy.containsFlash && decodedLegacy.userFlash.empty()
			&& decodedLegacy.patchRam == patchRam,
			"legacy Monomachine state remains patch-only");
	}

	void testFactoryCache()
	{
		std::vector<uint8_t> rom(md::g_romSize, 0xff);
		rom.front() = 0x12;
		rom.back() = 0x34;
		auto initialized = rom;
		initialized[2 * md::g_uwFlashSectorSize + 9] = 0x56;

		std::vector<uint8_t> cache;
		std::vector<uint8_t> decoded;
		check(md::encodeFactoryFlashCache(cache, initialized, rom),
			"UW factory cache encodes");
		check(cache.size() == 36 + 8 + md::g_uwFlashSectorSize,
			"factory cache contains exactly one sparse sector");
		check(cache.size() < md::g_romSize,
			"factory cache does not copy the complete ROM");
		check(md::decodeFactoryFlashCache(decoded, cache, rom),
			"UW factory cache decodes");
		check(decoded == initialized, "UW factory cache round-trips byte exactly");

		auto wrongRom = rom;
		wrongRom[1234] ^= 1;
		decoded = {1, 2, 3};
		check(!md::decodeFactoryFlashCache(decoded, cache, wrongRom),
			"factory cache rejects the wrong ROM");
		check(decoded == std::vector<uint8_t>({1, 2, 3}),
			"wrong-ROM cache rejection leaves output unchanged");
		auto corrupt = cache;
		corrupt.back() ^= 1;
		check(!md::decodeFactoryFlashCache(decoded, corrupt, rom),
			"factory cache rejects corrupted sector data");
	}

	void testCacheFilePublication()
	{
		const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto filename = baseLib::filesystem::getCurrentDirectory()
			+ ".md-cache-exclusive-test-" + std::to_string(nonce);
		const std::vector<uint8_t> first{1, 2, 3, 4};
		const std::vector<uint8_t> second{9, 8, 7};
		const std::vector<uint8_t> recovered{5, 6, 7, 8, 9};
		std::vector<uint8_t> readback;
		check(baseLib::filesystem::writeFileExclusive(filename, first),
			"immutable factory cache is created exclusively");
		check(!baseLib::filesystem::writeFileExclusive(filename, second),
			"second exclusive writer cannot replace the cache");
		check(baseLib::filesystem::writeFileAtomic(filename, recovered),
			"invalid cache can be replaced atomically");
		check(baseLib::filesystem::readFile(readback, filename)
			&& readback == recovered,
			"atomic replacement publishes the complete new cache");
		baseLib::filesystem::remove(filename);
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

	// B-034: the state a save writes under the plug-in's lock (the audio thread waits for it). With the fingerprints
	// md::Hardware holds, the ROM and the factory baseline are not scanned again; the bytes are the same.
	void testKnownFingerprints()
	{
		const auto fnv = [](const std::vector<uint8_t>& _d) { uint64_t r = 14695981039346656037ull; for(const auto b : _d) { r ^= b; r *= 1099511628211ull; } return r; };
		const auto patchRam = makePatchRam();
		std::vector<uint8_t> rom(md::g_romSize, 0xff);
		for(size_t i = 0; i < rom.size(); i += 4093)
			rom[i] = static_cast<uint8_t>(i >> 8);
		auto factory = rom;
		for(size_t i = 0; i < factory.size(); i += 777)
			factory[i] ^= 0x5a;
		auto flash = factory;
		flash[5 * md::g_uwFlashSectorSize + 3] ^= 0x11;
		const md::FlashFingerprints known{fnv(rom), fnv(factory)};
		using clock = std::chrono::steady_clock;
		const auto time = [&](const md::FlashFingerprints* _k, std::vector<uint8_t>& _out)
		{
			double best = 1e9;
			for(int i = 0; i < 5; ++i)
			{
				_out.clear();
				const auto t0 = clock::now();
				const bool ok = md::encodeStateWithFactoryBaseline(_out, patchRam, flash, factory, rom, md::MachineModel::Machinedrum,
					synthLib::StateTypeGlobal, _k);
				best = std::min(best, std::chrono::duration<double, std::milli>(clock::now() - t0).count());
				if(!ok)
					return -1.0;
			}
			return best;
		};
		std::vector<uint8_t> scanned, cached;
		const auto before = time(nullptr, scanned);
		const auto after = time(&known, cached);
		std::printf("  B-034: UW state encode %.2f ms scanning the ROM and the baseline, %.2f ms with the stored fingerprints\n", before, after);
		check(before >= 0 && after >= 0 && scanned == cached, "the stored fingerprints write the same state bytes");
		md::DecodedState decoded;
		check(md::decodeState(decoded, cached, rom, md::MachineModel::Machinedrum, synthLib::StateTypeGlobal), "and it reads back");
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
	std::puts("Machinedrum UW state and cache tests\n");
	testCrc32();
	testKnownFingerprints();
	testSparseProjectState();
	testMonomachineUserFlashState();
	testFactoryCache();
	testCacheFilePublication();
	testStateCapture();
	std::printf("\n%s (%d failures)\n", g_failures == 0 ? "OK" : "FAILED",
		g_failures);
	return g_failures == 0 ? 0 : 1;
}
