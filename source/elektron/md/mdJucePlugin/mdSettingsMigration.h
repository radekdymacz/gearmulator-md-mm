#pragma once

#include "mdLib/mdtypes.h"

#include "juce_core/juce_core.h"

namespace mdJucePlugin
{
	// The editors keep their settings in files of their own (doc/release/SIGNING.md, "Identifiers"). Up to
	// 0.3.0 they used upstream Gearmulator's names, so an upstream build of the same machine read and rewrote
	// them (its panel skin over the editor page, ours over its panel). The data folder stays shared: the ROMs,
	// the patch manager and the MIDI learn presets are the machine's, whichever build plays it.
	// These are file names on disk, not the product names (scripts/mdmm-product.env): they stay as they are
	// when the product is renamed, so a user's settings are never left behind.

	// The editor's config file in <data folder>/config/, and the one it used up to 0.3.0 (upstream's).
	const char* editorConfigFileName(md::MachineModel _model);		// "Machinedrum Editor.xml"
	const char* legacyConfigFileName(md::MachineModel _model);		// "Gearmulator MD.xml"

	// The standalone app's settings (audio device, the window's plug-in state) in ~/Library/Application Support:
	// <name>.settings. The legacy name is JUCE's default, the plug-in name, which upstream's app uses too.
	const char* editorStandaloneSettingsName(md::MachineModel _model);	// "Machinedrum Editor"
	const char* legacyStandaloneSettingsName(md::MachineModel _model);	// "Gearmulator MD"

	enum class SettingsCopy
	{
		Copied,				// _own did not exist; it is now a copy of _legacy
		AlreadyMigrated,	// _own exists: nothing is touched
		NothingToCopy,		// neither exists: the editor starts with its defaults
		Failed				// the copy could not be made: _own is still missing, _legacy untouched
	};

	// One-time move to the editor's own settings file, as a copy: _legacy is never changed or deleted (an
	// upstream build may still use it), and _own, once it exists, is never overwritten. The copy goes to a
	// temporary sibling first, so a half-written file is never taken for a finished migration.
	SettingsCopy copySettingsOnce(const juce::File& _legacy, const juce::File& _own);
}
