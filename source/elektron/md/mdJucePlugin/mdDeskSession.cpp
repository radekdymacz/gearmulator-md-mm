#include "mdDeskSession.h"

#include "mdSessions.h"

#include "mdPluginProcessor.h"

#include "synthLib/midiTypes.h"

namespace mdJucePlugin
{
	namespace
	{
		// The session's pace: the playhead (the app modulators move on its steps) is looked at every
		// 8 ms, faster than the fastest step (300 BPM at 2X: 25 ms); the desk's own work runs at
		// about 30 Hz (every fourth step).
		constexpr int g_stepMs = 8;
	}

	pluginLib::Processor& pluginProcessorOf(AudioPluginAudioProcessor& _processor)
	{
		return _processor;
	}

	DeskSession::DeskSession(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
		startTimer(g_stepMs);
	}

	DeskSession::~DeskSession()
	{
		stopTimer();
	}

	std::unique_ptr<DeskSession> DeskSession::create(AudioPluginAudioProcessor& _processor)
	{
		if(_processor.getModel() == md::MachineModel::Monomachine)
			return makeMmSession(_processor);
		return makeMdSession(_processor);
	}

	void DeskSession::attach(ToPage _toPage)
	{
		m_toPage = std::move(_toPage);
		onAttach();
	}

	void DeskSession::detach()
	{
		onDetach();
		m_toPage = nullptr;
	}

	void DeskSession::toPage(const Value& _message) const
	{
		if(m_toPage)
			m_toPage(_message);
	}

	void DeskSession::reply(const Value& _message, const bool _ok, const std::string& _note) const
	{
		toPage(deskCore::resultMessage(_message, _ok ? std::vector<std::string>{} : std::vector<std::string>{_note}, _note));
	}

	void DeskSession::setExternalMidi(const bool _on) const
	{
		m_processor.setExternalMidi(_on);
	}

	void DeskSession::revealRomFolder(const Value& _message) const
	{
		const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
		folder.createDirectory();
		folder.revealToUser();
		reply(_message, true, {});
	}

	MidiWire::MidiWire(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	void MidiWire::pump(const std::function<void(const std::vector<uint8_t>&)>& _in)
	{
		for(auto& m : m_pacer.take(juce::Time::getMillisecondCounterHiRes()))
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Editor);
			if(!m.empty() && m[0] == 0xf0)
				e.sysex.assign(m.begin(), m.end());
			else
			{
				e.a = m.size() > 0 ? m[0] : 0;
				e.b = m.size() > 1 ? m[1] : 0;
				e.c = m.size() > 2 ? m[2] : 0;
			}
			m_processor.sendExternalMidi(e);
		}
		std::vector<synthLib::SMidiEvent> in;
		m_processor.drainExternalMidiIn(in);
		for(const auto& e : in)
			_in(std::vector<uint8_t>(e.sysex.begin(), e.sysex.end()));
	}
}
