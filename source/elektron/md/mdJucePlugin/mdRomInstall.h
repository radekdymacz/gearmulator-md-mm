#pragma once

#include "mdLib/mdtypes.h"

#include "juce_core/juce_core.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	// P7: install a user's firmware from a file they chose or dropped, on this computer only (nothing is sent
	// anywhere). A .bin, or a .zip that holds exactly one 8 MiB .bin; checked (md::checkRom) before it is
	// copied into the ROM folder, never over another file.
	struct RomInstall
	{
		bool ok = false;
		std::string text;			// for the page's card: "✓ Machinedrum OS 1.63 found", or why not
		juce::File installed;		// the file in the ROM folder (ok only)
	};

	// The image a file holds: the .bin itself, or the one 8 MiB .bin in a .zip. Empty, with the reason, if none.
	std::optional<std::vector<uint8_t>> readRomImage(const juce::File& _file, std::string& _why);

	RomInstall installRom(const juce::File& _file, md::MachineModel _model, const juce::File& _romFolder);

	// The firmware images of this machine inside the ROM folder (any name; judged by their contents).
	std::vector<juce::File> romsInFolder(md::MachineModel _model, const juce::File& _romFolder);

	// REPLACE: the chosen file is installed as above, and then every other image of this machine in the ROM
	// folder goes (the new one stays if anything fails first). Only files inside the ROM folder are touched.
	RomInstall replaceRom(const juce::File& _file, md::MachineModel _model, const juce::File& _romFolder);

	// REMOVE: every image of this machine in the ROM folder is deleted; nothing outside the folder is.
	struct RomRemoval
	{
		int removed = 0;
		std::string text;
	};
	RomRemoval removeRoms(md::MachineModel _model, const juce::File& _romFolder);
}
