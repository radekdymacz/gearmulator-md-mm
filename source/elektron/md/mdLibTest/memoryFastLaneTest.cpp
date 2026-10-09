// The memory fast lane (md::Microcontroller::setMemoryFastLane, the L11 speed-up) must be invisible: with it on
// and off, every access returns the same value and leaves the same memory behind. Two machines get the same
// random accesses, 8, 16 and 32 bits wide, concentrated on the edges of every window of the memory map where a
// fast lane goes wrong (the last byte of a window, a long word that straddles its end, an alias).

#include "mdLib/mdmc.h"
#include "mdLib/mdmemorymap.h"
#include "mdLib/mdrom.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace
{
	using namespace md::memorymap;

	// Windows with side effects that need a machine around them (SIM timers, DSP host ports) are not the
	// fast lane's business: it never serves them. Accesses that touch one are left out.
	bool touchesPeripheral(const uint32_t _addr, const uint32_t _length)
	{
		for(const auto& window : {g_sim, g_dsp1Hdi08, g_dsp2Hdi08})
			if(_addr < window.end && _addr + _length > window.begin)
				return true;
		return false;
	}

	const std::array<uint32_t, 11> g_windowEdges =
	{
		g_flashLow.begin, g_patchBootstrap.begin, g_mainRam.begin, g_loaderRam.begin, g_patchOsAlias.begin,
		g_internalSram.begin, g_flashFull.begin, g_mainHighAlias.begin, g_mainExecAlias.begin,
		g_mmUserFlash.begin, g_flashFull.end
	};
	const std::array<md::memorymap::Range, 9> g_windows =
	{
		g_flashLow, g_patchBootstrap, g_mainRam, g_loaderRam, g_patchOsAlias, g_internalSram, g_flashFull,
		g_mainHighAlias, g_mainExecAlias
	};

	enum class Access { Read8, Read16, Read32, ReadImm16, ReadImm32, Write8, Write16, Write32 };
	constexpr uint32_t g_accessCount = 8;
	constexpr std::array<uint32_t, g_accessCount> g_accessBytes = {1, 2, 4, 2, 4, 1, 2, 4};

	uint32_t pickAddress(std::mt19937& _random)
	{
		const auto& window = g_windows[_random() % g_windows.size()];
		switch(_random() % 4)
		{
		case 0:	return window.begin - 6 + _random() % 12;			// around the start
		case 1:	return window.end - 6 + _random() % 12;				// around the end
		case 2:	return g_windowEdges[_random() % g_windowEdges.size()] - 6 + _random() % 12;
		default:	return window.begin + _random() % window.size();	// anywhere inside
		}
	}

	bool sameMemory(md::Microcontroller& _a, md::Microcontroller& _b)
	{
		for(const auto& window : {g_patchBootstrap, g_mainRam, g_loaderRam, g_internalSram, g_mainHighAlias})
			for(uint32_t address = window.begin; address < window.end; address += 2)
				if(_a.read16(address) != _b.read16(address))
				{
					std::printf("FAIL: memory differs at %08x\n", address);
					return false;
				}
		for(uint32_t address = g_flashLow.begin; address < g_flashLow.end; address += 2)
			if(_a.read16(address) != _b.read16(address))
			{
				std::printf("FAIL: flash differs at %08x\n", address);
				return false;
			}
		return true;
	}

	bool run(const md::MachineModel _model, const char* const _name)
	{
		std::vector<uint8_t> image(md::g_romSize);
		std::mt19937 random(0x5eed + static_cast<unsigned>(_model));
		for(auto& byte : image)
			byte = static_cast<uint8_t>(random());
		const md::Rom rom(image, "synthetic-fast-lane-image");

		auto fast = std::make_unique<md::Microcontroller>(rom, _model);
		auto slow = std::make_unique<md::Microcontroller>(rom, _model);
		if(!fast->memoryFastLane())
		{
			std::puts("FAIL: the fast lane is not on by default");
			return false;
		}
		slow->setMemoryFastLane(false);
		if(slow->memoryFastLane())
		{
			std::puts("FAIL: the fast lane did not switch off");
			return false;
		}

		constexpr uint32_t g_operations = 600000;
		for(uint32_t operation = 0; operation < g_operations; ++operation)
		{
			const uint32_t address = pickAddress(random);
			const uint32_t value = random();
			const auto access = static_cast<Access>(random() % g_accessCount);
			if(touchesPeripheral(address, g_accessBytes[static_cast<size_t>(access)]))
				continue;

			uint32_t a = 0;
			uint32_t b = 0;
			switch(access)
			{
			case Access::Read8:		a = fast->read8(address);		b = slow->read8(address);		break;
			case Access::Read16:	a = fast->read16(address);		b = slow->read16(address);		break;
			case Access::Read32:	a = fast->read32(address);		b = slow->read32(address);		break;
			case Access::ReadImm16:	a = fast->readImm16(address);	b = slow->readImm16(address);	break;
			case Access::ReadImm32:	a = fast->readImm32(address);	b = slow->readImm32(address);	break;
			case Access::Write8:
				fast->write8(address, static_cast<uint8_t>(value));
				slow->write8(address, static_cast<uint8_t>(value));
				break;
			case Access::Write16:
				fast->write16(address, static_cast<uint16_t>(value));
				slow->write16(address, static_cast<uint16_t>(value));
				break;
			case Access::Write32:
				fast->write32(address, value);
				slow->write32(address, value);
				break;
			}
			if(a != b)
			{
				std::printf("FAIL: %s access %u at %08x differs: %08x with the lane, %08x without\n", _name,
					static_cast<unsigned>(access), address, a, b);
				return false;
			}
		}
		if(!sameMemory(*fast, *slow))
		{
			std::printf("FAIL: %s memory differs after the accesses\n", _name);
			return false;
		}
		return true;
	}
}

int main()
{
	if(!run(md::MachineModel::Machinedrum, "Machinedrum") || !run(md::MachineModel::Monomachine, "Monomachine"))
		return 1;
	std::puts("PASS: the memory fast lane gives the same results and memory on and off");
	return 0;
}
