#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>

namespace deskCore
{
	// Reading documents from a machine, one request in flight (P1: a burst of requests
	// costs step jitter, and a dump answer is large). Urgent reads go to the front; a
	// request without an answer is sent again (Policy::retries); after that it goes back to
	// the front of the queue but waits Policy::backoffMs (a slow wire, a cable pulled for a
	// while) while the others go, Policy::rounds times, and then it is given up: next() says
	// so (Step::gaveUp), so the adapter can report it. Pure: the caller gives the time and sends what next()
	// returns.
	template<typename Ref>
	class LoadQueue
	{
	public:
		struct Policy
		{
			double gapMs = 30;			// between two requests
			int retries = 1;			// resends of one request in a row
			int rounds = 2;				// times a request goes back into the queue once its resends ran out
			double backoffMs = 2000;	// a request back in the queue waits this long before it goes again
		};

		// What next() asks of the caller: a request to send, and a request given up (no reply
		// after every resend and every round). Both can be set.
		struct Step
		{
			std::optional<Ref> send;
			std::optional<Ref> gaveUp;
		};

		// Read _ref. Already queued or in flight: urgent moves it to the front (a request
		// waiting out its backoff keeps waiting).
		void want(const Ref& _ref, const bool _urgent)
		{
			if(m_loading == _ref)
				return;
			const auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Entry& _e) { return _e.ref == _ref; });
			Entry e{_ref, 0};
			if(it != m_queue.end())
			{
				if(!_urgent)
					return;
				e = *it;
				m_queue.erase(it);
			}
			if(_urgent)
				m_queue.push_front(e);
			else
				m_queue.push_back(e);
		}

		bool contains(const Ref& _ref) const
		{
			return m_loading == _ref
				|| std::any_of(m_queue.begin(), m_queue.end(), [&](const Entry& _e) { return _e.ref == _ref; });
		}

		// A document arrived: the request for it is answered.
		void arrived(const Ref& _ref)
		{
			if(m_loading == _ref)
				m_loading.reset();
			m_rounds.erase(_ref);
		}

		// The request to send now, if any: the one in flight again after _timeoutMs without
		// an answer, or (when _mayStart and the gap has passed) the next queued one whose
		// backoff is over. A request out of resends goes back into the queue, or is given up.
		Step next(const double _nowMs, const double _timeoutMs, const bool _mayStart, const Policy& _policy)
		{
			Step step;
			if(m_loading)
			{
				if(_nowMs - m_sentMs < _timeoutMs)
					return step;
				if(m_retries++ < _policy.retries)
				{
					m_sentMs = m_lastRequestMs = _nowMs;
					step.send = m_loading;
					return step;
				}
				const auto ref = *m_loading;
				m_loading.reset();
				if(++m_rounds[ref] <= _policy.rounds)
					m_queue.push_front({ref, _nowMs + _policy.backoffMs});	// first once its backoff is over
				else
				{
					m_rounds.erase(ref);
					step.gaveUp = ref;
				}
			}
			if(!_mayStart || _nowMs - m_lastRequestMs < _policy.gapMs)
				return step;
			const auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Entry& _e) { return _e.notBeforeMs <= _nowMs; });
			if(it == m_queue.end())
				return step;
			m_loading = it->ref;
			m_queue.erase(it);
			m_retries = 0;
			m_sentMs = m_lastRequestMs = _nowMs;
			step.send = m_loading;
			return step;
		}

		const std::optional<Ref>& loading() const { return m_loading; }
		// Queued plus in flight.
		size_t pending() const { return m_queue.size() + (m_loading ? 1 : 0); }
		bool idle() const { return !pending(); }

	private:
		struct Entry
		{
			Ref ref;
			double notBeforeMs = 0;
		};

		std::deque<Entry> m_queue;
		std::optional<Ref> m_loading;
		std::map<Ref, int> m_rounds;	// rounds a request without a reply has had so far
		double m_sentMs = 0;
		double m_lastRequestMs = -1e9;
		int m_retries = 0;
	};
}
