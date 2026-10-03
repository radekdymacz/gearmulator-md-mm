#pragma once

#include "deskPacer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace deskCore
{
	// How a whole-document push is paced (DESIGN-edit-flow.md §4.4, per engine in its Profile): at most
	// one dump per document every minIntervalMs, and the read-back asked for once, when no new value
	// came for quietMs. The page shows each edit at once; only the machine's copy trails.
	struct PushPolicy
	{
		enum class Mode : uint8_t
		{
			Paced,		// a newer value goes once minIntervalMs passed since the one before
			Held		// the value sent is not on the wire yet (it waits on the panel's SYSEX RECV, MM): a
						// newer one waits for it, however long
		};

		double minIntervalMs = 200;
		double quietMs = 150;
		Mode mode = Mode::Paced;
	};

	// _base for a document whose dump is _replyBytes long: over a wire (DIN) a dump takes its time on
	// it, so no faster than that.
	inline PushPolicy wirePolicy(PushPolicy _base, const bool _wire, const size_t _replyBytes)
	{
		if(_wire)
			_base.minIntervalMs = std::max(_base.minIntervalMs, DinPacer::wireMs(_replyBytes));
		return _base;
	}

	// Paced, latest-wins delivery for one document (P6, paced since DESIGN-edit-flow.md). A dump goes
	// out at once when the last one left at least minIntervalMs ago; a newer value waits as next and
	// replaces what waited. Nothing is read back while the gesture runs: once no new value came for
	// quietMs, due() says ReadBack, once, and the reply confirms the last value sent. So a lock draw
	// at 60 edits a second costs at most 5 dumps a second and one read-back per gesture.
	// T is the document value (MD) or the dump's bytes (MM). Pure: the caller gives the time.
	template<typename T>
	class PushSlot
	{
	public:
		enum class ReadBack
		{
			NotWaiting,			// nothing sent and unconfirmed: an ordinary refresh
			Confirmed,			// the machine holds the last value sent, nothing waits
			Other				// something else (an older value, or a newer one waits): keep waiting
		};
		enum class Due
		{
			Nothing,
			Send,				// next() may go out now: takeNext()
			ReadBack			// the gesture is quiet: ask for the read-back, then askedBack()
		};

		// True: send _value now (it is the value sent). False: it waits as next (latest wins).
		bool want(const T& _value, const double _nowMs, const PushPolicy& _policy)
		{
			m_lastWantMs = _nowMs;
			if(m_sent && (_policy.mode == PushPolicy::Mode::Held || _nowMs - m_sentMs < _policy.minIntervalMs))
			{
				m_next = _value;
				return false;
			}
			m_next.reset();
			sent(_value, _nowMs);
			return true;
		}

		Due due(const double _nowMs, const PushPolicy& _policy) const
		{
			if(m_next && _nowMs - m_sentMs >= _policy.minIntervalMs)
				return Due::Send;
			if(m_sent && !m_next && !m_asked && _nowMs - m_lastWantMs >= _policy.quietMs)
				return Due::ReadBack;
			return Due::Nothing;
		}

		// The value that waited is sent now (after Due::Send).
		const T& takeNext(const double _nowMs)
		{
			sent(*m_next, _nowMs);
			m_next.reset();
			return *m_sent;
		}

		// The read-back was asked for (after Due::ReadBack, or a push that asks at once).
		void askedBack(const double _nowMs)
		{
			m_asked = true;
			m_askedMs = _nowMs;
		}

		ReadBack onReadBack(const T& _firmware)
		{
			if(!m_sent)
				return ReadBack::NotWaiting;
			if(m_next || !(_firmware == *m_sent))
				return ReadBack::Other;
			reset();
			return ReadBack::Confirmed;
		}

		// A read-back asked for longer than _timeoutMs ago has not come.
		bool timedOut(const double _nowMs, const double _timeoutMs) const { return m_asked && _nowMs - m_askedMs > _timeoutMs; }

		// The last value sent and not yet confirmed.
		const std::optional<T>& inFlight() const { return m_sent; }
		const std::optional<T>& next() const { return m_next; }
		bool busy() const { return m_sent.has_value() || m_next.has_value(); }
		bool asked() const { return m_asked; }
		double sentMs() const { return m_sentMs; }
		double askedMs() const { return m_askedMs; }

		// The read-back never came, or the machine started over: forget everything; the next value
		// goes out at once.
		void abandon() { reset(); }

	private:
		void sent(const T& _value, const double _nowMs)
		{
			m_sent = _value;
			m_sentMs = _nowMs;
			m_asked = false;
		}
		void reset()
		{
			m_sent.reset();
			m_next.reset();
			m_asked = false;
			m_sentMs = -1e18;
		}

		std::optional<T> m_sent;
		std::optional<T> m_next;
		double m_sentMs = -1e18;
		double m_lastWantMs = -1e18;
		double m_askedMs = 0;
		bool m_asked = false;
	};
	// Every document's PushSlot, and the one pump around them (shared by the adapters). A push is parked
	// while the dump it sent is queued on the machine's side and not on the wire yet (the MM's SYSEX RECV):
	// a newer value waits for it (PushPolicy::Mode::Held), and the pump leaves it alone until it is
	// unparked. Pure: the caller gives the time and the policies, and runs the effects pump() returns.
	template<typename Ref, typename T>
	class Pushes
	{
	public:
		struct Push
		{
			PushSlot<T> slot;
			bool parked = false;
		};

		// What the pump asks the adapter to do, in order.
		struct Effect
		{
			enum class Kind : uint8_t
			{
				Send,		// the value that waited goes out now (value)
				AskBack,	// the gesture is quiet: ask for the read-back of ref
				TimedOut	// the read-back never came: the push was given up (fail it, read ref again)
			};
			Kind kind = Kind::Send;
			Ref ref{};
			std::optional<T> value;
		};

		// True: send _value now. False: it waits as next (latest wins).
		bool want(const Ref& _ref, const T& _value, const double _nowMs, PushPolicy _policy)
		{
			auto& p = m_pushes[_ref];
			if(p.parked)
				_policy.mode = PushPolicy::Mode::Held;
			return p.slot.want(_value, _nowMs, _policy);
		}

		// _policyOf(ref) -> PushPolicy, _timeoutOf(ref) -> ms the read-back may take.
		template<typename PolicyOf, typename TimeoutOf>
		std::vector<Effect> pump(const double _nowMs, const PolicyOf& _policyOf, const TimeoutOf& _timeoutOf)
		{
			using Due = typename PushSlot<T>::Due;
			std::vector<Effect> effects;
			for(auto& [ref, push] : m_pushes)
			{
				if(!push.slot.busy() || push.parked)
					continue;
				switch(push.slot.due(_nowMs, _policyOf(ref)))
				{
				case Due::Send:
					effects.push_back({Effect::Kind::Send, ref, push.slot.takeNext(_nowMs)});
					continue;	// just sent: nothing asked back yet, so nothing timed out
				case Due::ReadBack:
					push.slot.askedBack(_nowMs);
					effects.push_back({Effect::Kind::AskBack, ref, std::nullopt});
					break;
				case Due::Nothing:
					break;
				}
				if(!push.slot.timedOut(_nowMs, _timeoutOf(ref)))
					continue;
				push.slot.abandon();
				effects.push_back({Effect::Kind::TimedOut, ref, std::nullopt});
			}
			return effects;
		}

		Push& operator[](const Ref& _ref) { return m_pushes[_ref]; }
		Push* find(const Ref& _ref)
		{
			const auto it = m_pushes.find(_ref);
			return it == m_pushes.end() ? nullptr : &it->second;
		}
		const Push* find(const Ref& _ref) const
		{
			const auto it = m_pushes.find(_ref);
			return it == m_pushes.end() ? nullptr : &it->second;
		}
		bool busy(const Ref& _ref) const
		{
			const auto* p = find(_ref);
			return p && p->slot.busy();
		}
		bool anyBusy() const
		{
			for(const auto& [ref, push] : m_pushes)
				if(push.slot.busy())
					return true;
			return false;
		}
		void clear() { m_pushes.clear(); }

		auto begin() const { return m_pushes.begin(); }
		auto end() const { return m_pushes.end(); }

	private:
		std::map<Ref, Push> m_pushes;
	};

	// A mailbox for runs that must not overlap (the panel keys of a pattern chain): one run on its way at
	// a time, and while it is, the latest value offered waits (an older one waiting is replaced). The run
	// on its way is the caller's fact (_busy, _ready), not the mailbox's. Pure.
	template<typename T>
	class Latest
	{
	public:
		// _value to start now, or nothing: a run is on its way, so _value waits (latest wins). A value
		// started now replaces what waited too.
		std::optional<T> offer(T _value, const bool _busy)
		{
			if(_busy)
			{
				m_waiting = std::move(_value);
				return std::nullopt;
			}
			m_waiting.reset();
			return std::optional<T>(std::move(_value));
		}

		// What waited, once nothing is on its way (_ready); it leaves the mailbox.
		std::optional<T> takeWhen(const bool _ready)
		{
			if(!_ready || !m_waiting)
				return std::nullopt;
			auto v = std::move(m_waiting);
			m_waiting.reset();
			return v;
		}

		// Nothing waits any more (another choice ended what waited).
		void drop() { m_waiting.reset(); }
		bool waiting() const { return m_waiting.has_value(); }

	private:
		std::optional<T> m_waiting;
	};
}
