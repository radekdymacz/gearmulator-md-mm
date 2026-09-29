#pragma once

#include <optional>

namespace deskCore
{
	// How a whole-document push is paced (DESIGN-edit-flow.md §4.4, per engine in its Profile): at most
	// one dump per document every minIntervalMs, and the read-back asked for once, when no new value
	// came for quietMs. The page shows each edit at once; only the machine's copy trails.
	struct PushPolicy
	{
		double minIntervalMs = 200;
		double quietMs = 150;
	};

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
			if(m_sent && _nowMs - m_sentMs < _policy.minIntervalMs)
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
}
