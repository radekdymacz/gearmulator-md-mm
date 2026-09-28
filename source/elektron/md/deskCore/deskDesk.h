#pragma once

#include "deskCore.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace deskCore
{
	// One editor behind its page (P6): the core, one machine adapter (an engine from the model's
	// engine map) and the one router that reads the model's command table. The same for the
	// Machinedrum and the Monomachine; a model adds only what is its own (the MD's setup).
	//
	// MachineT is the model's adapter (Machine<Model> plus the model's device facts); it is made
	// from (profile, device port). Model provides: commands() (the table), catalogue() (the
	// document the page gets on ready), refusal(lifecycle) (why a gated command waits).
	// Single threaded.
	template<typename Model, typename MachineT>
	class Desk
	{
	public:
		using Profile = typename MachineT::Profile;
		using DevicePort = typename MachineT::Port;
		using Documents = typename Model::Documents;

		Desk(Profile _profile, DevicePort _device, std::function<void(const Value&)> _toPage, std::function<double()> _nowMs)
			: m_toPage(std::move(_toPage))
			, m_nowMs(std::move(_nowMs))
			, m_core([this](const Value& _m)
			{
				if(m_toPage)
					m_toPage(_m);
			})
		{
			setEngine(std::move(_profile), std::move(_device));
		}
		virtual ~Desk() = default;

		Desk(const Desk&) = delete;
		Desk& operator=(const Desk&) = delete;

		// A page message, routed by the command table. False for a host command (the plug-in's),
		// which the caller handles; the table's row says which (hostOp()).
		bool onPageMessage(const Value& _message)
		{
			const auto op = opOf(_message);
			const auto* spec = Model::commands().find(op);
			if(!spec)
			{
				m_core.result(_message, {"unknown command " + op}, {});
				flush();
				return true;
			}
			if(spec->owner == Owner::Host)
				return false;
			const auto lc = m_machine->lifecycle();
			const bool open = spec->gate == Gate::None || (spec->gate == Gate::Midi ? takesMidi(lc) : takesInput(lc));
			if(!open)
			{
				m_core.result(_message, {Model::refusal(lc)}, {});
				flush();
				return true;
			}
			if(const auto errors = Model::Table::check(*spec, _message); !errors.empty())
			{
				m_core.result(_message, errors, {});
				flush();
				return true;
			}
			switch(spec->owner)
			{
			case Owner::Core:
				if(op == "ready")
					onReady(_message);
				else
					m_core.onCommand(_message);
				break;
			case Owner::Machine: m_core.onMachineCommand(_message); break;
			case Owner::Setup: onSetup(_message); break;
			case Owner::Host: break;
			}
			flush();
			return true;
		}

		static HostOp hostOp(const Value& _message)
		{
			const auto* spec = Model::commands().find(opOf(_message));
			return spec && spec->owner == Owner::Host ? spec->host : HostOp::None;
		}

		// An engine from the model's engine map: a new adapter; the documents start over.
		void setEngine(Profile _profile, DevicePort _device)
		{
			m_core.setMachine(nullptr);
			m_machine = std::make_unique<MachineT>(std::move(_profile), std::move(_device));
			m_core.setMachine(m_machine.get());
			if(m_pageReady)
			{
				Value reset = Value::object();
				reset.set("type", "reset");
				m_core.publish(reset);
				onReady({});
				flush();
			}
		}

		// The engine map's entries (published as machine.engines).
		void setEngines(std::vector<EngineChoice> _engines) { m_core.setEngines(std::move(_engines)); }

		void onDeviceSysex(const Bytes& _message)
		{
			m_machine->onSysex(_message);
			flush();
		}

		void tick()
		{
			m_machine->tick(m_nowMs(), m_core.view());
			flush();
		}

		// The page went away (the editor window closed): nothing is published until the next ready.
		void detachPage()
		{
			m_pageReady = false;
			m_core.detach();
		}

		void flush()
		{
			m_core.pump();
			m_core.flush();
		}

		MachineT& machine() { return *m_machine; }
		const MachineT& machine() const { return *m_machine; }
		const Core<Model>& coreState() const { return m_core; }
		const Documents& documents() const { return m_core.view(); }
		Lifecycle lifecycle() const { return m_machine->lifecycle(); }
		bool isInputReady() const { return takesInput(lifecycle()); }
		const std::string& engine() const { return m_machine->profile().id; }

	protected:
		virtual void onSetup(const Value& _message) { m_core.result(_message, {"no setup here"}, {}); }
		// The page (re)attached: after the core's own publishing, what the model adds.
		virtual void onReadyExtra() {}
		void publish(const Value& _message) const { m_core.publish(_message); }
		void result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note) const
		{
			m_core.result(_message, _errors, _note);
		}
		double now() const { return m_nowMs(); }
		Core<Model>& core() { return m_core; }

	private:
		void onReady(const Value& _message)
		{
			m_pageReady = true;
			m_core.pageReady();
			Value cat = Value::object();
			cat.set("type", "catalogue");
			cat.set("doc", Model::catalogue());
			m_core.publish(cat);
			onReadyExtra();
			if(_message.isObject())
				m_core.result(_message, {}, {});
		}

		std::function<void(const Value&)> m_toPage;
		std::function<double()> m_nowMs;
		Core<Model> m_core;
		std::unique_ptr<MachineT> m_machine;
		bool m_pageReady = false;
	};
}
