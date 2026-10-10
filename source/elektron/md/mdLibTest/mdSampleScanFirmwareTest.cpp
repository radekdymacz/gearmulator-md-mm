// P9 sample list on the audio thread (doc/modern-ux/RESEARCH-emulation-cpu.md, "Around the core", item 2): once the
// editor asks for the samples, md::DeskDevice's audio thread copies the raw sample memory a few chunks a block and
// allocates nothing; the list is built by the reader (md::SampleExchange::read) and equals the one read straight from
// the machine's memory. Before, the audio thread built it, one slot a block for 52 blocks: 143 allocations (3.2 MB),
// up to 128 us in one block.
// Needs MD OS 1.63 (GEARMULATOR_MD_FIRMWARE_BIN); exits 77 without it.

#include "mdLib/mddeskdevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdtypes.h"

#include "synthLib/device.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	// allocations made by this thread while the device processes a block
	thread_local bool t_count = false;
	thread_local uint64_t t_allocations = 0;
}

void* operator new(const size_t _size)
{
	if(t_count)
		++t_allocations;
	if(void* p = std::malloc(_size ? _size : 1))
		return p;
	throw std::bad_alloc();
}
void* operator new[](const size_t _size) { return operator new(_size); }
void operator delete(void* _p) noexcept { std::free(_p); }
void operator delete[](void* _p) noexcept { std::free(_p); }
void operator delete(void* _p, size_t) noexcept { std::free(_p); }
void operator delete[](void* _p, size_t) noexcept { std::free(_p); }

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Rig
	{
		explicit Rig(md::DeskDevice& _device) : device(_device)
		{
			for(size_t i = 0; i < out.size(); ++i)
				outputs[i] = out[i].data();
			for(size_t i = 0; i < in.size(); ++i)
				inputs[i] = in[i].data();
			midiOut.reserve(1024);
		}

		uint64_t block()
		{
			midiOut.clear();
			t_allocations = 0;
			t_count = true;
			device.process(inputs, outputs, g_block, midiIn, midiOut);
			t_count = false;
			return t_allocations;
		}

		static constexpr size_t g_block = 128;
		md::DeskDevice& device;
		std::array<std::array<float, g_block>, 6> out{};
		std::array<std::array<float, g_block>, 2> in{};
		synthLib::TAudioInputs inputs{};
		synthLib::TAudioOutputs outputs{};
		std::vector<synthLib::SMidiEvent> midiIn, midiOut;
	};
}

int main()
{
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "mdSampleScanFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n";
		return 77;
	}
	try
	{
		std::ifstream file(path, std::ios::binary);
		const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		synthLib::DeviceCreateParams params;
		params.romData = rom;
		params.romName = path;
		params.customData = md::deviceCustomData(md::MachineModel::Machinedrum);
		auto device = std::make_unique<md::DeskDevice>(params);
		require(device->isValid(), "the firmware is not MD OS 1.63");
		Rig rig(*device);

		// up and its UW flash settled (a first start initializes it), so the memory holds still while it is copied
		auto& hw = device->getHardware();
		for(uint32_t i = 0; !(hw.isFirmwareMidiReady() && hw.factoryFlashCacheReady()); ++i)
		{
			require(i < md::g_samplerate * 60 / Rig::g_block, "the machine did not start");
			rig.block();
		}
		for(int i = 0; i < 500; ++i)
			rig.block();

		auto exchange = device->sampleExchange();
		uint32_t sequence = 0;
		require(!exchange->read(sequence) && sequence == 0, "a sample list before anyone asked");
		uint64_t allocations = 0;
		uint32_t blocks = 0;
		std::shared_ptr<const elektronData::MdSampleBank> bank;
		while(sequence == 0)
		{
			require(++blocks < 2000, "no sample list after 2000 blocks");
			allocations += rig.block();
			bank = exchange->read(sequence);
		}
		std::cout << "sample list after " << blocks << " blocks, " << allocations << " allocations on the audio thread\n";
		require(allocations == 0, "the audio thread allocated while it read the samples");
		require(bank && bank->rom.size() == elektronData::g_mdRomSlots && bank->ram.size() == elektronData::g_mdRamSlots
			&& bank->used() > 0, "the list misses slots or the factory samples");
		const auto live = elektronData::readMdSampleBank(md::sampleMemoryOf(hw));
		require(*bank == live && bank->signature == live.signature,
			"the list built from the audio thread's copy differs from the machine's memory");

		// nothing changed: no new copy; asked again: a new one, the same list
		for(int i = 0; i < 400; ++i)
			rig.block();
		uint32_t again = 0;
		require(exchange->read(again) && again == sequence, "an unchanged memory was copied again");
		device->refreshSampleBank();
		allocations = 0;
		for(blocks = 0; again == sequence; )
		{
			require(++blocks < 2000, "no new list after a refresh");
			allocations += rig.block();
			bank = exchange->read(again);
		}
		require(allocations == 0 && *bank == live, "the refreshed list differs or allocated");

		// the device goes, a reader still holding the exchange is fine
		device.reset();
		require(exchange->read(again) != nullptr, "the exchange lost its list with the device");
	}
	catch(const std::exception& e)
	{
		std::cerr << "mdSampleScanFirmwareTest: FAIL: " << e.what() << '\n';
		return 1;
	}
	std::cout << "mdSampleScanFirmwareTest: ok\n";
	return 0;
}
