#include "mdDesk.h"

#include "mdDeskMachine.h"

namespace mdDesk
{
	using Value = elektronData::json::Value;

	Desk::Desk(Port _port, const Profile& _profile) : Desk(defaultAdapter(_profile, _port.device), _port)
	{
	}

	Desk::Desk(std::unique_ptr<MdAdapter> _adapter, Port _port)
		: deskCore::Desk<MdModel, MdAdapter>(std::move(_adapter), _port.toPage, _port.device.nowMs)
		, m_saveSetup(std::move(_port.saveSetup))
	{
	}

	std::unique_ptr<MdAdapter> Desk::defaultAdapter(const Profile& _profile, const DevicePort& _device)
	{
		return std::make_unique<MdMachine>(_profile, _device);
	}

	// ---- the editor's setup: the app modulators and the knob rows ----

	void Desk::onSetup(const Value& _message)
	{
		const auto op = deskCore::opOf(_message);
		std::vector<std::string> errors;
		if(op == "modSet")
		{
			// App-only LFO and random sources (mdDeskMod.h): the page sends the whole setup.
			const auto setup = modSetupFromJson(*_message.find("doc"), errors, g_mdModLimits);
			if(setup)
			{
				m_mods.setSetup(*setup);
				m_setup.modulators = *setup;
				saveSetup();
			}
			result(_message, errors, {});
			publishModulators();
			return;
		}
		// The Control workspace's knob rows: which CC each of the eight rows is.
		std::vector<int> ccs;
		for(const auto& v : _message.find("ccs")->asArray())
			ccs.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		errors = validateKnobCcs(ccs);
		if(errors.empty())
		{
			for(size_t i = 0; i < 8; ++i)
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
		Value m = Value::object();
		m.set("type", "mod");
		m.set("doc", modSetupToJson(m_mods.setup(), g_mdModLimits));
		Value values = Value::array();
		for(const auto v : m_mods.values())
			values.push(v);
		m.set("values", std::move(values));
		m.set("ccPerSecond", m_mods.ccPerSecond(now()));
		// The desk is the plug-in's (its session owns it): the modulators move with the editor closed.
		m.set("runs", "plug-in");
		m.set("ccLimit", g_modCcPerSecond);
		publish(m);
	}

	// App modulators move on the machine's own steps.
	void Desk::runModulators()
	{
		const auto& t = machine().telemetry();
		for(const auto& o : m_mods.onPlayhead(t.step, t.playing, now()))
			machine().sendModulation(o.track, o.param, o.value, documents());
		publishModulators();
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
