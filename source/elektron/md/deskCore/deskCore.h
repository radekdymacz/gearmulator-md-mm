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
			Settled,	// a submitted change of `ref` is no longer in flight (ok, or failed with `message`)
			Notice		// a message for the page as it is (telemetry, a failure)
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
		static Event settled(const Ref& _ref, const bool _ok, std::string _message = {})
		{
			Event e;
			e.kind = Kind::Settled;
			e.ref = _ref;
			e.ok = _ok;
			e.message = std::move(_message);
			return e;
		}
		static Event noticeOf(Value _message)
		{
			Event e;
			e.notice = std::move(_message);
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

		// An edit, undo or redo (the table's Owner::Core).
		void onCommand(const Value& _message)
		{
			const auto op = opOf(_message);
			if(op == "undo" || op == "redo")
			{
				undoRedo(op == "redo", _message);
				return;
			}
			const auto r = Model::apply(m_view, _message, m_clipboard, m_machine ? m_machine->context() : typename Model::Context{});
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
			const auto* force = _message.find("force");
			if(review.ask && !(force && force->isBool() && force->asBool()))
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

		// A machine-owned command: the adapter's outcome becomes the page's result (and ask).
		void onMachineCommand(const Value& _message)
		{
			if(!m_machine)
				return;
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
					if(const auto it = m_docs.find(e.ref); it != m_docs.end() && it->second.pending)
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
				}
			}
		}

		// The page (re)attached: everything once more.
		void pageReady()
		{
			m_pageReady = true;
			for(const auto& [ref, s] : m_docs)
				m_dirty.insert(ref);
			m_lastMachine.clear();
			if(m_machine)
				m_machine->pageReady();
		}

		void detach() { m_pageReady = false; }

		// Publish the documents that changed and the machine document when its value changed.
		void flush(const std::function<void(Value&)>& _decorate = {})
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
			if(_decorate)
				_decorate(doc);
			auto text = elektronData::json::write(doc);
			if(text == m_lastMachine)
				return;
			m_lastMachine = std::move(text);
			Value m = Value::object();
			m.set("type", "machine");
			m.set("doc", std::move(doc));
			publish(m);
		}

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
			Model::decorate(doc, m_history, *m_machine);
			return doc;
		}

		void result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note) const
		{
			Value r = Value::object();
			r.set("type", "result");
			r.set("op", opOf(_message));
			if(const auto* id = _message.find("id"); id && id->isNumber())
				r.set("id", *id);
			r.set("ok", _errors.empty());
			Value errors = Value::array();
			for(const auto& e : _errors)
				errors.push(e);
			r.set("errors", std::move(errors));
			r.set("note", _note);
			publish(r);
		}

		void publish(const Value& _message) const
		{
			if(m_pageReady && m_toPage)
				m_toPage(_message);
		}

		const Documents& view() const { return m_view; }
		const std::map<Ref, DocState<Doc>>& docs() const { return m_docs; }
		const DocState<Doc>* state(const Ref& _ref) const
		{
			const auto it = m_docs.find(_ref);
			return it == m_docs.end() ? nullptr : &it->second;
		}
		const History<Change>& history() const { return m_history; }
		const Clipboard& clipboard() const { return m_clipboard; }
		bool pageReadyNow() const { return m_pageReady; }
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

		void undoRedo(const bool _redo, const Value& _message)
		{
			auto changes = _redo ? m_history.redo() : m_history.undo();
			if(!changes)
			{
				result(_message, {_redo ? "Nothing to redo" : "Nothing to undo"}, {});
				return;
			}
			std::vector<std::string> errors;
			std::string note = _redo ? "Redo" : "Undo";
			deliver(*changes, errors, note);
			result(_message, errors, note);
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
			m_lastMachine.clear();
		}

		std::function<void(const Value&)> m_toPage;
		Adapter* m_machine = nullptr;
		std::map<Ref, DocState<Doc>> m_docs;
		Documents m_view;
		History<Change> m_history;
		Clipboard m_clipboard;
		std::set<Ref> m_dirty;
		std::string m_lastMachine;
		bool m_pageReady = false;
	};
}
