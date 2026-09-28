#include "mdDesk.h"

#include "elektronData/mdMachines.h"

namespace mdDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;

	Desk::Desk(Port _port)
		: m_port(std::move(_port))
		, m_core([this](const Value& _m)
		{
			if(m_port.toPage)
				m_port.toPage(_m);
		})
	{
		m_machine = std::make_unique<MdMachine>(*profile("emu"), devicePort());
		m_core.setMachine(m_machine.get());
	}

	MdMachine::Port Desk::devicePort() const
	{
		MdMachine::Port p;
		p.sendSysex = m_port.sendSysex;
		p.sendKitParam = m_port.sendKitParam;
		p.sendMute = m_port.sendMute;
		p.pressKey = m_port.pressKey;
		p.turnKnob = m_port.turnKnob;
		p.nowMs = m_port.nowMs;
		return p;
	}

	Value Desk::machineCatalogue()
	{
		Value machines = Value::array();
		for(uint32_t model = 0; model < 256; ++model)
		{
			const auto name = ed::mdMachineName(model);
			if(name.empty())
				continue;
			Value m = Value::object();
			m.set("model", static_cast<int>(model));
			m.set("machine", name);
			m.set("family", ed::mdMachineFamily(model));
			Value params = Value::array();
			for(const auto* p : ed::mdMachineParamNames(model))
				params.push(p && *p ? Value(p) : Value());
			m.set("params", std::move(params));
			machines.push(std::move(m));
		}
		Value doc = Value::object();
		doc.set("schema", "md-desk/machines");
		doc.set("version", 1);
		doc.set("machines", std::move(machines));
		return doc;
	}

	bool Desk::setEngine(const std::string& _id, const std::optional<Port>& _port)
	{
		const auto* p = profile(_id);
		if(!p)
			return false;
		if(_port)
			m_port = *_port;
		if(p->id == m_machine->profile().id && !_port)
			return true;
		// A new adapter: what the old one knew is not true of the new engine.
		m_core.setMachine(nullptr);
		m_machine = std::make_unique<MdMachine>(*p, devicePort());
		m_core.setMachine(m_machine.get());
		if(m_pageReady)
		{
			Value reset = Value::object();
			reset.set("type", "reset");
			m_core.publish(reset);
			onReady();
		}
		return true;
	}

	// ---- page -> desk ----

	bool Desk::gate(const deskCore::Command& _spec, const Value& _message)
	{
		using deskCore::Lifecycle;
		const auto lc = lifecycle();
		const auto refuse = [&](const char* _why)
		{
			m_core.result(_message, {_why}, {});
			return false;
		};
		if(_spec.gate == deskCore::Gate::None)
			return true;
		if(lc == Lifecycle::Missing || lc == Lifecycle::Unsupported)
			return refuse("No Machinedrum firmware is running");
		if(_spec.gate == deskCore::Gate::Midi ? !deskCore::takesMidi(lc) : !deskCore::takesInput(lc))
			return refuse(lc == Lifecycle::Animating ? "The machine is still starting: its start-up animation ignores keys. The editor"
				" takes input when it is over." : "The machine is starting (device busy). Try again in a moment.");
		return true;
	}

	bool Desk::onPageMessage(const Value& _message)
	{
		const auto op = deskCore::opOf(_message);
		const auto* spec = commandTable().find(op);
		if(!spec)
		{
			m_core.result(_message, {"unknown command " + op}, {});
			flush();
			return true;
		}
		if(spec->owner == deskCore::Owner::Host)
			return false;
		if(!gate(*spec, _message))
		{
			flush();
			return true;
		}
		if(const auto errors = deskCore::CommandTable::check(*spec, _message); !errors.empty())
		{
			m_core.result(_message, errors, {});
			flush();
			return true;
		}
		switch(spec->owner)
		{
		case deskCore::Owner::Core:
			if(op == "ready")
				onReady();
			else
				m_core.onCommand(_message);
			break;
		case deskCore::Owner::Machine:
			m_core.onMachineCommand(_message);
			break;
		case deskCore::Owner::Setup:
			onSetup(_message);
			break;
		case deskCore::Owner::Host:
			break;
		}
		flush();
		return true;
	}

	void Desk::onReady()
	{
		m_pageReady = true;
		m_core.pageReady();
		Value cat = Value::object();
		cat.set("type", "catalogue");
		cat.set("doc", machineCatalogue());
		m_core.publish(cat);
		publishSetup();
		publishModulators();
	}

	void Desk::detachPage()
	{
		m_pageReady = false;
		m_core.detach();
	}

	void Desk::flush()
	{
		m_core.pump();
		m_core.flush();
	}

	// ---- the editor's setup: the app modulators and the knob rows ----

	void Desk::onSetup(const Value& _message)
	{
		const auto op = deskCore::opOf(_message);
		std::vector<std::string> errors;
		if(op == "modSet")
		{
			// App-only LFO and random sources (mdDeskMod.h): the page sends the whole setup.
			const auto setup = modSetupFromJson(*_message.find("doc"), errors);
			if(setup)
			{
				m_mods.setSetup(*setup);
				m_setup.modulators = *setup;
				saveSetup();
			}
			m_core.result(_message, errors, {});
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
		m_core.result(_message, errors, {});
		publishSetup();
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
		m_core.publish(m);
	}

	void Desk::saveSetup() const
	{
		if(m_port.saveSetup)
			m_port.saveSetup(deskSetupToJson(m_setup));
	}

	void Desk::publishModulators()
	{
		Value m = Value::object();
		m.set("type", "mod");
		m.set("doc", modSetupToJson(m_mods.setup()));
		Value values = Value::array();
		for(const auto v : m_mods.values())
			values.push(v);
		m.set("values", std::move(values));
		m.set("ccPerSecond", m_mods.ccPerSecond(m_port.nowMs()));
		// The desk is the plug-in's (its processor owns it): the modulators move with the editor closed.
		m.set("runs", "plug-in");
		m.set("ccLimit", g_modCcPerSecond);
		m_core.publish(m);
	}

	// App modulators move on the machine's own steps.
	void Desk::runModulators()
	{
		const auto& t = m_machine->telemetry();
		for(const auto& o : m_mods.onPlayhead(t.step, t.playing, m_port.nowMs()))
			m_machine->sendModulation(o.track, o.param, o.value, m_core.view());
		publishModulators();
	}

	// ---- device -> desk ----

	void Desk::onDeviceSysex(const Bytes& _message)
	{
		m_machine->onSysex(_message);
		flush();
	}

	void Desk::onHostKitParam(const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		m_machine->onHostKitParam(_track, _index, _value, m_core.view());
		m_core.pump();
	}

	void Desk::onHostMute(const uint8_t _track, const bool _muted)
	{
		m_machine->onHostMute(_track, _muted);
	}

	void Desk::onTelemetry(const Telemetry& _telemetry)
	{
		const auto e = m_machine->onTelemetry(_telemetry, m_core.view());
		if(e.stepped)
			runModulators();
		m_core.pump();
	}

	void Desk::onWorkingKitMemory(const Bytes& _region)
	{
		m_machine->onWorkingKitMemory(_region);
		flush();
	}

	void Desk::setFirmware(const Firmware _firmware)
	{
		m_machine->setFirmware(_firmware);
		flush();
	}

	void Desk::tick()
	{
		m_machine->tick(m_port.nowMs(), m_core.view());
		flush();
	}
}
