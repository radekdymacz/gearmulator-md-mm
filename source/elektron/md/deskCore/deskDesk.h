#pragma once

#include "deskCore.h"
#include "deskLcd.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace deskCore
{
	// One editor behind its page (P6): the core, one machine adapter (an engine of the model's
	// engine map) and the one router that reads the model's command table. The same for the
	// Machinedrum and the Monomachine; a model adds only what is its own (the MD's setup).
	//
	// Adapter is the model's adapter interface: Machine<Model> plus the model's device facts
	// (telemetry, memory, host parameters). Which class implements it is the engine's choice: the
	// desk holds whatever adapter it is given (setEngine), so an engine with its own protocol is
	// its own adapter class, not a new desk. Model provides: commands() (the table), catalogue()
	// (the document the page gets on ready), lifecycleText(lifecycle) (what the state means for the
	// user; also why a gated command waits).
	// Single threaded.
	template<typename Model, typename Adapter>
	class Desk
	{
	public:
		using Documents = typename Model::Documents;

		// _ready: called after every ready of a page (the plug-in publishes its own documents then).
		Desk(std::unique_ptr<Adapter> _adapter, std::function<void(const Value&)> _toPage, std::function<double()> _nowMs,
			std::function<void()> _ready = {})
			: m_nowMs(std::move(_nowMs))
			, m_ready(std::move(_ready))
			, m_core(std::move(_toPage))
		{
			m_core.setOnStartOver([this] { onStartOver(); });
			setEngine(std::move(_adapter));
		}
		virtual ~Desk() = default;

		Desk(const Desk&) = delete;
		Desk& operator=(const Desk&) = delete;

		// A page message, routed by the model's command table. False for an op the model does not
		// know (the plug-in's own table has it, or nobody does): the caller answers it.
		bool onPageMessage(const Value& _message)
		{
			const auto* spec = Model::commands().find(opOf(_message));
			if(!spec)
				return false;
			const auto lc = m_machine->lifecycle();
			const bool open = spec->gate == Gate::None || (spec->gate == Gate::Midi ? takesMidi(lc) : takesInput(lc));
			if(!open)
				m_core.result(_message, {Model::lifecycleText(lc)}, {});
			else if(const auto errors = Model::Table::check(*spec, _message); !errors.empty())
				m_core.result(_message, errors, {});
			else
			{
				switch(spec->owner)
				{
				case Owner::Core:
					switch(spec->core)
					{
					case CoreOp::Ready: onReady(_message); break;
					case CoreOp::Undo: m_core.undo(_message); break;
					case CoreOp::Redo: m_core.redo(_message); break;
					case CoreOp::Edit: m_core.edit(_message); break;
					case CoreOp::Set: m_core.set(_message); break;
					}
					break;
				case Owner::Machine: m_core.onMachineCommand(_message); break;
				case Owner::Setup: onSetup(_message); break;
				default: m_core.result(_message, {"not a desk command"}, {}); break;
				}
			}
			flush();
			return true;
		}

		// An engine of the model's engine map: its adapter; the documents start over. The desk owns
		// the adapter from here; the old one is gone when this returns.
		void setEngine(std::unique_ptr<Adapter> _adapter)
		{
			m_core.setMachine(_adapter.get());
			m_machine = std::move(_adapter);
			if(m_core.pageReadyNow())
			{
				m_core.startOver();
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
			if(m_sysexTap)
				m_sysexTap(_message);
		}

		// B-019: every SysEx the machine sends, after the adapter had it (a .syx import's read-back). Unset: none.
		void setSysexTap(std::function<void(const Bytes&)> _tap) { m_sysexTap = std::move(_tap); }

		void tick()
		{
			m_machine->tick(m_nowMs(), m_core.view());
			flush();
		}

		// The machine's own screen while it starts (a device fact): the page's LCD until it takes input.
		void showLcd(const std::vector<uint8_t>& _bits)
		{
			if(!isInputReady())
				m_core.publish(lcdMessage(_bits));
			flush();
		}

		// The page went away (the editor window closed): nothing is published until the next ready.
		void detachPage() { m_core.detach(); }

		void flush()
		{
			m_core.pump();
			m_core.flush();
		}

		Adapter& machine() { return *m_machine; }
		const Adapter& machine() const { return *m_machine; }
		const Core<Model>& coreState() const { return m_core; }
		const Documents& documents() const { return m_core.view(); }
		Lifecycle lifecycle() const { return m_machine->lifecycle(); }
		bool isInputReady() const { return takesInput(lifecycle()); }
		// What the adapter is doing, for the diagnostics line.
		Value status() const { return m_machine->status(); }
		// A page has been up: the machine is polled and read from then on.
		bool pageSeen() const { return m_core.pageSeen(); }

	protected:
		virtual void onSetup(const Value& _message) { m_core.result(_message, {"no setup here"}, {}); }
		// The page (re)attached: after the core's own publishing, what the model adds.
		virtual void onReadyExtra() {}
		// The machine started over (a reset: the page drops every document): what the model holds beside the
		// core's documents is not known any more either.
		virtual void onStartOver() {}
		void publish(const Value& _message) { m_core.publish(_message); }
		void result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note)
		{
			m_core.result(_message, _errors, _note);
		}
		double now() const { return m_nowMs(); }
		Core<Model>& core() { return m_core; }

	private:
		void onReady(const Value& _message)
		{
			m_core.pageReady();
			Value cat = Value::object();
			cat.set("type", "catalogue");
			cat.set("doc", Model::catalogue());
			m_core.publish(cat);
			onReadyExtra();
			if(m_ready)
				m_ready();
			if(_message.isObject())
				m_core.result(_message, {}, {});
		}

		std::function<double()> m_nowMs;
		std::function<void()> m_ready;
		std::function<void(const Bytes&)> m_sysexTap;
		Core<Model> m_core;
		std::unique_ptr<Adapter> m_machine;
	};
}
