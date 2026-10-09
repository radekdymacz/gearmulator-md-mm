#pragma once

// md::Device's state save in two parts (codex review 2026-10, item 3): the capture under synthLib::Plugin's process
// lock (Device::beginStateCapture) and the encode after it is released (DeviceStateCapture::encode). The fork's file
// (doc/modern-ux/UPSTREAM.md); md::Device keeps the hook, its beginStateCapture override.

#include "mdhardware.h"
#include "mdstate.h"
#include "mdtypes.h"

#include "synthLib/device.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace md
{
	class FactoryFlashBaseline;

	// What the save needs, copied or shared under the lock: only what the running machine can still change is copied
	// (the patch RAM, the MD flash or the MM user flash, a pending overlay); the ROM, the factory baseline and a
	// requested state are shared and immutable, alive even when the device swaps its Hardware while this encodes.
	// The encode (CRCs, the sector compare, the first decode of the factory cache) gives the bytes the save under
	// the lock wrote.
	class DeviceStateCapture final : public synthLib::Device::StateCapture
	{
	public:
		DeviceStateCapture(MachineModel _model, synthLib::StateType _type) : model(_model), type(_type) {}

		bool encode(std::vector<uint8_t>& _state) override;

		const MachineModel model;
		const synthLib::StateType type;
		// A restore in progress of this state type: saved as it was requested
		std::shared_ptr<const std::vector<uint8_t>> requestedState;
		std::vector<uint8_t> patchRam;
		std::vector<uint8_t> userFlash;		// Monomachine
		std::vector<uint8_t> flash;			// Machinedrum, unless only the pending overlay is saved
		FlashSectorOverlay pendingOverlay;
		std::shared_ptr<FactoryFlashBaseline> factoryBaseline;
		std::shared_ptr<const Rom> rom;
		uint64_t romFingerprint = 0;
	};
}
