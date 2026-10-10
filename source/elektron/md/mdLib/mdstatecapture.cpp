#include "mdstatecapture.h"

#include "mddevice.h"
#include "mdfactorybaseline.h"

namespace md
{
	// md::Device's hook (mddevice.h), defined here with the capture it makes
	std::unique_ptr<synthLib::Device::StateCapture> Device::beginStateCapture(const synthLib::StateType _type)
	{
		auto capture = std::make_unique<DeviceStateCapture>(m_model, _type);
		if(isProjectStateRestorePending() && _type == m_requestedStateType && m_requestedState)
		{
			capture->requestedState = m_requestedState;
			return capture;
		}

		auto* stateHardware = m_hardware.get();
		if(m_deferredPreparedState && m_deferredPreparedState->m_hardware)
			stateHardware = m_deferredPreparedState->m_hardware.get();
		capture->patchRam = stateHardware->copyPatchRam();
		if(m_model == MachineModel::Monomachine)
		{
			capture->userFlash = stateHardware->copyUserFlash();
			return capture;
		}
		capture->rom = stateHardware->sharedRom();
		capture->romFingerprint = stateHardware->firmwareFingerprint();
		capture->factoryBaseline = stateHardware->factoryFlashBaseline();
		const bool pending = stateHardware->copyPendingFlashOverlay(capture->pendingOverlay);
		// A pending restore without a factory baseline saves its overlay alone; every other case saves the flash
		if(capture->factoryBaseline || !pending)
			capture->flash = stateHardware->copyFlashData();
		return capture;
	}

	bool DeviceStateCapture::encode(std::vector<uint8_t>& _state)
	{
		if(requestedState)
		{
			_state.insert(_state.end(), requestedState->begin(), requestedState->end());
			return true;
		}
		if(model == MachineModel::Monomachine)
			return encodeState(_state, patchRam, model, type, userFlash);

		// B-034: the ROM's and the baseline's fingerprints as md::Hardware keeps them; the baseline is decoded
		// once per cache, by the first save that needs it
		FlashFingerprints known;
		known.rom = romFingerprint;
		std::shared_ptr<const std::vector<uint8_t>> baseline;
		if(factoryBaseline && factoryBaseline->get(baseline, known.baseline))
			return encodeStateWithFactoryBaseline(_state, patchRam, flash, *baseline, rom->data(), model, type, &known);
		if(pendingOverlay.valid)
			return encodeState(_state, patchRam, pendingOverlay, rom->data(), model, type, &known.rom);
		// If interaction happened before the first machine-local baseline was
		// captured, preserve a complete flash image. An absolute sector set records
		// ROM-equal deletions and lets the replacement boot coherently without waiting
		// for another factory-initialization pass.
		return encodeState(_state, patchRam, flash, rom->data(), rom->data(), model, type);
	}
}
