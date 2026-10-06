#include "mdSettingsMigration.h"

namespace mdJucePlugin
{
	namespace
	{
		bool isMonomachine(const md::MachineModel _model)
		{
			return _model == md::MachineModel::Monomachine;
		}
	}

	const char* editorConfigFileName(const md::MachineModel _model)
	{
		return isMonomachine(_model) ? "Monomachine Editor.xml" : "Machinedrum Editor.xml";
	}

	const char* legacyConfigFileName(const md::MachineModel _model)
	{
		return isMonomachine(_model) ? "Gearmulator MM.xml" : "Gearmulator MD.xml";
	}

	const char* editorStandaloneSettingsName(const md::MachineModel _model)
	{
		return isMonomachine(_model) ? "Monomachine Editor" : "Machinedrum Editor";
	}

	const char* legacyStandaloneSettingsName(const md::MachineModel _model)
	{
		return isMonomachine(_model) ? "Gearmulator MM" : "Gearmulator MD";
	}

	SettingsCopy copySettingsOnce(const juce::File& _legacy, const juce::File& _own)
	{
		if(_own.exists())
			return SettingsCopy::AlreadyMigrated;
		if(!_legacy.existsAsFile())
			return SettingsCopy::NothingToCopy;

		if(!_own.getParentDirectory().createDirectory())
			return SettingsCopy::Failed;

		const auto partial = _own.getSiblingFile(_own.getFileName() + ".migrating");
		partial.deleteFile();
		if(!_legacy.copyFileTo(partial) || !partial.moveFileTo(_own))
		{
			partial.deleteFile();
			return SettingsCopy::Failed;
		}
		return SettingsCopy::Copied;
	}
}
