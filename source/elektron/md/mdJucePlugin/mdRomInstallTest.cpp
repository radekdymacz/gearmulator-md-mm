// P7: the ROM install validator (mdRomInstall, md::checkRom): wrong size, not a .bin, a .zip without a .bin,
// an unknown 8 MiB image, the other machine's firmware, and a copy that never overwrites. With the real
// firmware images (the user's own, only when GEARMULATOR_MD_FIRMWARE_BIN / GEARMULATOR_MM_FIRMWARE_BIN are
// set) also the accepted case; they are read in place, never copied into the repository.
#include "mdRomInstall.h"

#include "mdLib/mdromcheck.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	int g_failures = 0;
	void check(const bool _ok, const std::string& _what, const std::string& _text = {})
	{
		std::printf("  %s %s%s\n", _ok ? "ok  " : "FAIL", _what.c_str(), _text.empty() ? "" : (": " + _text).c_str());
		if(!_ok) ++g_failures;
	}
	bool has(const std::string& _s, const char* _part) { return _s.find(_part) != std::string::npos; }

	juce::File zipWith(const juce::File& _dir, const juce::String& _name, const juce::String& _entry, const juce::MemoryBlock& _data)
	{
		juce::ZipFile::Builder b;
		b.addEntry(new juce::MemoryInputStream(_data, true), 0, _entry, juce::Time::getCurrentTime());
		auto f = _dir.getChildFile(_name);
		f.deleteFile();
		juce::FileOutputStream out(f);
		b.writeToStream(out, nullptr);
		return f;
	}
}

int main()
{
	using md::MachineModel;
	const auto dir = juce::File::createTempFile("mdRomInstallTest");
	dir.createDirectory();
	const auto roms = dir.getChildFile("roms");

	auto small = dir.getChildFile("small.bin");
	small.replaceWithData("abc", 3);
	auto r = mdJucePlugin::installRom(small, MachineModel::Machinedrum, roms);
	check(!r.ok && has(r.text, "exactly 8 MiB"), "a file of the wrong size is refused", r.text);

	auto txt = dir.getChildFile("notes.txt");
	txt.replaceWithText("hello");
	r = mdJucePlugin::installRom(txt, MachineModel::Machinedrum, roms);
	check(!r.ok && has(r.text, ".bin"), "a file that is not a .bin or .zip is refused", r.text);

	juce::MemoryBlock text("readme", 6);
	auto noBin = zipWith(dir, "nobin.zip", "readme.txt", text);
	r = mdJucePlugin::installRom(noBin, MachineModel::Machinedrum, roms);
	check(!r.ok && has(r.text, "holds no .bin"), "a .zip without a .bin is refused", r.text);

	juce::MemoryBlock zeros(md::g_romSize, true);
	auto unknown = zipWith(dir, "unknown.zip", "unknown.bin", zeros);
	r = mdJucePlugin::installRom(unknown, MachineModel::Machinedrum, roms);
	check(!r.ok && has(r.text, "not the Machinedrum OS 1.63"), "an unknown 8 MiB image is refused", r.text);
	check(!roms.exists() || roms.getNumberOfChildFiles(juce::File::findFiles) == 0, "nothing was copied for a refused file");

	// the real images, when the user's own are given
	for(const auto& [var, model, other] : {std::tuple{"GEARMULATOR_MD_FIRMWARE_BIN", MachineModel::Machinedrum, MachineModel::Monomachine},
		std::tuple{"GEARMULATOR_MM_FIRMWARE_BIN", MachineModel::Monomachine, MachineModel::Machinedrum}})
	{
		const char* path = std::getenv(var);
		if(!path || !juce::File(path).existsAsFile())
		{
			std::printf("  skip %s not set: the accepted case is not checked\n", var);
			continue;
		}
		const juce::File rom(path);
		r = mdJucePlugin::installRom(rom, other, roms);
		check(!r.ok && has(r.text, "This is the"), std::string("the ") + md::firmwareName(model) + " is refused by the other editor", r.text);
		r = mdJucePlugin::installRom(rom, model, roms);
		check(r.ok && r.installed.existsAsFile() && has(r.text, "found"), std::string("the ") + md::firmwareName(model) + " is accepted and copied", r.text);
		const auto first = r.installed;
		r = mdJucePlugin::installRom(rom, model, roms);
		check(r.ok && r.installed == first && has(r.text, "already"), "the same image is not copied twice", r.text);
		first.replaceWithText("another file of that name");
		r = mdJucePlugin::installRom(rom, model, roms);
		check(r.ok && r.installed != first && first.loadFileAsString() == "another file of that name", "another file of that name is kept (no overwrite)", r.installed.getFileName().toStdString());
		juce::MemoryBlock img;
		rom.loadFileAsData(img);
		auto zipped = zipWith(dir, "zipped.zip", "inside/" + rom.getFileName(), img);
		r = mdJucePlugin::installRom(zipped, model, roms.getChildFile("z"));
		check(r.ok, "the image inside a .zip is accepted", r.text);

		// REPLACE and REMOVE act inside the ROM folder only
		const auto mine = dir.getChildFile("manage");
		const auto folder = mine.getChildFile("roms");
		folder.createDirectory();
		const auto decoy = mine.getChildFile("outside.bin");		// an image outside the folder: never touched
		decoy.replaceWithData(img.getData(), img.getSize());
		const auto note = folder.getChildFile("readme.txt");		// not an image: never touched
		note.replaceWithText("keep me");
		const auto oldName = folder.getChildFile("old-name.bin");
		oldName.replaceWithData(img.getData(), img.getSize());
		check(mdJucePlugin::romsInFolder(model, folder).size() == 1, "the folder holds one image of this machine");
		check(mdJucePlugin::romsInFolder(other, folder).empty(), "and none of the other machine's");
		const auto fresh = dir.getChildFile("new-dump.bin");
		fresh.replaceWithData(img.getData(), img.getSize());
		r = mdJucePlugin::replaceRom(fresh, model, folder);
		const auto now = mdJucePlugin::romsInFolder(model, folder);
		check(r.ok && now.size() == 1 && now[0].getFileName() == "new-dump.bin" && !oldName.existsAsFile() && fresh.existsAsFile(),
			"REPLACE installs the new image and drops the earlier one (the chosen file itself stays where it was)", r.text);
		r = mdJucePlugin::replaceRom(dir.getChildFile("nothing.bin"), model, folder);
		check(!r.ok && mdJucePlugin::romsInFolder(model, folder).size() == 1, "a failed REPLACE leaves the installed image");
		const auto gone = mdJucePlugin::removeRoms(model, folder);
		check(gone.removed == 1 && mdJucePlugin::romsInFolder(model, folder).empty(), "REMOVE deletes the image", gone.text);
		check(decoy.existsAsFile() && fresh.existsAsFile() && note.existsAsFile(), "REMOVE touched nothing outside the folder and no other file");
		check(mdJucePlugin::removeRoms(model, folder).removed == 0, "REMOVE with nothing to remove says so");
	}
	dir.deleteRecursively();
	std::printf("mdRomInstallTest: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
