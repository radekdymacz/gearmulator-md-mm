#include "mdDesk.h"

#include "mdDeskMachine.h"

#include <map>

namespace mdDesk
{
	using Value = elektronData::json::Value;

	Desk::Desk(Port _port, const Profile& _profile) : Desk(defaultAdapter(_profile, _port.device), _port)
	{
	}

	Desk::Desk(std::unique_ptr<MdAdapter> _adapter, Port _port)
		: deskCore::Desk<MdModel, MdAdapter>(std::move(_adapter), _port.toPage, _port.device.nowMs, _port.ready)
		, m_saveSetup(std::move(_port.saveSetup))
	{
	}

	namespace
	{
		const MdMachine* mdMachineOf(const MdAdapter& _a) { return dynamic_cast<const MdMachine*>(&_a); }
	}

	const mdDataLink::Session::State& Desk::linkState() const
	{
		static const mdDataLink::Session::State none;
		const auto* m = mdMachineOf(machine());
		return m ? m->linkState() : none;
	}

	bool Desk::isReady() const
	{
		const auto* m = mdMachineOf(machine());
		return m ? m->replied() : deskCore::takesMidi(lifecycle());
	}

	double Desk::lastRoundTripMs() const
	{
		const auto* m = mdMachineOf(machine());
		return m ? m->lastRoundTripMs() : -1;
	}

	std::unique_ptr<MdAdapter> Desk::defaultAdapter(const Profile& _profile, const DevicePort& _device)
	{
		return std::make_unique<MdMachine>(_profile, _device);
	}

	// ---- the editor's setup: the app modulators and the knob rows ----

	// The table's Owner::Setup rows, one function each.
	const std::map<std::string, void (Desk::*)(const Value&)>& Desk::setups()
	{
		static const std::map<std::string, void (Desk::*)(const Value&)> map{
			{"modSet", &Desk::setModulators}, {"knobs", &Desk::setKnobs},
			{"sampleWave", &Desk::sampleWave}, {"audition", &Desk::audition}, {"auditionStop", &Desk::auditionStop}};
		return map;
	}

	std::vector<std::string> Desk::setupOps()
	{
		std::vector<std::string> ops;
		for(const auto& [op, f] : setups())
			ops.push_back(op);
		return ops;
	}

	void Desk::onSetup(const Value& _message)
	{
		const auto it = setups().find(deskCore::opOf(_message));
		if(it == setups().end())
			result(_message, {"no such setup"}, {});
		else
			(this->*(it->second))(_message);
	}

	// App-only LFO and random sources (deskMod.h): the page sends the whole setup.
	void Desk::setModulators(const Value& _message)
	{
		std::vector<std::string> errors;
		if(const auto setup = modSetupFromJson(*_message.find("doc"), errors, g_mdModLimits))
		{
			m_mods.setSetup(*setup);
			m_setup.modulators = *setup;
			saveSetup();
		}
		result(_message, errors, {});
		publishModulators();
	}

	// The Control workspace's knob rows: which CC each row is.
	void Desk::setKnobs(const Value& _message)
	{
		std::vector<int> ccs;
		for(const auto& v : _message.find("ccs")->asArray())
			ccs.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		auto errors = validateKnobCcs(ccs);
		if(errors.empty())
		{
			for(size_t i = 0; i < m_setup.knobCcs.size() && i < ccs.size(); ++i)
				m_setup.knobCcs[i] = static_cast<uint8_t>(ccs[i]);
			saveSetup();
		}
		result(_message, errors, {});
		publishSetup();
	}

	void Desk::onReadyExtra()
	{
		publishSetup();
		publishModulators();
		publishSamples();
		publishSampleLoad(true);
		if(m_audition)
			publishAudition(true);
	}

	// ---- P9: UW samples ----

	void Desk::tick()
	{
		deskCore::Desk<MdModel, MdAdapter>::tick();
		publishSampleLoad();
		pollAudition();
		flush();
	}

	void Desk::onDeviceSysex(const Bytes& _message)
	{
		deskCore::Desk<MdModel, MdAdapter>::onDeviceSysex(_message);
		if(m_sampleLoad)
		{
			publishSampleLoad();
			flush();
		}
	}

	// The page dropped the samples with every other document; they are the machine's, read again.
	void Desk::onStartOver()
	{
		m_samples.reset();
	}

	void Desk::onSampleBank(const elektronData::MdSampleBank& _bank)
	{
		if(m_samples && *m_samples == _bank)
			return;
		m_samples = _bank;
		publishSamples();
		flush();
	}

	void Desk::publishSamples()
	{
		if(!m_samples)
			return;
		Value m = Value::object();
		m.set("type", "samples");
		m.set("doc", elektronData::mdSampleBankToJson(*m_samples));
		publish(m);
	}

	// The slot a sampleWave or audition names, or why there is none to read.
	const elektronData::MdSampleSlot* Desk::sampleSlotOf(const Value& _message, std::string& _why) const
	{
		const auto caps = machine().capabilities();
		if(!caps.has("sampleAudio"))
		{
			_why = caps.reason("sampleAudio");
			return nullptr;
		}
		if(!m_samples)
		{
			_why = "The samples are not read yet.";
			return nullptr;
		}
		const bool ram = _message.find("bank")->asString() == "ram";
		const auto slot = static_cast<size_t>(_message.find("slot")->asNumber());
		const auto& list = ram ? m_samples->ram : m_samples->rom;
		if(slot >= list.size())
		{
			_why = ram ? "There are four RAM slots (0-3)." : "There are 48 ROM slots (0-47).";
			return nullptr;
		}
		if(ram && !m_samples->ramReadable)
		{
			_why = m_samples->ramReason;
			return nullptr;
		}
		return &list[slot];
	}

	void Desk::sampleWave(const Value& _message)
	{
		std::string why;
		const auto* s = sampleSlotOf(_message, why);
		if(s)
			publish(elektronData::mdSampleWaveMessage(*s, static_cast<size_t>(_message.find("bins")->asNumber())));
		result(_message, s ? std::vector<std::string>{} : std::vector<std::string>{why}, {});
	}

	void Desk::audition(const Value& _message)
	{
		std::string why;
		const auto* s = sampleSlotOf(_message, why);
		if(s && (s->empty || !s->pcm || s->pcm->empty() || !s->rate))
		{
			why = "The slot is empty.";
			s = nullptr;
		}
		uint64_t id = 0;
		if(s && !(id = machine().audition({s->pcm, s->rate})))
			why = "This engine cannot play a sample.";
		if(!id)
		{
			result(_message, {why}, {});
			return;
		}
		m_audition = Audition{s->ram, s->slot, s->length, s->rate, id};
		publishAudition(true);
		result(_message, {}, {});
	}

	void Desk::auditionStop(const Value& _message)
	{
		machine().audition({});
		if(m_audition)
			publishAudition(false);
		m_audition.reset();
		result(_message, {}, {});
	}

	// Event-driven: a message when it starts and one when it stops, never per frame (the page moves
	// its playhead from the rate itself).
	void Desk::pollAudition()
	{
		if(!m_audition)
			return;
		const auto st = machine().auditionStatus();
		if(st.id == m_audition->id && st.playing)
			return;
		publishAudition(false);
		m_audition.reset();
	}

	void Desk::publishAudition(const bool _playing)
	{
		const auto& a = *m_audition;
		Value m = Value::object();
		m.set("type", "audition");
		m.set("state", _playing ? "playing" : "stopped");
		m.set("bank", a.ram ? "ram" : "rom");
		m.set("slot", static_cast<int>(a.slot));
		m.set("length", a.length);
		m.set("rate", a.rate);
		publish(m);
	}

	std::string Desk::loadSample(const uint8_t _slot, const std::string& _fileName, const Bytes& _file)
	{
		namespace ed = elektronData;
		const auto refuse = [&](const std::string& _why)
		{
			Value m = Value::object();
			m.set("type", "sampleLoad");
			m.set("slot", static_cast<int>(_slot));
			m.set("state", "failed");
			m.set("file", _fileName);
			m.set("text", _why);
			publish(m);
			flush();
			return _why;
		};
		if(_slot >= ed::g_mdRomSlots)
			return refuse("A sample goes into a ROM slot (1-48). The RAM slots take only what RAM-R records: "
				"the machine does not answer an SDS sample for them (measured on OS 1.63).");
		if(!isInputReady())
			return refuse(MdModel::lifecycleText(lifecycle()));
		std::string error;
		const auto clip = ed::decodeAudioFile(_file, error);
		if(!clip)
			return refuse(error);
		// The memory left: the emulated machine's bank says what the other slots hold; a real one does not
		// say. One second is kept for each RAM buffer (they share the same memory).
		constexpr uint32_t reserve = 4 * 44100;
		uint32_t others = 0;
		if(m_samples)
			for(const auto& s : m_samples->rom)
				if(s.slot != _slot)
					others += s.length;
		const uint32_t left = ed::g_mdSampleCapacity > others + reserve ? ed::g_mdSampleCapacity - others - reserve : 0;
		auto upload = ed::prepareMdSample(*clip, _fileName, left, error);
		if(!upload)
			return refuse(error);
		if(const auto why = machine().sendSample(_slot, *upload); !why.empty())
			return refuse(why);
		SampleLoad l;
		l.slot = _slot;
		l.file = _fileName;
		l.name = upload->name;
		l.rate = upload->rate;
		l.length = static_cast<uint32_t>(upload->samples.size());
		l.notes = upload->notes;
		if(!m_samples)
			l.notes.push_back("A real Machinedrum does not say how much sample memory is free; if it is full, it keeps the slot as it was.");
		m_sampleLoad = std::move(l);
		publishSampleLoad(true);
		flush();
		return {};
	}

	// The transfer's progress, when it changed (or _force: after a ready).
	void Desk::publishSampleLoad(const bool _force)
	{
		if(!m_sampleLoad)
			return;
		using S = SdsSender::State;
		const auto& p = machine().sampleProgress();
		auto& l = *m_sampleLoad;
		Value m = Value::object();
		m.set("type", "sampleLoad");
		m.set("slot", static_cast<int>(l.slot));
		m.set("state", p.state == S::Sending ? "sending" : p.state == S::Done ? "done" : p.state == S::Cancelled ? "cancelled" : "failed");
		m.set("file", l.file);
		m.set("name", l.name);
		m.set("rate", l.rate);
		m.set("length", l.length);
		m.set("sent", static_cast<int>(p.sent));
		m.set("total", static_cast<int>(p.total));
		m.set("handshake", p.handshake);
		m.set("retries", static_cast<int>(p.retries));
		Value notes = Value::array();
		for(const auto& n : l.notes)
			notes.push(n);
		m.set("notes", notes);
		std::string text = p.text;
		if(p.state == S::Done)
		{
			char t[160];
			std::snprintf(t, sizeof(t), "Sent to ROM-%02d: %s, %.2f s at %u Hz. The machine stores it now.", l.slot + 1, l.name.c_str(),
				l.length / double(l.rate ? l.rate : 1), l.rate);
			text = t;
		}
		m.set("text", text);
		// At most a message a per cent.
		const int key = static_cast<int>(p.state) * 1000 + static_cast<int>(p.total ? p.sent * 100 / p.total : 0);
		if(!_force && key == l.last)
			return;
		l.last = key;
		publish(m);
		if(p.state != S::Sending)
			m_sampleLoad.reset();
	}

	std::vector<std::string> Desk::loadSetup(const Value& _setup)
	{
		std::vector<std::string> errors;
		const auto s = deskSetupFromJson(_setup, errors);
		if(!s)
			return errors;
		m_setup = *s;
		m_mods.setSetup(m_setup.modulators);
		publishSetup();
		publishModulators();
		return errors;
	}

	void Desk::publishSetup()
	{
		Value m = Value::object();
		m.set("type", "setup");
		m.set("doc", deskSetupToJson(m_setup));
		publish(m);
	}

	void Desk::saveSetup() const
	{
		if(m_saveSetup)
			m_saveSetup(deskSetupToJson(m_setup));
	}

	void Desk::publishModulators()
	{
		publish(m_mods.message(g_mdModLimits, now()));
	}


	// App modulators move on the machine's own steps.
	void Desk::runModulators()
	{
		const auto& t = machine().telemetry();
		if(auto m = m_mods.step(machine(), documents(), g_mdModLimits, t.step, t.playing, now()))
			publish(*m);
	}

	// ---- device facts -> the adapter ----

	void Desk::onHostKitParam(const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		machine().onHostKitParam(_track, _index, _value, documents());
		flush();
	}

	void Desk::onHostMute(const uint8_t _track, const bool _muted)
	{
		machine().onHostMute(_track, _muted);
	}

	void Desk::onTelemetry(const Telemetry& _telemetry)
	{
		const auto e = machine().onTelemetry(_telemetry);
		if(e.stepped)
			runModulators();
		publishSampleLoad();
		pollAudition();
		flush();
	}

	void Desk::onWorkingKitMemory(const Bytes& _region)
	{
		machine().onWorkingKitMemory(_region, documents());
		flush();
	}

	void Desk::setProbe(const Probe _probe)
	{
		machine().setProbe(_probe);
		flush();
	}
}
