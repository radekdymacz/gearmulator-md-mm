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
			{"modSet", &Desk::setModulators}, {"knobs", &Desk::setKnobs}};
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
