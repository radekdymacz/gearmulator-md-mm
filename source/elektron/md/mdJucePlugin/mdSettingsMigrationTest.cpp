// The editors' own settings files (mdSettingsMigration.h): the one-time copy from the file they shared with
// upstream Gearmulator up to 0.3.0. A copy, never a move; never over a file the editor already has; nothing
// half-written left behind; the names are the editors' own, never upstream's.
#include "mdSettingsMigration.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;
	juce::Array<juce::File> partialsIn(const juce::File& _folder)
	{
		return _folder.findChildFiles(juce::File::findFiles, false, "*.migrating*");
	}
	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok) ++g_failures;
	}
}

int main()
{
	using namespace mdJucePlugin;
	using md::MachineModel;

	const auto dir = juce::File::createTempFile("mdSettingsMigrationTest");
	dir.createDirectory();
	const auto config = dir.getChildFile("config");

	std::printf("names\n");
	for(const auto model : {MachineModel::Machinedrum, MachineModel::Monomachine})
	{
		const std::string own = editorConfigFileName(model), legacy = legacyConfigFileName(model);
		check(own != legacy, own + " is not upstream's " + legacy);
		check(std::string(editorStandaloneSettingsName(model)) != legacyStandaloneSettingsName(model),
			std::string(editorStandaloneSettingsName(model)) + ".settings is not upstream's");
	}
	check(std::string(editorConfigFileName(MachineModel::Machinedrum)) == "Machinedrum Editor.xml", "Machinedrum Editor.xml");
	check(std::string(editorConfigFileName(MachineModel::Monomachine)) == "Monomachine Editor.xml", "Monomachine Editor.xml");
	check(std::string(legacyConfigFileName(MachineModel::Machinedrum)) == "Gearmulator MD.xml", "legacy: Gearmulator MD.xml");
	check(std::string(legacyConfigFileName(MachineModel::Monomachine)) == "Gearmulator MM.xml", "legacy: Gearmulator MM.xml");

	const auto legacy = config.getChildFile("Gearmulator MD.xml");
	const auto own = config.getChildFile("Machinedrum Editor.xml");

	std::printf("nothing to copy\n");
	check(copySettingsOnce(legacy, own) == SettingsCopy::NothingToCopy, "neither file: NothingToCopy");
	check(!own.exists(), "no file is made");

	std::printf("first start after 0.3.0\n");
	// what upstream leaves in the shared file after it ran: its own panel skin
	const juce::String upstreamWritten = "<?xml version=\"1.0\"?><PROPERTIES><VALUE name=\"skinFile\" val=\"mdDefault.rml\"/>"
		"<VALUE name=\"scale\" val=\"130\"/></PROPERTIES>";
	config.createDirectory();
	check(legacy.replaceWithText(upstreamWritten), "legacy file written");
	check(copySettingsOnce(legacy, own) == SettingsCopy::Copied, "Copied");
	check(own.loadFileAsString() == upstreamWritten, "the copy is byte for byte the legacy file");
	check(legacy.loadFileAsString() == upstreamWritten, "the legacy file is untouched");
	check(partialsIn(config).isEmpty(), "no partial file left");

	std::printf("already migrated\n");
	check(own.replaceWithText("editor's own"), "the editor writes its file");
	check(legacy.replaceWithText("upstream wrote again"), "upstream writes the shared file again");
	check(copySettingsOnce(legacy, own) == SettingsCopy::AlreadyMigrated, "AlreadyMigrated");
	check(own.loadFileAsString() == "editor's own", "the editor's file is never overwritten");
	check(legacy.loadFileAsString() == "upstream wrote again", "the legacy file is never touched");

	std::printf("a partial copy from an interrupted start\n");
	own.deleteFile();
	const auto partial = own.getSiblingFile(own.getFileName() + ".migrating");
	check(partial.replaceWithText("half"), "a stale partial file");
	check(copySettingsOnce(legacy, own) == SettingsCopy::Copied, "Copied next to the stale partial");
	check(own.loadFileAsString() == "upstream wrote again", "the finished copy is the legacy file");
	check(partial.loadFileAsString() == "half", "the stale partial is not shared with this start (names are unique)");
	check(partialsIn(config).size() == 1, "this start left no partial of its own");
	partial.deleteFile();

	std::printf("the copy cannot be made\n");
	own.deleteFile();
	const auto locked = dir.getChildFile("locked");
	locked.createDirectory();
	check(locked.setReadOnly(true, false), "a read-only folder");
	const auto lockedOwn = locked.getChildFile("Machinedrum Editor.xml");
	check(copySettingsOnce(legacy, lockedOwn) == SettingsCopy::Failed, "Failed");
	check(!lockedOwn.exists(), "no file is made");
	check(partialsIn(locked).isEmpty(), "no partial file left");
	locked.setReadOnly(false, false);

	std::printf("another process made the file first\n");
	// its file is there by the time this start checks again: Copied would be wrong, and it must not be replaced
	check(own.replaceWithText("the other process"), "the other process's file");
	check(copySettingsOnce(legacy, own) == SettingsCopy::AlreadyMigrated, "AlreadyMigrated");
	check(own.loadFileAsString() == "the other process", "its file is kept");
	own.deleteFile();

	std::printf("a config folder that does not exist yet\n");
	const auto fresh = dir.getChildFile("fresh").getChildFile("config");
	const auto freshLegacy = dir.getChildFile("Gearmulator MM.settings");
	check(freshLegacy.replaceWithText("standalone"), "legacy standalone settings");
	check(copySettingsOnce(freshLegacy, fresh.getChildFile("Monomachine Editor.settings")) == SettingsCopy::Copied, "Copied into a new folder");

	dir.deleteRecursively();
	if(g_failures)
	{
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("all passed\n");
	return 0;
}
