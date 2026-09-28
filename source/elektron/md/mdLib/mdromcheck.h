#pragma once

#include "mdtypes.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace md
{
	// P7: what a user's firmware file is, before the editor copies it into its ROM folder. Pure: bytes in,
	// a verdict out. Accepted images are named by their fingerprints (mdtypes.h: the FNV-1a identifiers and
	// the SHA-1 digests beside them), never by their contents.
	struct RomCheck
	{
		bool ok = false;
		std::string text;	// "✓ Machinedrum OS 1.63 found", or why not
	};

	inline uint64_t romFingerprint(const std::vector<uint8_t>& _data)
	{
		uint64_t f = 14695981039346656037ull;
		for(const auto b : _data)
		{
			f ^= b;
			f *= 1099511628211ull;
		}
		return f;
	}

	inline const char* firmwareName(const MachineModel _model)
	{
		return _model == MachineModel::Monomachine ? "Monomachine OS 1.32B" : "Machinedrum OS 1.63";
	}

	inline RomCheck checkRom(const std::vector<uint8_t>& _data, const MachineModel _want)
	{
		const std::string want = firmwareName(_want);
		if(_data.size() != g_romSize)
		{
			char mb[32];
			std::snprintf(mb, sizeof(mb), "%.2f", static_cast<double>(_data.size()) / (1024.0 * 1024.0));
			return {false, "This file is " + std::string(mb) + " MiB. The " + want + " image is exactly 8 MiB."};
		}
		const auto f = romFingerprint(_data);
		const auto mine = _want == MachineModel::Monomachine ? g_mmOs132bFingerprint : g_mdOs163Fingerprint;
		const auto other = _want == MachineModel::Monomachine ? g_mdOs163Fingerprint : g_mmOs132bFingerprint;
		if(f == mine)
			return {true, "\xE2\x9C\x93 " + want + " found"};
		if(f == other)
			return {false, std::string("This is the ") + firmwareName(_want == MachineModel::Monomachine ? MachineModel::Machinedrum : MachineModel::Monomachine)
				+ " firmware. This editor needs the " + want + " image."};
		return {false, "This 8 MiB image is not the " + want + " the editor knows (another OS version, or a damaged dump)."};
	}
}
