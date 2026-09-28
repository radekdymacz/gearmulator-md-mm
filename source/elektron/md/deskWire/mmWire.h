#pragma once

#include "deskWire.h"

#include "mdLib/mdautomation.h"

#include <optional>

namespace deskWire::mm
{
	// A parameter (page _pg, index _i) of track _t as the Monomachine's CC on its base channel (one
	// channel per track from the base channel on). Nothing when it has no CC.
	inline std::optional<Bytes> param(const uint8_t _baseChannel, const uint8_t _t, const uint8_t _pg, const uint8_t _i, const uint8_t _value)
	{
		const auto cc = ::md::automation::encodeParameterChange(::md::MachineModel::Monomachine, {_pg, _t, _i, _value}, _baseChannel);
		if(!cc)
			return std::nullopt;
		return Bytes{(*cc)[0], (*cc)[1], (*cc)[2]};
	}

	// An NRPN parameter on the base channel (MM manual, appendix B): the track as the NRPN MSB, the
	// parameter as the LSB, then the value.
	inline std::vector<Bytes> nrpn(const uint8_t _baseChannel, const uint8_t _t, const uint8_t _param, const uint8_t _value)
	{
		return deskWire::nrpn(_baseChannel, _t, _param, _value);
	}
}
