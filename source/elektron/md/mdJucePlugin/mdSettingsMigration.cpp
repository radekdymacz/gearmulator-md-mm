#include "mdSettingsMigration.h"

#if JUCE_WINDOWS
#	include <process.h>
#else
#	include <unistd.h>
#endif

namespace mdJucePlugin
{
	namespace
	{
		int processId()
		{
#if JUCE_WINDOWS
			return _getpid();
#else
			return static_cast<int>(::getpid());
#endif
		}

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

		// a name of this start's own (pid + random): two editors starting at once never share a partial file
		const auto partial = _own.getSiblingFile(_own.getFileName() + ".migrating-" +
			juce::String(processId()) + "-" +
			juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
		partial.deleteFile();
		if(!_legacy.copyFileTo(partial))
		{
			partial.deleteFile();
			return _own.exists() ? SettingsCopy::AlreadyMigrated : SettingsCopy::Failed;
		}
		// moveFileTo replaces an existing target: look once more, so a file another process made meanwhile stays
		if(_own.exists())
		{
			partial.deleteFile();
			return SettingsCopy::AlreadyMigrated;
		}
		if(!partial.moveFileTo(_own))
		{
			partial.deleteFile();
			// the other process may have won the race: its file is the migration
			return _own.exists() ? SettingsCopy::AlreadyMigrated : SettingsCopy::Failed;
		}
		return SettingsCopy::Copied;
	}
}
