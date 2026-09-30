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
		log(std::string("drop: install ") + (r.ok ? "ok: " : "failed: ") + r.text);
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

	namespace
	{
		// base64 straight into the buffer (no intermediate stream); false if the text is damaged or too long for the room
		bool decodeBase64(const std::string& _in, uint8_t* const _out, const size_t _room, size_t& _written)
		{
			static const auto table = []
			{
				std::array<int8_t, 256> t{};
				t.fill(-1);
				const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				for(int i = 0; i < 64; ++i)
					t[static_cast<uint8_t>(a[i])] = static_cast<int8_t>(i);
				return t;
			}();
			uint32_t acc = 0;
			int bits = 0;
			size_t n = 0;
			for(const char c : _in)
			{
				if(c == '=')
					break;
				const int v = table[static_cast<uint8_t>(c)];
				if(v < 0)
					return false;
				acc = (acc << 6) | static_cast<uint32_t>(v);
				bits += 6;
				if(bits >= 8)
				{
					bits -= 8;
					if(n >= _room)
						return false;
					_out[n++] = static_cast<uint8_t>((acc >> bits) & 0xff);
				}
			}
			_written = n;
			return true;
		}
	}

	void DeskSession::romBytes(const Value& _message)
	{
		const auto text = [&](const char* k) { const auto* v = _message.find(k); return v && v->isString() ? v->asString() : std::string(); };
		const auto num = [&](const char* k) { const auto* v = _message.find(k); return v && v->isNumber() ? v->asNumber() : -1.0; };
		const double tid = num("tid");
		const int index = static_cast<int>(num("index")), count = static_cast<int>(num("count"));
		const auto ack = [&](const bool _ok, const std::string& _text)
		{
			Value a = Value::object();
			a.set("type", "romBytesAck");
			a.set("tid", tid);
			a.set("index", index);
			a.set("ok", _ok);
			if(!_ok)
				a.set("text", _text);
			toPage(a);
		};
		const auto fail = [&](const std::string& _why)
		{
			log("drop: refused: " + _why);
			m_incoming = {};
			m_incoming.tid = tid;		// the rest of this transfer's pieces are answered, not restarted
			m_incoming.count = 0;
			Value m = Value::object();
			m.set("type", "romInstall");
			m.set("ok", false);
			m.set("text", _why);
			toPage(m);
			ack(false, _why);
			reply(_message, false, _why);
		};
		// a newer transfer replaces the running one; a piece of an older one is dropped without a word
		if(tid < m_incoming.tid)
		{
			ack(false, "cancelled");
			return;
		}
		if(tid > m_incoming.tid)
		{
			m_incoming = {};
			m_incoming.tid = tid;
			m_incoming.name = text("name");
			m_incoming.size = static_cast<size_t>(std::max(num("size"), 0.0));
			m_incoming.count = count;
			m_incoming.t0 = juce::Time::getMillisecondCounterHiRes();
			if(m_incoming.size > 64u * 1024u * 1024u || count < 1 || count > 4096)
				return fail("This file is far larger than a firmware image.");
			m_incoming.have.assign(static_cast<size_t>(count), false);
			m_incoming.bytes.assign(m_incoming.size, 0);
			log("drop: " + m_incoming.name + " " + std::to_string(m_incoming.size) + " bytes in " + std::to_string(count) + " pieces");
		}
		if(m_incoming.count == 0)
		{
			ack(false, "This transfer is over or was refused. Drop the file again.");
			return;
		}
		if(index < 0 || index >= m_incoming.count || text("name") != m_incoming.name || count != m_incoming.count)
			return fail("The file did not arrive as announced. Drop it again.");
		const double offset = num("offset");
		size_t wrote = 0;
		if(offset < 0 || static_cast<size_t>(offset) > m_incoming.size
			|| !decodeBase64(text("data"), m_incoming.bytes.data() + static_cast<size_t>(offset), m_incoming.size - static_cast<size_t>(offset), wrote))
			return fail("The file could not be read (its data was damaged on the way, or is larger than announced).");
		if(!m_incoming.have[static_cast<size_t>(index)])
		{
			m_incoming.have[static_cast<size_t>(index)] = true;
			++m_incoming.got;
			m_incoming.received += wrote;
		}
		if(m_incoming.got < m_incoming.count)
		{
			ack(true, {});
			return;
		}
		const auto ms = juce::Time::getMillisecondCounterHiRes() - m_incoming.t0;
		log("drop: bytes received " + std::to_string(m_incoming.received) + " in " + std::to_string(static_cast<int>(ms)) + " ms");
		if(m_incoming.received != m_incoming.size)
			return fail("The file arrived incomplete. Drop it again.");
		// Into a temp folder under its own (legal) name, then the same path as a chosen file; gone afterwards.
		const auto dir = juce::File::createTempFile("gmrom");
		dir.createDirectory();
		auto legal = juce::File::createLegalFileName(juce::String::fromUTF8(m_incoming.name.c_str()));
		if(legal.isEmpty())
			legal = "dropped.bin";
		const auto file = dir.getChildFile(legal);
		const bool written = file.replaceWithData(m_incoming.bytes.data(), m_incoming.bytes.size());
		const double keep = m_incoming.tid;
		m_incoming = {};
		m_incoming.tid = keep;
		m_incoming.count = 0;		// finished: a repeat of its pieces is refused, a newer transfer starts fresh
		if(!written)
		{
			dir.deleteRecursively();
			return fail("The file could not be written to a temporary folder.");
		}
		installRom(file);
		dir.deleteRecursively();
		ack(true, {});
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
