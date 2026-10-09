#include "mdDeskSession.h"
#include "synthLib/romLoader.h"
#include "mdLib/mdromcheck.h"

#include "mdSessions.h"
#include "mdRomInstall.h"

#include "mdPluginProcessor.h"
#include "mdDeskHost.h"

#include "synthLib/midiTypes.h"

#include "juce_core/juce_core.h"
#include <algorithm>
#include <array>

namespace mdJucePlugin
{
	bool followsHost(AudioPluginAudioProcessor& _processor)
	{
		const juce::AudioProcessor& p = pluginProcessorOf(_processor);
		return p.wrapperType != juce::AudioProcessor::wrapperType_Undefined && p.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
	}

	double hostBpmOf(AudioPluginAudioProcessor& _processor)
	{
		return _processor.getHostBpm();
	}

	std::optional<elektronData::json::Value> audioRunOf(AudioPluginAudioProcessor& _processor)
	{
		using Value = elektronData::json::Value;
		const auto& b = _processor.bootDiagnostics();
		if(!b.sampled())
			return std::nullopt;
		auto m = Value::object();
		m.set("type", "audioRun");
		m.set("plugin", !juce::JUCEApplicationBase::isStandaloneApp());
		m.set("seconds", static_cast<int>(b.last().wallMs / 1000.0));
		m.set("blocks", static_cast<double>(b.last().blocks));
		m.set("bypassed", static_cast<double>(b.last().bypassed));
		m.set("blocksPerSecond", b.rate().known ? b.rate().blocksPerSecond : 0.0);
		m.set("realtime", b.rate().known && b.last().blocks > 0 ? Value(b.rate().realtime) : Value());
		return m;
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
		const auto r = mdJucePlugin::replaceRom(_file, m_processor.getModel(), folder);
		Value m = Value::object();
		m.set("type", "romInstall");
		m.set("ok", r.ok);
		m.set("text", r.text);
		toPage(m);
		log(std::string("rom install ") + (r.ok ? "ok: " : "failed: ") + r.text);
		if(!r.ok)
			return;
		// The machine starts again with the new firmware, without reopening the plug-in; the page's boot
		// card shows its start-up.
		const bool restarted = pluginProcessorOf(m_processor).rebootDevice();
		if(restarted)
			m_processor.restoreHeldState();	// the project the app was opened with (no ROM held it)
		if(!restarted)
		{
			Value e = Value::object();
			e.set("type", "romInstall");
			e.set("ok", false);
			e.set("text", r.text + ". The machine did not start with it: close and reopen the plug-in.");
			toPage(e);
		}
	}

	void DeskSession::romInfo(const Value& _message) const
	{
		const auto model = m_processor.getModel();
		const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
		// The image the machine would start with: the first valid one along the same search paths.
		juce::File found;
		for(const auto& path : synthLib::RomLoader::findFiles(".bin", md::g_romSize, md::g_romSize))
		{
			const juce::File f(juce::String::fromUTF8(path.c_str()));
			juce::MemoryBlock mb;
			if(!f.loadFileAsData(mb))
				continue;
			const std::vector<uint8_t> bytes(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
			if(md::checkRom(bytes, model).ok)
			{
				found = f;
				break;
			}
		}
		Value m = Value::object();
		m.set("type", "romInfo");
		m.set("installed", found != juce::File());
		m.set("os", std::string(md::firmwareName(model)));
		m.set("name", found.getFileName().toStdString());
		m.set("size", static_cast<double>(found.getSize()));
		m.set("folder", folder.getFullPathName().toStdString());
		m.set("inFolder", found != juce::File() && found.isAChildOf(folder));
		toPage(m);
		reply(_message, true, {});
	}

	void DeskSession::removeRom(const Value& _message)
	{
		const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
		const auto r = mdJucePlugin::removeRoms(m_processor.getModel(), folder);
		if(r.removed == 0)
		{
			reply(_message, false, r.text);
			return;
		}
		// The machine stops (the stand-in takes its place) and the page's start-up card asks for a ROM. The
		// project stays with the app meanwhile (DeskHost::holdState): the next ROM gets it back.
		juce::MemoryBlock project;
		m_processor.getStateInformation(project);
		if(!pluginProcessorOf(m_processor).rebootDevice())
		{
			reply(_message, false, r.text + " The machine did not stop: close and reopen the plug-in.");
			return;
		}
		m_processor.getDeskHost()->holdState(project.getData(), static_cast<int>(project.getSize()));
		reply(_message, true, r.text);
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
		m_processor.getDeskHost()->setSetup(elektronData::json::write(_setup));
		m_seen = m_processor.getDeskHost()->setupVersion();
	}

	std::optional<std::string> SetupStore::restored()
	{
		const auto version = m_processor.getDeskHost()->setupVersion();
		if(m_seen && *m_seen == version)
			return std::nullopt;
		m_seen = version;
		return m_processor.getDeskHost()->setup();
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
				p->getExternalMidi().send(e);
			}, [p]
			{
				std::vector<synthLib::SMidiEvent> in;
				p->getExternalMidi().drainIn(in);
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
		m_processor.getExternalMidi().set(true);
	}

	PluginWire::~PluginWire()
	{
		m_processor.getExternalMidi().set(false);
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
