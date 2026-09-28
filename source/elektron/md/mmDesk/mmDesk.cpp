#include "mmDesk.h"

#include "elektronData/mmMachines.h"

namespace mmDesk
{
	namespace ed = elektronData;
	using ed::json::Value;

	namespace
	{
		Value strings(const std::vector<std::string>& _v)
		{
			Value a = Value::array();
			for(const auto& s : _v)
				a.push(s);
			return a;
		}
	}

	Desk::Desk(Port _port)
		: m_port(std::move(_port))
		, m_core([this](const Value& _m)
		{
			if(m_port.toPage)
				m_port.toPage(_m);
		})
	{
		m_machine = std::make_unique<MmMachine>(*profile("emu"), devicePort());
		m_core.setMachine(m_machine.get());
	}

	MmMachine::Port Desk::devicePort() const
	{
		MmMachine::Port p;
		p.sendSysex = m_port.sendSysex;
		p.sendParam = m_port.sendParam;
		p.sendNrpn = m_port.sendNrpn;
		p.pressKeys = m_port.pressKeys;
		p.nowMs = m_port.nowMs;
		return p;
	}

	Value Desk::catalogue()
	{
		Value c = Value::object();
		c.set("schema", "mm-desk/catalogue");
		c.set("version", 1);
		Value machines = Value::array();
		for(const auto& mi : ed::mmMachines())
		{
			Value m = Value::object();
			m.set("id", mi.id);
			m.set("name", mi.name);
			m.set("family", mi.family);
			Value syn = Value::array();
			Value enums = Value::object();
			for(uint8_t i = 0; i < 8; ++i)
			{
				syn.push(mi.synth[i]);
				const auto e = ed::mmSynthEnum(mi.id, i);
				if(!e.empty())
					enums.set(std::to_string(i), strings(e));
			}
			m.set("synth", std::move(syn));
			m.set("enums", std::move(enums));
			m.set("fx", mi.fx);
			m.set("mk2", mi.mk2);
			machines.push(std::move(m));
		}
		c.set("machines", std::move(machines));
		Value pages = Value::array();
		for(uint8_t pg = 1; pg < 8; ++pg)
		{
			Value names = Value::array();
			for(const auto* n : ed::mmFixedPage(pg))
				names.push(n);
			pages.push(std::move(names));
		}
		c.set("fixedPages", std::move(pages));
		Value lfo = Value::object();
		lfo.set("pages", strings(ed::mmLfoPages()));
		lfo.set("trigs", strings(ed::mmLfoTrigs()));
		lfo.set("waves", strings(ed::mmLfoWaves()));
		lfo.set("mults", strings(ed::mmLfoMults()));
		lfo.set("pitchDests", strings(ed::mmPitchDests()));
		c.set("lfo", std::move(lfo));
		c.set("enumRule", "index = floor(value * n / 128); value = ceil(index * 128 / n)");
		return c;
	}

	bool Desk::setEngineId(const std::string& _id, const std::optional<Port>& _port)
	{
		const auto* p = profile(_id);
		if(!p)
			return false;
		if(_port)
			m_port = *_port;
		if(p->id == m_machine->profile().id && !_port)
			return true;
		m_core.setMachine(nullptr);
		m_machine = std::make_unique<MmMachine>(*p, devicePort());
		const auto engine = m_engine;
		m_engine = Engine::Missing;
		setEngine(engine);
		m_core.setMachine(m_machine.get());
		if(m_pageReady)
		{
			Value reset = Value::object();
			reset.set("type", "reset");
			m_core.publish(reset);
			onReady();
			flush();
		}
		return true;
	}

	void Desk::setEngine(const Engine _engine)
	{
		m_engine = _engine;
		using F = MmMachine::Firmware;
		switch(_engine)
		{
		case Engine::Missing: m_machine->setFirmware(F::Missing); break;
		case Engine::Unsupported: m_machine->setFirmware(F::Unsupported); break;
		case Engine::Loading: m_machine->setFirmware(F::Loading); break;
		case Engine::Booting: m_machine->setFirmware(F::Booting); break;
		case Engine::Ready: m_machine->setFirmware(F::Ready); break;
		}
	}

	Desk::Engine Desk::engine() const
	{
		switch(m_machine->lifecycle())
		{
		case deskCore::Lifecycle::Missing: return Engine::Missing;
		case deskCore::Lifecycle::Unsupported: return Engine::Unsupported;
		case deskCore::Lifecycle::Loading: return Engine::Loading;
		case deskCore::Lifecycle::Ready:
		case deskCore::Lifecycle::HwLost: return Engine::Ready;
		default: return Engine::Booting;
		}
	}

	std::optional<ed::MmPattern> Desk::pattern(const uint8_t _slot) const
	{
		const auto& p = m_core.view().patterns;
		const auto it = p.find(_slot & 127);
		return it == p.end() ? std::nullopt : std::optional<ed::MmPattern>(it->second);
	}

	std::optional<ed::MmSong> Desk::song(const uint8_t _slot) const
	{
		const auto& s = m_core.view().songs;
		const auto it = s.find(_slot % 24);
		return it == s.end() ? std::nullopt : std::optional<ed::MmSong>(it->second);
	}

	std::optional<ed::MmGlobal> Desk::global(const uint8_t _slot) const
	{
		const auto& g = m_core.view().globals;
		const auto it = g.find(_slot & 7);
		return it == g.end() ? std::nullopt : std::optional<ed::MmGlobal>(it->second);
	}

	// ---- page ----

	bool Desk::onPageMessage(const Value& _msg)
	{
		const auto op = deskCore::opOf(_msg);
		const auto* spec = commandTable().find(op);
		if(!spec)
		{
			m_core.result(_msg, {"unknown command " + op}, {});
			flush();
			return true;
		}
		if(spec->owner == deskCore::Owner::Host)
			return false;
		if(spec->gate != deskCore::Gate::None && !m_machine->ready())
		{
			m_core.result(_msg, {"The engine is not ready yet."}, {});
			flush();
			return true;
		}
		if(const auto errors = deskCore::CommandTable::check(*spec, _msg); !errors.empty())
		{
			m_core.result(_msg, errors, {});
			flush();
			return true;
		}
		if(op == "ready")
		{
			onReady();
			m_core.result(_msg, {}, {});
		}
		else if(spec->owner == deskCore::Owner::Core)
			m_core.onCommand(_msg);
		else if(spec->owner == deskCore::Owner::Machine)
			m_core.onMachineCommand(_msg);
		flush();
		return true;
	}

	void Desk::onReady()
	{
		m_pageReady = true;
		m_core.pageReady();
		Value cat = Value::object();
		cat.set("type", "catalogue");
		cat.set("doc", catalogue());
		m_core.publish(cat);
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

	// ---- machine ----

	void Desk::onDeviceSysex(const Bytes& _m)
	{
		m_machine->onSysex(_m);
		flush();
	}

	void Desk::onTelemetry(const Telemetry& _t)
	{
		m_machine->onTelemetry(_t);
	}

	void Desk::onWorkingKit(const Bytes& _region)
	{
		m_machine->onWorkingKit(_region);
	}

	void Desk::tick()
	{
		m_machine->tick(m_port.nowMs(), m_core.view());
		flush();
	}
}
