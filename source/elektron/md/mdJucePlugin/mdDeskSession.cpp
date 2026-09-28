#include "mdDeskSession.h"

#include "mdSessions.h"
#include "mdRomInstall.h"

#include "mdPluginProcessor.h"

#include "synthLib/midiTypes.h"

#include "juce_core/juce_core.h"

namespace mdJucePlugin
{
	bool followsHost(AudioPluginAudioProcessor& _processor)
	{
		const juce::AudioProcessor& p = pluginProcessorOf(_processor);
		return p.wrapperType != juce::AudioProcessor::wrapperType_Undefined && p.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
	}

	pluginLib::Processor& pluginProcessorOf(AudioPluginAudioProcessor& _processor)
	{
		return _processor;
	}

	double sessionNowMs()
	{
		return juce::Time::getMillisecondCounterHiRes();
	}

	DeskSession::DeskSession(AudioPluginAudioProcessor& _processor) : m_processor(_processor)
	{
	}

	DeskSession::~DeskSession()
	{
		stopTimer();
	}

	std::unique_ptr<DeskSession> DeskSession::create(AudioPluginAudioProcessor& _processor)
	{
		auto session = _processor.getModel() == md::MachineModel::Monomachine ? makeMmSession(_processor) : makeMdSession(_processor);
		// Steps start once the whole session exists.
		session->startTimer(static_cast<int>(g_stepMs));
		return session;
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

	void DeskSession::installRom(const juce::File& _file)
	{
		const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
		const auto r = mdJucePlugin::installRom(_file, m_processor.getModel(), folder);
		Value m = Value::object();
		m.set("type", "romInstall");
		m.set("ok", r.ok);
		m.set("text", r.text);
		toPage(m);
		if(!r.ok)
			return;
		// The machine starts again with the new firmware, without reopening the plug-in; the page's boot
		// card shows its start-up.
		const bool restarted = pluginProcessorOf(m_processor).rebootDevice();
		if(!restarted)
		{
			Value e = Value::object();
			e.set("type", "romInstall");
			e.set("ok", false);
			e.set("text", r.text + ". The machine did not start with it: close and reopen the plug-in.");
			toPage(e);
		}
	}

	void DeskSession::revealRomFolder(const Value& _message) const
	{
		const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
		folder.createDirectory();
		folder.revealToUser();
		reply(_message, true, {});
	}

	void SetupStore::save(const Value& _setup)
	{
		m_processor.setDeskSetup(elektronData::json::write(_setup));
		m_seen = m_processor.getDeskSetupVersion();
	}

	std::optional<std::string> SetupStore::restored()
	{
		const auto version = m_processor.getDeskSetupVersion();
		if(m_seen && *m_seen == version)
			return std::nullopt;
		m_seen = version;
		return m_processor.getDeskSetup();
	}

	namespace
	{
		deskWire::MidiWire wireOf(AudioPluginAudioProcessor& _processor)
		{
			auto* p = &_processor;
			return deskWire::MidiWire([p](const deskWire::Bytes& _m)
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
				std::vector<deskWire::Bytes> out;
				for(const auto& e : in)
					if(!e.sysex.empty())
						out.emplace_back(e.sysex.begin(), e.sysex.end());
				return out;
			});
		}
	}

	PluginWire::PluginWire(AudioPluginAudioProcessor& _processor) : m_processor(_processor), m_wire(wireOf(_processor))
	{
		m_processor.setExternalMidi(true);
	}

	PluginWire::~PluginWire()
	{
		m_processor.setExternalMidi(false);
	}

	Availability midiOutAvailability(const DeskSession& _session)
	{
		auto& p = _session.processor();
		// The standalone's MIDI output is chosen in its AUDIO / MIDI panel, after the engine. A plug-in
		// declares no MIDI out to its host (NEEDS_MIDI_OUTPUT FALSE), so there only the plug-in's own
		// MIDI port reaches a machine.
		if(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone || static_cast<const juce::AudioProcessor&>(p).producesMidi()
			|| p.getMidiPorts().getOutputId().isNotEmpty())
			return {};
		return {false, "The plug-in has no MIDI out in this host: HW MIDI works in the standalone app."};
	}
}
