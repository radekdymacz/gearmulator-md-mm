#pragma once

#include "json.h"

#include <string>
#include <utility>
#include <vector>

namespace elektronData::json
{
	// Contract version 2 (P6): the firmware's pass-through fields (dump format bytes, residue
	// after names, opaque LFO state, undecoded bytes) are grouped under one "firmware" member,
	// so an editor, or a second engine, sees at a glance what is machine residue and what is
	// editable. Which fields those are is data (FirmwareLayout); the codecs keep their own
	// layout (version 1) and this boundary moves the fields. Readers are tolerant: a version 1
	// document passes through unchanged. Pure.
	struct FirmwareLayout
	{
		std::vector<std::string> topLevel;								// moved as they are
		std::vector<std::pair<std::string, std::string>> perTrack;		// "lfo.state" of tracks[i] -> firmware.<name>[i]
		std::string mergedObject;										// an object whose members join firmware ("hidden")
	};

	// The names a layout puts under "firmware" (top-level fields and per-track lists; a merged
	// object's members are the codec's own). A name the codec never writes is a layout typo: the
	// corpus test checks every name shows up in some document of its kind.
	std::vector<std::string> firmwareNames(const FirmwareLayout& _layout);

	// A version 1 document -> version 2 (the given version number).
	Value groupFirmware(const Value& _v1, const FirmwareLayout& _layout, int _version);
	// A version 2 document -> the codec's version 1 layout; any other version is returned as is.
	Value ungroupFirmware(const Value& _doc, const FirmwareLayout& _layout, int _version, int _v1Version);
}
