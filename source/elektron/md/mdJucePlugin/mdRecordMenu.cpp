#include "mdRecordMenu.h"

// before standaloneApp.h: JUCE's standalone headers define Component as a macro
#include "juceUiLib/messageRoute.h"

#include "jucePluginEditorLib/standaloneApp.h"

namespace mdJucePlugin
{
	namespace
	{
		constexpr double g_closeWaitSeconds = 5.0;

		juce::String twoDigits(const int _v)
		{
			return juce::String(_v).paddedLeft('0', 2);
		}

		// The name System Settings lists the app under: its bundle's.
		juce::String appBundleName()
		{
			return juce::File::getSpecialLocation(juce::File::currentApplicationFile).getFileNameWithoutExtension();
		}

	}

	RecordMenu::RecordMenu(jucePluginEditorLib::StandaloneWindow& _window, juce::String _editorName, juce::String _model, const void* _noticeOwner)
		: m_window(_window)
		, m_editorName(std::move(_editorName))
		, m_model(std::move(_model))
		, m_title(_window.getName())
		, m_noticeOwner(_noticeOwner)
	{
		m_commands.registerAllCommandsForTarget(this);
		m_commands.setFirstCommandTarget(this);
	}

	RecordMenu::~RecordMenu()
	{
		m_commands.setFirstCommandTarget(nullptr);
	}

	juce::File RecordMenu::folder(const juce::String& _editorName)
	{
		return juce::File::getSpecialLocation(juce::File::userMoviesDirectory).getChildFile(_editorName);
	}

	juce::File RecordMenu::takeFile(const juce::File& _folder, const juce::String& _editorName, const juce::Time& _start)
	{
		const auto name = _editorName + " " + juce::String(_start.getYear()) + "-" + twoDigits(_start.getMonth() + 1) + "-" + twoDigits(_start.getDayOfMonth())
			+ " " + twoDigits(_start.getHours()) + "-" + twoDigits(_start.getMinutes()) + "-" + twoDigits(_start.getSeconds());
		return _folder.getChildFile(name + ".mp4").getNonexistentSibling(false);
	}

	juce::PopupMenu RecordMenu::menu()
	{
		juce::PopupMenu m;
		m.addCommandItem(&m_commands, ToggleRecording, toggleText());
		m.addSeparator();
		m.addCommandItem(&m_commands, ShowRecordings);
		return m;
	}

	void RecordMenu::getAllCommands(juce::Array<juce::CommandID>& _commands)
	{
		_commands.addArray({ToggleRecording, ShowRecordings});
	}

	void RecordMenu::getCommandInfo(const juce::CommandID _id, juce::ApplicationCommandInfo& _info)
	{
		if(_id == ShowRecordings)
		{
			_info.setInfo("Show Recordings in Finder", "Opens the folder the recordings are in", "Record", 0);
			return;
		}
		if(_id != ToggleRecording)
			return;
		const auto state = m_recorder.state();
		_info.setInfo(toggleText(), "Records the window and its sound to a movie", "Record", 0);
		_info.addDefaultKeypress('r', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
		_info.setActive(!m_closing && (state == ScreenRecorder::State::Idle || state == ScreenRecorder::State::Recording));
	}

	bool RecordMenu::perform(const InvocationInfo& _info)
	{
		if(_info.commandID == ShowRecordings)
		{
			const auto dir = folder(m_editorName);
			dir.createDirectory();
			dir.startAsProcess();
			return true;
		}
		if(_info.commandID == ToggleRecording)
		{
			toggle();
			return true;
		}
		return false;
	}

	juce::String RecordMenu::toggleText() const
	{
		switch(m_recorder.state())
		{
		case ScreenRecorder::State::Idle:		return ScreenRecorder::unavailableReason().isEmpty() ? "Start Recording" : "Start Recording (needs macOS 15)";
		case ScreenRecorder::State::Starting:	return "Starting Recording...";
		case ScreenRecorder::State::Recording:	return "Stop Recording";
		case ScreenRecorder::State::Stopping:	return "Finishing Recording...";
		}
		return "Start Recording";
	}

	void RecordMenu::toggle()
	{
		if(m_recorder.state() == ScreenRecorder::State::Recording)
		{
			m_recorder.stop();
			showState();
			return;
		}
		if(m_recorder.state() == ScreenRecorder::State::Idle)
			start();
	}

	void RecordMenu::start()
	{
		if(const auto unavailable = ScreenRecorder::unavailableReason(); unavailable.isNotEmpty())
		{
			notify("Cannot record", unavailable + " This Mac runs an older macOS.", {"OK"});
			return;
		}
		if(!ScreenRecorder::screenRecordingAllowed())
		{
			// the first time macOS asks itself; either way say what to do
			ScreenRecorder::askForScreenRecording();
			askForPermission();
			return;
		}
		const auto dir = folder(m_editorName);
		if(!dir.createDirectory())
		{
			notify("Cannot record", "Cannot create the folder " + dir.getFullPathName() + ".", {"OK"});
			return;
		}
		m_startTime = juce::Time::getCurrentTime();
		m_recorder.start(m_window, takeFile(dir, m_editorName, m_startTime),
			[this](const ScreenRecorder::Started& _s) { started(_s); },
			[this](const ScreenRecorder::Ended& _e) { ended(_e); });
		showState();
	}

	void RecordMenu::started(const ScreenRecorder::Started& _started)
	{
		m_picture = _started.picture;
		showState();
		if(_started.recording || m_closing)
			return;
		if(_started.notAllowed)
		{
			askForPermission();
			return;
		}
		notify("Cannot record", _started.problem, {"OK"});
	}

	void RecordMenu::ended(const ScreenRecorder::Ended& _ended)
	{
		showState();
		const bool written = _ended.file.existsAsFile() && _ended.file.getSize() > 0;
		if(written)
			writeSidecar(_ended);
		if(m_closing)
			return;
		if(!written)
		{
			notify("The recording failed",
				(_ended.problem.isNotEmpty() ? _ended.problem + "\n\n" : juce::String()) + "Nothing was written.", {"OK"});
			return;
		}
		_ended.file.revealToUser();
		if(_ended.early || _ended.problem.isNotEmpty())
		{
			notify("The recording stopped early",
				_ended.problem + "\n\nWhat was recorded (" + juce::String(_ended.seconds, 1) + " s) is kept in\n" + _ended.file.getFullPathName(), {"OK"});
		}
	}

	void RecordMenu::showState()
	{
		const auto state = m_recorder.state();
		const bool rec = state == ScreenRecorder::State::Recording || state == ScreenRecorder::State::Stopping;
		m_window.setName(rec ? m_title + juce::String(juce::CharPointer_UTF8("  \xe2\x97\x8f REC")) : m_title);
		m_window.refreshMenus();
	}

	// For the reels (marketing/reels): what the take is, beside the movie as <name>.json. The
	// sequencer's bar lines are not in it (they would need the session's transport here).
	void RecordMenu::writeSidecar(const ScreenRecorder::Ended& _ended) const
	{
		auto* video = new juce::DynamicObject();
		video->setProperty("codec", "h264");
		video->setProperty("width", m_picture.width);
		video->setProperty("height", m_picture.height);
		video->setProperty("fps", m_picture.fps);

		auto* audio = new juce::DynamicObject();
		audio->setProperty("codec", "aac");
		audio->setProperty("sampleRate", ScreenRecorder::sampleRate);
		audio->setProperty("channels", ScreenRecorder::channels);

		auto* root = new juce::DynamicObject();
		root->setProperty("editor", m_editorName);
		root->setProperty("model", m_model);
		root->setProperty("version", JucePlugin_VersionString);
		root->setProperty("movie", _ended.file.getFileName());
		root->setProperty("started", m_startTime.toISO8601(true));
		root->setProperty("seconds", _ended.seconds);
		root->setProperty("endedEarly", _ended.early);
		root->setProperty("video", juce::var(video));
		root->setProperty("audio", juce::var(audio));

		_ended.file.withFileExtension("json").replaceWithText(juce::JSON::toString(juce::var(root)) + "\n");
	}

	void RecordMenu::askForPermission() const
	{
		const auto app = appBundleName();
		notify("Screen Recording is not allowed",
			"To record the window and its sound, allow \"" + app + "\" in System Settings > Privacy & Security > "
			"Screen & System Audio Recording, then quit " + app + " and open it again.",
			{"Open System Settings", "Cancel"},
			[](const int _button)
			{
				if(_button == 0)
					ScreenRecorder::openScreenRecordingSettings();
			});
	}

	// The editor's own dialog (the page's notice, messageRoute.h), in this app's window; when no page
	// can take it, the system's alert. _buttons: the last one is the safe answer; _answered gets the
	// index of the one pressed (never called when the window closes first).
	void RecordMenu::notify(const juce::String& _title, const juce::String& _text, const std::vector<juce::String>& _buttons,
		std::function<void(int)> _answered) const
	{
		genericUI::messageRoute::Notice n;
		n.title = _title.toStdString();
		n.text = _text.toStdString();
		for(const auto& b : _buttons)
			n.buttons.push_back(b.toStdString());
		n.answered = _answered;
		const genericUI::messageRoute::OwnerScope owner(m_noticeOwner);
		if(genericUI::messageRoute::offer(std::move(n)))
			return;

		auto options = juce::MessageBoxOptions()
			.withIconType(juce::MessageBoxIconType::WarningIcon)
			.withTitle(_title)
			.withMessage(_text);
		for(const auto& b : _buttons)
			options = options.withButton(b);
		juce::NativeMessageBox::showAsync(options, [_answered](const int _button)
		{
			if(_answered)
				_answered(_button);
		});
	}

	void RecordMenu::close()
	{
		m_closing = true;
		if(m_recorder.state() != ScreenRecorder::State::Idle)
			m_recorder.stopAndWait(g_closeWaitSeconds);
	}
}
