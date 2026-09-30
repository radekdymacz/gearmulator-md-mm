#include "mdRomInstall.h"

#include "mdLib/mdromcheck.h"

namespace mdJucePlugin
{
	std::optional<std::vector<uint8_t>> readRomImage(const juce::File& _file, std::string& _why)
	{
		if(!_file.existsAsFile())
		{
			_why = "The file is not there any more.";
			return std::nullopt;
		}
		if(_file.hasFileExtension("zip"))
		{
			juce::ZipFile zip(_file);
			const juce::ZipFile::ZipEntry* found = nullptr;
			int bins = 0;
			for(int i = 0; i < zip.getNumEntries(); ++i)
			{
				const auto* e = zip.getEntry(i);
				if(!e || !e->filename.endsWithIgnoreCase(".bin") || e->filename.contains("__MACOSX"))
					continue;
				++bins;
				if(e->uncompressedSize == md::g_romSize)
					found = e;
			}
			if(!found)
			{
				_why = bins ? "The .zip holds no 8 MiB .bin." : "The .zip holds no .bin file.";
				return std::nullopt;
			}
			std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(*found));
			juce::MemoryBlock mb;
			if(!in || in->readIntoMemoryBlock(mb) != static_cast<size_t>(md::g_romSize))
			{
				_why = "The .bin in the .zip could not be read.";
				return std::nullopt;
			}
			return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
		}
		if(!_file.hasFileExtension("bin"))
		{
			_why = "Choose the firmware image (.bin), or a .zip with it.";
			return std::nullopt;
		}
		if(_file.getSize() > 64 * 1024 * 1024)
		{
			_why = "This file is far larger than a firmware image.";
			return std::nullopt;
		}
		juce::MemoryBlock mb;
		if(!_file.loadFileAsData(mb))
		{
			_why = "The file could not be read.";
			return std::nullopt;
		}
		return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
	}

	RomInstall installRom(const juce::File& _file, const md::MachineModel _model, const juce::File& _romFolder)
	{
		std::string why;
		const auto image = readRomImage(_file, why);
		if(!image)
			return {false, why, {}};
		const auto check = md::checkRom(*image, _model);
		if(!check.ok)
			return {false, check.text, {}};
		if(!_romFolder.createDirectory())
			return {false, "The ROM folder could not be made: " + _romFolder.getFullPathName().toStdString(), {}};
		// The name the user knows, as a .bin; an existing file of that name is kept (the same image is not copied twice).
		const auto base = _file.hasFileExtension("zip") ? _file.getFileNameWithoutExtension() : _file.getFileNameWithoutExtension();
		auto target = _romFolder.getChildFile(base + ".bin");
		for(int n = 2; target.existsAsFile(); ++n)
		{
			juce::MemoryBlock have;
			if(target.getSize() == static_cast<juce::int64>(image->size()) && target.loadFileAsData(have)
				&& std::memcmp(have.getData(), image->data(), image->size()) == 0)
				return {true, check.text + " (already in the ROM folder)", target};
			target = _romFolder.getChildFile(base + "-" + juce::String(n) + ".bin");
		}
		if(!target.replaceWithData(image->data(), image->size()))
			return {false, "The ROM could not be written to " + target.getFullPathName().toStdString(), {}};
		return {true, check.text, target};
	}

	std::vector<juce::File> romsInFolder(const md::MachineModel _model, const juce::File& _romFolder)
	{
		std::vector<juce::File> found;
		if(!_romFolder.isDirectory())
			return found;
		for(const auto& f : _romFolder.findChildFiles(juce::File::findFiles, true, "*.bin"))
		{
			if(!f.isAChildOf(_romFolder) || f.getSize() != static_cast<juce::int64>(md::g_romSize))
				continue;
			juce::MemoryBlock mb;
			if(!f.loadFileAsData(mb))
				continue;
			const std::vector<uint8_t> bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
			if(md::checkRom(bytes, _model).ok)
				found.push_back(f);
		}
		return found;
	}

	namespace
	{
		int deleteAllBut(const md::MachineModel _model, const juce::File& _romFolder, const juce::File& _keep, std::string& _failed)
		{
			int n = 0;
			for(const auto& f : romsInFolder(_model, _romFolder))
			{
				if(_keep != juce::File() && f == _keep)
					continue;
				if(f.deleteFile())
					++n;
				else
					_failed += (_failed.empty() ? "" : ", ") + f.getFileName().toStdString();
			}
			return n;
		}
	}

	RomInstall replaceRom(const juce::File& _file, const md::MachineModel _model, const juce::File& _romFolder)
	{
		auto r = installRom(_file, _model, _romFolder);
		if(!r.ok)
			return r;
		std::string failed;
		const auto gone = deleteAllBut(_model, _romFolder, r.installed, failed);
		if(gone > 0)
			r.text += " (replaced the earlier image)";
		if(!failed.empty())
			r.text += ". The earlier image could not be removed: " + failed;
		return r;
	}

	RomRemoval removeRoms(const md::MachineModel _model, const juce::File& _romFolder)
	{
		std::string failed;
		RomRemoval r;
		r.removed = deleteAllBut(_model, _romFolder, {}, failed);
		if(!failed.empty())
			r.text = "Could not remove " + failed + " from the ROM folder.";
		else if(r.removed == 0)
			r.text = "There is no firmware image in the ROM folder to remove.";
		else
			r.text = "Removed the firmware image from the ROM folder.";
		return r;
	}
}
