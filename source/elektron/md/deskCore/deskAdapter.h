#pragma once

#include "deskCore.h"
#include "deskLifecycle.h"
#include "deskPush.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace deskCore
{
	// What every adapter shares (P6): the events it reports to the core and what it has observed.
	// Base is the model's adapter interface (Machine<Model> plus the model's device facts).
	template<typename Model, typename Base = Machine<Model>>
	class AdapterBase : public Base
	{
	public:
		using Doc = typename Model::Document;
		using Ref = typename Model::Ref;
		using Ev = Event<Doc, Ref>;

		std::vector<Ev> drain() override
		{
			auto e = std::move(m_events);
			m_events.clear();
			return e;
		}

		size_t knownCount() const { return m_known.size(); }
		// Documents whose read was given up (no reply) and that have not arrived since.
		size_t unreadCount() const { return m_unread.size(); }

	protected:
		// The machine holds _doc.
		void observe(const Doc& _doc, const Source _source)
		{
			m_known.insert(Model::refOf(_doc));
			m_unread.erase(Model::refOf(_doc));
			m_events.push_back(Ev::observed(_doc, _source));
		}
		// Nothing is known about _ref any more (it is read again).
		void forget(const Ref& _ref)
		{
			m_known.erase(_ref);
			m_events.push_back(Ev::forget(_ref));
		}
		// A submitted change is done: the machine holds _doc.
		void settle(const Doc& _doc, const Source _source)
		{
			const auto ref = Model::refOf(_doc);
			m_known.insert(ref);
			m_unread.erase(ref);
			m_events.push_back(Ev::settledWith(_doc, _source, ref));
		}
		// A submitted change did not reach the machine.
		void fail(const Ref& _ref, std::string _message) { m_events.push_back(Ev::failed(_ref, std::move(_message))); }
		// The playhead for the page: the core publishes it as {"type":"telemetry", ...}.
		void publishTelemetry(Value _body) { m_events.push_back(Ev::telemetry(std::move(_body))); }
		// The machine started over: nothing it held is known.
		void startedOver()
		{
			m_known.clear();
			m_unread.clear();
			m_events.push_back(Ev::reset());
		}
		bool known(const Ref& _ref) const { return m_known.count(_ref) != 0; }
		// The read of _ref was given up (LoadQueue's Step::gaveUp): it is unread until it arrives
		// (an Unread: how often, when, and whether the person was told). A document that arrived since is
		// not unread.
		void gaveUpReading(const Ref& _ref, const double _nowMs)
		{
			if(known(_ref))
				return;
			auto& u = m_unread[_ref];
			++u.giveUps;
			u.atMs = _nowMs;
		}
		// True once per unread document: the first time it is worth telling (the caller decides when: the
		// document is the one that plays). A document given up in the background and wanted later is told then.
		bool tellUnread(const Ref& _ref)
		{
			const auto it = m_unread.find(_ref);
			if(it == m_unread.end() || it->second.told)
				return false;
			it->second.told = true;
			return true;
		}
		bool unread(const Ref& _ref) const { return m_unread.count(_ref) != 0; }
		// An unread document may be asked for again on its own (at a status reply) once its backoff is over:
		// 5 s after the first give-up, doubling, at most a minute. A machine that answers status but never
		// answers this request costs one round a minute, not a round every few seconds (panel keys wait for loads).
		bool mayAskAgain(const Ref& _ref, const double _nowMs) const
		{
			const auto it = m_unread.find(_ref);
			return it != m_unread.end() && _nowMs - it->second.atMs >= reaskMs(it->second.giveUps);
		}
		static double reaskMs(const int _giveUps) { return std::min(60000.0, 5000.0 * static_cast<double>(1 << std::clamp(_giveUps - 1, 0, 4))); }
		bool knowsAnything() const { return !m_known.empty(); }

	private:
		std::vector<Ev> m_events;
		std::set<Ref> m_known;
		struct Unread
		{
			int giveUps = 0;
			double atMs = 0;
			bool told = false;
		};
		std::map<Ref, Unread> m_unread;
	};

	// A machine at the end of a cable (HW MIDI): its lifecycle is its replies (P6, shared).
	struct WireFacts
	{
		bool replied = false;			// a status reply since the wire was chosen (or the machine started)
		uint32_t statusReplies = 0;
		double lastReplyMs = -1e9;
		double sinceMs = 0;				// the wire was chosen

		explicit WireFacts(const double _nowMs = 0) : sinceMs(_nowMs) {}

		// Any message from the machine.
		void heard(const double _nowMs) { lastReplyMs = _nowMs; }
		// A status reply: the machine answers.
		void statusReply(const double _nowMs)
		{
			heard(_nowMs);
			replied = true;
			++statusReplies;
		}

		LifeFacts facts(const double _nowMs) const
		{
			LifeFacts f;
			f.probe = LifeFacts::Probe::Wire;
			f.replied = replied;
			f.animation = LifeFacts::Animation::Absent;
			f.silentMs = _nowMs - (replied ? lastReplyMs : sinceMs);
			return f;
		}
	};

	// Live edits sent and not yet seen in the machine's memory (P6, shared by the working kits):
	// the value before the first edit not seen yet and after the latest, and when the latest went
	// out. A memory image settles them once it shows every changed field (the model's reflects),
	// or once they are older than g_timeoutMs (the machine then holds something else: it wins).
	template<typename Doc>
	struct Expectation
	{
		static constexpr double g_timeoutMs = 1000;

		std::optional<Doc> from;
		std::optional<Doc> to;
		double atMs = 0;

		bool any() const { return to.has_value(); }
		bool expecting(const double _nowMs) const { return to && _nowMs - atMs < g_timeoutMs; }
		void sent(const Doc& _from, const Doc& _to, const double _nowMs)
		{
			if(!expecting(_nowMs) || !from)
				from = _from;
			to = _to;
			atMs = _nowMs;
		}
		void clear()
		{
			from.reset();
			to.reset();
		}
		// An image is taken now: it shows the edits, or they are too old to wait for.
		template<typename Reflects>
		bool takes(const Doc& _image, const double _nowMs, const Reflects& _reflects) const
		{
			return !expecting(_nowMs) || !from || _reflects(_image, *from, *to);
		}
	};

	// The same idea for one machine field read from memory (DESIGN-UNIFY.md 4.4: a mute, POLY, the
	// tempo): the value a command set is what the machine document says from the moment the command
	// is taken, until memory shows it (settled) or until memory has disagreed for _settleMs (given up:
	// memory wins, so a press on the machine's panel shows). The clock is the caller's (the session's
	// step clock; a fake one in tests). Pure.
	template<typename T>
	struct FieldExpectation
	{
		std::optional<T> to;
		double atMs = 0;

		static FieldExpectation sent(const T& _value, const double _nowMs) { return {_value, _nowMs}; }
		// still waiting for memory to show it
		bool holds(const std::optional<T>& _memory, const double _nowMs, const double _settleMs) const
		{
			return to && !(_memory && *_memory == *to) && _nowMs - atMs < _settleMs;
		}
		// what the machine says now (nullopt: not known)
		std::optional<T> shown(const std::optional<T>& _memory, const double _nowMs, const double _settleMs) const
		{
			return holds(_memory, _nowMs, _settleMs) ? to : _memory;
		}
		// memory was read: settled or given up, nothing is expected any more
		FieldExpectation observed(const std::optional<T>& _memory, const double _nowMs, const double _settleMs) const
		{
			return holds(_memory, _nowMs, _settleMs) ? *this : FieldExpectation{};
		}
	};

	// What a dump that arrives means for the push of its document (P6: one policy for every
	// adapter).
	enum class ReadBackAction : uint8_t
	{
		Observe,		// nothing in flight: an ordinary refresh
		Settle,			// the machine holds what was sent: the change is done
		Wait			// something else (an older reply, or a newer value waits for its turn): wait; the
						// read-back asked for at quiet confirms it, or its timeout fails the push
	};

	template<typename T>
	ReadBackAction readBack(PushSlot<T>& _slot, const T& _value)
	{
		using R = typename PushSlot<T>::ReadBack;
		switch(_slot.onReadBack(_value))
		{
		case R::NotWaiting: return ReadBackAction::Observe;
		case R::Confirmed: return ReadBackAction::Settle;
		case R::Other: return ReadBackAction::Wait;
		}
		return ReadBackAction::Wait;
	}
}
