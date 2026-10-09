#include "mdAudioMidiLink.h"
#include "mdMidiPortRefusal.h"

#include "deskCore/deskCommands.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include <cmath>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		std::string str(const juce::String& _s) { return _s.toStdString(); }

		const std::string* stringOf(const json::Value& _m, const char* _key)
		{
			const auto* v = _m.find(_key);
			return v && v->isString() ? &v->asString() : nullptr;
		}

		json::Value named(const std::string& _id, const json::Value& _list)
		{
			json::Value v = json::Value::object();
			v.set("id", _id);
			v.set("list", _list);
			return v;
		}

		json::Value strings(const juce::StringArray& _a)
		{
			json::Value v = json::Value::array();
			for(const auto& s : _a)
				v.push(str(s));
			return v;
		}

		// A MIDI port's name for its identifier (what the page sends), or the identifier when it is not listed.
		std::string portName(const juce::Array<juce::MidiDeviceInfo>& _ports, const std::string& _identifier)
		{
			for(const auto& p : _ports)
				if(p.identifier == juce::String(_identifier))
					return str(p.name);
			return _identifier;
		}

		constexpr bool g_windows =
#if JUCE_WINDOWS
			true;
#else
			false;
#endif
	}

	AudioMidiLink::AudioMidiLink(juce::AudioProcessor& _processor, ToPage _toPage, Log _log)
		: m_processor(_processor)
		, m_toPage(std::move(_toPage))
		, m_log(std::move(_log))
	{
		if(auto* h = holder())
		{
			m_listening = &h->deviceManager;
			m_listening->addChangeListener(this);
		}
		logDevices();
	}

	AudioMidiLink::~AudioMidiLink()
	{
		if(m_listening)
			m_listening->removeChangeListener(this);
	}

	juce::StandalonePluginHolder* AudioMidiLink::holder() const
	{
		auto* const h = juce::StandalonePluginHolder::getInstance();
		return h && h->processor.get() == &m_processor ? h : nullptr;
	}

	void AudioMidiLink::changeListenerCallback(juce::ChangeBroadcaster*)
	{
		publish();
		logDevices();
	}

	std::string AudioMidiLink::summary() const
	{
		auto* h = holder();
		if(!h)
			return {};
		auto& dm = h->deviceManager;
		auto* dev = dm.getCurrentAudioDevice();
		juce::String line = "audio: " + (dm.getCurrentAudioDeviceType().isNotEmpty() ? dm.getCurrentAudioDeviceType() : juce::String("no driver"));
		if(dev)
		{
			const auto rate = dev->getCurrentSampleRate();
			const auto buffer = dev->getCurrentBufferSizeSamples();
			line << ", " << dev->getName() << ", " << juce::String(rate, 0) << " Hz, buffer " << buffer
				<< (rate > 0 ? " (" + juce::String(1000.0 * buffer / rate, 1) + " ms)" : juce::String())
				<< ", output latency " << dev->getOutputLatencyInSamples() << ", " << (dev->isPlaying() ? "running" : "NOT running");
		}
		else
		{
			line << ", no device open";
		}
		line << "; MIDI in:";
		const auto inputs = juce::MidiInput::getAvailableDevices();
		if(inputs.isEmpty())
			line << " none";
		for(const auto& m : inputs)
			line << " " << m.name << (dm.isMidiInputDeviceEnabled(m.identifier) ? " (on)" : " (off)");
		const auto outputs = juce::MidiOutput::getAvailableDevices();
		const auto out = dm.getDefaultMidiOutputIdentifier();
		line << "; MIDI out: " << (out.isEmpty() ? juce::String("none") : juce::String(portName(outputs, str(out))))
			<< " (of " << outputs.size() << ")";
		return str(line);
	}

	void AudioMidiLink::logDevices()
	{
		if(!m_log)
			return;
		auto line = summary();
		if(line.empty() || line == m_logged)
			return;
		m_logged = line;
		m_log(line);
	}

	void AudioMidiLink::publish()
	{
		json::Value m = json::Value::object();
		m.set("type", "audio");
		m.set("doc", document());
		m_toPage(std::move(m));
	}

	json::Value AudioMidiLink::document() const
	{
		json::Value d = json::Value::object();
		d.set("schema", "gm-audio/devices");
		d.set("version", 1);
		auto* h = holder();
		d.set("standalone", h != nullptr);
		if(!h)
			return d;
		auto& dm = h->deviceManager;
		const auto setup = dm.getAudioDeviceSetup();
		auto* dev = dm.getCurrentAudioDevice();
		auto* type = dm.getCurrentDeviceTypeObject();

		juce::StringArray types;
		for(const auto* t : dm.getAvailableDeviceTypes())
			types.add(t->getTypeName());
		d.set("driver", named(str(dm.getCurrentAudioDeviceType()), strings(types)));
		d.set("output", named(str(setup.outputDeviceName), type ? strings(type->getDeviceNames(false)) : json::Value::array()));

		const bool inputActive = dev && !dev->getActiveInputChannels().isZero();
		json::Value in = named(inputActive ? str(setup.inputDeviceName) : std::string(), type ? strings(type->getDeviceNames(true)) : json::Value::array());
		in.set("muted", static_cast<bool>(h->getMuteInputValue().getValue()));
		d.set("input", std::move(in));

		json::Value outs = json::Value::array();
		if(dev)
		{
			const auto names = dev->getOutputChannelNames();
			const auto active = dev->getActiveOutputChannels();
			for(int i = 0; i < names.size(); ++i)
			{
				json::Value c = json::Value::object();
				c.set("name", str(names[i]));
				c.set("on", active[i]);
				outs.push(std::move(c));
			}
		}
		d.set("outputChannels", std::move(outs));

		json::Value rate = json::Value::object(), buffer = json::Value::object();
		json::Value rates = json::Value::array(), buffers = json::Value::array();
		if(dev)
		{
			for(const auto r : dev->getAvailableSampleRates())
				rates.push(r);
			for(const auto b : dev->getAvailableBufferSizes())
				buffers.push(b);
		}
		rate.set("value", dev ? dev->getCurrentSampleRate() : 0.0);
		rate.set("list", std::move(rates));
		buffer.set("value", dev ? dev->getCurrentBufferSizeSamples() : 0);
		buffer.set("list", std::move(buffers));
		d.set("sampleRate", std::move(rate));
		d.set("bufferSize", std::move(buffer));
		const double sr = dev ? dev->getCurrentSampleRate() : 0.0;
		d.set("latencyMs", dev && sr > 0 ? std::round((dev->getOutputLatencyInSamples() + dev->getCurrentBufferSizeSamples()) * 10000.0 / sr) / 10.0 : 0.0);
		d.set("running", dev != nullptr && dev->isPlaying());

		json::Value midiIn = json::Value::array();
		for(const auto& m : juce::MidiInput::getAvailableDevices())
		{
			json::Value v = json::Value::object();
			v.set("id", str(m.identifier));
			v.set("name", str(m.name));
			v.set("on", dm.isMidiInputDeviceEnabled(m.identifier));
			midiIn.push(std::move(v));
		}
		d.set("midiInputs", std::move(midiIn));
		json::Value outList = json::Value::array();
		for(const auto& m : juce::MidiOutput::getAvailableDevices())
		{
			json::Value v = json::Value::object();
			v.set("id", str(m.identifier));
			v.set("name", str(m.name));
			outList.push(std::move(v));
		}
		d.set("midiOutput", named(str(dm.getDefaultMidiOutputIdentifier()), outList));
		d.set("bluetooth", juce::BluetoothMidiDevicePairingDialogue::isAvailable());
		return d;
	}

	// One audioSet: deskHost's table checked the names (set, do) and the argument types; what each
	// setting needs is checked here.
	std::string AudioMidiLink::apply(const json::Value& _c)
	{
		using S = deskHost::AudioSetting;
		using D = deskHost::AudioAction;
		auto* h = holder();
		if(!h)
			return "The host owns audio and MIDI in the plug-in";
		auto& dm = h->deviceManager;
		const auto* device = stringOf(_c, "device");
		const auto* on = _c.find("on");
		const auto* index = _c.find("index");
		const auto* value = _c.find("value");
		const bool onValue = on && (on->isBool() ? on->asBool() : on->isNumber() && on->asNumber() != 0);
		const auto setup = [&](const std::function<void(juce::AudioDeviceManager::AudioDeviceSetup&)>& _f) -> std::string
		{
			auto s = dm.getAudioDeviceSetup();
			_f(s);
			const auto error = dm.setAudioDeviceSetup(s, true);
			h->saveAudioDeviceState();
			return str(error);
		};
		const auto saved = [&]
		{
			h->saveAudioDeviceState();
			return std::string();
		};

		if(const auto* act = stringOf(_c, "do"))
		{
			const auto d = deskHost::audioActionOf(*act);
			if(!d)
				return "audioSet: unknown action " + *act;
			switch(*d)
			{
			case D::Test:
				dm.playTestSound();
				return {};
			case D::Bluetooth:
				return juce::BluetoothMidiDevicePairingDialogue::open() ? std::string() : "Bluetooth MIDI is not available on this computer";
			case D::Count: break;
			}
			return {};
		}
		const auto* what = stringOf(_c, "set");
		if(!what)
			return "audioSet: set or do";
		const auto setting = deskHost::audioSettingOf(*what);
		if(!setting)
			return "audioSet: unknown setting " + *what;
		const auto needs = [&](const char* _args) { return "audioSet " + *what + ": needs " + _args; };
		switch(*setting)
		{
		case S::Driver:
			if(!device)
				return needs("device");
			dm.setCurrentAudioDeviceType(juce::String(*device), true);
			return saved();
		case S::Output:
			if(!device)
				return needs("device");
			return setup([&](auto& _s) { _s.outputDeviceName = juce::String(*device); _s.useDefaultOutputChannels = true; });
		case S::Input:
			if(!device)
				return needs("device");
			return setup([&](auto& _s)
			{
				_s.inputDeviceName = juce::String(*device);
				_s.useDefaultInputChannels = !device->empty();
				if(device->empty())
					_s.inputChannels.clear();
			});
		case S::Mute:
			if(!on)
				return needs("on");
			h->getMuteInputValue().setValue(onValue);
			return saved();
		case S::OutputChannel:
		{
			if(!on || !index)
				return needs("index and on");
			auto* dev = dm.getCurrentAudioDevice();
			const int i = static_cast<int>(index->asNumber());
			if(!dev || i >= dev->getOutputChannelNames().size())
				return "No such output channel";
			auto active = dev->getActiveOutputChannels();
			active.setBit(i, onValue);
			if(active.isZero())
				return "At least one output channel stays on";
			return setup([&](auto& _s) { _s.useDefaultOutputChannels = false; _s.outputChannels = active; });
		}
		case S::SampleRate:
			if(!value)
				return needs("value");
			return setup([&](auto& _s) { _s.sampleRate = value->asNumber(); });
		case S::BufferSize:
			if(!value)
				return needs("value");
			return setup([&](auto& _s) { _s.bufferSize = static_cast<int>(value->asNumber()); });
		case S::MidiInput:
		{
			if(!device || !on)
				return needs("device and on");
			dm.setMidiInputDeviceEnabled(juce::String(*device), onValue);
			saved();
			// B-037: a port the system did not open stays off without a word from JUCE (on Windows: in use elsewhere)
			const auto refused = midiPortRefusal::text(midiPortRefusal::Kind::Input, portName(juce::MidiInput::getAvailableDevices(), *device),
				onValue, dm.isMidiInputDeviceEnabled(juce::String(*device)), g_windows);
			if(!refused.empty() && m_log)
				m_log("MIDI: " + refused);
			return refused;
		}
		case S::MidiOutput:
		{
			if(!device)
				return needs("device");
			dm.setDefaultMidiOutputDevice(juce::String(*device));
			h->player.setMidiOutput(dm.getDefaultMidiOutput());
			saved();
			// B-037: JUCE goes back to no output when the port did not open
			const auto refused = midiPortRefusal::text(midiPortRefusal::Kind::Output, portName(juce::MidiOutput::getAvailableDevices(), *device),
				!device->empty(), dm.getDefaultMidiOutputIdentifier() == juce::String(*device), g_windows);
			if(!refused.empty() && m_log)
				m_log("MIDI: " + refused);
			return refused;
		}
		case S::Count: break;
		}
		return "audioSet: unknown setting " + *what;
	}

	// The window's audio actions (deskHost's table: the arguments are checked there).
	bool AudioMidiLink::handle(const deskHost::Action _action, const json::Value& _message)
	{
		using A = deskHost::Action;
		switch(_action)
		{
		case A::AudioPublish:
			publish();
			m_toPage(deskCore::resultMessage(_message, {}, {}));
			return true;
		case A::AudioMeter:
		{
			const auto* on = _message.find("on");
			m_meter = on && (on->isBool() ? on->asBool() : on->isNumber() && on->asNumber() != 0) && holder();
			if(m_meter && !m_inputLevel)
				m_inputLevel = holder()->deviceManager.getInputLevelGetter();
			if(!m_meter)
				m_inputLevel = nullptr;
			m_toPage(deskCore::resultMessage(_message, {}, {}));
			return true;
		}
		case A::AudioSet:
		{
			// Its error is the result's (one channel); the document is the devices as they are now.
			const auto error = apply(_message);
			m_toPage(deskCore::resultMessage(_message, error.empty() ? std::vector<std::string>{} : std::vector<std::string>{error}, {}));
			publish();
			return true;
		}
		default:
			return false;
		}
	}

	void AudioMidiLink::tick()
	{
		if(!m_meter || !m_inputLevel || ++m_meterTicks % 2)
			return;
		json::Value m = json::Value::object();
		m.set("type", "audioLevel");
		m.set("in", std::round(static_cast<double>(m_inputLevel->getCurrentLevel()) * 1000.0) / 1000.0);
		m_toPage(std::move(m));
	}
}
