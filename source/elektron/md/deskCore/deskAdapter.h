#pragma once

#include "deskCore.h"
#include "deskLifecycle.h"
#include "deskPush.h"

#include <cstdint>
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

	protected:
		// The machine holds _doc.
		void observe(const Doc& _doc, const Source _source)
		{
			m_known.insert(Model::refOf(_doc));
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
			m_events.push_back(Ev::settledWith(_doc, _source, ref));
		}
		// A submitted change did not reach the machine.
		void fail(const Ref& _ref, std::string _message) { m_events.push_back(Ev::failed(_ref, std::move(_message))); }
		void notice(Value _message) { m_events.push_back(Ev::noticeOf(std::move(_message))); }
		// The machine started over: nothing it held is known.
		void startedOver()
		{
			m_known.clear();
			m_events.push_back(Ev::reset());
		}
		bool known(const Ref& _ref) const { return m_known.count(_ref) != 0; }
		bool knowsAnything() const { return !m_known.empty(); }

	private:
		std::vector<Ev> m_events;
		std::set<Ref> m_known;
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

	// What a dump that arrives means for the push of its document (P6: one policy for every
	// adapter).
	enum class ReadBackAction : uint8_t
	{
		Observe,		// nothing in flight: an ordinary refresh
		Settle,			// the machine holds what was sent: the change is done
		ObserveSendNext,// it holds what was sent, a newer value waits: observe, then send inFlight()
		Wait			// something else (an older reply): wait; the read-back timeout fails the push
	};

	template<typename T>
	ReadBackAction readBack(PushSlot<T>& _slot, const T& _value)
	{
		using R = typename PushSlot<T>::ReadBack;
		switch(_slot.onReadBack(_value))
		{
		case R::NotWaiting: return ReadBackAction::Observe;
		case R::Confirmed: return ReadBackAction::Settle;
		case R::ConfirmedSendNext: return ReadBackAction::ObserveSendNext;
		case R::Other: return ReadBackAction::Wait;
		}
		return ReadBackAction::Wait;
	}
}
