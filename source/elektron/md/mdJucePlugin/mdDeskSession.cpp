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

	void MidiWire::pump(const double _nowMs, const std::function<void(const Bytes&)>& _in)
	{
		for(auto& m : m_pacer.take(_nowMs))
			m_out(m);
		for(const auto& m : m_in())
			_in(m);
	}

	uint8_t MidiWire::realtimeOf(const std::string& _key)
	{
		static const std::map<std::string, uint8_t> keys{{"play", 0xfa}, {"stop", 0xfc}};
		const auto it = keys.find(_key);
		return it == keys.end() ? 0 : it->second;
	}

	MidiWire midiWireOf(AudioPluginAudioProcessor& _processor)
	{
		auto* p = &_processor;
		return MidiWire([p](const MidiWire::Bytes& _m)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Editor);
			if(!_m.empty() && _m[0] == 0xf0)
				e.sysex.assign(_m.begin(), _m.end());
			else
			{
				e.a = _m.size() > 0 ? _m[0] : 0;
				e.b = _m.size() > 1 ? _m[1] : 0;
				e.c = _m.size() > 2 ? _m[2] : 0;
			}
			p->sendExternalMidi(e);
		}, [p]
		{
			std::vector<synthLib::SMidiEvent> in;
			p->drainExternalMidiIn(in);
			std::vector<MidiWire::Bytes> out;
			for(const auto& e : in)
				if(!e.sysex.empty())
					out.emplace_back(e.sysex.begin(), e.sysex.end());
			return out;
		});
	}
}
