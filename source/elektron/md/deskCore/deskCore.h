#pragma once

#include "deskCapabilities.h"
#include "deskCommands.h"
#include "deskHistory.h"
#include "deskLifecycle.h"

#include "elektronData/json.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace deskCore
{
	using Value = elektronData::json::Value;
	using Bytes = std::vector<uint8_t>;

	// Where a document's observed value came from.
	enum class Source : uint8_t
	{
		None,
		Dump,		// a dump read back from the machine
		Memory,		// the machine's memory (the working kit that plays)
		Tracked		// what the adapter sent and saw (no read-back exists for it)
	};

	inline const char* sourceName(const Source _s)
	{
		switch(_s)
		{
		case Source::Dump: return "dump";
		case Source::Memory: return "memory";
		case Source::Tracked: return "tracked";
		default: return "none";
		}
	}

	// One document's state (P6): what the machine was last seen to hold, and what was
	// submitted and not yet seen. The page is shown pending ? pending : observed.
	template<typename Doc>
	struct DocState
	{
		std::optional<Doc> observed;
		std::optional<Doc> pending;
		Source source = Source::None;

		const Doc* view() const { return pending ? &*pending : observed ? &*observed : nullptr; }
	};

	// What an adapter answers a command or a delivery with.
	struct Outcome
	{
		std::vector<std::string> errors;	// non-empty: refused
		std::string note;					// one line for the user
		std::optional<Value> ask;			// a question for the user: nothing was done
	};

	// What an adapter tells the core, drained after every call into it (no callbacks
	// cross the seam).
	template<typename Doc, typename Ref>
	struct Event
	{
		enum class Kind : uint8_t
		{
			Observed,	// the machine holds `doc`
			Forget,		// nothing is known about `ref` any more (it will be read again)
			Settled,	// a submitted change of `ref` is no longer in flight: ok with the value the machine now
						// holds (doc, when it is known), or failed with `message`
			Notice,		// a message for the page as it is (telemetry, a failure)
			Reset		// the machine started over (a reboot, a restored project): nothing it held before is
						// known any more, and undo steps no longer apply to it
		};

		Kind kind = Kind::Notice;
		std::optional<Doc> doc;
		Source source = Source::None;
		Ref ref{};
		bool ok = true;
		std::string message;
		Value notice;

		static Event observed(Doc _doc, const Source _source)
		{
			Event e;
			e.kind = Kind::Observed;
			e.doc = std::move(_doc);
			e.source = _source;
			return e;
		}
		static Event forget(const Ref& _ref)
		{
			Event e;
			e.kind = Kind::Forget;
			e.ref = _ref;
			return e;
		}
		// The change reached the machine and it holds _doc now (a read-back, or what a live edit
		// sent when nothing reads it back: _source says which).
		static Event settledWith(Doc _doc, const Source _source, const Ref& _ref)
		{
			Event e;
			e.kind = Kind::Settled;
			e.ref = _ref;
			e.doc = std::move(_doc);
			e.source = _source;
			return e;
		}
		// The change did not reach the machine (refused, no read-back, the machine holds something
		// else): the view falls back to what was last observed.
		static Event failed(const Ref& _ref, std::string _message)
		{
			Event e;
			e.kind = Kind::Settled;
			e.ref = _ref;
			e.ok = false;
			e.message = std::move(_message);
			return e;
		}
		static Event noticeOf(Value _message)
		{
			Event e;
			e.notice = std::move(_message);
			return e;
		}
		static Event reset()
		{
			Event e;
			e.kind = Kind::Reset;
			return e;
		}
	};

	// The Machine adapter protocol (P6): the one seam between the engine-neutral core and
	// an engine (the emulated firmware, a machine over HW MIDI, our own engine later).
	// Everything that crosses it is a value: changes, commands and documents in, events,
	// the machine's state, its lifecycle and capabilities out. Model-specific facts
	// (telemetry, memory, host parameters) go to the model's own adapter base class.
	template<typename Model>
	class Machine
	{
	public:
		using Doc = typename Model::Document;
		using Ref = typename Model::Ref;
		using Change = typename Model::Change;
		using Documents = typename Model::Documents;
		using Context = typename Model::Context;
		using Ev = Event<Doc, Ref>;

		virtual ~Machine() = default;

		// Before a command's changes are delivered: refuse them (errors) or ask first (ask).
		virtual Outcome review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view) = 0;
		// Deliver one change. Errors refuse it; nothing was sent then.
		virtual Outcome submit(const Change& _change, const Documents& _view) = 0;
		// A machine-owned command (the command table's Owner::Machine).
		virtual Outcome command(const Value& _command, const Documents& _view) = 0;
		// Before a machine command runs: a question for the user (ask), or nothing. The core asks
		// unless the command carries force (the user's answer); the adapter never reads force.
		virtual Outcome askFor(const Value& _command, const Documents& _view) { (void)_command; (void)_view; return {}; }
		virtual void onSysex(const Bytes& _message) = 0;
		virtual void tick(double _nowMs, const Documents& _view) = 0;
		// The page (re)attached: ask the machine what it holds now.
		virtual void pageReady() {}

		virtual std::vector<Ev> drain() = 0;
		// The machine document (the model's schema) without the core's parts.
		virtual Value state(const Documents& _view) const = 0;
		virtual Capabilities capabilities() const = 0;
		virtual Lifecycle lifecycle() const = 0;
		virtual Context context() const = 0;
		// Something is on the wire (the page's TX LED).
		virtual bool busy() const = 0;
	};

	// The engine-neutral core (P6): documents as observed + pending, pure edits (the
	// model's apply), undo, the clipboard, and publishing the merged view. It knows
	// nothing about SysEx, keys, RAM or the wire.
	//
	// Model provides: Kind, Ref, Document (variant), Documents (the typed view apply
	// reads), Change {before, after, ref()}, Clipboard, Context, EditResult {changes,
	// errors, note, optional<Clipboard> clipboard}, and the static functions refOf, get,
	// set, erase, apply, docMessage, decorate.
	template<typename Model>
	class Core
	{
	public:
		using Doc = typename Model::Document;
		using Ref = typename Model::Ref;
		using Change = typename Model::Change;
		using Documents = typename Model::Documents;
		using Clipboard = typename Model::Clipboard;
		using Adapter = Machine<Model>;
		using Ev = typename Adapter::Ev;

		explicit Core(std::function<void(const Value&)> _toPage) : m_toPage(std::move(_toPage)) {}

		void setMachine(Adapter* _machine)
		{
			m_machine = _machine;
			forgetAll();
		}

		void undo(const Value& _message) { undoRedo(false, _message); }
		void redo(const Value& _message) { undoRedo(true, _message); }

		// A pure edit (the table's CoreOp::Edit).
		void edit(const Value& _message) { run(_message, Model::apply(m_view, _message, m_clipboard, context())); }
		// A whole document as the intent (CoreOp::Set): the pure transform "replace it with this
		// validated value".
		void set(const Value& _message) { run(_message, Model::setDocument(m_view, _message, context())); }

	private:
		typename Model::Context context() const { return m_machine ? m_machine->context() : typename Model::Context{}; }

		void run(const Value& _message, const typename Model::EditResult& r)
		{
			if(r.clipboard)
				m_clipboard = *r.clipboard;
			if(!r.errors.empty() || r.changes.empty() || !m_machine)
			{
				result(_message, r.errors, r.note);
				return;
			}
			const auto review = m_machine->review(_message, r.changes, m_view);
			if(!review.errors.empty())
			{
				result(_message, review.errors, {});
				return;
			}
			if(review.ask && !forced(_message))
			{
				publish(*review.ask);
				result(_message, {}, {});
				return;
			}
			std::vector<std::string> errors;
			std::string note = r.note;
			const auto delivered = deliver(r.changes, errors, note);
			const auto* g = _message.find("g");
			m_history.record(delivered, g && g->isNumber() && g->asNumber() > 0 ? static_cast<uint64_t>(g->asNumber()) : 0);
			result(_message, errors, note);
		}

	public:

		// A machine-owned command: the adapter's outcome becomes the page's result (and ask).
		void onMachineCommand(const Value& _message)
		{
			if(!m_machine)
				return;
			if(!forced(_message))
			{
				const auto a = m_machine->askFor(_message, m_view);
				if(a.ask || !a.errors.empty())
				{
					if(a.ask)
						publish(*a.ask);
					result(_message, a.errors, a.note);
					return;
				}
			}
			const auto o = m_machine->command(_message, m_view);
			pump();
			if(o.ask)
				publish(*o.ask);
			result(_message, o.errors, o.note);
		}

		// Everything the adapter reported since the last call.
		void pump()
		{
			if(!m_machine)
				return;
			for(auto& e : m_machine->drain())
			{
				switch(e.kind)
				{
				case Ev::Kind::Observed:
				{
					const auto ref = Model::refOf(*e.doc);
					auto& s = m_docs[ref];
					if(s.pending && *s.pending == *e.doc)
						s.pending.reset();
					s.observed = std::move(e.doc);
					s.source = e.source;
					changed(ref);
					break;
				}
				case Ev::Kind::Forget:
					m_docs.erase(e.ref);
					changed(e.ref);
					break;
				case Ev::Kind::Settled:
					if(e.doc)
					{
						auto& s = m_docs[e.ref];
						s.observed = std::move(e.doc);
						s.source = e.source;
						s.pending.reset();
						changed(e.ref);
					}
					else if(const auto it = m_docs.find(e.ref); it != m_docs.end() && it->second.pending)
					{
						it->second.pending.reset();
						changed(e.ref);
					}
					if(!e.ok && !e.message.empty())
					{
						Value m = Value::object();
						m.set("type", "error");
						m.set("message", e.message);
						publish(m);
					}
					break;
				case Ev::Kind::Notice:
					publish(e.notice);
					break;
				case Ev::Kind::Reset:
				{
					Value reset = Value::object();
					reset.set("type", "reset");
					publish(reset);
					forgetAll();
					break;
				}
				}
			}
		}

		// The page (re)attached: everything once more.
		void pageReady()
		{
			m_pageReady = true;
			m_pageSeen = true;
			for(const auto& [ref, s] : m_docs)
				m_dirty.insert(ref);
			m_lastMachine.reset();
			if(m_machine)
				m_machine->pageReady();
		}

		void detach() { m_pageReady = false; }

		// Publish the documents that changed and the machine document when its value changed, then
		// the results of the commands since the last flush: a page that gets a result already has
		// the documents it refers to.
		void flush()
		{
			publishDocuments();
			auto results = std::move(m_results);
			m_results.clear();
			for(const auto& r : results)
				publish(r);
		}

	private:
		void publishDocuments()
		{
			if(!m_pageReady || !m_machine)
				return;
			for(const auto& ref : m_dirty)
			{
				const auto it = m_docs.find(ref);
				if(it == m_docs.end() || !it->second.view())
					continue;
				publish(Model::docMessage(ref, *it->second.view(), it->second.pending.has_value(), it->second.source));
			}
			m_dirty.clear();
			auto doc = machineDocument();
			if(m_lastMachine && *m_lastMachine == doc)
				return;
			m_lastMachine = doc;
			Value m = Value::object();
			m.set("type", "machine");
			m.set("doc", std::move(doc));
			publish(m);
		}

	public:

		// The machine document: the adapter's state plus the core's parts.
		Value machineDocument() const
		{
			auto doc = m_machine->state(m_view);
			Value history = Value::object();
			history.set("undo", m_history.canUndo());
			history.set("redo", m_history.canRedo());
			history.set("undoCount", static_cast<int>(m_history.size()));
			history.set("redoCount", static_cast<int>(m_history.redoSize()));
			doc.set("history", std::move(history));
			doc.set("lifecycle", lifecycleName(m_machine->lifecycle()));
			doc.set("capabilities", m_machine->capabilities().toJson());
			Value engines = Value::array();
			for(const auto& e : m_engines)
				engines.push(e.toJson());
			doc.set("engines", std::move(engines));
			Model::decorate(doc, m_history);
			return doc;
		}

		// A command's result: published by the next flush, after the documents it changed.
		void result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note)
		{
			m_results.push_back(resultMessage(_message, _errors, _note));
		}

		void publish(const Value& _message)
		{
			if(m_pageReady && m_toPage)
				m_toPage(_message);
		}

		// The engine map's entries, for the page's engine menu (machine.engines).
		void setEngines(std::vector<EngineChoice> _engines) { m_engines = std::move(_engines); }

		const Documents& view() const { return m_view; }
		const std::map<Ref, DocState<Doc>>& docs() const { return m_docs; }
		const DocState<Doc>* state(const Ref& _ref) const
		{
			const auto it = m_docs.find(_ref);
			return it == m_docs.end() ? nullptr : &it->second;
		}
		const History<Change>& history() const { return m_history; }
		const Clipboard& clipboard() const { return m_clipboard; }
		// The one page fact: a page is attached and has said ready (documents go out), and a page
		// has been up at least once (the machine is polled and read from then on).
		bool pageReadyNow() const { return m_pageReady; }
		bool pageSeen() const { return m_pageSeen; }
		bool anyPending() const
		{
			for(const auto& [ref, s] : m_docs)
				if(s.pending)
					return true;
			return false;
		}

	private:
		std::vector<Change> deliver(const std::vector<Change>& _changes, std::vector<std::string>& _errors, std::string& _note)
		{
			std::vector<Change> delivered;
			for(const auto& c : _changes)
			{
				if(c.before == c.after)
					continue;	// the page shows it already (an undo of something the machine already undid)
				const auto o = m_machine->submit(c, m_view);
				if(!o.errors.empty())
				{
					_errors.insert(_errors.end(), o.errors.begin(), o.errors.end());
					pump();
					continue;
				}
				if(!o.note.empty())
					_note += (_note.empty() ? "" : ". ") + o.note;
				const auto ref = c.ref();
				m_docs[ref].pending = c.after;
				changed(ref);
				delivered.push_back(c);
				pump();
			}
			return delivered;
		}

		// Undo and redo deliver like any edit: from what the page shows now (each change's `before`
		// is the current view), and only what was delivered is recorded.
		void undoRedo(const bool _redo, const Value& _message)
		{
			const auto d = _redo ? History<Change>::Direction::Redo : History<Change>::Direction::Undo;
			auto changes = m_history.next(d);
			if(!changes)
			{
				result(_message, {_redo ? "Nothing to redo" : "Nothing to undo"}, {});
				return;
			}
			for(auto& c : *changes)
				if(auto now = Model::get(m_view, c.ref()))
					c.before = *now;
			std::vector<std::string> errors;
			std::string note = _redo ? "Redo" : "Undo";
			m_history.done(d, deliver(*changes, errors, note));
			result(_message, errors, note);
		}

		static bool forced(const Value& _message)
		{
			const auto* f = _message.find("force");
			return f && ((f->isBool() && f->asBool()) || (f->isNumber() && f->asNumber() != 0));
		}

		void changed(const Ref& _ref)
		{
			const auto it = m_docs.find(_ref);
			const auto* v = it == m_docs.end() ? nullptr : it->second.view();
			if(v)
				Model::set(m_view, *v);
			else
				Model::erase(m_view, _ref);
			m_dirty.insert(_ref);
		}

		void forgetAll()
		{
			m_docs.clear();
			m_view = {};
			m_dirty.clear();
			m_history.clear();
			m_lastMachine.reset();
		}

		std::function<void(const Value&)> m_toPage;
		Adapter* m_machine = nullptr;
		std::map<Ref, DocState<Doc>> m_docs;
		Documents m_view;
		History<Change> m_history;
		Clipboard m_clipboard;
		std::set<Ref> m_dirty;
		std::optional<Value> m_lastMachine;		// the machine document last published
		bool m_pageReady = false;
		bool m_pageSeen = false;
		std::vector<EngineChoice> m_engines;
		std::vector<Value> m_results;	// results waiting for the flush
	};
}
