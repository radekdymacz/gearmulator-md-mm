#include "mdAudioMidiLink.h"

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
	}

	AudioMidiLink::AudioMidiLink(juce::AudioProcessor& _processor, ToPage _toPage)
		: m_processor(_processor)
		, m_toPage(std::move(_toPage))
	{
		if(auto* h = holder())
		{
			m_listening = &h->deviceManager;
			m_listening->addChangeListener(this);
		}
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
		d.set("error", m_lastError);
		return d;
	}

	std::string AudioMidiLink::apply(const json::Value& _c)
	{
		auto* h = holder();
		if(!h)
			return "The host owns audio and MIDI in the plug-in";
		auto& dm = h->deviceManager;
		const auto* what = stringOf(_c, "set");
		const auto* act = stringOf(_c, "do");
		const auto* id = stringOf(_c, "device");	// "id" is the request's
		const auto* on = _c.find("on");
		const auto* value = _c.find("value");
		const bool onValue = on && on->isBool() && on->asBool();
		const auto setup = [&](const std::function<void(juce::AudioDeviceManager::AudioDeviceSetup&)>& _f) -> std::string
		{
			auto s = dm.getAudioDeviceSetup();
			_f(s);
			const auto error = dm.setAudioDeviceSetup(s, true);
			h->saveAudioDeviceState();
			return str(error);
		};
		if(act && *act == "test")
		{
			dm.playTestSound();
			return {};
		}
		if(act && *act == "bluetooth")
		{
			if(!juce::BluetoothMidiDevicePairingDialogue::open())
				return "Bluetooth MIDI is not available on this computer";
			return {};
		}
		if(!what)
			return "audioSet: set or do";
		if(*what == "driver" && id)
		{
			dm.setCurrentAudioDeviceType(juce::String(*id), true);
			h->saveAudioDeviceState();
			return {};
		}
		if(*what == "output" && id)
			return setup([&](auto& s) { s.outputDeviceName = juce::String(*id); s.useDefaultOutputChannels = true; });
		if(*what == "input" && id)
			return setup([&](auto& s)
			{
				s.inputDeviceName = juce::String(*id);
				s.useDefaultInputChannels = !id->empty();
				if(id->empty())
					s.inputChannels.clear();
			});
		if(*what == "mute" && on && on->isBool())
		{
			h->getMuteInputValue().setValue(onValue);
			h->saveAudioDeviceState();
			return {};
		}
		if(*what == "outputChannel" && on && _c.find("index") && _c.find("index")->isNumber())
		{
			auto* dev = dm.getCurrentAudioDevice();
			const int index = static_cast<int>(_c.find("index")->asNumber());
			if(!dev || index < 0 || index >= dev->getOutputChannelNames().size())
				return "No such output channel";
			auto active = dev->getActiveOutputChannels();
			active.setBit(index, onValue);
			if(active.isZero())
				return "At least one output channel stays on";
			return setup([&](auto& s) { s.useDefaultOutputChannels = false; s.outputChannels = active; });
		}
		if(*what == "sampleRate" && value && value->isNumber())
			return setup([&](auto& s) { s.sampleRate = value->asNumber(); });
		if(*what == "bufferSize" && value && value->isNumber())
			return setup([&](auto& s) { s.bufferSize = static_cast<int>(value->asNumber()); });
		if(*what == "midiInput" && id && on && on->isBool())
		{
			dm.setMidiInputDeviceEnabled(juce::String(*id), onValue);
			h->saveAudioDeviceState();
			return {};
		}
		if(*what == "midiOutput" && id)
		{
			dm.setDefaultMidiOutputDevice(juce::String(*id));
			h->player.setMidiOutput(dm.getDefaultMidiOutput());
			h->saveAudioDeviceState();
			return {};
		}
		return "audioSet: unknown setting " + *what;
	}

	bool AudioMidiLink::handle(const json::Value& _message)
	{
		const auto* op = stringOf(_message, "op");
		if(!op)
			return false;
		if(*op == "audio")
		{
			publish();
			return true;
		}
		if(*op == "audioMeter")
		{
			const auto* on = _message.find("on");
			m_meter = on && on->isBool() && on->asBool() && holder();
			if(m_meter && !m_inputLevel)
				m_inputLevel = holder()->deviceManager.getInputLevelGetter();
			if(!m_meter)
				m_inputLevel = nullptr;
			return true;
		}
		if(*op != "audioSet")
			return false;
		m_lastError = apply(_message);
		m_toPage(deskCore::resultMessage(_message, m_lastError.empty() ? std::vector<std::string>{} : std::vector<std::string>{m_lastError}, {}));
		publish();
		return true;
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
